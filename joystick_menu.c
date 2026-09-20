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

static Mix_Music *music = NULL;
static SDL_Window *window = NULL;
static SDL_Renderer *renderer = NULL;
static SDL_Texture *logo_texture = NULL;
static SDL_Texture *background_texture = NULL;
static SDL_Texture *cover_texture = NULL;
static TTF_Font *font = NULL;

#define INPUT_COOLDOWN_MS 200
#define AXIS_DEADZONE 8000
#define LOGO_HEIGHT 200
#define FONT_SIZE 18

static Uint64 last_input_time = 0;

typedef struct {
    const char *dir_name;
    const char *display_name;
    const char *mame_sys;
    const char *launch_arg;
    const char *allowed_exts;
} SystemEntry;

static const SystemEntry systems[] = {
    { "sms1", "Master System", "sms1", "-cart", "sms,bin,zip,rom" },
    { "genesis", "Mega Drive", "genesis", "-cart", "md,bin,zip,rom" },
    { "snes", "Super Nintendo", "snes", "-cart", "smc,sfc,zip,rom" },
    { "nes", "Nintendo 8-bit", "nes", "-cart", "nes,zip,rom" },
    { "segacd", "Mega CD", "segacd", "-cdrom", "cue,chd,iso,rom" },
    { "ps1", "PlayStation 1", "psu", "-cdrom", "cue,chd,iso,rom" },
    { "ps2", "PlayStation 2", "ps2", "-cdrom", "cue,chd,iso,bin" },
    { "ps3", "PlayStation 3", "rpcs3", NULL, "iso,pkg" },
    { "ps4", "PlayStation 4", "shadps4", NULL, "elf,bin,pkg" },
    { "neogeo", "Neo Geo", "neogeo", NULL, "zip,7z,neo,rom" },
};

static int selected_system_index = 0;
static int system_scroll_offset = 0;
static int in_rom_menu = 0;

static int system_menu_count = sizeof(systems) / sizeof(SystemEntry) + 2;

typedef struct {
    char *display_name;
    char *rom_path;
} RomEntry;

static RomEntry *rom_list = NULL;
static int rom_count = 0;
static int selected_rom_index = 0;
static int rom_scroll_offset = 0;

static void draw_system_menu(void);
static void draw_rom_menu(void);
static void load_rom_list(const SystemEntry *sys);
static void free_rom_list(void);
static void handle_events(const SDL_Event *event);
static void handle_joystick_input(const SDL_Event *event);
static void move_selection(int direction);
static int has_allowed_extension(const char *filename, const char *allowed_exts);
static void render_text_centered(const char *text, float y, SDL_Color color);
static void render_text(const char *text, float x, float y, SDL_Color color);
static void draw_scrollbar(int item_count, int visible_lines, int scroll_offset, int start_y, int line_height, int win_w);
static int file_exists(const char *path);
static void append_mame_rompath(char *buffer, size_t buffer_size, const char *path);
static void build_mame_rompath(char *buffer, size_t buffer_size, const char *rom_dir);
static SDL_Texture *load_cover_for_rom(const char *rom_path);

static void draw_system_menu(void) {
    int win_w, win_h; SDL_GetWindowSize(window, &win_w, &win_h);
    int item_count = system_menu_count;
    int line_height = FONT_SIZE + 10;
    int visible_lines = (win_h - LOGO_HEIGHT - 40) / line_height;

    if (selected_system_index < system_scroll_offset) system_scroll_offset = selected_system_index;
    if (selected_system_index >= system_scroll_offset + visible_lines) system_scroll_offset = selected_system_index - visible_lines + 1;

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

    if (selected_rom_index < rom_scroll_offset) rom_scroll_offset = selected_rom_index;
    if (selected_rom_index >= rom_scroll_offset + visible_lines) rom_scroll_offset = selected_rom_index - visible_lines + 1;

    int start_y = LOGO_HEIGHT + 20;

    for (int i = 0; i < rom_count; ++i) {
        if (i < rom_scroll_offset) continue;
        if (i >= rom_scroll_offset + visible_lines) break;

        SDL_Color color = { 200, 200, 200, 255 };
        if (i == selected_rom_index) color.r = color.g = 255;

        render_text_centered(rom_list[i].display_name, start_y + (i - rom_scroll_offset) * line_height, color);
    }

    draw_scrollbar(rom_count, visible_lines, rom_scroll_offset, start_y, line_height, win_w);

    if (rom_list && rom_list[selected_rom_index].rom_path) {
        if (cover_texture) {
            SDL_DestroyTexture(cover_texture);
            cover_texture = NULL;
        }

        cover_texture = load_cover_for_rom(rom_list[selected_rom_index].rom_path);
        if (!cover_texture) {
            cover_texture = IMG_LoadTexture(renderer, "assets/cover.png");
        }

        if (cover_texture) {
            SDL_FRect dst = { win_w - 80 - 150.0f, 30 + 0.0f, 220.0f, 220.0f };
            SDL_RenderTexture(renderer, cover_texture, NULL, &dst);
        }
    }
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

    /*
    Mix_Init(MIX_INIT_OGG);
    SDL_AudioSpec desired_spec = { .freq = 44100, .format = SDL_AUDIO_F32, .channels = 2 };
    Mix_OpenAudio(0, &desired_spec);

    music = Mix_LoadMUS("assets/background1.ogg");

    if (music) {
        Mix_VolumeMusic(64);
        Mix_PlayMusic(music, -1);
    }
    */

    SDL_Event event;
    int running = 1;

    while (running) {
        while (SDL_PollEvent(&event)) {
            if (event.type == SDL_EVENT_QUIT) running = 0;

            if (event.type == SDL_EVENT_JOYSTICK_ADDED) {
                SDL_Log("Joystick found.");
                SDL_OpenJoystick(event.jdevice.which);
            }

            if (event.type == SDL_EVENT_JOYSTICK_REMOVED) {
                SDL_Log("Joystick removed.");
                SDL_CloseJoystick(SDL_GetJoystickFromID(event.jdevice.which));
            }

            handle_events(&event);
            handle_joystick_input(&event);
        }

        int win_w, win_h;
        SDL_GetWindowSize(window, &win_w, &win_h);

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

    free_rom_list();
    TTF_CloseFont(font);
    SDL_DestroyTexture(logo_texture);
    SDL_DestroyTexture(background_texture);

    if (cover_texture) {
        SDL_DestroyTexture(cover_texture);
        cover_texture = NULL;
    }

    Mix_FreeMusic(music);
    Mix_CloseAudio();
    Mix_Quit();
    TTF_Quit();
    SDL_Quit();

    return 0;
}

static void render_text_centered(const char *text, float y, SDL_Color color) {
    SDL_Surface *surface = TTF_RenderText_Blended(font, text, SDL_strlen(text), color);
    SDL_Texture *texture = SDL_CreateTextureFromSurface(renderer, surface);
    int text_w = surface->w, text_h = surface->h;
    SDL_DestroySurface(surface);
    int win_w;
    SDL_GetWindowSize(window, &win_w, NULL);
    SDL_FRect dst = { (win_w - text_w) / 2.0f, y, (float)text_w, (float)text_h };
    SDL_RenderTexture(renderer, texture, NULL, &dst);
    SDL_DestroyTexture(texture);
}

static void render_text(const char *text, float x, float y, SDL_Color color) {
    SDL_Surface *surface = TTF_RenderText_Blended(font, text, SDL_strlen(text), color);
    SDL_Texture *texture = SDL_CreateTextureFromSurface(renderer, surface);
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
    float handle_y = start_y + (scroll_offset / (float)item_count) * scrollbar_height;
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

static void load_rom_list(const SystemEntry *sys) {
    free_rom_list();
    const char *configured_root = getenv("ROM_ROOT");
    const char *home = getenv("HOME");
    char rom_root[512];
    if (configured_root && configured_root[0] != '\0') {
        SDL_strlcpy(rom_root, configured_root, sizeof(rom_root));
    } else if (file_exists("./roms")) {
        SDL_strlcpy(rom_root, "./roms", sizeof(rom_root));
    } else if (home && home[0] != '\0') {
        snprintf(rom_root, sizeof(rom_root), "%s/mame/roms", home);
    } else {
        SDL_strlcpy(rom_root, "./roms", sizeof(rom_root));
    }

    char system_root[512];
    snprintf(system_root, sizeof(system_root), "%s/%s", rom_root, sys->dir_name);
    DIR *dir = opendir(system_root);
    if (!dir) {
        SDL_strlcpy(system_root, rom_root, sizeof(system_root));
        dir = opendir(system_root);
    }
    if (!dir) {
        SDL_Log("ROM directory not found: %s", rom_root);
        return;
    }

    int capacity = 20;
    rom_list = calloc(capacity, sizeof(RomEntry));
    rom_count = 0;
    struct dirent *entry;


    // 1) Load all files in the main system folder with allowed extensions
    while ((entry = readdir(dir))) {
        if (strcmp(entry->d_name, ".") == 0 || strcmp(entry->d_name, "..") == 0) continue;

        char full_path[512];
        snprintf(full_path, sizeof(full_path), "%s/%s", system_root, entry->d_name);

        struct stat st;
        if (stat(full_path, &st) == -1) continue;

        if (S_ISREG(st.st_mode) && has_allowed_extension(entry->d_name, sys->allowed_exts)) {
            if (rom_count >= capacity) {
                capacity *= 2;
                rom_list = realloc(rom_list, capacity * sizeof(RomEntry));
            }
            rom_list[rom_count].display_name = strdup(entry->d_name);  // show file name
            rom_list[rom_count].rom_path = strdup(full_path);
            rom_count++;
        }
    }

    rewinddir(dir);

    // 2) Now go through subdirectories and add their files
    while ((entry = readdir(dir))) {
        if (strcmp(entry->d_name, ".") == 0 || strcmp(entry->d_name, "..") == 0) continue;

        char sub_path[512];
        snprintf(sub_path, sizeof(sub_path), "%s/%s", system_root, entry->d_name);

        struct stat st;
        if (stat(sub_path, &st) == -1) continue;

        if (S_ISDIR(st.st_mode)) {
            DIR *subdir = opendir(sub_path);
            if (!subdir) continue;

            struct dirent *sub_entry;
            while ((sub_entry = readdir(subdir))) {
                if (sub_entry->d_type == DT_REG && has_allowed_extension(sub_entry->d_name, sys->allowed_exts)) {
                    if (rom_count >= capacity) {
                        capacity *= 2;
                        rom_list = realloc(rom_list, capacity * sizeof(RomEntry));
                    }
                    rom_list[rom_count].display_name = strdup(sub_entry->d_name);  // show file name only, not subdir
                    char full_file_path[1024];
                    snprintf(full_file_path, sizeof(full_file_path), "%s/%s/%s", system_root, entry->d_name, sub_entry->d_name);
                    rom_list[rom_count].rom_path = strdup(full_file_path);
                    rom_count++;
                }
            }
            closedir(subdir);
        }
    }

    closedir(dir);

    // Add "Exit" option
    rom_list = realloc(rom_list, (rom_count + 1) * sizeof(RomEntry));
    rom_list[rom_count].display_name = strdup("Exit");
    rom_list[rom_count].rom_path = NULL;
    rom_count++;
}

static void free_rom_list(void) {
    for (int i = 0; i < rom_count; ++i) {
        SDL_free(rom_list[i].display_name);
        SDL_free(rom_list[i].rom_path);
    }
    SDL_free(rom_list);
    rom_list = NULL;
    rom_count = 0;
    if (cover_texture) {
        SDL_DestroyTexture(cover_texture);
        cover_texture = NULL;
    }
}

static void handle_events(const SDL_Event *event)
{
    switch (event->type)
    {
    // ... other event types like JOYSTICK_ADDED, JOYSTICK_AXIS_MOTION, etc.

    case SDL_EVENT_KEY_DOWN:
        if (event->key.key == SDLK_ESCAPE && in_rom_menu) {
            in_rom_menu = 0;
            free_rom_list();
            selected_rom_index = 0;
            rom_scroll_offset = 0;
        } else if (event->key.key == SDLK_UP || event->key.key == SDLK_DOWN) {
            Uint64 now = SDL_GetTicks();
            if (now >= last_input_time + INPUT_COOLDOWN_MS) {
                move_selection(event->key.key == SDLK_UP ? -1 : 1);
                last_input_time = now;
            }
        } else if (event->key.key == SDLK_RETURN || event->key.key == SDLK_KP_ENTER) {
            SDL_Event selection_event = {0};
            selection_event.type = SDL_EVENT_JOYSTICK_BUTTON_DOWN;
            selection_event.jbutton.button = 0;
            handle_joystick_input(&selection_event);
        }
        break;

    // ... other event types
    default:
        break;
    }
}

static void move_selection(int direction) {
    if (in_rom_menu) {
        if (rom_count > 0) {
            selected_rom_index = (selected_rom_index + rom_count + direction) % rom_count;
        }
    } else {
        selected_system_index = (selected_system_index + system_menu_count + direction) % system_menu_count;
    }
}

static void handle_joystick_input(const SDL_Event *event) {
    Uint64 now = SDL_GetTicks();
    if (event->type == SDL_EVENT_JOYSTICK_AXIS_MOTION &&
        now < last_input_time + INPUT_COOLDOWN_MS) return;

    if (event->type == SDL_EVENT_JOYSTICK_AXIS_MOTION && event->jaxis.axis == 1) {
        int direction = 0;

        if (event->jaxis.value < -AXIS_DEADZONE) {
            direction = -1;
        } else if (event->jaxis.value > AXIS_DEADZONE) {
            direction = 1;
        }

        if (direction) {
            move_selection(direction);
            last_input_time = now;
        }
    }

    if (event->type == SDL_EVENT_JOYSTICK_BUTTON_DOWN && event->jbutton.button == 0) {
        if (in_rom_menu) {
            if (!rom_list[selected_rom_index].rom_path) {
                in_rom_menu = 0;
                free_rom_list();
                return;
            }

            const SystemEntry *sys = &systems[selected_system_index];
            const char *rom_path = rom_list[selected_rom_index].rom_path;
            struct stat st;
            if (stat(rom_path, &st) == -1) return;

            char final_rom_path[512] = "";

            if (S_ISDIR(st.st_mode)) {
                DIR *d = opendir(rom_path);
                struct dirent *ent;
                if (d) {
                    while ((ent = readdir(d))) {
                        if (ent->d_type == DT_REG && has_allowed_extension(ent->d_name, sys->allowed_exts)) {
                            snprintf(final_rom_path, sizeof(final_rom_path), "%s/%s", rom_path, ent->d_name);
                            break;
                        }
                    }
                    closedir(d);
                }
            } else if (S_ISREG(st.st_mode)) {
                snprintf(final_rom_path, sizeof(final_rom_path), "%s", rom_path);
            }

            if (final_rom_path[0] != '\0') {
                char cmd[1024];
                //Mix_PauseMusic();

                if (strcmp(sys->mame_sys, "ps2") == 0) {
                    const char *pcsx2_bin = getenv("PCSX2_BIN");
                    if (!pcsx2_bin || pcsx2_bin[0] == '\0') {
                        pcsx2_bin = "/Applications/PCSX2-v2.6.3.app/Contents/MacOS/PCSX2";
                    }
                    if (access(pcsx2_bin, X_OK) != 0) {
                        SDL_Log("PCSX2 not found at %s. Run: make install-macos-deps", pcsx2_bin);
                    } else {
                        snprintf(cmd, sizeof(cmd), "\"%s\" \"%s\"", pcsx2_bin, final_rom_path);
                        system(cmd);
                    }
                } else if (strcmp(sys->mame_sys, "rpcs3") == 0) {
                    const char *rpcs3_bin = getenv("RPCS3_BIN");
                    if (!rpcs3_bin || rpcs3_bin[0] == '\0') {
                        rpcs3_bin = "/Applications/RPCS3.app/Contents/MacOS/rpcs3";
                    }
                    if (access(rpcs3_bin, X_OK) != 0) {
                        SDL_Log("RPCS3 not found at %s. Run: make install-macos-deps", rpcs3_bin);
                    } else {
                        snprintf(cmd, sizeof(cmd), "\"%s\" \"%s\"", rpcs3_bin, final_rom_path);
                        system(cmd);
                    }
                } else if (strcmp(sys->mame_sys, "shadps4") == 0) {
                    char shadps4_path[512] = "";
                    const char *shadps4_bin = getenv("SHADPS4_BIN");
                    const char *home = getenv("HOME");

                    if (shadps4_bin && shadps4_bin[0] != '\0') {
                        SDL_strlcpy(shadps4_path, shadps4_bin, sizeof(shadps4_path));
                    } else if (home && home[0] != '\0') {
                        snprintf(shadps4_path, sizeof(shadps4_path), "%s/Downloads/shadps4-macos-sdl-0/shadps4", home);
                    }

                    if (shadps4_path[0] == '\0' || access(shadps4_path, X_OK) != 0) {
                        const char *app_bin = "/Applications/shadPS4.app/Contents/MacOS/shadPS4";
                        if (access(app_bin, X_OK) == 0) {
                            SDL_strlcpy(shadps4_path, app_bin, sizeof(shadps4_path));
                        } else {
                            app_bin = "/Applications/shadPS4.app/Contents/MacOS/shadps4";
                            if (access(app_bin, X_OK) == 0) {
                                SDL_strlcpy(shadps4_path, app_bin, sizeof(shadps4_path));
                            }
                        }
                    }

                    if (shadps4_path[0] == '\0' || access(shadps4_path, X_OK) != 0) {
                        SDL_Log("shadPS4 not found. Set SHADPS4_BIN or place it in ~/Downloads/shadps4-macos-sdl-0/shadps4");
                    } else {
                        snprintf(cmd, sizeof(cmd), "\"%s\" \"%s\"", shadps4_path, final_rom_path);
                        system(cmd);
                    }
                } else if (strcmp(sys->mame_sys, "neogeo") == 0) {
                    char rom_name[256];
                    char rom_dir[512];
                    char *last_slash = strrchr(final_rom_path, '/');
                    char *romdot = strrchr(final_rom_path, '.');
                    const char *name_start = last_slash ? last_slash + 1 : final_rom_path;
                    size_t name_len = (romdot && romdot > name_start) ? (size_t)(romdot - name_start) : strlen(name_start);

                    if (name_len >= sizeof(rom_name)) {
                        name_len = sizeof(rom_name) - 1;
                    }
                    memcpy(rom_name, name_start, name_len);
                    rom_name[name_len] = '\0';

                    if (last_slash) {
                        size_t dir_len = (size_t)(last_slash - final_rom_path);
                        if (dir_len >= sizeof(rom_dir)) {
                            dir_len = sizeof(rom_dir) - 1;
                        }
                        memcpy(rom_dir, final_rom_path, dir_len);
                        rom_dir[dir_len] = '\0';
                    } else {
                        SDL_strlcpy(rom_dir, ".", sizeof(rom_dir));
                    }

                    char mame_rompath[2048];
                    build_mame_rompath(mame_rompath, sizeof(mame_rompath), rom_dir);

                    SDL_Log("mame -skip_gameinfo -rompath \"%s\" %s", mame_rompath, rom_name);

                    snprintf(cmd, sizeof(cmd), "mame -skip_gameinfo -rompath \"%s\" %s", mame_rompath, rom_name);
                    system(cmd);
                } else {
                    snprintf(cmd, sizeof(cmd), "mame %s %s \"%s\"", sys->mame_sys, sys->launch_arg, final_rom_path);
                    system(cmd);
                }

                //Mix_ResumeMusic();
            }

            in_rom_menu = 0;
            free_rom_list();
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
                load_rom_list(&systems[selected_system_index]);
                in_rom_menu = 1;
                selected_rom_index = 0;
                rom_scroll_offset = 0;
            }
        }

        last_input_time = now;
    }
}

static int file_exists(const char *path) {
    struct stat st;
    return (stat(path, &st) == 0);
}

static void append_mame_rompath(char *buffer, size_t buffer_size, const char *path) {
    if (!buffer || buffer_size == 0 || !path || path[0] == '\0' || !file_exists(path)) {
        return;
    }

    size_t used = strlen(buffer);
    if (used > 0 && used + 1 < buffer_size) {
        SDL_strlcat(buffer, ";", buffer_size);
    }
    SDL_strlcat(buffer, path, buffer_size);
}

static void build_mame_rompath(char *buffer, size_t buffer_size, const char *rom_dir) {
    const char *home = getenv("HOME");
    char home_mame[512];
    char home_mame_roms[512];
    char home_mame_bios[512];

    if (!buffer || buffer_size == 0) {
        return;
    }

    buffer[0] = '\0';
    append_mame_rompath(buffer, buffer_size, rom_dir);
    append_mame_rompath(buffer, buffer_size, "roms/neogeo");
    append_mame_rompath(buffer, buffer_size, "roms");
    append_mame_rompath(buffer, buffer_size, "bios");

    if (home && home[0] != '\0') {
        snprintf(home_mame, sizeof(home_mame), "%s/mame", home);
        append_mame_rompath(buffer, buffer_size, home_mame);

        snprintf(home_mame_roms, sizeof(home_mame_roms), "%s/mame/roms", home);
        append_mame_rompath(buffer, buffer_size, home_mame_roms);

        snprintf(home_mame_bios, sizeof(home_mame_bios), "%s/Library/Application Support/mame/bios", home);
        append_mame_rompath(buffer, buffer_size, home_mame_bios);
    }
}

static SDL_Texture *load_cover_for_rom(const char *rom_path) {
    if (!rom_path) return NULL;

    const char *filename = strrchr(rom_path, '/');
    filename = filename ? filename + 1 : rom_path;

    const char *dot = strrchr(filename, '.');
    int base_len = dot ? (int)(dot - filename) : (int)strlen(filename);

    char cover_path[512];
    SDL_Texture *tex = NULL;

    snprintf(cover_path, sizeof(cover_path), "./covers/%.*s.png", base_len, filename);
    if (file_exists(cover_path)) {
        tex = IMG_LoadTexture(renderer, cover_path);
        if (tex) return tex;
    }

    snprintf(cover_path, sizeof(cover_path), "./covers/%.*s.jpg", base_len, filename);

    if (file_exists(cover_path)) {
        tex = IMG_LoadTexture(renderer, cover_path);
        if (tex) return tex;
    }

    return NULL;
}
