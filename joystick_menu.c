/*
 * This example code creates a simple menu navigable by joystick input.
 * It dynamically lists ROMs from a file and launches them via MAME.
 *
 * This code is public domain. Feel free to use it for any purpose!
 */

#define SDL_MAIN_USE_CALLBACKS 1 /* use the callbacks instead of main() */
#include <SDL3/SDL.h>
#include <SDL3/SDL_main.h>
#include <SDL3/SDL_events.h> // Required for SDL_Event and SDL_PushEvent
#include <stdio.h> // For printf and SDL_Log, FILE operations
#include <string.h> // For strcspn, strcmp, strtok_r (more portable than strsep)
#include <stdlib.h> // For system, strtol, malloc, free
#include <errno.h> // For errno and strerror
#include <dirent.h> // For directory scanning
#include <sys/stat.h> // For stat (to check if it's a file)

// --- Global Variables and Constants ---
static SDL_Window *window = NULL;
static SDL_Renderer *renderer = NULL;

// Struct to hold details for each ROM menu item
typedef struct RomMenuItem {
    char *display_name;   // e.g., "Super Mario World (USA)"
    char *mame_system;    // e.g., "snes" or "null" for arcade games (short mame system name)
    char *rom_full_path;  // e.g., "/Users/.../Super Mario World (USA).sfc"
} RomMenuItem;

static RomMenuItem *rom_menu_items = NULL;
static int rom_menu_item_count = 0;
static int selected_option_index = 0;

// Cooldown for joystick axis/hat movement to prevent rapid scrolling
#define INPUT_COOLDOWN_MS 200 // 200 milliseconds between actionable inputs
static Uint64 last_input_time = 0;

// Threshold for joystick axis motion (value from -32768 to 32767)
#define AXIS_DEADZONE 8000 // Ignore small movements, require significant push

// --- Function Prototypes ---
static void handle_joystick_input(const SDL_Event *event);
static void draw_menu(void);
static int scan_rom_directory(const char *dir_path);
static void cleanup_rom_list(void);

// --- SDL Callbacks (required for SDL_MAIN_USE_CALLBACKS) ---

/* This function runs once at startup. */
SDL_AppResult SDL_AppInit(void **appstate, int argc, char *argv[])
{
    // Initialize SDL subsystems: Video for display, Joystick for input
    if (!SDL_Init(SDL_INIT_VIDEO | SDL_INIT_JOYSTICK)) {
        SDL_Log("Couldn't initialize SDL: %s", SDL_GetError());
        return SDL_APP_FAILURE;
    }

    // Create a window and a 2D rendering context
    // Using dimensions and title from the original working example
    if (!SDL_CreateWindowAndRenderer("examples/input/joystick-events", 640, 480, 0, &window, &renderer)) {
        SDL_Log("Couldn't create window/renderer: %s", SDL_GetError());
        SDL_Quit(); // Quit SDL if window/renderer creation fails
        return SDL_APP_FAILURE;
    }

    // Scan ROMs from the specified directory
    // Assuming a 'roms' subfolder relative to the executable
    if (scan_rom_directory("./roms/") != 0) {
        SDL_Log("Failed to scan ROM directory. Menu might be empty.");
    } else {
        SDL_Log("Successfully scanned %d ROMs.", rom_menu_item_count);
    }

    SDL_Log("SDL App Initialized successfully.");
    return SDL_APP_CONTINUE; /* carry on with the program! */
}

/* This function runs when a new event (mouse input, keypresses, etc) occurs. */
SDL_AppResult SDL_AppEvent(void *appstate, SDL_Event *event)
{
    if (event->type == SDL_EVENT_QUIT) {
        return SDL_APP_SUCCESS; /* end the program, reporting success to the OS. */
    } else if (event->type == SDL_EVENT_JOYSTICK_ADDED) {
        // Handle a new joystick being plugged in or already connected at startup
        const SDL_JoystickID which = event->jdevice.which;
        SDL_Joystick *joystick = SDL_OpenJoystick(which);
        if (!joystick) {
            SDL_Log("Joystick #%u add, but not opened: %s", (unsigned int)which, SDL_GetError());
        } else {
            SDL_Log("Joystick #%u ('%s') added and opened.", (unsigned int)which, SDL_GetJoystickName(joystick));
        }
    } else if (event->type == SDL_EVENT_JOYSTICK_REMOVED) {
        // Handle a joystick being unplugged
        const SDL_JoystickID which = event->jdevice.which;
        SDL_Joystick *joystick = SDL_GetJoystickFromID(which); // Get the pointer if it's still valid
        if (joystick) {
            SDL_CloseJoystick(joystick); // Close the joystick device
        }
        SDL_Log("Joystick #%u removed.", (unsigned int)which);
    } else {
        // Pass relevant joystick events to our custom handler
        handle_joystick_input(event);
    }

    return SDL_APP_CONTINUE; /* carry on with the program! */
}

/* This function runs once per frame, and is the heart of the program. */
SDL_AppResult SDL_AppIterate(void *appstate)
{
    // Clear the renderer with a black color
    SDL_SetRenderDrawColor(renderer, 0, 0, 0, 255);
    SDL_RenderClear(renderer);

    // Draw the menu (now uses dynamically loaded ROMs)
    draw_menu();

    // Present the rendered content to the window
    SDL_RenderPresent(renderer);

    return SDL_APP_CONTINUE; /* carry on with the program! */
}

/* This function runs once at shutdown. */
void SDL_AppQuit(void *appstate, SDL_AppResult result)
{
    // Clean up dynamically allocated ROM menu items
    cleanup_rom_list();

    // SDL will clean up the window/renderer automatically when using SDL_MAIN_USE_CALLBACKS.
    // Explicitly quitting SDL ensures all subsystems are deinitialized.
    SDL_Log("SDL App Quitting with result: %d", (int)result);
    SDL_Quit();
}

// --- Custom Functions for Menu Logic and Drawing ---

// handle_joystick_input: Processes joystick events for menu navigation
static void handle_joystick_input(const SDL_Event *event)
{
    Uint64 now = SDL_GetTicks();

    // Apply cooldown to prevent multiple selections/movements from a single brief input
    // This is especially important for analog stick movements.
    if (now < last_input_time + INPUT_COOLDOWN_MS) {
        return;
    }

    switch (event->type) {
        case SDL_EVENT_JOYSTICK_AXIS_MOTION: {
            // Check for Y-axis (axis 1 is common for vertical stick movement)
            if (event->jaxis.axis == 1) {
                // Corrected logic for UP/DOWN
                if (event->jaxis.value < -AXIS_DEADZONE) { // Moved Up
                    if (rom_menu_item_count > 0) {
                        selected_option_index = (selected_option_index - 1 + rom_menu_item_count) % rom_menu_item_count;
                        last_input_time = now;
                    }
                } else if (event->jaxis.value > AXIS_DEADZONE) { // Moved Down
                    if (rom_menu_item_count > 0) {
                        selected_option_index = (selected_option_index + 1) % rom_menu_item_count;
                        last_input_time = now;
                    }
                }
            }
            break;
        }
        case SDL_EVENT_JOYSTICK_HAT_MOTION: {
            // Handle D-pad (hat) movements - Corrected logic for UP/DOWN
            if (event->jhat.value == SDL_HAT_UP) {
                if (rom_menu_item_count > 0) {
                    selected_option_index = (selected_option_index - 1 + rom_menu_item_count) % rom_menu_item_count;
                    last_input_time = now;
                }
            } else if (event->jhat.value == SDL_HAT_DOWN) {
                if (rom_menu_item_count > 0) {
                    selected_option_index = (selected_option_index + 1) % rom_menu_item_count;
                    last_input_time = now;
                }
            }
            break;
        }
        case SDL_EVENT_JOYSTICK_BUTTON_DOWN: {
            // Handle button press for selection (Button 0 is often 'A'/'X')
            if (event->jbutton.button == 0) {
                if (rom_menu_item_count > 0 && selected_option_index < rom_menu_item_count) {
                    RomMenuItem *selected_item = &rom_menu_items[selected_option_index];

                    if (selected_item->display_name && strcmp(selected_item->display_name, "Exit") == 0) {
                        SDL_Log("Selected: Exit. Quitting application.");
                        SDL_Event quit_event;
                        quit_event.type = SDL_EVENT_QUIT;
                        quit_event.quit.timestamp = SDL_GetTicks();
                        SDL_PushEvent(&quit_event);
                    } else {
                        SDL_Log("Selected: %s", selected_item->display_name);

                        // --- Launch MAME instance ---
                        char command_buffer[2048]; // Sufficiently large buffer for the command
                        //const char *mame_executable_path = "/Users/auser/Downloads/mame-mame0277/mame"; // <-- VERIFY THIS PATH!
                        const char *mame_executable_path = "/Users/auser/Downloads/mame0277-arm64/mame"; // <-- VERIFY THIS PATH!

                        if (selected_item->mame_system && strcmp(selected_item->mame_system, "null") == 0) { // Arcade game
                            // For arcade, MAME expects the short name (e.g., 'pacman') and -rompath to the directory.
                            // Need to extract the filename (short name) and its parent directory from rom_full_path.
                            const char *rom_file_name_ptr = SDL_strrchr(selected_item->rom_full_path, '/');
                            if (rom_file_name_ptr) {
                                rom_file_name_ptr++; // Move past the slash to get the actual filename
                            } else {
                                rom_file_name_ptr = selected_item->rom_full_path; // No slash, assume it's just the filename
                            }

                            // Duplicate the rom_full_path to get the directory
                            char *rom_directory_temp = SDL_strdup(selected_item->rom_full_path);
                            if (!rom_directory_temp) {
                                SDL_Log("Memory allocation failed for rom_directory_temp.");
                                return;
                            }
                            char *last_slash = SDL_strrchr(rom_directory_temp, '/');
                            if (last_slash) {
                                *last_slash = '\0'; // Null-terminate to get the directory path
                            } else {
                                // If no slash, it means the rom_path itself is just a filename,
                                // so assume current directory.
                                SDL_strlcpy(rom_directory_temp, ".", 2);
                            }

                            // Extract base name (without extension) for MAME's short name
                            char rom_short_name[256];
                            SDL_strlcpy(rom_short_name, rom_file_name_ptr, sizeof(rom_short_name));
                            char *dot = SDL_strrchr(rom_short_name, '.');
                            if (dot) {
                                *dot = '\0'; // Null-terminate at the dot to remove extension
                            }

                            SDL_snprintf(command_buffer, sizeof(command_buffer),
                                         "\"%s\" -rompath \"%s\" %s",
                                         mame_executable_path, rom_directory_temp, rom_short_name);
                            SDL_free(rom_directory_temp); // Free duplicated string
                        } else { // Console game (e.g., SNES, Genesis)
                            // MAME Console Command: <mame_exe> <system_name> -cart <full_rom_path>
                            SDL_snprintf(command_buffer, sizeof(command_buffer),
                                         "\"%s\" %s -cart \"%s\"",
                                         mame_executable_path, selected_item->mame_system, selected_item->rom_full_path);
                        }

                        SDL_Log("Executing command: %s", command_buffer);
                        int ret = system(command_buffer); // Execute the command
                        if (ret == -1) {
                            SDL_Log("Error launching MAME: %s", strerror(errno));
                        } else if (ret != 0) {
                            SDL_Log("MAME exited with non-zero status: %d", ret);
                        } else {
                            SDL_Log("MAME launched successfully.");
                        }
                    }
                }
                last_input_time = now;
            }
            break;
        }
        default:
            // Ignore other joystick events not relevant to menu navigation
            break;
    }
}

// draw_menu: Renders the menu options on the screen
static void draw_menu(void)
{
    int win_w, win_h;
    SDL_GetWindowSize(window, &win_w, &win_h);

    // Calculate vertical starting position to center the menu
    int line_height = SDL_DEBUG_TEXT_FONT_CHARACTER_SIZE + 10;
    int total_menu_height = rom_menu_item_count * line_height;
    int start_y = (win_h - total_menu_height) / 2;

    for (int i = 0; i < rom_menu_item_count; ++i) {
        Uint8 text_color_r = 200, text_color_g = 200, text_color_b = 200; // Default: Light gray

        // Highlight the currently selected option
        if (i == selected_option_index) {
            text_color_r = 255;
            text_color_g = 255;
            text_color_b = 0; // Selected: Yellow
        }

        SDL_SetRenderDrawColor(renderer, text_color_r, text_color_g, text_color_b, 255);

        // Calculate X position for centering text horizontally
        float text_width = SDL_strlen(rom_menu_items[i].display_name) * SDL_DEBUG_TEXT_FONT_CHARACTER_SIZE;
        float x = (win_w - text_width) / 2.0f;
        float y = (float)(start_y + i * line_height);

        // Render the text using SDL's debug text function
        SDL_RenderDebugText(renderer, x, y, rom_menu_items[i].display_name);
    }
}

// scan_rom_directory: Scans a directory for ROM files and populates the menu.
static int scan_rom_directory(const char *dir_path) {
    DIR *dir;
    struct dirent *entry;
    struct stat filestat;
    char full_path[2048]; // Buffer for full file path

    // List of supported extensions and their corresponding MAME system names
    // For arcade .zip/.7z, use "null" for mame_system
    const struct {
        const char *ext;
        const char *mame_sys;
    } supported_rom_types[] = {
        { "nes", "nes" },
        { "sfc", "snes" },
        { "smc", "snes" },
        { "md", "genesis" },
        { "gen", "genesis" },
        { "bin", "genesis" }, // Can be Genesis, or others - check context
        { "zip", "null" },    // Assuming .zip is for arcade games
        { "7z",  "null" },    // Assuming .7z is for arcade games
        { "gb", "gameboy" },
        { "gba", "gba" },
        { "n64", "n64" },
        { "ps1", "psx" },
        // Add more as needed
        { NULL, NULL } // Sentinel
    };

    dir = opendir(dir_path);
    if (!dir) {
        SDL_Log("Error: Could not open ROM directory '%s': %s", dir_path, strerror(errno));
        return -1;
    }

    int capacity = 10;
    rom_menu_items = (RomMenuItem *)SDL_calloc(capacity, sizeof(RomMenuItem));
    if (!rom_menu_items) {
        SDL_Log("Memory allocation failed for ROM list during scan.");
        closedir(dir);
        return -1;
    }
    rom_menu_item_count = 0;

    while ((entry = readdir(dir)) != NULL) {
        // Skip '.' and '..'
        if (strcmp(entry->d_name, ".") == 0 || strcmp(entry->d_name, "..") == 0) {
            continue;
        }

        SDL_snprintf(full_path, sizeof(full_path), "%s%s", dir_path, entry->d_name);

        if (stat(full_path, &filestat) == -1) {
            SDL_Log("Error stating file %s: %s", full_path, strerror(errno));
            continue;
        }

        if (S_ISREG(filestat.st_mode)) { // Check if it's a regular file
            const char *extension = SDL_strrchr(entry->d_name, '.');
            if (extension) {
                extension++; // Move past the dot
                for (int i = 0; supported_rom_types[i].ext != NULL; ++i) {
                    if (SDL_strcmp(extension, supported_rom_types[i].ext) == 0) {
                        // Found a supported ROM type

                        // Reallocate if needed
                        if (rom_menu_item_count >= capacity) {
                            capacity *= 2;
                            RomMenuItem *new_items = (RomMenuItem *)SDL_realloc(rom_menu_items, capacity * sizeof(RomMenuItem));
                            if (!new_items) {
                                SDL_Log("Memory reallocation failed for ROM list during scan. Stopping.");
                                closedir(dir);
                                cleanup_rom_list(); // Clean up what was already allocated
                                return -1;
                            }
                            rom_menu_items = new_items;
                        }

                        // Populate RomMenuItem
                        rom_menu_items[rom_menu_item_count].display_name = SDL_strdup(entry->d_name);
                        rom_menu_items[rom_menu_item_count].mame_system = SDL_strdup(supported_rom_types[i].mame_sys);
                        rom_menu_items[rom_menu_item_count].rom_full_path = SDL_strdup(full_path);

                        if (!rom_menu_items[rom_menu_item_count].display_name ||
                            !rom_menu_items[rom_menu_item_count].mame_system ||
                            !rom_menu_items[rom_menu_item_count].rom_full_path) {
                            SDL_Log("Memory allocation failed for ROM item strings.");
                            // Clean up partially allocated item
                            SDL_free(rom_menu_items[rom_menu_item_count].display_name);
                            SDL_free(rom_menu_items[rom_menu_item_count].mame_system);
                            SDL_free(rom_menu_items[rom_menu_item_count].rom_full_path);
                            continue; // Skip this ROM
                        }
                        rom_menu_item_count++;
                        break; // Move to next directory entry
                    }
                }
            }
        }
    }
    closedir(dir);

    // Add an "Exit" option at the end of the list
    if (rom_menu_item_count >= capacity) {
        capacity++; // Just need one more slot
        RomMenuItem *new_items = (RomMenuItem *)SDL_realloc(rom_menu_items, capacity * sizeof(RomMenuItem));
        if (!new_items) {
            SDL_Log("Memory reallocation failed for Exit option.");
            // Continue without an Exit option, or handle as fatal.
        } else {
            rom_menu_items = new_items;
        }
    }

    if (rom_menu_items) { // Ensure rom_menu_items is not NULL before trying to add Exit
        rom_menu_items[rom_menu_item_count].display_name = SDL_strdup("Exit");
        rom_menu_items[rom_menu_item_count].mame_system = SDL_strdup("null"); // No system needed for Exit
        rom_menu_items[rom_menu_item_count].rom_full_path = SDL_strdup("null"); // No path needed for Exit
        if (rom_menu_items[rom_menu_item_count].display_name &&
            rom_menu_items[rom_menu_item_count].mame_system &&
            rom_menu_items[rom_menu_item_count].rom_full_path) {
            rom_menu_item_count++;
        } else {
             SDL_Log("Failed to allocate Exit option strings.");
             SDL_free(rom_menu_items[rom_menu_item_count].display_name);
             SDL_free(rom_menu_items[rom_menu_item_count].mame_system);
             SDL_free(rom_menu_items[rom_menu_item_count].rom_full_path);
        }
    }


    return 0; // Success
}


// cleanup_rom_list: Frees all dynamically allocated memory for ROM menu items.
static void cleanup_rom_list(void) {
    if (rom_menu_items) {
        for (int i = 0; i < rom_menu_item_count; ++i) {
            SDL_free(rom_menu_items[i].display_name);
            SDL_free(rom_menu_items[i].mame_system);
            SDL_free(rom_menu_items[i].rom_full_path);
        }
        SDL_free(rom_menu_items);
        rom_menu_items = NULL;
        rom_menu_item_count = 0;
    }
}
