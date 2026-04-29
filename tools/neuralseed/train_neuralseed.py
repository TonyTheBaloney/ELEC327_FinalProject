#!/usr/bin/env python3
"""Prepare WAV captures, train NeuralSeed GRU10, and export firmware weights."""

import argparse
import json
import os
import shutil
import subprocess
import sys
from dataclasses import dataclass
from pathlib import Path


@dataclass(frozen=True)
class CaptureProfile:
    effect: str
    target_wav: str
    device: str
    config_name: str
    note: str = ""

    @property
    def data_name(self):
        return self.device


CAPTURES = {
    "distortion": CaptureProfile(
        effect="distortion",
        target_wav="distortion.wav",
        device="elec327_distortion",
        config_name="elec327_distortion_gru10",
    ),
    "chorus": CaptureProfile(
        effect="chorus",
        target_wav="chorus.wav",
        device="elec327_chorus",
        config_name="elec327_chorus_gru10",
        note="Chorus is time-based, so this capture is experimental for NeuralSeed.",
    ),
}


def run(command, cwd, env):
    print("+ " + " ".join(str(part) for part in command), flush=True)
    subprocess.run(command, cwd=cwd, env=env, check=True)


def write_config(config_path, profile):
    config = {
        "hidden_size": 10,
        "unit_type": "GRU",
        "input_size": 1,
        "output_size": 1,
        "skip_con": 1,
        "loss_fcns": {"ESR": 0.75, "DC": 0.25},
        "pre_filt": "None",
        "device": profile.device,
        "file_name": profile.data_name,
    }
    config_path.write_text(json.dumps(config, indent=2) + "\n")


def require_file(path):
    if not path.is_file():
        raise SystemExit(f"Missing required file: {path}")


def main():
    default_repo_root = Path(__file__).resolve().parents[2]
    default_train_root = Path("/home/harvey/NeuralSeedTraining")
    default_artifact_root = default_repo_root / "training" / "neuralseed"

    parser = argparse.ArgumentParser()
    parser.add_argument("effect", choices=sorted(CAPTURES))
    parser.add_argument("--repo-root", type=Path, default=default_repo_root)
    parser.add_argument("--train-root", type=Path, default=default_train_root)
    parser.add_argument("--artifact-root", type=Path, default=default_artifact_root)
    parser.add_argument("--gpu", type=int, default=int(os.environ.get("GPU_ID", "3")))
    parser.add_argument("--epochs", type=int, default=int(os.environ.get("EPOCHS", "2000")))
    parser.add_argument("--validation-frequency", type=int, default=2)
    parser.add_argument("--validation-patience", type=int, default=25)
    parser.add_argument("--random-split", type=float, default=None)
    parser.add_argument("--fresh", action="store_true")
    parser.add_argument("--export-only", action="store_true")
    parser.add_argument("--no-export", action="store_true")
    parser.add_argument("--model-json", type=Path, default=None)
    args = parser.parse_args()

    repo_root = args.repo_root.resolve()
    train_root = args.train_root.resolve()
    artifact_root = args.artifact_root.resolve()
    agm_dir = train_root / "Automated-GuitarAmpModelling"
    profile = CAPTURES[args.effect]

    require_file(agm_dir / "prep_wav.py")
    require_file(agm_dir / "dist_model_recnet.py")
    require_file(repo_root / "data" / "di.wav")
    require_file(repo_root / "data" / profile.target_wav)

    if profile.note:
        print(profile.note)

    data_dir = artifact_root / "data" / profile.effect
    config_dir = artifact_root / "configs"
    results_root = artifact_root / "results"
    tensorboard_root = artifact_root / "tensorboard"

    for subset in ("train", "val", "test"):
        (data_dir / subset).mkdir(parents=True, exist_ok=True)
    config_dir.mkdir(parents=True, exist_ok=True)
    results_root.mkdir(parents=True, exist_ok=True)
    tensorboard_root.mkdir(parents=True, exist_ok=True)

    config_path = config_dir / f"{profile.config_name}.json"
    result_dir = results_root / f"{profile.device}-{profile.config_name}"

    env = os.environ.copy()
    env["PYTHONNOUSERSITE"] = "1"
    env["PYTHONUNBUFFERED"] = "1"
    env.setdefault("CUDA_DEVICE_ORDER", "PCI_BUS_ID")

    if not args.export_only:
        if args.fresh and result_dir.exists():
            shutil.rmtree(result_dir)
        if args.fresh and data_dir.exists():
            shutil.rmtree(data_dir)
            for subset in ("train", "val", "test"):
                (data_dir / subset).mkdir(parents=True, exist_ok=True)

        write_config(config_path, profile)

        prep_cmd = [
            sys.executable,
            "-u",
            str(agm_dir / "prep_wav.py"),
            profile.data_name,
            "-s",
            str(repo_root / "data" / "di.wav"),
            str(repo_root / "data" / profile.target_wav),
            "--path",
            str(data_dir),
        ]
        if args.random_split is not None:
            prep_cmd.extend(["--random_split", str(args.random_split)])
        run(prep_cmd, cwd=agm_dir, env=env)

        train_cmd = [
            sys.executable,
            "-u",
            str(agm_dir / "dist_model_recnet.py"),
            "-l",
            profile.config_name,
            "--config_location",
            str(config_dir),
            "--save_location",
            str(results_root),
            "--data_location",
            str(data_dir),
            "--tensorboard_location",
            str(tensorboard_root),
            "--cuda",
            "1",
            "--cuda_device",
            str(args.gpu),
            "--epochs",
            str(args.epochs),
            "--validation_f",
            str(args.validation_frequency),
            "--validation_p",
            str(args.validation_patience),
        ]
        run(train_cmd, cwd=agm_dir, env=env)

    if args.no_export:
        return 0

    model_json = args.model_json
    if model_json is None:
        model_json = result_dir / "model_best.json"
        if not model_json.is_file():
            model_json = result_dir / "model.json"
    require_file(model_json)

    export_script = Path(__file__).resolve().with_name("export_model_header.py")
    output_header = repo_root / "AudioProcessing" / "src" / "NeuralSeedModelData.h"
    run(
        [
            sys.executable,
            "-u",
            str(export_script),
            str(model_json),
            "--output",
            str(output_header),
            "--name",
            f"{profile.effect}:{model_json}",
        ],
        cwd=repo_root,
        env=env,
    )

    print(f"Training result: {result_dir}")
    print(f"Exported firmware weights: {output_header}")
    return 0


if __name__ == "__main__":
    sys.exit(main())
