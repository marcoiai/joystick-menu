#!/usr/bin/env bash
set -euo pipefail

PACKAGES=(sdl3 sdl3-image sdl3-ttf sdl3-mixer)

missing=()
for pkg in "${PACKAGES[@]}"; do
  pkg-config --exists "$pkg" || missing+=("$pkg")
done

if ((${#missing[@]})); then
  echo "Missing SDL3 dependencies: ${missing[*]}"
  echo
  if command -v pacman >/dev/null 2>&1; then
    echo "CachyOS/Arch: sudo pacman -S --needed base-devel pkgconf sdl3 sdl3_image sdl3_ttf sdl3_mixer"
  elif command -v apt >/dev/null 2>&1; then
    echo "Debian/Ubuntu: install gcc, pkg-config and the SDL3 development packages available for your release."
  elif command -v dnf >/dev/null 2>&1; then
    echo "Fedora: install gcc, pkgconf-pkg-config and the SDL3 development packages available for your release."
  fi
  exit 1
fi

echo "Building joystick_menu..."
gcc joystick_menu.c -o joystick_menu \
  $(pkg-config --cflags --libs "${PACKAGES[@]}")

echo "Build complete: ./joystick_menu"
