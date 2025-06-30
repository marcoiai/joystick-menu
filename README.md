Basic commands

Requirements:
SDL3
SDL3_image
SDL3_ttf (earlier version used SDL_RenderDebugText)

export DYLD_FRAMEWORK_PATH=./ (here goes path of LD Library on Linux and DYlD Library paths - adjust it's inconsistent)
./joystick_menu

clang list_joysticks.c -o list_joysticks -F/Library/Frameworks -framework SDL3
clang joystick_menu.c -o joystick_menu -I/opt/homebrew/include -L/opt/homebrew/lib -lSDL3 -lSDL3_image -lSDL3_ttf (macOS last to work)

gcc joystick_menu.c -o joystick_menu -I/usr/local/include -L/usr/local/lib -lSDL3 -lSDL3_image

gcc joystick_menu.c -o joystick_menu -I/usr/local/include -L/usr/local/lib -lSDL3 -lSDL3_ttf -lSDL3_image  (last to work)

the roms and bios reside in directory:
~/mame/roms in form of .zip files or .rom

