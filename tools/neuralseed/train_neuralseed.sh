#!/usr/bin/env bash
set -euo pipefail

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
CONDA_ENV="${CONDA_ENV:-neuralseed-train}"
GPU_ID="${GPU_ID:-3}"

export PYTHONNOUSERSITE=1
export PYTHONUNBUFFERED=1
export CUDA_DEVICE_ORDER="${CUDA_DEVICE_ORDER:-PCI_BUS_ID}"

exec conda run --no-capture-output -n "${CONDA_ENV}" python -u "${SCRIPT_DIR}/train_neuralseed.py" --gpu "${GPU_ID}" "$@"
