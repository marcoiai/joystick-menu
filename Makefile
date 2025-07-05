# Path to Windows SDL3 libraries and includes
SDL_WIN_DIR = sdl3-win

# Compiler
CC = x86_64-w64-mingw32-gcc

# Compiler flags
CFLAGS = -I$(SDL_WIN_DIR)/include

# Linker flags
LDFLAGS = -L$(SDL_WIN_DIR)/lib -lSDL3 -lSDL3_image -lSDL3_mixer -lSDL3_ttf

# Output
TARGET = joystick_menu.exe

# Sources
SRC = joystick_menu.c

# Build target
all: $(TARGET)

$(TARGET): $(SRC)
	$(CC) $(SRC) -o $(TARGET) $(CFLAGS) $(LDFLAGS)

# Clean
clean:
	rm -f $(TARGET)
