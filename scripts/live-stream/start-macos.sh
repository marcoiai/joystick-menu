#!/bin/bash

set -euo pipefail

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
ROOT_DIR="$(cd "${SCRIPT_DIR}/../.." && pwd)"
STATE_DIR="${ROOT_DIR}/live-stream"
RUN_DIR="${STATE_DIR}/run"
LOG_DIR="${STATE_DIR}/logs"
PUBLIC_DIR="${STATE_DIR}/public"
LIVE_DIR="${PUBLIC_DIR}/live"

PORT="${JOYSTICK_MENU_LIVE_STREAM_PORT:-8600}"
FPS="${JOYSTICK_MENU_LIVE_STREAM_FPS:-30}"
WIDTH="${JOYSTICK_MENU_LIVE_STREAM_WIDTH:-1280}"
SEGMENT_SECONDS="${JOYSTICK_MENU_LIVE_STREAM_SEGMENT_SECONDS:-1}"
SEGMENT_LIST_SIZE="${JOYSTICK_MENU_LIVE_STREAM_LIST_SIZE:-5}"
CAPTURE_CURSOR="${JOYSTICK_MENU_LIVE_STREAM_CAPTURE_CURSOR:-0}"
VIDEO_SIZE="${JOYSTICK_MENU_LIVE_STREAM_VIDEO_SIZE:-}"
AUDIO_SAMPLE_RATE="${JOYSTICK_MENU_LIVE_STREAM_AUDIO_SAMPLE_RATE:-48000}"
AUDIO_FILTER="${JOYSTICK_MENU_LIVE_STREAM_AUDIO_FILTER:-aresample=async=1000:min_hard_comp=0.100000:first_pts=0}"
SCREEN_INDEX_OVERRIDE="${JOYSTICK_MENU_LIVE_STREAM_SCREEN_INDEX:-}"
SCREEN_NAME_OVERRIDE="${JOYSTICK_MENU_LIVE_STREAM_SCREEN_NAME:-}"
AUDIO_INDEX_OVERRIDE="${JOYSTICK_MENU_LIVE_STREAM_AUDIO_INDEX:-}"
AUDIO_NAME_OVERRIDE="${JOYSTICK_MENU_LIVE_STREAM_AUDIO_NAME:-}"
START_TIMEOUT_SECONDS="${JOYSTICK_MENU_LIVE_STREAM_START_TIMEOUT_SECONDS:-8}"

mkdir -p "${RUN_DIR}" "${LOG_DIR}" "${LIVE_DIR}"

find_binary() {
  local env_value="$1"
  shift

  if [[ -n "${env_value}" && -x "${env_value}" ]]; then
    printf '%s\n' "${env_value}"
    return 0
  fi

  local candidate=""
  candidate="$(command -v "$1" 2>/dev/null || true)"
  if [[ -n "${candidate}" && -x "${candidate}" ]]; then
    printf '%s\n' "${candidate}"
    return 0
  fi

  shift
  for candidate in "$@"; do
    if [[ -n "${candidate}" && -x "${candidate}" ]]; then
      printf '%s\n' "${candidate}"
      return 0
    fi
  done

  return 1
}

FFMPEG_BIN="$(find_binary "${JOYSTICK_MENU_FFMPEG:-}" ffmpeg /opt/homebrew/bin/ffmpeg /usr/local/bin/ffmpeg /opt/local/bin/ffmpeg)" || {
  echo "Could not find ffmpeg. Set JOYSTICK_MENU_FFMPEG or install ffmpeg." >&2
  exit 1
}

PYTHON_BIN="$(find_binary "${JOYSTICK_MENU_PYTHON:-}" python3 /opt/homebrew/bin/python3 /usr/local/bin/python3 /usr/bin/python3)" || {
  echo "Could not find python3. Set JOYSTICK_MENU_PYTHON or install python3." >&2
  exit 1
}

DEVICE_LOG="${LOG_DIR}/ffmpeg-devices.log"
DEVICE_LISTING="$("${FFMPEG_BIN}" -hide_banner -f avfoundation -list_devices true -i "" 2>&1 || true)"
printf '%s\n' "${DEVICE_LISTING}" > "${DEVICE_LOG}"

select_avfoundation_device() {
  local media_kind="$1"
  local preferred_index="$2"
  local preferred_name="$3"

  DEVICE_LISTING="${DEVICE_LISTING}" "${PYTHON_BIN}" - "${media_kind}" "${preferred_index}" "${preferred_name}" <<'PY'
import os
import re
import sys

media_kind = sys.argv[1].strip().lower()
preferred_index = sys.argv[2].strip()
preferred_name = sys.argv[3].strip().lower()
devices = []
in_section = False

start_marker = f"AVFoundation {media_kind} devices"
stop_marker = "AVFoundation audio devices" if media_kind == "video" else None

for line in os.environ.get("DEVICE_LISTING", "").splitlines():
    if start_marker in line:
        in_section = True
        continue
    if stop_marker and stop_marker in line:
        break
    if not in_section:
        continue

    match = re.search(r"\[(\d+)\]\s+(.+)$", line)
    if not match:
        continue
    devices.append((int(match.group(1)), match.group(2).strip()))

if preferred_index:
    for index, name in devices:
        if str(index) == preferred_index:
            print(f"{index}|{name}")
            raise SystemExit(0)

if preferred_name:
    for index, name in devices:
        if preferred_name in name.lower():
            print(f"{index}|{name}")
            raise SystemExit(0)

if media_kind == "video":
    for index, name in devices:
        lowered = name.lower()
        if "capture screen" in lowered or "screen capture" in lowered:
            print(f"{index}|{name}")
            raise SystemExit(0)

    if devices:
        index, name = devices[-1]
        print(f"{index}|{name}")
        raise SystemExit(0)

raise SystemExit(1)
PY
}

SCREEN_DEVICE_INFO="$(select_avfoundation_device "video" "${SCREEN_INDEX_OVERRIDE}" "${SCREEN_NAME_OVERRIDE}")" || {
  cat >&2 <<EOF
Could not find a macOS screen capture device in ffmpeg.
This usually means macOS Screen Recording permission is missing for the app that
launches this script, or the terminal needs to be fully restarted after the
permission was granted.

Check:
  System Settings -> Privacy & Security -> Screen Recording

Then allow your terminal app (Terminal, iTerm, etc.) and, if needed, fully quit
and reopen it before trying again.

Device log: ${DEVICE_LOG}
EOF
  exit 1
}

SCREEN_INDEX="${SCREEN_DEVICE_INFO%%|*}"
SCREEN_NAME="${SCREEN_DEVICE_INFO#*|}"
AUDIO_INDEX=""
AUDIO_NAME=""

if [[ -n "${AUDIO_INDEX_OVERRIDE}" || -n "${AUDIO_NAME_OVERRIDE}" ]]; then
  AUDIO_DEVICE_INFO="$(select_avfoundation_device "audio" "${AUDIO_INDEX_OVERRIDE}" "${AUDIO_NAME_OVERRIDE}")" || {
    cat >&2 <<EOF
Could not find the requested macOS audio capture device in ffmpeg.
Set JOYSTICK_MENU_LIVE_STREAM_AUDIO_INDEX or JOYSTICK_MENU_LIVE_STREAM_AUDIO_NAME
to one of the devices listed in:

  ${DEVICE_LOG}
EOF
    exit 1
  }

  AUDIO_INDEX="${AUDIO_DEVICE_INFO%%|*}"
  AUDIO_NAME="${AUDIO_DEVICE_INFO#*|}"
fi

HOST_NAME="$(scutil --get LocalHostName 2>/dev/null || true)"
DEFAULT_INTERFACE="$(route -n get default 2>/dev/null | awk '/interface:/{print $2; exit}')"
LAN_IP=""

if [[ -n "${DEFAULT_INTERFACE}" ]]; then
  LAN_IP="$(ipconfig getifaddr "${DEFAULT_INTERFACE}" 2>/dev/null || true)"
fi

if [[ -z "${LAN_IP}" ]]; then
  LAN_IP="$(ipconfig getifaddr en0 2>/dev/null || true)"
fi

CURRENT_URL=""
HOSTNAME_URL=""
if [[ -n "${LAN_IP}" ]]; then
  CURRENT_URL="http://${LAN_IP}:${PORT}/live/index.m3u8"
fi
if [[ -n "${HOST_NAME}" ]]; then
  HOSTNAME_URL="http://${HOST_NAME}.local:${PORT}/live/index.m3u8"
fi
if [[ -z "${CURRENT_URL}" ]]; then
  CURRENT_URL="${HOSTNAME_URL:-http://127.0.0.1:${PORT}/live/index.m3u8}"
fi

SERVER_LOG="${LOG_DIR}/http-server.log"
FFMPEG_LOG="${LOG_DIR}/ffmpeg-live.log"

: > "${SERVER_LOG}"
: > "${FFMPEG_LOG}"

log_ffmpeg() {
  local message="$1"
  printf '%s %s\n' "$(date '+%Y-%m-%d %H:%M:%S')" "${message}" >>"${FFMPEG_LOG}"
}

"${SCRIPT_DIR}/stop-macos.sh" --quiet >/dev/null 2>&1 || true

rm -rf "${LIVE_DIR}"
mkdir -p "${LIVE_DIR}"

nohup "${PYTHON_BIN}" "${SCRIPT_DIR}/http_server.py" \
  --bind 0.0.0.0 \
  --port "${PORT}" \
  --directory "${PUBLIC_DIR}" >>"${SERVER_LOG}" 2>&1 &
SERVER_PID=$!
printf '%s\n' "${SERVER_PID}" > "${RUN_DIR}/http-server.pid"

sleep 0.3
if ! kill -0 "${SERVER_PID}" 2>/dev/null; then
  echo "The local HLS server failed to start. See ${SERVER_LOG}." >&2
  exit 1
fi

INPUT_DEVICE="${SCREEN_INDEX}:none"
if [[ -n "${AUDIO_INDEX}" ]]; then
  INPUT_DEVICE="${SCREEN_INDEX}:${AUDIO_INDEX}"
fi

VIDEO_FILTER="setsar=1,fps=${FPS},setpts=N/(${FPS}*TB)"
if [[ "${WIDTH}" =~ ^[0-9]+$ ]] && (( WIDTH > 0 )); then
  VIDEO_FILTER="scale=${WIDTH}:-2:flags=lanczos,${VIDEO_FILTER}"
fi

FFMPEG_ARGS=(
  -f avfoundation
)

if [[ -n "${VIDEO_SIZE}" ]]; then
  FFMPEG_ARGS+=(
    -video_size "${VIDEO_SIZE}"
  )
fi

FFMPEG_ARGS+=(
  -pixel_format uyvy422
  -capture_cursor "${CAPTURE_CURSOR}"
  -i "${INPUT_DEVICE}"
)

FFMPEG_ARGS+=(
  -vf "${VIDEO_FILTER}"
  -c:v libx264
  -preset ultrafast
  -tune zerolatency
  -pix_fmt yuv420p
  -g "${FPS}"
  -keyint_min "${FPS}"
  -sc_threshold 0
)

if [[ -n "${AUDIO_INDEX}" ]]; then
  FFMPEG_ARGS+=(
    -af "${AUDIO_FILTER}"
    -c:a aac
    -b:a 160k
    -ac 2
    -ar "${AUDIO_SAMPLE_RATE}"
  )
fi

FFMPEG_ARGS+=(
  -f hls
  -hls_time "${SEGMENT_SECONDS}"
  -hls_list_size "${SEGMENT_LIST_SIZE}"
  -hls_flags delete_segments
  "${LIVE_DIR}/index.m3u8"
)

format_command() {
  local out=""
  local arg=""
  for arg in "$@"; do
    if [[ -z "${out}" ]]; then
      out="$(printf '%q' "${arg}")"
    else
      out="${out} $(printf '%q' "${arg}")"
    fi
  done
  printf '%s\n' "${out}"
}

FFMPEG_COMMAND="$(format_command "${FFMPEG_BIN}" "${FFMPEG_ARGS[@]}")"
log_ffmpeg "Selected screen device [${SCREEN_INDEX}] ${SCREEN_NAME}"
if [[ -n "${AUDIO_INDEX}" ]]; then
  log_ffmpeg "Selected audio device [${AUDIO_INDEX}] ${AUDIO_NAME}"
else
  log_ffmpeg "Audio capture disabled"
fi
log_ffmpeg "FFmpeg command started: ${FFMPEG_COMMAND}"

nohup "${FFMPEG_BIN}" "${FFMPEG_ARGS[@]}" >>"${FFMPEG_LOG}" 2>&1 &
FFMPEG_PID=$!
printf '%s\n' "${FFMPEG_PID}" > "${RUN_DIR}/ffmpeg.pid"
log_ffmpeg "FFmpeg PID: ${FFMPEG_PID}"

READY=0
POLL_COUNT=$(( START_TIMEOUT_SECONDS * 10 ))
for _ in $(seq 1 "${POLL_COUNT}"); do
  if [[ -f "${LIVE_DIR}/index.m3u8" ]]; then
    READY=1
    break
  fi

  if ! kill -0 "${FFMPEG_PID}" 2>/dev/null; then
    log_ffmpeg "index.m3u8 did not appear because FFmpeg exited early."
    "${SCRIPT_DIR}/stop-macos.sh" --quiet >/dev/null 2>&1 || true
    echo "ffmpeg stopped before the HLS playlist was created. See ${FFMPEG_LOG}." >&2
    exit 1
  fi

  sleep 0.1
done

if [[ "${READY}" -ne 1 ]]; then
  log_ffmpeg "index.m3u8 did not appear before timeout (${START_TIMEOUT_SECONDS}s)."
  "${SCRIPT_DIR}/stop-macos.sh" --quiet >/dev/null 2>&1 || true
  echo "Timed out waiting for live/index.m3u8. See ${FFMPEG_LOG}." >&2
  exit 1
fi

log_ffmpeg "index.m3u8 appeared at ${LIVE_DIR}/index.m3u8"

printf '%s\n' "${CURRENT_URL}" > "${STATE_DIR}/current-url.txt"
if [[ -n "${HOSTNAME_URL}" ]]; then
  printf '%s\n' "${HOSTNAME_URL}" > "${STATE_DIR}/hostname-url.txt"
fi

cat > "${STATE_DIR}/status.txt" <<EOF
state=running
port=${PORT}
screen_index=${SCREEN_INDEX}
screen_name=${SCREEN_NAME}
audio_index=${AUDIO_INDEX}
audio_name=${AUDIO_NAME}
current_url=${CURRENT_URL}
hostname_url=${HOSTNAME_URL}
EOF

printf '%s\n' "${CURRENT_URL}"
