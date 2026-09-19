#!/usr/bin/env bash
set -euo pipefail

SDL_PKGS=(sdl3 sdl3-image sdl3-ttf sdl3-mixer)

have_build_deps() {
  command -v gcc >/dev/null 2>&1 &&
  command -v pkg-config >/dev/null 2>&1 &&
  command -v curl >/dev/null 2>&1 &&
  command -v mame >/dev/null 2>&1 || return 1

  for pkg in "${SDL_PKGS[@]}"; do
    pkg-config --exists "$pkg" || return 1
  done
}

install_deps() {
  echo "Missing dependencies. Installing..."

  if command -v pacman >/dev/null 2>&1; then
    sudo pacman -S --needed --noconfirm \
      base-devel pkgconf curl mame \
      sdl3 sdl3_image sdl3_ttf sdl3_mixer
  elif command -v apt-get >/dev/null 2>&1; then
    sudo apt-get update
    sudo apt-get install -y \
      build-essential pkg-config curl mame \
      libsdl3-dev libsdl3-image-dev libsdl3-ttf-dev libsdl3-mixer-dev
  elif command -v dnf >/dev/null 2>&1; then
    sudo dnf install -y \
      gcc gcc-c++ make pkgconf-pkg-config curl mame \
      SDL3-devel SDL3_image-devel SDL3_ttf-devel SDL3_mixer-devel
  else
    echo "Unsupported package manager."
    echo "Install GCC, pkg-config, curl, MAME, SDL3, SDL3_image, SDL3_ttf and SDL3_mixer manually."
    exit 1
  fi
}

if ! have_build_deps; then
  install_deps
fi

if ! have_build_deps; then
  echo "Dependencies are still missing after installation."
  exit 1
fi

echo "Building joystick_menu..."
gcc joystick_menu.c -o joystick_menu \
  $(pkg-config --cflags --libs "${SDL_PKGS[@]}")

echo "Build complete: ./joystick_menu"
