#!/usr/bin/env bash
set -euo pipefail

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
REPO_ROOT="$(cd "${SCRIPT_DIR}/../.." && pwd)"
LOG_DIR="${LOG_DIR:-${REPO_ROOT}/training/neuralseed/logs}"

usage() {
    cat <<EOF
Usage:
  ./tools/neuralseed/tail_neuralseed_log.sh [effect]

Examples:
  ./tools/neuralseed/tail_neuralseed_log.sh distortion
  ./tools/neuralseed/tail_neuralseed_log.sh chorus

If no effect is given, this follows the newest NeuralSeed training log.
EOF
}

if [[ "${1:-}" == "-h" || "${1:-}" == "--help" ]]; then
    usage
    exit 0
fi

if [[ ! -d "${LOG_DIR}" ]]; then
    echo "No NeuralSeed log directory exists yet: ${LOG_DIR}" >&2
    exit 1
fi

if [[ $# -gt 0 ]]; then
    safe_effect="$(printf '%s' "$1" | tr -c 'A-Za-z0-9_.-' '_')"
    log_file="${LOG_DIR}/${safe_effect}_latest.log"
    if [[ ! -e "${log_file}" ]]; then
        echo "No latest log found for effect '$1' in ${LOG_DIR}" >&2
        exit 1
    fi
else
    log_file="$(
        find "${LOG_DIR}" -maxdepth 1 -type f -name '*.log' -printf '%T@ %p\n' \
            | sort -nr \
            | awk 'NR == 1 {sub(/^[^ ]+ /, ""); print}'
    )"
    if [[ -z "${log_file}" ]]; then
        echo "No NeuralSeed training logs found in ${LOG_DIR}" >&2
        exit 1
    fi
fi

echo "Following ${log_file}"
exec tail -f "${log_file}"
