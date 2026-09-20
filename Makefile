UNAME_S := $(shell uname -s)

SRC = joystick_menu.c

ifeq ($(UNAME_S),Darwin)
CC ?= clang
TARGET ?= joystick_menu
SDL_MODULES = sdl3 sdl3-image sdl3-ttf sdl3-mixer
CFLAGS += -O2 -Wall -Wextra $(shell pkg-config --cflags $(SDL_MODULES))
LDFLAGS += $(shell pkg-config --libs $(SDL_MODULES))
else
# Path to Windows SDL3 libraries and includes
SDL_WIN_DIR = sdl3-win
CC = x86_64-w64-mingw32-gcc
TARGET = joystick_menu.exe
CFLAGS = -I$(SDL_WIN_DIR)/include
LDFLAGS = -L$(SDL_WIN_DIR)/lib -lSDL3 -lSDL3_image -lSDL3_mixer -lSDL3_ttf
endif

all: $(TARGET)

$(TARGET): $(SRC)
	$(CC) $(CFLAGS) $(SRC) -o $@ $(LDFLAGS)

# Clean
clean:
	rm -f $(TARGET)

ifeq ($(UNAME_S),Darwin)
install-macos-deps:
	brew install pkg-config sdl3 sdl3_image sdl3_ttf sdl3_mixer
	brew install --cask pcsx2
	brew install sevenzip
	@set -e; \
	if test -x /Applications/RPCS3.app/Contents/MacOS/rpcs3; then \
		printf 'RPCS3 is already installed.\n'; \
	else \
		tmp_dir=$$(mktemp -d); \
		archive="$$tmp_dir/rpcs3-macos.7z"; \
		url=$$(curl -fsSL https://api.github.com/repos/RPCS3/rpcs3-binaries-mac/releases/latest | sed -n 's/.*"browser_download_url": "\([^"]*macos\.7z\)".*/\1/p' | head -n 1); \
		test -n "$$url"; \
		curl -fL "$$url" -o "$$archive"; \
		7zz x "$$archive" -o"$$tmp_dir/extracted" >/dev/null; \
		app_path=$$(find "$$tmp_dir/extracted" -type d -name 'RPCS3.app' -print -quit); \
		test -n "$$app_path"; \
		ditto "$$app_path" /Applications/RPCS3.app; \
		rm -rf "$$tmp_dir"; \
		printf 'RPCS3 installed in /Applications/RPCS3.app.\n'; \
	fi
endif
