Basic commands

export DYLD_FRAMEWORK_PATH=./                                                 
./joystick_menu

clang list_joysticks.c -o list_joysticks -F/Library/Frameworks -framework SDL3
