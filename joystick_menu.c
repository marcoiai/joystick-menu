#include <SDL3/SDL.h>
#include <SDL3/SDL_main.h>
#include <SDL3_image/SDL_image.h>
#include <SDL3_ttf/SDL_ttf.h>
#include <SDL3_mixer/SDL_mixer.h>
#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <stdarg.h>
#include <dirent.h>
#include <spawn.h>
#include <sys/stat.h>
#include <unistd.h>
#include <sys/wait.h>
#include <glob.h>

static Mix_Music *music = NULL;
static SDL_Window *window = NULL;
static SDL_Renderer *renderer = NULL;
static SDL_Texture *logo_texture = NULL;
static SDL_Texture *cover_texture = NULL;
static SDL_Texture *background_texture = NULL;
static TTF_Font *font = NULL;

#define INPUT_COOLDOWN_MS 200
#define AXIS_DEADZONE 8000
#define LOGO_HEIGHT 200
#define FONT_SIZE 18

#define MAX_INPUT_LENGTH 256
static char input_text[MAX_INPUT_LENGTH] = "";
static int typing_in_input = 0;
static int app_running = 1;

static Uint64 last_input_time = 0;
extern char **environ;

typedef struct {
    const char *dir_name;
    const char *display_name;
    const char *mame_sys;
    const char *launch_arg;
    const char *allowed_exts;
} SystemEntry;

static const SystemEntry systems[] = {
    { "sms1", "Master System", "sms1", "-cart", "sms,bin,zip" },
    { "genesis", "Mega Drive", "genesis", "-cart", "md,bin,zip" },
    { "snes", "Super Nintendo", "snes", "-cart", "smc,sfc,zip" },
    { "nes", "Nintendo 8-bit", "nes", "-cart", "nes,zip" },
    { "segacd", "Mega CD", "segacd", "-cdrom", "cue,chd,iso" },
    { "psu", "PlayStation 1", "psu", "-cdrom", "cue,chd,iso" },
    { "ps2", "PlayStation 2", "pcsx2", NULL, "iso,chd,cso" },
    { "neogeo", "Neo Geo", "neogeo", NULL, "neo" },
    { "ps3", "PlayStation 3", "rpcs3", "-iso", "iso" },
};

static int selected_system_index = 0;
static int system_scroll_offset = 0;
static int in_rom_menu = 0;
static int live_stream_enabled = 0;

static int system_menu_count = sizeof(systems) / sizeof(SystemEntry) + 3;
static char launch_status_text[256] = "";
static int launch_status_is_error = 0;

typedef struct {
    char *display_name;
    char *rom_path;
    int owns_display_name;
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
static void draw_launch_status(int win_h);
static int file_exists(const char *path);
static void draw_interactive_input_field(void);
static SDL_Texture *load_cover_for_rom(const char *rom_path);
static void leave_rom_menu(void);
static int run_process(const char *const argv[]);
static int run_process_silent(const char *const argv[]);
static void clear_launch_status(void);
static void set_launch_status(int is_error, const char *fmt, ...);
static void format_command(const char *const argv[], char *buffer, size_t buffer_size);
static void build_mame_rompath(char *buffer, size_t buffer_size);
static void close_open_joysticks(void);
static int initialize_frontend_runtime(void);
static void shutdown_frontend_runtime(void);
static int live_stream_menu_index(void);
static int cover_scraper_menu_index(void);
static int exit_menu_index(void);
static const char *live_stream_menu_label(void);
static void toggle_live_stream_bridge(void);
static int start_live_stream_bridge(void);
static void stop_live_stream_bridge(void);
static int resolve_launch_rom_path(const SystemEntry *sys, const char *rom_path, char *final_rom_path, size_t final_size);
static int extract_rom_id(const char *rom_path, char *rom_id, size_t rom_id_size);
static const char *find_pcsx2_app_path(void);
static const char *resolve_absolute_path(const char *path);
static const char *find_pcsx2_binary_path(void);
static void launch_selected_rom(void);
static void handle_current_selection(void);

static void handle_joystick_input(const SDL_Event *event);
static void handle_keyboard_input(const SDL_Event *event);

static int live_stream_menu_index(void) {
    return (int)(sizeof(systems) / sizeof(SystemEntry));
}

static int cover_scraper_menu_index(void) {
    return live_stream_menu_index() + 1;
}

static int exit_menu_index(void) {
    return cover_scraper_menu_index() + 1;
}

static const char *live_stream_menu_label(void) {
#ifdef __APPLE__
    return live_stream_enabled ? "Live Cast Bridge: ON (select to stop)" : "Live Cast Bridge: OFF (select to start)";
#else
    return "Live Cast Bridge: macOS only";
#endif
}

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
        if (i < live_stream_menu_index()) {
            label = systems[i].display_name;
        } else if (i == live_stream_menu_index()) {
            label = live_stream_menu_label();
        } else if (i == cover_scraper_menu_index()) {
            label = "Run Cover Scraper";
        } else {
            label = "Exit";
        }
        render_text_centered(label, start_y + (i - system_scroll_offset) * line_height, color);
    }

    draw_scrollbar(item_count, visible_lines, system_scroll_offset, start_y, line_height, win_w);
    draw_launch_status(win_h);
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

    draw_interactive_input_field(); // Draw input field regardless of ROMs found
    draw_launch_status(win_h);
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

static void leave_rom_menu(void) {
    in_rom_menu = 0;
    typing_in_input = 0;
    SDL_StopTextInput(window);
    free_rom_list();
    free_all_rom_list();
    input_text[0] = '\0';
}

static void clear_launch_status(void) {
    launch_status_text[0] = '\0';
    launch_status_is_error = 0;
}

static void set_launch_status(int is_error, const char *fmt, ...) {
    va_list args;
    va_start(args, fmt);
    vsnprintf(launch_status_text, sizeof(launch_status_text), fmt, args);
    va_end(args);
    launch_status_is_error = is_error;
}

static void format_command(const char *const argv[], char *buffer, size_t buffer_size) {
    size_t used = 0;
    if (!buffer || buffer_size == 0) {
        return;
    }

    buffer[0] = '\0';
    for (int i = 0; argv && argv[i]; ++i) {
        int written = snprintf(buffer + used, buffer_size - used, "%s%s", i == 0 ? "" : " ", argv[i]);
        if (written < 0) {
            break;
        }
        if ((size_t)written >= buffer_size - used) {
            used = buffer_size - 1;
            break;
        }
        used += (size_t)written;
    }
}

static void build_mame_rompath(char *buffer, size_t buffer_size) {
    const char *candidates[] = { "roms", "bios", NULL, NULL };
    char resolved[4096];
    size_t used = 0;

    if (!buffer || buffer_size == 0) {
        return;
    }

    const char *home = getenv("HOME");
    char home_mame_roms[4096];
    if (home && home[0] != '\0') {
        snprintf(home_mame_roms, sizeof(home_mame_roms), "%s/mame/roms", home);
        candidates[2] = home_mame_roms;
    }

    buffer[0] = '\0';
    for (int i = 0; candidates[i]; ++i) {
        const char *path = candidates[i];
        const char *final_path = path;

        if (!file_exists(path)) {
            continue;
        }

        if (realpath(path, resolved)) {
            final_path = resolved;
        }

        int written = snprintf(buffer + used, buffer_size - used, "%s%s",
            used == 0 ? "" : ";", final_path);
        if (written < 0) {
            break;
        }
        if ((size_t)written >= buffer_size - used) {
            buffer[buffer_size - 1] = '\0';
            break;
        }
        used += (size_t)written;
    }
}

static int run_process_internal(const char *const argv[], int log_success) {
    char command[2048];
    format_command(argv, command, sizeof(command));

    pid_t pid = 0;
    int spawn_result = posix_spawnp(&pid, argv[0], NULL, NULL, (char *const *)argv, environ);
    if (spawn_result != 0) {
        SDL_Log("Failed to launch process (%d): %s", spawn_result, command);
        return 0;
    }

    int status = 0;
    if (waitpid(pid, &status, 0) < 0) {
        perror("waitpid failed");
        return 0;
    }

    if (!WIFEXITED(status)) {
        if (WIFSIGNALED(status)) {
            SDL_Log("Process was terminated by signal %d: %s", WTERMSIG(status), command);
        } else {
            SDL_Log("Process ended unexpectedly: %s", command);
        }
        return 0;
    }

    if (WEXITSTATUS(status) != 0) {
        SDL_Log("Process exited with status %d for command: %s", WEXITSTATUS(status), command);
        return 0;
    }

    if (log_success) {
        SDL_Log("Process finished successfully: %s", command);
    }
    return 1;
}

static int run_process(const char *const argv[]) {
    return run_process_internal(argv, 1);
}

static int run_process_silent(const char *const argv[]) {
    return run_process_internal(argv, 0);
}

static void close_open_joysticks(void) {
    int joystick_count = 0;
    SDL_JoystickID *joystick_ids = SDL_GetJoysticks(&joystick_count);

    if (!joystick_ids) {
        return;
    }

    for (int i = 0; i < joystick_count; ++i) {
        SDL_Joystick *joystick = SDL_GetJoystickFromID(joystick_ids[i]);
        if (joystick) {
            SDL_CloseJoystick(joystick);
        }
    }

    SDL_free(joystick_ids);
}

static int initialize_frontend_runtime(void) {
    if (!window || !renderer) {
        if (!SDL_CreateWindowAndRenderer("Joystick Menu", 1024, 768, 0, &window, &renderer)) {
            SDL_Log("SDL_CreateWindowAndRenderer failed: %s", SDL_GetError());
            return 0;
        }
    }

    if (!font) {
        font = TTF_OpenFont("assets/Roboto-Regular.ttf", FONT_SIZE);
        if (!font) {
            SDL_Log("TTF_OpenFont failed: %s", SDL_GetError());
            return 0;
        }
    }

    if (!logo_texture) {
        logo_texture = IMG_LoadTexture(renderer, "assets/logo.png");
    }

    if (!background_texture) {
        background_texture = IMG_LoadTexture(renderer, "assets/background.jpg");
        if (background_texture) {
            SDL_SetTextureBlendMode(background_texture, SDL_BLENDMODE_BLEND);
            SDL_SetTextureAlphaMod(background_texture, 80);
        }
    }

    if (!(Mix_Init(MIX_INIT_OGG) & MIX_INIT_OGG)) {
        SDL_Log("Mix_Init did not initialize OGG support: %s", SDL_GetError());
    }

    SDL_AudioSpec desired_spec = { .freq = 44100, .format = SDL_AUDIO_F32, .channels = 2 };
    if (!Mix_OpenAudio(0, &desired_spec)) {
        SDL_Log("Mix_OpenAudio failed: %s", SDL_GetError());
    }

    if (!music) {
        music = Mix_LoadMUS("assets/background1.ogg");
    }

    if (music && !Mix_PlayingMusic()) {
        Mix_VolumeMusic(64);
        Mix_PlayMusic(music, -1);
    }

    return 1;
}

static void shutdown_frontend_runtime(void) {
    typing_in_input = 0;
    if (window) {
        SDL_StopTextInput(window);
    }

    close_open_joysticks();

    if (music) {
        Mix_HaltMusic();
        Mix_FreeMusic(music);
        music = NULL;
    }
    Mix_CloseAudio();
    Mix_Quit();

    if (cover_texture) {
        SDL_DestroyTexture(cover_texture);
        cover_texture = NULL;
    }
    if (logo_texture) {
        SDL_DestroyTexture(logo_texture);
        logo_texture = NULL;
    }
    if (background_texture) {
        SDL_DestroyTexture(background_texture);
        background_texture = NULL;
    }
    if (font) {
        TTF_CloseFont(font);
        font = NULL;
    }
    if (renderer) {
        SDL_DestroyRenderer(renderer);
        renderer = NULL;
    }
    if (window) {
        SDL_DestroyWindow(window);
        window = NULL;
    }
}

static int start_live_stream_bridge(void) {
#ifdef __APPLE__
    const char *const argv[] = { "./scripts/live-stream/start-macos.sh", NULL };
    return run_process_silent(argv);
#else
    return 0;
#endif
}

static void stop_live_stream_bridge(void) {
#ifdef __APPLE__
    const char *const argv[] = { "./scripts/live-stream/stop-macos.sh", "--quiet", NULL };
    if (!run_process_silent(argv)) {
        SDL_Log("Live Cast Bridge stop script reported a problem.");
    }
#endif
}

static void toggle_live_stream_bridge(void) {
#ifdef __APPLE__
    live_stream_enabled = !live_stream_enabled;
    if (!live_stream_enabled) {
        stop_live_stream_bridge();
        set_launch_status(0, "Live Cast disabled.");
        return;
    }

    set_launch_status(0, "Live Cast armed. Launch a ROM to stream, or select again to stop.");
#else
    set_launch_status(1, "Live Cast Bridge currently needs macOS, ffmpeg, and python3.");
#endif
}

static int resolve_launch_rom_path(const SystemEntry *sys, const char *rom_path, char *final_rom_path, size_t final_size) {
    struct stat st;
    if (stat(rom_path, &st) == -1) {
        SDL_Log("Failed to stat ROM path: %s", rom_path);
        return 0;
    }

    if (S_ISREG(st.st_mode)) {
        snprintf(final_rom_path, final_size, "%s", rom_path);
        return 1;
    }

    if (!S_ISDIR(st.st_mode)) {
        SDL_Log("Unsupported ROM path type: %s", rom_path);
        return 0;
    }

    DIR *d = opendir(rom_path);
    if (!d) {
        SDL_Log("Failed to open ROM directory for launch: %s", rom_path);
        return 0;
    }

    struct dirent *ent;
    while ((ent = readdir(d))) {
        if (strcmp(ent->d_name, ".") == 0 || strcmp(ent->d_name, "..") == 0) {
            continue;
        }

        char candidate_path[1024];
        snprintf(candidate_path, sizeof(candidate_path), "%s/%s", rom_path, ent->d_name);

        struct stat candidate_st;
        if (stat(candidate_path, &candidate_st) == -1) {
            continue;
        }

        if (S_ISREG(candidate_st.st_mode) && has_allowed_extension(ent->d_name, sys->allowed_exts)) {
            snprintf(final_rom_path, final_size, "%s", candidate_path);
            closedir(d);
            return 1;
        }
    }

    closedir(d);
    SDL_Log("No valid ROM file found in directory %s for launch.", rom_path);
    return 0;
}

static int extract_rom_id(const char *rom_path, char *rom_id, size_t rom_id_size) {
    const char *last_slash = strrchr(rom_path, '/');
    const char *filename = last_slash ? last_slash + 1 : rom_path;
    const char *romdot = strrchr(filename, '.');
    size_t name_len = romdot ? (size_t)(romdot - filename) : strlen(filename);

    if (name_len == 0 || name_len >= rom_id_size) {
        return 0;
    }

    memcpy(rom_id, filename, name_len);
    rom_id[name_len] = '\0';
    return 1;
}

static const char *find_pcsx2_app_path(void) {
    static char app_path[1024];
    glob_t g;
    memset(&g, 0, sizeof(g));

    if (glob("/Applications/PCSX2*.app", 0, NULL, &g) == 0 && g.gl_pathc > 0) {
        SDL_strlcpy(app_path, g.gl_pathv[0], sizeof(app_path));
        globfree(&g);
        return app_path;
    }

    globfree(&g);
    return NULL;
}

static const char *resolve_absolute_path(const char *path) {
    static char abs_path[4096];
    if (realpath(path, abs_path)) {
        return abs_path;
    }
    return path;
}

static const char *find_pcsx2_binary_path(void) {
    static char binary_path[4096];
    const char *app_path = find_pcsx2_app_path();
    if (!app_path) {
        return NULL;
    }

    snprintf(binary_path, sizeof(binary_path), "%s/Contents/MacOS/PCSX2", app_path);
    if (access(binary_path, X_OK) == 0) {
        return binary_path;
    }

    char pattern[4096];
    snprintf(pattern, sizeof(pattern), "%s/Contents/MacOS/*", app_path);

    glob_t g;
    memset(&g, 0, sizeof(g));
    if (glob(pattern, 0, NULL, &g) == 0) {
        for (size_t i = 0; i < g.gl_pathc; ++i) {
            if (access(g.gl_pathv[i], X_OK) == 0) {
                SDL_strlcpy(binary_path, g.gl_pathv[i], sizeof(binary_path));
                globfree(&g);
                return binary_path;
            }
        }
    }

    globfree(&g);
    return NULL;
}

static void launch_selected_rom(void) {
    if (!rom_list || selected_rom_index < 0 || selected_rom_index >= rom_count) {
        SDL_Log("Attempted to access invalid ROM index or rom_list is NULL. Index: %d, Count: %d", selected_rom_index, rom_count);
        return;
    }

    if (rom_list[selected_rom_index].rom_path == NULL) {
        leave_rom_menu();
        return;
    }

    const SystemEntry *sys = &systems[selected_system_index];
    const char *rom_path = rom_list[selected_rom_index].rom_path;
    char final_rom_path[1024] = "";
    int launched = 0;
    int previous_music_volume = Mix_VolumeMusic(-1);
    int was_music_playing = Mix_PlayingMusic();
    char mame_rompath[4096] = "";
#ifdef __APPLE__
    int live_stream_started = 0;
    int live_stream_failed = 0;
    int frontend_runtime_shutdown = 0;
#endif

    if (!resolve_launch_rom_path(sys, rom_path, final_rom_path, sizeof(final_rom_path))) {
        set_launch_status(1, "Launch failed: ROM file could not be resolved");
        return;
    }
    const char *launch_rom_path = resolve_absolute_path(final_rom_path);
    build_mame_rompath(mame_rompath, sizeof(mame_rompath));

#ifdef __APPLE__
    if (live_stream_enabled) {
        shutdown_frontend_runtime();
        frontend_runtime_shutdown = 1;
        live_stream_started = start_live_stream_bridge();
        if (!live_stream_started) {
            live_stream_failed = 1;
            SDL_Log("Live Cast Bridge failed to start. Continuing with emulator launch.");
        }
    } else if (was_music_playing) {
        Mix_VolumeMusic(0);
    }
#else
    if (was_music_playing) {
        Mix_VolumeMusic(0);
    }
#endif

    if (strcmp(sys->mame_sys, "neogeo") == 0) {
        char rom_id[256];
        if (!extract_rom_id(final_rom_path, rom_id, sizeof(rom_id))) {
            SDL_Log("Failed to parse NeoGeo ROM id from path: %s", final_rom_path);
            set_launch_status(1, "Launch failed: invalid Neo Geo ROM name");
        } else {
            const char *const argv[] = { "mame", "-rompath", mame_rompath, sys->mame_sys, rom_id, NULL };
            SDL_Log("mame -rompath %s %s %s", mame_rompath, sys->mame_sys, rom_id);
            launched = run_process(argv);
        }
    } else if (strcmp(sys->mame_sys, "rpcs3") == 0) {
        char game_arg[1200];
        snprintf(game_arg, sizeof(game_arg), "--game=%s", launch_rom_path);
        const char *const argv[] = {
            "open",
            "-W",
            "-a",
            "/Users/auser/Applications/RPCS3/RPCS3.app/Contents/MacOS/launcher",
            "--args",
            game_arg,
            NULL
        };
        launched = run_process(argv);
    } else if (strcmp(sys->mame_sys, "pcsx2") == 0) {
        const char *pcsx2_bin = find_pcsx2_binary_path();
        if (pcsx2_bin) {
            const char *const argv_nogui[] = {
                pcsx2_bin,
                "-nogui",
                "-batch",
                "-fastboot",
                "-fullscreen",
                "--",
                launch_rom_path,
                NULL
            };
            launched = run_process(argv_nogui);
            if (!launched) {
                const char *const argv_gui[] = {
                    pcsx2_bin,
                    "-batch",
                    "-fastboot",
                    "-fullscreen",
                    "--",
                    launch_rom_path,
                    NULL
                };
                launched = run_process(argv_gui);
            }
        } else {
            const char *const argv[] = {
                "open",
                "-W",
                "-a",
                "PCSX2",
                "--args",
                "-batch",
                "-fastboot",
                "-fullscreen",
                "--",
                launch_rom_path,
                NULL
            };
            launched = run_process(argv);
        }
    } else {
        const char *const argv[] = { "mame", "-rompath", mame_rompath, sys->mame_sys, sys->launch_arg, launch_rom_path, NULL };
        launched = run_process(argv);
    }

    if (!launched && mame_rompath[0] != '\0'
        && strcmp(sys->mame_sys, "rpcs3") != 0
        && strcmp(sys->mame_sys, "pcsx2") != 0) {
        set_launch_status(1, "Launch failed: check MAME output for missing ROM/BIOS files");
    }

#ifdef __APPLE__
    if (live_stream_enabled) {
        stop_live_stream_bridge();
    }
    if (frontend_runtime_shutdown) {
        if (!initialize_frontend_runtime()) {
            set_launch_status(1, "Launch failed: could not restore menu");
            app_running = 0;
            return;
        }
    } else if (was_music_playing) {
        Mix_VolumeMusic(previous_music_volume >= 0 ? previous_music_volume : 64);
    }
    if (live_stream_enabled && !live_stream_started && launched) {
        set_launch_status(1, "Live Cast Bridge did not start. Check live-stream/logs.");
    }
#else
    if (was_music_playing) {
        Mix_VolumeMusic(previous_music_volume >= 0 ? previous_music_volume : 64);
    }
#endif

    if (launched) {
#ifdef __APPLE__
        if (!live_stream_failed) {
            clear_launch_status();
        }
#else
        clear_launch_status();
#endif
        leave_rom_menu();
    }
}

static void handle_current_selection(void) {
    if (in_rom_menu) {
        launch_selected_rom();
        return;
    }

    if (selected_system_index == exit_menu_index()) {
        app_running = 0;
        return;
    }

    if (selected_system_index == live_stream_menu_index()) {
        toggle_live_stream_bridge();
        return;
    }

    if (selected_system_index == cover_scraper_menu_index()) {
        const char *const argv[] = { "./cover-scraper", NULL };
        run_process(argv);
        return;
    }

    load_all_rom_list(&systems[selected_system_index]);
    filter_rom_list(input_text);
    clear_launch_status();
    in_rom_menu = 1;
}

int main(int argc, char *argv[]) {
    (void)argc;
    (void)argv;

#ifdef __APPLE__
    const char *preserve_live_stream = getenv("JOYSTICK_MENU_PRESERVE_LIVE_STREAM");
    if (!(preserve_live_stream && preserve_live_stream[0] != '\0' && strcmp(preserve_live_stream, "0") != 0)) {
        stop_live_stream_bridge();
        live_stream_enabled = 0;
    } else {
        live_stream_enabled = 1;
    }
#endif
    clear_launch_status();

    if (!SDL_Init(SDL_INIT_VIDEO | SDL_INIT_JOYSTICK | SDL_INIT_AUDIO)) {
        SDL_Log("SDL_Init failed: %s", SDL_GetError());
        return 1;
    }

    if (!TTF_Init()) {
        SDL_Log("TTF_Init failed: %s", SDL_GetError());
        SDL_Quit();
        return 1;
    }

    if (!initialize_frontend_runtime()) {
        TTF_Quit();
        SDL_Quit();
        return 1;
    }

    SDL_Event event;
    while (app_running) {
        while (SDL_PollEvent(&event)) {
            if (event.type == SDL_EVENT_QUIT) {
                app_running = 0;
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
#ifdef __APPLE__
    if (live_stream_enabled) {
        stop_live_stream_bridge();
    }
#endif
    shutdown_frontend_runtime();
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

static void draw_launch_status(int win_h) {
    if (launch_status_text[0] == '\0') {
        return;
    }

    SDL_Color color = launch_status_is_error
        ? (SDL_Color){255, 120, 120, 255}
        : (SDL_Color){170, 220, 170, 255};
    render_text_centered(launch_status_text, win_h - FONT_SIZE - 35, color);
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
            all_rom_list[all_rom_count].owns_display_name = 0;

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
                char sub_file_path[1024];
                snprintf(sub_file_path, sizeof(sub_file_path), "%s/%s", sub_path, sub_entry->d_name);

                struct stat sub_st;
                if (stat(sub_file_path, &sub_st) == -1) {
                    continue;
                }

                if (S_ISREG(sub_st.st_mode) && has_allowed_extension(sub_entry->d_name, sys->allowed_exts)) {
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
                    all_rom_list[all_rom_count].owns_display_name = 0;

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
        for (int i = 0; i < rom_count; ++i) {
            if (rom_list[i].owns_display_name && rom_list[i].display_name) {
                SDL_free(rom_list[i].display_name);
                rom_list[i].display_name = NULL;
            }
        }
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
        rom_list[all_rom_count].owns_display_name = 1;

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
            temp_rom_list[temp_rom_count].owns_display_name = 1;

            if (!temp_rom_list[temp_rom_count].display_name) {
                SDL_Log("Failed to strdup 'Exit' for filtered list");
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
        if (!SDL_OpenJoystick(event->jdevice.which)) {
            SDL_Log("Failed to open joystick id %u: %s", event->jdevice.which, SDL_GetError());
        }
    } else if (event->type == SDL_EVENT_JOYSTICK_REMOVED) {
        SDL_Log("Joystick removed.");
        SDL_Joystick *js = SDL_GetJoystickFromID(event->jdevice.which);
        if (js) {
            SDL_CloseJoystick(js);
        }
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
        handle_current_selection();
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
                handle_current_selection();
                last_input_time = now;
            } else if (event->key.scancode == SDL_SCANCODE_ESCAPE) {
                if (in_rom_menu) {
                    leave_rom_menu();
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
