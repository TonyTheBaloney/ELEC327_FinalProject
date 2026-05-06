#!/usr/bin/env python3
"""Select a bundled NeuralSeed model from all_model_data.h for firmware."""

import argparse
import re
import sys
from dataclasses import dataclass
from pathlib import Path


FIRMWARE_HIDDEN_SIZE = 10
DEFAULT_SOURCE = Path("training/neuralseed/provided/all_model_data.h")
DEFAULT_OUTPUT = Path("AudioProcessing/src/NeuralSeedModelData.h")


@dataclass(frozen=True)
class ProvidedModel:
    number: int
    name: str
    metadata: dict

    @property
    def unit_type(self):
        return self.metadata.get("unit_type", "")

    @property
    def input_size(self):
        return int(self.metadata.get("input_size", "-1"))

    @property
    def hidden_size(self):
        return int(self.metadata.get("hidden_size", "-1"))

    @property
    def is_gru10(self):
        return self.unit_type == "GRU" and self.hidden_size == FIRMWARE_HIDDEN_SIZE

    @property
    def is_native_firmware_shape(self):
        return self.is_gru10 and self.input_size == 1


def parse_metadata(comment):
    metadata = {}
    for line in comment.splitlines():
        if ":" not in line:
            continue
        key, value = line.split(":", 1)
        metadata[key.strip()] = value.strip()
    return metadata


def parse_models(source):
    pattern = re.compile(
        r"//========================================================================\n"
        r"//(?P<name>[^\n]+)\n"
        r"/\*\n(?P<meta>.*?)\*/\n\s*Model(?P<number>\d+)\.rec_weight_ih_l0",
        re.DOTALL,
    )
    models = []
    for match in pattern.finditer(source):
        models.append(
            ProvidedModel(
                number=int(match.group("number")),
                name=match.group("name").strip(),
                metadata=parse_metadata(match.group("meta")),
            )
        )
    return models


def compatibility_label(model):
    if model.is_native_firmware_shape:
        return "firmware-native"
    if model.is_gru10:
        return f"GRU10, input_size={model.input_size}"
    return f"not drop-in ({model.unit_type}{model.hidden_size})"


def print_model_list(models):
    for model in models:
        print(f"{model.number:>2}  {model.name:<32}  {compatibility_label(model)}")


def find_model(models, selector):
    selector_lower = selector.lower()
    for model in models:
        if selector == str(model.number) or selector_lower == model.name.lower():
            return model
    raise SystemExit(f"Unknown provided model: {selector}")


def extract_model_block(source, model):
    needle = f"Model{model.number}.rec_weight_ih_l0"
    idx = source.index(needle)
    start = source.rfind("//========================================================================", 0, idx)
    rec_bias = source.index(f"Model{model.number}.rec_bias", idx)
    end = source.index("};", rec_bias) + 2
    return source[start:end].replace(f"Model{model.number}.", "model.")


def build_header(model, block, source_path):
    extra_input_note = ""
    if model.is_gru10 and model.input_size != 1:
        extra_input_note = (
            "\n"
            "    // NOTE: this model was trained with more than one input. The current\n"
            "    // firmware model type is GRULayerT<float, 1, 10>, so only the first\n"
            "    // input-weight row is used unless Main.cpp is changed to feed controls.\n"
        )

    return f"""#pragma once

#include <vector>

struct NeuralSeedModelData
{{
    std::vector<std::vector<float>> rec_weight_ih_l0;
    std::vector<std::vector<float>> rec_weight_hh_l0;
    std::vector<std::vector<float>> lin_weight;
    std::vector<float> lin_bias;
    std::vector<std::vector<float>> rec_bias;
}};

static NeuralSeedModelData CreateSelectedNeuralSeedModelData()
{{
    NeuralSeedModelData model;
    // Selected provided checkpoint: Model{model.number} {model.name}
    // Source: {source_path}
{extra_input_note}
{block}

    return model;
}}
"""


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument(
        "model",
        nargs="?",
        help="Model number or exact model name from the bundled all_model_data.h",
    )
    parser.add_argument("--list", action="store_true", help="List bundled models")
    parser.add_argument("--source", type=Path, default=DEFAULT_SOURCE)
    parser.add_argument("--output", type=Path, default=DEFAULT_OUTPUT)
    args = parser.parse_args()

    if not args.source.is_file():
        raise SystemExit(f"Missing bundled model data: {args.source}")

    source = args.source.read_text()
    models = parse_models(source)
    if not models:
        raise SystemExit(f"No models found in {args.source}")

    if args.list:
        print_model_list(models)
        return 0

    if args.model is None:
        parser.error("provide a model number/name, or use --list")

    model = find_model(models, args.model)
    if not model.is_gru10:
        raise SystemExit(
            f"Model{model.number} {model.name} is {model.unit_type}{model.hidden_size}, "
            "but this firmware is compiled for GRU10."
        )

    if model.input_size != 1:
        print(
            f"Warning: Model{model.number} has input_size={model.input_size}; "
            "the current firmware only feeds one input.",
            file=sys.stderr,
        )

    block = extract_model_block(source, model)
    args.output.write_text(build_header(model, block, args.source))
    print(f"Selected Model{model.number}: {model.name}")
    print(f"Wrote {args.output}")
    return 0


if __name__ == "__main__":
    sys.exit(main())
