Basic commands

Requirements:
SDL3
SDL3_image
SDL3_ttf (earlier version used SDL_RenderDebugText)

export DYLD_FRAMEWORK_PATH=./ (here goes path of LD Library on Linux and DYlD Library paths - adjust it's inconsistent)
./joystick_menu

macOS:
clang list_joysticks.c -o list_joysticks -F/Library/Frameworks -framework SDL3 (macOS last to work)

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

the roms and bios reside in directory:
~/mame/roms in form of .zip files or .rom

Thanks to https://pixabay.com/music/ for free roalty music, check it out
Music by <a href="https://pixabay.com/users/jumpingbunny-47869633/?utm_source=link-attribution&utm_medium=referral&utm_campaign=music&utm_content=359782">jumpingbunny</a> from <a href="https://pixabay.com/music//?utm_source=link-attribution&utm_medium=referral&utm_campaign=music&utm_content=359782">Pixabay</a>
