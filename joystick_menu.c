#include <SDL3/SDL.h>
#include <SDL3/SDL_main.h>
#include <SDL3_image/SDL_image.h>
#include <SDL3_ttf/SDL_ttf.h>
#include <SDL3_mixer/SDL_mixer.h>
#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <dirent.h>
#include <sys/stat.h>
#include <unistd.h>
#include <sys/wait.h>

/*
#include <SDL3/SDL.h>
#include <SDL3/SDL_main.h>
#include <SDL3_image/SDL_image.h>
#include <SDL3_ttf/SDL_ttf.h>
#include <SDL3_mixer/SDL_mixer.h>
*/
static Mix_Music *music = NULL;
static SDL_Window *window = NULL;
static SDL_Renderer *renderer = NULL;
static SDL_Texture *logo_texture = NULL;
static SDL_Texture *background_texture = NULL;
static TTF_Font *font = NULL;

#define INPUT_COOLDOWN_MS 200
#define AXIS_DEADZONE 8000
#define LOGO_HEIGHT 200
#define FONT_SIZE 18

#define MAX_INPUT_LENGTH 256
static char input_text[MAX_INPUT_LENGTH] = "";
static int typing_in_input = 0;

static Uint64 last_input_time = 0;

typedef struct {
    const char *dir_name;
    const char *display_name;
    const char *mame_sys;
    const char *launch_arg;
    const char *allowed_exts;
} SystemEntry;

static const SystemEntry systems[] = {
    { "sms1", "Master System", "sms1", "-cart", "sms,bin" },
    { "genesis", "Mega Drive", "genesis", "-cart", "md,bin" },
    { "snes", "Super Nintendo", "snes", "-cart", "smc,sfc" },
    { "nes", "Nintendo 8-bit", "nes", "-cart", "nes" },
    { "segacd", "Mega CD", "segacd", "-cdrom", "cue,chd,iso" },
    { "psu", "PlayStation 1", "psu", "-cdrom", "cue,chd,iso" },
};

static int selected_system_index = 0;
static int system_scroll_offset = 0;
static int in_rom_menu = 0;

static int system_menu_count = sizeof(systems) / sizeof(SystemEntry) + 2;

typedef struct {
    char *display_name;
    char *rom_path;
} RomEntry;

static RomEntry *all_rom_list = NULL; // Stores all ROMs for the current system
static int all_rom_count = 0;

static RomEntry *rom_list = NULL; // Stores the currently filtered ROMs
static int rom_count = 0;
static int selected_rom_index = 0;
static int rom_scroll_offset = 0;

// Function Prototypes (Declarations)
static void draw_system_menu(void);
static void draw_rom_menu(void);
static void load_all_rom_list(const SystemEntry *sys); // Renamed and modified
static void free_all_rom_list(void); // New function to free all_rom_list
static void free_rom_list(void); // Existing, will now free the filtered list
static void filter_rom_list(const char *filter_text); // New filter function
static int has_allowed_extension(const char *filename, const char *allowed_exts);
static void render_text_centered(const char *text, float y, SDL_Color color);
static void render_text(const char *text, float x, float y, SDL_Color color);
static void draw_scrollbar(int item_count, int visible_lines, int scroll_offset, int start_y, int line_height, int win_w);
static int file_exists(const char *path);
static void draw_interactive_input_field(void);

static void handle_joystick_input(const SDL_Event *event);
static void handle_keyboard_input(const SDL_Event *event);

static void draw_system_menu(void) {
    int win_w, win_h; SDL_GetWindowSize(window, &win_w, &win_h);
    int item_count = system_menu_count;
    int line_height = FONT_SIZE + 10;
    int visible_lines = (win_h - LOGO_HEIGHT - 40) / line_height;

    // Adjust scroll offset to keep selected item visible
    if (selected_system_index < system_scroll_offset) {
        system_scroll_offset = selected_system_index;
    } else if (selected_system_index >= system_scroll_offset + visible_lines) {
        system_scroll_offset = selected_system_index - visible_lines + 1;
    }

    int start_y = LOGO_HEIGHT + 20;

    for (int i = 0; i < item_count; ++i) {
        if (i < system_scroll_offset) continue;
        if (i >= system_scroll_offset + visible_lines) break;

        SDL_Color color = { 200, 200, 200, 255 };

        if (i == selected_system_index) color.r = color.g = 255;

        const char *label = NULL;
        if (i < (item_count - 2)) {
            label = systems[i].display_name;
        } else if (i == (item_count - 2)) {
            label = "Run Cover Scraper";
        } else {
            label = "Exit";
        }
        render_text_centered(label, start_y + (i - system_scroll_offset) * line_height, color);
    }

    draw_scrollbar(item_count, visible_lines, system_scroll_offset, start_y, line_height, win_w);
}

static void draw_rom_menu(void) {
    int win_w, win_h; SDL_GetWindowSize(window, &win_w, &win_h);
    int line_height = FONT_SIZE + 10;
    int visible_lines = (win_h - LOGO_HEIGHT - 40) / line_height;

    //SDL_Log("draw_rom_menu called. rom_list: %p, rom_count: %d, selected_rom_index: %d, rom_scroll_offset: %d",
    //        (void*)rom_list, rom_count, selected_rom_index, rom_scroll_offset);

    // Adjust scroll offset to keep selected item visible
    if (selected_rom_index < rom_scroll_offset) {
        rom_scroll_offset = selected_rom_index;
    } else if (selected_rom_index >= rom_scroll_offset + visible_lines) {
        rom_scroll_offset = selected_rom_index - visible_lines + 1;
    }

    int start_y = LOGO_HEIGHT + 20;

    if (!rom_list || rom_count == 0) {
        render_text_centered("No ROMs found or matching filter.", start_y, (SDL_Color){255, 0, 0, 255});
    } else {
        for (int i = 0; i < rom_count; ++i) {
            if (i < rom_scroll_offset) continue;
            if (i >= rom_scroll_offset + visible_lines) break;

            SDL_Color color = { 200, 200, 200, 255 };
            if (i == selected_rom_index) color.r = color.g = 255;

            //SDL_Log("  Rendering ROM item %d: display_name_ptr=%p", i, (void*)rom_list[i].display_name);

            if (rom_list[i].display_name) {
                render_text_centered(rom_list[i].display_name, start_y + (i - rom_scroll_offset) * line_height, color);
            } else {
                SDL_Log("  WARNING: rom_list[%d].display_name is NULL!", i);
                render_text_centered("[NULL NAME]", start_y + (i - rom_scroll_offset) * line_height, (SDL_Color){255, 165, 0, 255});
            }
        }

        draw_scrollbar(rom_count, visible_lines, rom_scroll_offset, start_y, line_height, win_w);
    }

    draw_interactive_input_field(); // Draw input field regardless of ROMs found
}

static void draw_interactive_input_field(void) {
    int win_w, win_h;
    SDL_GetWindowSize(window, &win_w, &win_h);

    float input_y = win_h - FONT_SIZE - 40;
    float input_x = 50.0f;
    float input_width = win_w - 100.0f;
    float input_height = FONT_SIZE + 10;

    SDL_SetRenderDrawColor(renderer, 50, 50, 50, 200);
    SDL_FRect input_bg_rect = {input_x, input_y, input_width, input_height};
    SDL_RenderFillRect(renderer, &input_bg_rect);

    SDL_Color border_color = {100, 100, 100, 255};

    if (typing_in_input) {
        border_color.r = 255;
        border_color.g = 255;
        border_color.b = 0;
    }

    SDL_SetRenderDrawColor(renderer, border_color.r, border_color.g, border_color.b, border_color.a);
    SDL_RenderRect(renderer, &input_bg_rect);

    SDL_Color text_color = {255, 255, 255, 255};
    char display_text[MAX_INPUT_LENGTH + 2];
    snprintf(display_text, sizeof(display_text), "%s%s", input_text, (typing_in_input && (SDL_GetTicks() / 500) % 2) ? "|" : "");
    render_text(display_text, input_x + 5, input_y + 5, text_color);
}

int main(int argc, char *argv[]) {
    SDL_Init(SDL_INIT_VIDEO | SDL_INIT_JOYSTICK | SDL_INIT_AUDIO);
    TTF_Init();

    SDL_CreateWindowAndRenderer("Joystick Menu", 1024, 768, 0, &window, &renderer);
    font = TTF_OpenFont("assets/Roboto-Regular.ttf", FONT_SIZE);

    logo_texture = IMG_LoadTexture(renderer, "assets/logo.png");
    background_texture = IMG_LoadTexture(renderer, "assets/background.jpg");

    if (background_texture) {
        SDL_SetTextureBlendMode(background_texture, SDL_BLENDMODE_BLEND);
        SDL_SetTextureAlphaMod(background_texture, 80);
    }

    Mix_Init(MIX_INIT_OGG);
    SDL_AudioSpec desired_spec = { .freq = 44100, .format = SDL_AUDIO_F32, .channels = 2 };
    Mix_OpenAudio(0, &desired_spec);

    music = Mix_LoadMUS("assets/background1.ogg");

    if (music) {
        Mix_VolumeMusic(64);
        Mix_PlayMusic(music, -1);
    }

    SDL_Event event;
    int running = 1;

    while (running) {
        while (SDL_PollEvent(&event)) {
            if (event.type == SDL_EVENT_QUIT) {
                running = 0;
            }
            else if (event.type == SDL_EVENT_JOYSTICK_ADDED ||
                     event.type == SDL_EVENT_JOYSTICK_REMOVED ||
                     event.type == SDL_EVENT_JOYSTICK_AXIS_MOTION ||
                     event.type == SDL_EVENT_JOYSTICK_BUTTON_DOWN) {
                handle_joystick_input(&event);
            }
            else if (event.type == SDL_EVENT_KEY_DOWN ||
                     event.type == SDL_EVENT_TEXT_INPUT) {
                handle_keyboard_input(&event);
            }
        }

        int win_w, win_h;
        SDL_GetWindowSize(window, &win_w, &win_h); // Fix: Second argument should be win_h

        SDL_SetRenderDrawColor(renderer, 0, 0, 0, 255);
        SDL_RenderClear(renderer);

        if (background_texture) {
            SDL_FRect dst = { 0, 0, (float)win_w, (float)win_h };
            SDL_RenderTexture(renderer, background_texture, NULL, &dst);
        }

        if (logo_texture) {
            SDL_FRect dst = { (win_w - 200) / 2.0f, 40.0f, 200.0f, 100.0f };
            SDL_RenderTexture(renderer, logo_texture, NULL, &dst);
        }

        if (in_rom_menu)
            draw_rom_menu();
        else
            draw_system_menu();

        SDL_Color sig_color = { 150, 150, 150, 255 };
        render_text("by MARCO AURELIO SIMAO", 10, win_h - FONT_SIZE - 10, sig_color);

        SDL_RenderPresent(renderer);
        SDL_Delay(16);
    }

    free_all_rom_list(); // Free the master list
    free_rom_list(); // Free the filtered list (if anything is left)
    TTF_CloseFont(font);
    SDL_DestroyTexture(logo_texture);
    SDL_DestroyTexture(background_texture);

    Mix_FreeMusic(music);
    Mix_CloseAudio();
    Mix_Quit();
    TTF_Quit();
    SDL_Quit();

    return 0;
}

static void render_text_centered(const char *text, float y, SDL_Color color) {
    // Defensive check for font being NULL
    if (!font) {
        SDL_Log("Error: Font is NULL in render_text_centered!");
        return;
    }
    // FIX 1: Use TTF_RenderText_Blended and provide string length
    SDL_Surface *surface = TTF_RenderText_Blended(font, text, SDL_strlen(text), color);

    if (!surface) {
        SDL_Log("TTF_RenderText_Blended error (render_text_centered line 301): %s", SDL_GetError());
        return;
    }

    SDL_Texture *texture = SDL_CreateTextureFromSurface(renderer, surface);

    if (!texture) {
        SDL_Log("SDL_CreateTextureFromSurface error: %s", SDL_GetError());
        SDL_DestroySurface(surface);
        return;
    }

    int text_w = surface->w, text_h = surface->h;
    SDL_DestroySurface(surface);
    int win_w;
    SDL_GetWindowSize(window, &win_w, NULL);
    SDL_FRect dst = { (win_w - text_w) / 2.0f, y, (float)text_w, (float)text_h };
    SDL_RenderTexture(renderer, texture, NULL, &dst);
    SDL_DestroyTexture(texture);
}

static void render_text(const char *text, float x, float y, SDL_Color color) {
    // Defensive check for font being NULL
    if (!font) {
        SDL_Log("Error: Font is NULL in render_text!");
        return;
    }

    // FIX 2: Use TTF_RenderText_Blended and provide string length
    SDL_Surface *surface = TTF_RenderText_Blended(font, text, SDL_strlen(text), color);

    if (!surface) {
        //SDL_Log("TTF_RenderText_Blended error (render_text line 333): %s", SDL_GetError());
        return;
    }

    SDL_Texture *texture = SDL_CreateTextureFromSurface(renderer, surface);

    if (!texture) {
        SDL_Log("SDL_CreateTextureFromSurface error: %s", SDL_GetError());
        SDL_DestroySurface(surface);
        return;
    }

    int text_w = surface->w, text_h = surface->h;
    SDL_DestroySurface(surface);
    SDL_FRect dst = { x, y, (float)text_w, (float)text_h };
    SDL_RenderTexture(renderer, texture, NULL, &dst);
    SDL_DestroyTexture(texture);
}

static void draw_scrollbar(int item_count, int visible_lines, int scroll_offset, int start_y, int line_height, int win_w) {
    if (item_count <= visible_lines) return;

    float scrollbar_height = visible_lines * line_height;
    float handle_height = scrollbar_height * (visible_lines / (float)item_count);
    // Ensure handle_height is at least a minimum size for visibility
    if (handle_height < 10.0f) handle_height = 10.0f;

    float handle_y = start_y + (scroll_offset / (float)(item_count - visible_lines)) * (scrollbar_height - handle_height);
    if (item_count == visible_lines) { // Prevent division by zero if all items are visible
        handle_y = start_y;
    } else {
        handle_y = start_y + (scroll_offset / (float)(item_count - visible_lines)) * (scrollbar_height - handle_height);
    }
    if (scroll_offset == 0) handle_y = start_y; // Correct start position
    if (scroll_offset >= item_count - visible_lines) handle_y = start_y + scrollbar_height - handle_height; // Correct end position


    SDL_FRect bar = { win_w - 20.0f, (float)start_y, 8.0f, scrollbar_height };
    SDL_FRect handle = { win_w - 20.0f, handle_y, 8.0f, handle_height };
    SDL_SetRenderDrawColor(renderer, 80, 80, 80, 200);
    SDL_RenderFillRect(renderer, &bar);
    SDL_SetRenderDrawColor(renderer, 200, 200, 200, 255);
    SDL_RenderFillRect(renderer, &handle);
}

static int has_allowed_extension(const char *filename, const char *allowed_exts) {
    const char *dot = strrchr(filename, '.');
    if (!dot || dot == filename) return 0;

    char ext[16];
    SDL_strlcpy(ext, dot + 1, sizeof(ext));
    char temp[64];
    SDL_strlcpy(temp, allowed_exts, sizeof(temp));

    char *token = strtok(temp, ",");
    while (token) {
        if (SDL_strcasecmp(ext, token) == 0) return 1;
        token = strtok(NULL, ",");
    }
    return 0;
}

static void load_all_rom_list(const SystemEntry *sys) {
    //SDL_Log("load_all_rom_list called for system: %s", sys->display_name);
    free_all_rom_list(); // Always free existing master list first

    char path[512];
    snprintf(path, sizeof(path), "./roms/%s/", sys->dir_name);
    DIR *dir = opendir(path);

    if (!dir) {
        SDL_Log("Could not open ROM directory: %s", path);
        return;
    }

    int capacity = 20;
    all_rom_list = calloc(capacity, sizeof(RomEntry));

    if (all_rom_list == NULL) {
        closedir(dir);
        SDL_Log("Failed to allocate memory for all_rom_list (initial)");
        return;
    }
    all_rom_count = 0;
    struct dirent *entry;

    // 1) Load all files in the main system folder with allowed extensions
    while ((entry = readdir(dir))) {
        if (strcmp(entry->d_name, ".") == 0 || strcmp(entry->d_name, "..") == 0) continue;

        char full_path[512];
        snprintf(full_path, sizeof(full_path), "./roms/%s/%s", sys->dir_name, entry->d_name);

        struct stat st;
        if (stat(full_path, &st) == -1) continue;

        if (S_ISREG(st.st_mode) && has_allowed_extension(entry->d_name, sys->allowed_exts)) {
            if (all_rom_count >= capacity) {
                capacity *= 2;
                RomEntry *new_all_rom_list = realloc(all_rom_list, capacity * sizeof(RomEntry));

                if (new_all_rom_list == NULL) {
                    SDL_Log("Failed to reallocate memory for all_rom_list (main files)");
                    free_all_rom_list();
                    closedir(dir);
                    return;
                }
                all_rom_list = new_all_rom_list;
            }

            all_rom_list[all_rom_count].display_name = strdup(entry->d_name);
            all_rom_list[all_rom_count].rom_path = strdup(full_path);

            if (!all_rom_list[all_rom_count].display_name || !all_rom_list[all_rom_count].rom_path) {
                SDL_Log("Failed to strdup string for ROM entry: %s", entry->d_name);
                free_all_rom_list();
                closedir(dir);
                return;
            }
            all_rom_count++;
        }
    }

    rewinddir(dir);

    // 2) Now go through subdirectories and add their files
    while ((entry = readdir(dir))) {
        if (strcmp(entry->d_name, ".") == 0 || strcmp(entry->d_name, "..") == 0) continue;

        char sub_path[512];
        snprintf(sub_path, sizeof(sub_path), "./roms/%s/%s", sys->dir_name, entry->d_name);

        struct stat st;
        if (stat(sub_path, &st) == -1) continue;

        if (S_ISDIR(st.st_mode)) {
            DIR *subdir = opendir(sub_path);

            if (!subdir) {
                SDL_Log("Could not open subdirectory: %s", sub_path);
                continue;
            }

            struct dirent *sub_entry;
            while ((sub_entry = readdir(subdir))) {
                if (sub_entry->d_type == DT_REG && has_allowed_extension(sub_entry->d_name, sys->allowed_exts)) {
                    if (all_rom_count >= capacity) {
                        capacity *= 2;
                        RomEntry *new_all_rom_list = realloc(all_rom_list, capacity * sizeof(RomEntry));

                        if (new_all_rom_list == NULL) {
                            SDL_Log("Failed to reallocate memory for all_rom_list (sub files)");
                            free_all_rom_list();
                            closedir(subdir);
                            closedir(dir);
                            return;
                        }
                        all_rom_list = new_all_rom_list;
                    }
                    all_rom_list[all_rom_count].display_name = strdup(sub_entry->d_name);
                    char full_file_path[1024];
                    snprintf(full_file_path, sizeof(full_file_path), "./roms/%s/%s/%s", sys->dir_name, entry->d_name, sub_entry->d_name);
                    all_rom_list[all_rom_count].rom_path = strdup(full_file_path);

                    if (!all_rom_list[all_rom_count].display_name || !all_rom_list[all_rom_count].rom_path) {
                        SDL_Log("Failed to strdup string for sub-ROM entry: %s", sub_entry->d_name);
                        free_all_rom_list();
                        closedir(subdir);
                        closedir(dir);
                        return;
                    }
                    all_rom_count++;
                }
            }
            closedir(subdir);
        }
    }

    closedir(dir);

    // No "Exit" option added to all_rom_list, it's added to the filtered rom_list
    //SDL_Log("Finished loading ALL ROM list. all_rom_list: %p, all_rom_count: %d", (void*)all_rom_list, all_rom_count);
}

static void free_all_rom_list(void) {
    //SDL_Log("free_all_rom_list called. all_rom_list: %p, all_rom_count: %d", (void*)all_rom_list, all_rom_count);

    if (all_rom_list) {
        for (int i = 0; i < all_rom_count; ++i) {
            if (all_rom_list[i].display_name) {
                SDL_free(all_rom_list[i].display_name);
                all_rom_list[i].display_name = NULL;
            }
            if (all_rom_list[i].rom_path) {
                SDL_free(all_rom_list[i].rom_path);
                all_rom_list[i].rom_path = NULL;
            }
        }
        SDL_free(all_rom_list);
        all_rom_list = NULL;
    }
    all_rom_count = 0;
}

// Existing free_rom_list, now frees the *filtered* list
static void free_rom_list(void) {
    SDL_Log("free_rom_list called (filtered). rom_list: %p, rom_count: %d", (void*)rom_list, rom_count);

    if (rom_list) {
        // Only free the memory for the RomEntry array itself, not the strings,
        // as they are merely pointers to strings owned by all_rom_list.
        SDL_free(rom_list);
        rom_list = NULL;
    }

    rom_count = 0;
}

static void filter_rom_list(const char *filter_text) {
    SDL_Log("filter_rom_list called with filter: '%s'", filter_text);
    free_rom_list(); // Clear the current filtered list

    // Handle empty filter text
    if (filter_text == NULL || strlen(filter_text) == 0) {
        // If filter is empty, display all ROMs + "Exit"
        rom_count = all_rom_count + 1;
        rom_list = calloc(rom_count, sizeof(RomEntry));

        if (!rom_list) {
            SDL_Log("Failed to allocate memory for filtered rom_list (empty filter)");
            return;
        }

        for (int i = 0; i < all_rom_count; ++i) {
            rom_list[i] = all_rom_list[i]; // Copy pointers
        }

        rom_list[all_rom_count].display_name = strdup("Exit");
        rom_list[all_rom_count].rom_path = NULL;

        if (!rom_list[all_rom_count].display_name) {
            SDL_Log("Failed to strdup 'Exit' for filtered list (empty filter)");
            free_rom_list(); // Free partial allocation
            return;
        }
    } else {
        // Filter based on input_text
        int temp_rom_count = 0;
        RomEntry *temp_rom_list = calloc(all_rom_count + 1, sizeof(RomEntry)); // Max possible + Exit

        if (!temp_rom_list) {
            SDL_Log("Failed to allocate temporary memory for filtered rom_list");
            return;
        }

        for (int i = 0; i < all_rom_count; ++i) {
            if (all_rom_list[i].display_name && SDL_strcasestr(all_rom_list[i].display_name, filter_text) != NULL) {
                temp_rom_list[temp_rom_count++] = all_rom_list[i]; // Copy pointers
            }
        }

        // Add "Exit" option
        if (temp_rom_count < (all_rom_count + 1)) { // Ensure space for "Exit"
            temp_rom_list[temp_rom_count].display_name = strdup("Exit");
            temp_rom_list[temp_rom_count].rom_path = NULL;

            if (!temp_rom_list[temp_rom_count].display_name) {
                SDL_Log("Failed to strdup 'Exit' for filtered list");
                // Need to free temp_rom_list and its strdup'd "Exit" if it was allocated
                for(int i = 0; i < temp_rom_count; ++i) {
                    if (i == temp_rom_count && temp_rom_list[i].display_name) SDL_free(temp_rom_list[i].display_name); // Only if we strdup'd "Exit"
                }
                SDL_free(temp_rom_list);
                return;
            }

            temp_rom_count++;
        }

        rom_list = realloc(temp_rom_list, temp_rom_count * sizeof(RomEntry));

        if (!rom_list && temp_rom_count > 0) { // realloc can return NULL if size is 0, but if temp_rom_count > 0, it's an error
            SDL_Log("Failed to reallocate filtered rom_list to final size");
            // Original temp_rom_list is still valid, but we should free it and its strdup'd "Exit"
            if (temp_rom_list[temp_rom_count-1].display_name) SDL_free(temp_rom_list[temp_rom_count-1].display_name);
            SDL_free(temp_rom_list);
            rom_count = 0;
            return;
        }
        rom_count = temp_rom_count;
    }

    selected_rom_index = 0;
    rom_scroll_offset = 0;
    //SDL_Log("Finished filtering ROM list. Displaying %d ROMs.", rom_count);
}


static int file_exists(const char *path) {
    struct stat st;
    return (stat(path, &st) == 0);
}

static void handle_joystick_input(const SDL_Event *event) {
    Uint64 now = SDL_GetTicks();
    if (typing_in_input) return;

    if (now < last_input_time + INPUT_COOLDOWN_MS) return;

    if (event->type == SDL_EVENT_JOYSTICK_ADDED) {
        SDL_Log("Joystick found.");
        SDL_OpenJoystick(event->jdevice.which);
    } else if (event->type == SDL_EVENT_JOYSTICK_REMOVED) {
        SDL_Log("Joystick removed.");
        SDL_CloseJoystick(SDL_GetJoystickFromID(event->jdevice.which));
    } else if (event->type == SDL_EVENT_JOYSTICK_AXIS_MOTION && event->jaxis.axis == 1) {
        int direction = 0;
        if (event->jaxis.value < -AXIS_DEADZONE) direction = -1;
        else if (event->jaxis.value > AXIS_DEADZONE) direction = 1;

        if (direction) {
            if (in_rom_menu) {
                if (rom_count > 0) { // Use rom_count (filtered)
                    selected_rom_index = (selected_rom_index + rom_count + direction) % rom_count;
                } else {
                    selected_rom_index = 0; // No ROMs, reset selection
                }
            } else {
                int item_count = system_menu_count;
                selected_system_index = (selected_system_index + item_count + direction) % item_count;
            }
            last_input_time = now;
        }
    } else if (event->type == SDL_EVENT_JOYSTICK_BUTTON_DOWN && event->jbutton.button == 0) {
        if (in_rom_menu) {
            if (!rom_list || selected_rom_index < 0 || selected_rom_index >= rom_count) {
                 SDL_Log("Attempted to access invalid ROM index or rom_list is NULL. Index: %d, Count: %d", selected_rom_index, rom_count);
                 if (rom_list && rom_count > 0 && selected_rom_index == rom_count - 1) { // Check if it's the "Exit" option
                     in_rom_menu = 0;
                     free_rom_list(); // Free filtered list
                     free_all_rom_list(); // Free master list
                     input_text[0] = '\0'; // Clear input
                     return;
                 }
                 return;
            }

            if (rom_list[selected_rom_index].rom_path == NULL) { // "Exit" option
                in_rom_menu = 0;
                free_rom_list(); // Free filtered list
                free_all_rom_list(); // Free master list
                input_text[0] = '\0'; // Clear input
                return;
            }

            const SystemEntry *sys = &systems[selected_system_index];
            const char *rom_path = rom_list[selected_rom_index].rom_path;
            struct stat st;

            if (stat(rom_path, &st) == -1) {
                SDL_Log("Failed to stat ROM path: %s", rom_path);
                return;
            }

            char final_rom_path[512] = "";

            if (S_ISDIR(st.st_mode)) {
                DIR *d = opendir(rom_path);

                if (!d) {
                    SDL_Log("Failed to open ROM directory for launch: %s", rom_path);
                    return;
                }
                struct dirent *ent;
                while ((ent = readdir(d))) {
                    if (ent->d_type == DT_REG && has_allowed_extension(ent->d_name, sys->allowed_exts)) {
                        snprintf(final_rom_path, sizeof(final_rom_path), "%s/%s", rom_path, ent->d_name);
                        break;
                    }
                }
                closedir(d);
            } else if (S_ISREG(st.st_mode)) {
                snprintf(final_rom_path, sizeof(final_rom_path), "%s", rom_path);
            }

            if (final_rom_path[0] != '\0') {
                char cmd[1024];
                snprintf(cmd, sizeof(cmd), "mame %s %s \"%s\"", sys->mame_sys, sys->launch_arg, final_rom_path);
                Mix_PauseMusic();
                int ret = system(cmd);

                if (ret != 0) {
                    SDL_Log("MAME command failed with exit code %d: %s", ret, cmd);
                }
                Mix_ResumeMusic();
            } else {
                SDL_Log("No valid ROM file found in directory %s for launch.", rom_path);
            }

            in_rom_menu = 0;
            free_rom_list(); // Free filtered list
            free_all_rom_list(); // Free master list
            input_text[0] = '\0'; // Clear input
        } else {
            int item_count = system_menu_count;

            if (selected_system_index == item_count - 1) {
                exit(0);
            } else if (selected_system_index == item_count - 2) {
                pid_t pid = fork();

                if (pid == 0) {
                    execl("./cover-scraper", "./cover-scraper", (char *)NULL);
                    perror("Failed to exec cover-scraper");
                    _exit(1);
                } else if (pid > 0) {
                    int status;
                    waitpid(pid, &status, 0);
                } else {
                    perror("Failed to fork");
                }
            } else {
                load_all_rom_list(&systems[selected_system_index]); // Load all ROMs
                filter_rom_list(input_text); // Filter initially
                in_rom_menu = 1;
            }
        }
        last_input_time = now;
    }
}

static void handle_keyboard_input(const SDL_Event *event) {
    Uint64 now = SDL_GetTicks();

    if (event->type == SDL_EVENT_KEY_DOWN) {
        if (event->key.scancode == SDL_SCANCODE_TAB) {
            if (in_rom_menu) {
                typing_in_input = !typing_in_input;
                if (typing_in_input) {
                    SDL_StartTextInput(window);
                    // input_text[0] = '\0'; // Don't clear on toggle, allow user to refine
                } else {
                    SDL_StopTextInput(window);
                }
                last_input_time = now;
            }
        } else if (typing_in_input) {
            if (event->key.scancode == SDL_SCANCODE_RETURN || event->key.scancode == SDL_SCANCODE_KP_ENTER) {
                typing_in_input = 0;
                SDL_StopTextInput(window);
                SDL_Log("Input field content: %s", input_text);

                filter_rom_list(input_text); // Apply filter when done typing
                last_input_time = now;
            } else if (event->key.scancode == SDL_SCANCODE_BACKSPACE && strlen(input_text) > 0) {
                input_text[strlen(input_text) - 1] = '\0';
                filter_rom_list(input_text); // Apply filter on backspace
                last_input_time = now;
            } else if (event->key.scancode == SDL_SCANCODE_ESCAPE) { // Added to exit typing mode
                typing_in_input = 0;
                SDL_StopTextInput(window);
                input_text[0] = '\0'; // Clear input if escape is pressed
                filter_rom_list(input_text); // Re-filter to show all ROMs
                last_input_time = now;
            }
        } else { // Not typing in input field
            if (now < last_input_time + INPUT_COOLDOWN_MS) return;

            if (event->key.scancode == SDL_SCANCODE_UP) {
                if (in_rom_menu) {
                    if (rom_count > 0) { // Use rom_count (filtered)
                        selected_rom_index = (selected_rom_index + rom_count - 1) % rom_count;
                    } else {
                        selected_rom_index = 0;
                    }
                }
                else {
                    selected_system_index = (selected_system_index + system_menu_count - 1) % system_menu_count;
                }
                last_input_time = now;
            } else if (event->key.scancode == SDL_SCANCODE_DOWN) {
                if (in_rom_menu) {
                    if (rom_count > 0) { // Use rom_count (filtered)
                        selected_rom_index = (selected_rom_index + 1) % rom_count;
                    } else {
                        selected_rom_index = 0;
                    }
                }
                else {
                    selected_system_index = (selected_system_index + 1) % system_menu_count;
                }
                last_input_time = now;
            } else if (event->key.scancode == SDL_SCANCODE_RETURN || event->key.scancode == SDL_SCANCODE_KP_ENTER) {
                if (in_rom_menu) {
                    if (!rom_list || selected_rom_index < 0 || selected_rom_index >= rom_count) {
                        SDL_Log("Attempted to access invalid ROM index or rom_list is NULL. Index: %d, Count: %d", selected_rom_index, rom_count);
                        if (rom_list && rom_count > 0 && selected_rom_index == rom_count - 1) { // Check for "Exit"
                            in_rom_menu = 0;
                            free_rom_list(); // Free filtered list
                            free_all_rom_list(); // Free master list
                            input_text[0] = '\0'; // Clear input
                            return;
                        }
                        return;
                    }

                    if (rom_list[selected_rom_index].rom_path == NULL) { // "Exit" option
                        in_rom_menu = 0;
                        free_rom_list(); // Free filtered list
                        free_all_rom_list(); // Free master list
                        input_text[0] = '\0'; // Clear input
                        return;
                    }

                    const SystemEntry *sys = &systems[selected_system_index];
                    const char *rom_path = rom_list[selected_rom_index].rom_path;
                    struct stat st;
                    if (stat(rom_path, &st) == -1) {
                        SDL_Log("Failed to stat ROM path: %s", rom_path);
                        return;
                    }

                    char final_rom_path[512] = "";
                    if (S_ISDIR(st.st_mode)) {
                        DIR *d = opendir(rom_path);
                        if (!d) {
                            SDL_Log("Failed to open ROM directory for launch: %s", rom_path);
                            return;
                        }
                        struct dirent *ent;
                        while ((ent = readdir(d))) {
                            if (ent->d_type == DT_REG && has_allowed_extension(ent->d_name, sys->allowed_exts)) {
                                snprintf(final_rom_path, sizeof(final_rom_path), "%s/%s", rom_path, ent->d_name);
                                break;
                            }
                        }
                        closedir(d);
                    } else if (S_ISREG(st.st_mode)) {
                        snprintf(final_rom_path, sizeof(final_rom_path), "%s", rom_path);
                    }

                    if (final_rom_path[0] != '\0') {
                        char cmd[1024];
                        snprintf(cmd, sizeof(cmd), "mame %s %s \"%s\"", sys->mame_sys, sys->launch_arg, final_rom_path);
                        Mix_PauseMusic();
                        int ret = system(cmd);

                        if (ret != 0) {
                            SDL_Log("MAME command failed with exit code %d: %s", ret, cmd);
                        }

                        Mix_ResumeMusic();
                    } else {
                        SDL_Log("No valid ROM file found in directory %s for launch.", rom_path);
                    }

                    in_rom_menu = 0;
                    free_rom_list(); // Free filtered list
                    free_all_rom_list(); // Free master list
                    input_text[0] = '\0'; // Clear input
                } else {
                    int item_count = system_menu_count;
                    if (selected_system_index == item_count - 1) {
                        exit(0);
                    } else if (selected_system_index == item_count - 2) {
                        pid_t pid = fork();

                        if (pid == 0) {
                            execl("./cover-scraper", "./cover-scraper", (char *)NULL);
                            perror("Failed to exec cover-scraper");
                            _exit(1);
                        } else if (pid > 0) {
                            int status;
                            waitpid(pid, &status, 0);
                        } else {
                            perror("Failed to fork");
                        }
                    } else {
                        load_all_rom_list(&systems[selected_system_index]); // Load all ROMs
                        filter_rom_list(input_text); // Filter initially
                        in_rom_menu = 1;
                    }
                }
                last_input_time = now;
            } else if (event->key.scancode == SDL_SCANCODE_ESCAPE) {
                if (in_rom_menu) {
                    in_rom_menu = 0;
                    free_rom_list(); // Free filtered list
                    free_all_rom_list(); // Free master list
                    input_text[0] = '\0'; // Clear input
                }
                last_input_time = now;
            }
        }
    } else if (event->type == SDL_EVENT_TEXT_INPUT && typing_in_input) {
        if (strlen(input_text) + strlen(event->text.text) < MAX_INPUT_LENGTH) {
            strcat(input_text, event->text.text);
            filter_rom_list(input_text); // Apply filter on each text input
        }
    }
}