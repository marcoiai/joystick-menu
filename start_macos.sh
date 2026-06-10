#!/bin/bash

set -euo pipefail

ROOT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
cd "${ROOT_DIR}"

export PATH="/opt/homebrew/bin:/usr/local/bin:$PATH"

BUILD_CMD=(
  gcc
  joystick_menu.c
  -o
  joystick_menu
)

while IFS= read -r token; do
  BUILD_CMD+=("${token}")
done < <(pkg-config --cflags --libs sdl3 sdl3-image sdl3-mixer sdl3-ttf)

ensure_build() {
  if [[ ! -x "./joystick_menu" || "./joystick_menu.c" -nt "./joystick_menu" ]]; then
    echo "Building joystick_menu..."
    "${BUILD_CMD[@]}"
  fi
}

MODE="${1:-menu}"

case "${MODE}" in
  menu)
    ./scripts/live-stream/stop-macos.sh --quiet >/dev/null 2>&1 || true
    ensure_build
    exec ./joystick_menu
    ;;
  live|cast|bridge)
    ./scripts/live-stream/stop-macos.sh --quiet >/dev/null 2>&1 || true
    LIVE_URL="$(./scripts/live-stream/start-macos.sh)"
    echo "Live Cast URL: ${LIVE_URL}"
    echo "Load that URL in your player/cast workflow, then close the menu when done."
    ensure_build
    export JOYSTICK_MENU_PRESERVE_LIVE_STREAM=1
    exec ./joystick_menu
    ;;
  stop)
    exec ./scripts/live-stream/stop-macos.sh
    ;;
  *)
    cat <<'EOF'
Usage:
  ./start_macos.sh          Start the menu normally
  ./start_macos.sh live     Start the Live Cast Bridge, then launch the menu
  ./start_macos.sh stop     Stop the Live Cast Bridge
EOF
    exit 1
    ;;
esac
