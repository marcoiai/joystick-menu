# Live Cast Handoff

This folder preserves the current live-cast work so it can be moved to another branch or machine without relying on chat history.

## Files

- `handoff/live-cast-macos.patch`
- `handoff/LIVE_CAST_HANDOFF.md`

## Apply On Another Branch

From the repo root:

```bash
git apply handoff/live-cast-macos.patch
```

If the target branch already changed the same files, use:

```bash
git apply --reject --whitespace=fix handoff/live-cast-macos.patch
```

## What The Patch Includes

- `joystick_menu.c`
  - adds the `Live Cast Bridge` menu item
  - shows start/stop status text in the menu
  - starts the bridge before ROM launch and stops it after launch exits on macOS
- `README.md`
  - documents the live-cast workflow and tuning env vars
- `start_macos.sh`
  - wrapper to build, launch, or stop the macOS live-cast flow
- `stop_macos.sh`
  - wrapper to stop the bridge
- `scripts/live-stream/start-macos.sh`
  - starts the local HLS server
  - selects AVFoundation video/audio devices
  - supports BlackHole or another loopback device for emulator audio
  - supports latency and scaling env vars
  - includes audio resync via `aresample`
- `scripts/live-stream/stop-macos.sh`
  - stops ffmpeg and the HTTP server cleanly
- `scripts/live-stream/http_server.py`
  - serves `m3u8` and `ts` HLS files with no-cache headers

## Working State Reached

- Android playback works with VLC using:

```text
http://<mac-lan-ip>:8600/live/index.m3u8
```

- Emulator audio works when:
  - `BlackHole 2ch` is installed
  - macOS output is set to a `Multi-Output Device`
  - that Multi-Output Device includes both the normal speakers and `BlackHole 2ch`
  - the bridge is started with `JOYSTICK_MENU_LIVE_STREAM_AUDIO_INDEX=0` or `JOYSTICK_MENU_LIVE_STREAM_AUDIO_NAME="BlackHole 2ch"`

## Known Good Example Command

```bash
./start_macos.sh stop
JOYSTICK_MENU_LIVE_STREAM_AUDIO_INDEX=0 \
JOYSTICK_MENU_LIVE_STREAM_SEGMENT_SECONDS=1 \
JOYSTICK_MENU_LIVE_STREAM_LIST_SIZE=4 \
JOYSTICK_MENU_LIVE_STREAM_WIDTH=960 \
JOYSTICK_MENU_LIVE_STREAM_FPS=24 \
./start_macos.sh live
```

## Important Note For Linux

The patch is safe to apply on Linux, but the helper scripts in `scripts/live-stream/` are macOS-specific because they use AVFoundation device capture.

The C side is already guarded with `#ifdef __APPLE__`, so Linux builds should still compile. To make live casting work on Linux, the next step will be replacing the macOS capture logic with a Linux capture path such as:

- `x11grab` for X11
- PipeWire for Wayland
- a Linux-friendly HTTP/HLS wrapper around that capture pipeline

## Resume Point

When resuming on Linux, start from:

1. apply the patch
2. verify the branch still builds
3. decide whether Linux capture target is X11, Wayland, or both
4. replace `scripts/live-stream/start-macos.sh` with a Linux capture script or add a parallel Linux script

