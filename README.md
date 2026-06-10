Basic commands

Requirements:
SDL3
SDL3_image
SDL3_ttf (earlier version used SDL_RenderDebugText)

export DYLD_FRAMEWORK_PATH=./ (here goes path of LD Library on Linux and DYlD Library paths - adjust it's inconsistent)
./joystick_menu

clang list_joysticks.c -o list_joysticks -F/Library/Frameworks -framework SDL3
clang joystick_menu.c -o joystick_menu -I/opt/homebrew/include -L/opt/homebrew/lib -lSDL3 -lSDL3_image -lSDL3_ttf (macOS last to work)
macOS:
clang list_joysticks.c -o list_joysticks -F/Library/Frameworks -framework SDL3 (macOS last to work)

Live Cast Bridge on macOS:
- Install `ffmpeg` with `brew install ffmpeg`
- Launch `./joystick_menu` or `./start.sh`
- In the main menu, toggle `Live Cast Bridge: OFF/ON`; selecting it again stops future casts
- Launch a ROM; the project starts a local HLS bridge and writes the active URL to `live-stream/current-url.txt`
- Use that URL from `always-player` or any other HLS-capable player on your network
- You can also stop the bridge from the shell with `./start_macos.sh stop`

Notes:
- First launch may require macOS Screen Recording permission for `ffmpeg`
- The default stream is video-only unless you provide an AVFoundation audio device with `JOYSTICK_MENU_LIVE_STREAM_AUDIO_INDEX` or `JOYSTICK_MENU_LIVE_STREAM_AUDIO_NAME`
- To capture emulator sound on macOS, point the bridge at a virtual audio device such as BlackHole or Loopback, for example:
  `JOYSTICK_MENU_LIVE_STREAM_AUDIO_NAME="BlackHole 2ch" ./start_macos.sh`
- If audio sounds fast or glitchy, set the Multi-Output Device, your speakers, and `BlackHole 2ch` to the same sample rate in Audio MIDI Setup. The bridge targets `48,000 Hz` by default.
- If the capture looks cropped or zoomed, set `JOYSTICK_MENU_LIVE_STREAM_VIDEO_SIZE` to a known-good desktop size such as `1920x1080`
- Logs and generated HLS files are written under `live-stream/`

Linux:
gcc joystick_menu.c -o joystick_menu -I/usr/local/include -L/usr/local/lib -lSDL3 -lSDL3_image

gcc joystick_menu.c -o joystick_menu -I/usr/local/include -L/usr/local/lib -lSDL3 -lSDL3_ttf -lSDL3_image

gcc joystick_menu.c -o joystick_menu -I/usr/local/include -L/usr/local/lib -lSDL3 -lSDL3_image -lSDL3_mixer -lSDL3_ttf (last to work)

For Windows on Linux:
sudo apt update
sudo apt install mingw-w64

For Windows on macOS with homebrew:
brew install mingw-w64

Download Windows SDL3 Libraries
You must use precompiled Windows versions of SDL3, SDL3_image, SDL3_mixer, SDL3_ttf:
https://github.com/libsdl-org/SDL/releases
https://github.com/libsdl-org/SDL_image/releases
https://github.com/libsdl-org/SDL_mixer/releases
https://github.com/libsdl-org/SDL_ttf/releases

The launcher now searches MAME content in:
- `./roms`
- `./bios`
- `~/mame/roms` (when launched through `joystick_menu`)

For direct `mame` runs from this repo, the bundled `mame.ini` searches:
- `./roms`
- `./bios`

SNES note:
- Some carts and hacks need extra enhancement-chip dumps beyond `snes.zip`.
- `Super Mario Kart 8 v1.2.sfc` needs `dsp1.bin`.
- MAME will look for that file in a set such as `sns_dsp1leg_hi.zip` or `snes.zip` on the configured `rompath`.

Thanks to https://pixabay.com/music/ for free roalty music, check it out
Music by <a href="https://pixabay.com/users/jumpingbunny-47869633/?utm_source=link-attribution&utm_medium=referral&utm_campaign=music&utm_content=359782">jumpingbunny</a> from <a href="https://pixabay.com/music//?utm_source=link-attribution&utm_medium=referral&utm_campaign=music&utm_content=359782">Pixabay</a>
