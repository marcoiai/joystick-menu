Basic commands

export DYLD_FRAMEWORK_PATH=./                                                 
./joystick_menu

clang list_joysticks.c -o list_joysticks -F/Library/Frameworks -framework SDL3
gcc joystick_menu.c -o joystick_menu -I/usr/local/include -L/usr/local/lib -lSDL3 -lSDL3_image (last to work)

the roms and bios reside in directory:
~/mame/roms in form of .zip files or .rom



