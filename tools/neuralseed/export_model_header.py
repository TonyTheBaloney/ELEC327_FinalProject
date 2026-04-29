#!/usr/bin/env python3
"""Export a NeuralSeed-compatible GRU10 JSON model to firmware header data."""

import argparse
import json
import sys
from pathlib import Path


def scalar(value):
    return repr(float(value))


def vector(values):
    return "{" + ", ".join(scalar(value) for value in values) + "}"


def matrix(rows, indent="        "):
    if not rows:
        return "{}"
    lines = ["{"]
    for index, row in enumerate(rows):
        suffix = "," if index < len(rows) - 1 else ""
        lines.append(f"{indent}{vector(row)}{suffix}")
    lines.append("    }")
    return "\n".join(lines)


def transpose(rows):
    return [list(row) for row in zip(*rows)]


def rz_swap(row, hidden_size):
    return row[hidden_size : hidden_size * 2] + row[:hidden_size] + row[hidden_size * 2 :]


def convert_gru_matrix(rows, hidden_size):
    return [rz_swap(row, hidden_size) for row in rows]


def validate_model(data, model_path):
    model = data.get("model_data", {})
    expected = {
        "model": "SimpleRNN",
        "input_size": 1,
        "skip": 1,
        "output_size": 1,
        "unit_type": "GRU",
        "hidden_size": 10,
    }

    errors = []
    for key, value in expected.items():
        if model.get(key) != value:
            errors.append(f"{key}={model.get(key)!r}, expected {value!r}")

    required_state = (
        "rec.weight_ih_l0",
        "rec.weight_hh_l0",
        "rec.bias_ih_l0",
        "rec.bias_hh_l0",
        "lin.weight",
        "lin.bias",
    )
    state = data.get("state_dict", {})
    for key in required_state:
        if key not in state:
            errors.append(f"missing state_dict[{key!r}]")

    if errors:
        joined = "\n  - ".join(errors)
        raise SystemExit(f"{model_path} is not compatible with this firmware:\n  - {joined}")


def build_header(data, selected_name):
    model = data["model_data"]
    state = data["state_dict"]
    hidden_size = int(model["hidden_size"])

    rec_weight_ih = convert_gru_matrix(transpose(state["rec.weight_ih_l0"]), hidden_size)
    rec_weight_hh = convert_gru_matrix(transpose(state["rec.weight_hh_l0"]), hidden_size)
    rec_bias = convert_gru_matrix(
        [state["rec.bias_ih_l0"], state["rec.bias_hh_l0"]],
        hidden_size,
    )

    comments = "\n".join(f"    {key} : {value}" for key, value in model.items())

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
    // Selected checkpoint: {selected_name}
    /*
{comments}
    */

    model.rec_weight_ih_l0 = {matrix(rec_weight_ih)};

    model.rec_weight_hh_l0 = {matrix(rec_weight_hh)};

    model.lin_weight = {matrix(state["lin.weight"])};

    model.lin_bias = {vector(state["lin.bias"])};

    model.rec_bias = {matrix(rec_bias)};

    return model;
}}
"""


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument("model_json", type=Path)
    parser.add_argument(
        "--output",
        type=Path,
        default=Path("AudioProcessing/src/NeuralSeedModelData.h"),
    )
    parser.add_argument("--name", default=None)
    args = parser.parse_args()

    with args.model_json.open() as model_file:
        data = json.load(model_file)

    validate_model(data, args.model_json)
    selected_name = args.name or args.model_json.parent.name
    args.output.write_text(build_header(data, selected_name))
    print(f"Wrote {args.output}")


if __name__ == "__main__":
    sys.exit(main())
