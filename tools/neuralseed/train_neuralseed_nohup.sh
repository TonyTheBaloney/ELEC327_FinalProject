#!/usr/bin/env bash
set -euo pipefail

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
REPO_ROOT="$(cd "${SCRIPT_DIR}/../.." && pwd)"
LOG_DIR="${LOG_DIR:-${REPO_ROOT}/training/neuralseed/logs}"

usage() {
    cat <<EOF
Usage:
  ./tools/neuralseed/train_neuralseed_nohup.sh <effect> [training options]

Examples:
  ./tools/neuralseed/train_neuralseed_nohup.sh distortion --fresh --epochs 300 --gpu 3
  GPU_ID=2 ./tools/neuralseed/train_neuralseed_nohup.sh chorus --fresh --epochs 300

Logs:
  ${LOG_DIR}

The options after <effect> are passed through to train_neuralseed.sh.
EOF
}

if [[ $# -lt 1 || "${1:-}" == "-h" || "${1:-}" == "--help" ]]; then
    usage
    exit 0
fi

effect="$1"
safe_effect="$(printf '%s' "${effect}" | tr -c 'A-Za-z0-9_.-' '_')"
timestamp="$(date +%Y%m%d_%H%M%S)"
log_file="${LOG_DIR}/${safe_effect}_${timestamp}.log"
pid_file="${LOG_DIR}/${safe_effect}_${timestamp}.pid"
latest_log="${LOG_DIR}/${safe_effect}_latest.log"
latest_pid="${LOG_DIR}/${safe_effect}_latest.pid"

mkdir -p "${LOG_DIR}"

{
    printf 'Started: %s\n' "$(date)"
    printf 'Working directory: %s\n' "${REPO_ROOT}"
    printf 'Command:'
    printf ' %q' "${SCRIPT_DIR}/train_neuralseed.sh" "$@"
    printf '\n\n'
} > "${log_file}"

cd "${REPO_ROOT}"
nohup "${SCRIPT_DIR}/train_neuralseed.sh" "$@" >> "${log_file}" 2>&1 < /dev/null &
pid="$!"

printf '%s\n' "${pid}" > "${pid_file}"
ln -sfn "$(basename "${log_file}")" "${latest_log}"
ln -sfn "$(basename "${pid_file}")" "${latest_pid}"

echo "Started NeuralSeed training in the background."
echo "PID: ${pid}"
echo "Log: ${log_file}"
echo "Follow log:"
echo "  ./tools/neuralseed/tail_neuralseed_log.sh ${effect}"
