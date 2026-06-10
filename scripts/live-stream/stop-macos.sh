#!/bin/bash

set -euo pipefail

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
ROOT_DIR="$(cd "${SCRIPT_DIR}/../.." && pwd)"
STATE_DIR="${ROOT_DIR}/live-stream"
RUN_DIR="${STATE_DIR}/run"
LOG_DIR="${STATE_DIR}/logs"
LIVE_DIR="${STATE_DIR}/public/live"
QUIET="${1:-}"
FFMPEG_LOG="${LOG_DIR}/ffmpeg-live.log"

mkdir -p "${LOG_DIR}"

log_ffmpeg() {
  local message="$1"
  printf '%s %s\n' "$(date '+%Y-%m-%d %H:%M:%S')" "${message}" >>"${FFMPEG_LOG}"
}

stop_pid_file() {
  local pid_file="$1"
  local label="$2"
  local pid=""

  if [[ ! -f "${pid_file}" ]]; then
    return 0
  fi

  pid="$(tr -d '[:space:]' < "${pid_file}")"
  rm -f "${pid_file}"

  if [[ -z "${pid}" ]] || ! kill -0 "${pid}" 2>/dev/null; then
    return 0
  fi

  if [[ "${label}" == "ffmpeg" ]]; then
    log_ffmpeg "Stopping FFmpeg PID ${pid} with SIGTERM"
  fi
  kill "${pid}" 2>/dev/null || true
  for _ in $(seq 1 20); do
    if ! kill -0 "${pid}" 2>/dev/null; then
      if [[ "${label}" == "ffmpeg" ]]; then
        log_ffmpeg "FFmpeg PID ${pid} stopped cleanly"
      fi
      return 0
    fi
    sleep 0.1
  done

  if [[ "${label}" == "ffmpeg" ]]; then
    log_ffmpeg "FFmpeg PID ${pid} did not exit after SIGTERM; sending SIGKILL"
  fi
  kill -9 "${pid}" 2>/dev/null || true
  if [[ "${label}" == "ffmpeg" ]]; then
    log_ffmpeg "FFmpeg PID ${pid} stopped after SIGKILL"
  fi
}

stop_pid_file "${RUN_DIR}/ffmpeg.pid" "ffmpeg"
stop_pid_file "${RUN_DIR}/http-server.pid" "http-server"

rm -rf "${LIVE_DIR}"
mkdir -p "${LIVE_DIR}"

cat > "${STATE_DIR}/status.txt" <<EOF
state=stopped
EOF

if [[ "${QUIET}" != "--quiet" ]]; then
  printf '%s\n' "Live Cast bridge stopped."
fi
