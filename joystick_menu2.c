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

#define INPUT_COOLDOWN_MS 200
#define AXIS_DEADZONE 8000
#define LOGO_HEIGHT 120
#define FONT_SIZE 16

static Mix_Music *music = NULL;
static SDL_Window *window = NULL;
static SDL_Renderer *renderer = NULL;
static SDL_Texture *logo_texture = NULL;
static SDL_Texture *background_texture = NULL;
static TTF_Font *font = NULL;

static Uint64 last_input_time = 0;

typedef struct {
    const char *dir_name;
    const char *display_name;
    const char *mame_sys;
    const char *launch_arg;
    const char *allowed_exts;
} SystemEntry;

static const SystemEntry systems[] = {
    { "sms1", "Master System",   "sms1",   "-cart",  "sms,bin" },
    { "genesis", "Mega Drive",   "genesis","-cart",  "md,bin" },
    { "snes", "Super Nintendo",  "snes",   "-cart",  "smc,sfc" },
    { "nes", "Nintendo 8-bit",   "nes",    "-cart",  "nes" },
    { "segacd", "Mega CD",       "segacd", "-cdrom", "cue,chd" },
};

static int selected_system_index = 0;
static int system_scroll_offset = 0;
static int in_rom_menu = 0;

typedef struct {
    char *display_name;
    char *rom_path;
} RomEntry;

static RomEntry *rom_list = NULL;
static int rom_count = 0;
static int selected_rom_index = 0;
static int rom_scroll_offset = 0;

static SDL_Texture *cover_texture = NULL;

static void free_cover_texture(void) {
    if (cover_texture) {
        SDL_DestroyTexture(cover_texture);
        cover_texture = NULL;
    }
}

static void render_text_centered(const char *text, float y, SDL_Color color) {
    SDL_Surface *surface = TTF_RenderText_Blended(font, text, SDL_strlen(text), color);
    if (!surface) return;

    SDL_Texture *texture = SDL_CreateTextureFromSurface(renderer, surface);
    int text_w = surface->w;
    int text_h = surface->h;
    //SDL_FreeSurface(surface);
    SDL_DestroySurface(surface);


    int win_w;
    SDL_GetWindowSize(window, &win_w, NULL);

    SDL_FRect dst = { (win_w - text_w) / 2.0f, y, (float)text_w, (float)text_h };
    SDL_RenderTexture(renderer, texture, NULL, &dst);
    SDL_DestroyTexture(texture);
}

static void render_text(const char *text, float x, float y, SDL_Color color) {
    SDL_Surface *surface = TTF_RenderText_Blended(font, text, SDL_strlen(text), color);
    if (!surface) return;

    SDL_Texture *texture = SDL_CreateTextureFromSurface(renderer, surface);
    int text_w = surface->w;
    int text_h = surface->h;
    //SDL_FreeSurface(surface);
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

    // Draw background bar
    SDL_SetRenderDrawColor(renderer, 80, 80, 80, 200);
    SDL_RenderFillRect(renderer, &bar);

    // Draw handle
    SDL_SetRenderDrawColor(renderer, 200, 200, 200, 255);
    SDL_RenderFillRect(renderer, &handle);
}

static void draw_system_menu(void) {
    int win_w, win_h;
    SDL_GetWindowSize(window, &win_w, &win_h);

    int item_count = (int)(sizeof(systems) / sizeof(SystemEntry)) + 1;
    int line_height = FONT_SIZE + 10;
    int visible_lines = (win_h - LOGO_HEIGHT - 40) / line_height;

    if (selected_system_index < system_scroll_offset)
        system_scroll_offset = selected_system_index;
    if (selected_system_index >= system_scroll_offset + visible_lines)
        system_scroll_offset = selected_system_index - visible_lines + 1;

    int start_y = LOGO_HEIGHT + 20;

    for (int i = 0; i < item_count; ++i) {
        if (i < system_scroll_offset) continue;
        if (i >= system_scroll_offset + visible_lines) break;

        SDL_Color color = { 200, 200, 200, 255 };
        if (i == selected_system_index)
            color.r = color.g = 255;

        const char *label = (i < item_count - 1) ? systems[i].display_name : "Exit";
        render_text_centered(label, start_y + (i - system_scroll_offset) * line_height, color);
    }

    draw_scrollbar(item_count, visible_lines, system_scroll_offset, start_y, line_height, win_w);
}

static void draw_rom_menu(void) {
    int win_w, win_h;
    SDL_GetWindowSize(window, &win_w, &win_h);

    int line_height = FONT_SIZE + 10;
    int visible_lines = (win_h - LOGO_HEIGHT - 40) / line_height;

    if (selected_rom_index < rom_scroll_offset)
        rom_scroll_offset = selected_rom_index;
    if (selected_rom_index >= rom_scroll_offset + visible_lines)
        rom_scroll_offset = selected_rom_index - visible_lines + 1;

    int start_y = LOGO_HEIGHT + 20;

    for (int i = 0; i < rom_count; ++i) {
        if (i < rom_scroll_offset) continue;
        if (i >= rom_scroll_offset + visible_lines) break;

        SDL_Color color = { 200, 200, 200, 255 };
        if (i == selected_rom_index)
            color.r = color.g = 255;

        render_text_centered(rom_list[i].display_name, start_y + (i - rom_scroll_offset) * line_height, color);
    }

    draw_scrollbar(rom_count, visible_lines, rom_scroll_offset, start_y, line_height, win_w);

    // Draw cover art for selected ROM on right side (if loaded)
    if (cover_texture) {
        SDL_FRect dst = { (float)(win_w - 220), (float)(start_y), 200.0f, 150.0f };
        SDL_RenderTexture(renderer, cover_texture, NULL, &dst);
    }
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
        if (SDL_strcasecmp(ext, token) == 0)
            return 1;
        token = strtok(NULL, ",");
    }
    return 0;
}

static void free_rom_list(void) {
    if (!rom_list) return;

    for (int i = 0; i < rom_count; ++i) {
        SDL_free(rom_list[i].display_name);
        SDL_free(rom_list[i].rom_path);
    }
    SDL_free(rom_list);
    rom_list = NULL;
    rom_count = 0;
}

static void load_rom_list(const SystemEntry *sys) {
    free_rom_list();
    free_cover_texture();

    char path[512];
    SDL_snprintf(path, sizeof(path), "./roms/%s/", sys->dir_name);

    DIR *dir = opendir(path);
    if (!dir) return;

    int capacity = 20;
    rom_list = SDL_calloc(capacity, sizeof(RomEntry));
    rom_count = 0;

    struct dirent *entry;
    while ((entry = readdir(dir))) {
        // Some systems may not have d_type, so skip check if undefined
#ifdef DT_REG
        if (entry->d_type != DT_REG) continue;
#endif
        if (!has_allowed_extension(entry->d_name, sys->allowed_exts)) continue;

        if (rom_count >= capacity) {
            capacity *= 2;
            rom_list = SDL_realloc(rom_list, capacity * sizeof(RomEntry));
        }

        rom_list[rom_count].display_name = SDL_strdup(entry->d_name);
        SDL_snprintf(path, sizeof(path), "./roms/%s/%s", sys->dir_name, entry->d_name);
        rom_list[rom_count].rom_path = SDL_strdup(path);
        rom_count++;
    }
    closedir(dir);

    rom_list = SDL_realloc(rom_list, (rom_count + 1) * sizeof(RomEntry));
    rom_list[rom_count].display_name = SDL_strdup("Exit");
    rom_list[rom_count].rom_path = NULL;
    rom_count++;
}

static void load_cover_art_for_selected_rom(void) {
    free_cover_texture();
    if (!in_rom_menu || selected_rom_index >= rom_count) return;
    if (!rom_list[selected_rom_index].rom_path) return;

    // Build cover art path by replacing extension with ".png"
    char cover_path[512];
    SDL_strlcpy(cover_path, rom_list[selected_rom_index].rom_path, sizeof(cover_path));
    char *dot = strrchr(cover_path, '.');
    if (dot) {
        SDL_strlcpy(dot, ".png", 5);  // replace extension with .png
        cover_texture = IMG_LoadTexture(renderer, cover_path);
        if (!cover_texture) {
            // Could not load cover art - silently ignore
        }
    }
}

static void handle_joystick_input(const SDL_Event *event) {
    Uint64 now = SDL_GetTicks();
    if (now < last_input_time + INPUT_COOLDOWN_MS) return;

    if (event->type == SDL_EVENT_JOYSTICK_AXIS_MOTION && event->jaxis.axis == 1) {
        int direction = 0;
        if (event->jaxis.value < -AXIS_DEADZONE) direction = -1;
        else if (event->jaxis.value > AXIS_DEADZONE) direction = 1;

        if (direction) {
            if (in_rom_menu) {
                selected_rom_index = (selected_rom_index + rom_count + direction) % rom_count;
                load_cover_art_for_selected_rom();
            } else {
                int item_count = (int)(sizeof(systems) / sizeof(SystemEntry)) + 1;
                selected_system_index = (selected_system_index + item_count + direction) % item_count;
            }
            last_input_time = now;
        }
    }

    if (event->type == SDL_EVENT_JOYSTICK_BUTTON_DOWN && event->jbutton.button == 0) {
        if (in_rom_menu) {
            if (rom_list[selected_rom_index].rom_path == NULL) {
                // Exit selected in ROM menu: resume music
                in_rom_menu = 0;
                free_rom_list();
                free_cover_texture();
                /*
                if (Mix_PlayingMusic() == 0) {
                    //Mix_ResumeMusic();
                }*/
            } else {
                // Launch ROM: pause music first
                if (Mix_PlayingMusic() != 0) {
                    //Mix_PauseMusic();
                }

                const SystemEntry *sys = &systems[selected_system_index];
                char cmd[1024];
                SDL_snprintf(cmd, sizeof(cmd), "mame %s %s \"%s\"",
                             sys->mame_sys, sys->launch_arg, rom_list[selected_rom_index].rom_path);
                system(cmd);

                // After returning from system, resume music and exit ROM menu
                in_rom_menu = 0;
                free_rom_list();
                free_cover_texture();
                //Mix_ResumeMusic();
            }
        } else {
            int item_count = (int)(sizeof(systems) / sizeof(SystemEntry));
            if (selected_system_index == item_count) {
                exit(0);
            } else {
                load_rom_list(&systems[selected_system_index]);
                in_rom_menu = 1;
                selected_rom_index = 0;
                rom_scroll_offset = 0;
                free_cover_texture();
                load_cover_art_for_selected_rom();
            }
        }
        last_input_time = now;
    }
}

int main(int argc, char *argv[]) {
    (void)argc; (void)argv;

    if (SDL_Init(SDL_INIT_VIDEO | SDL_INIT_JOYSTICK | SDL_INIT_AUDIO) != 0) {
        SDL_Log("SDL_Init failed: %s", SDL_GetError());
        return 1;
    }

    if (TTF_Init() != 0) {
        SDL_Log("TTF_Init failed: %s", SDL_GetError());
        SDL_Quit();
        return 1;
    }

    if (SDL_CreateWindowAndRenderer("Joystick Menu", 640, 480, 0, &window, &renderer) != 0) {
        SDL_Log("Window/Renderer creation failed: %s", SDL_GetError());
        TTF_Quit();
        SDL_Quit();
        return 1;
    }

    font = TTF_OpenFont("assets/Roboto-Regular.ttf", FONT_SIZE);
    if (!font) {
        SDL_Log("Failed to load font: %s", SDL_GetError());
        SDL_DestroyRenderer(renderer);
        SDL_DestroyWindow(window);
        TTF_Quit();
        SDL_Quit();
        return 1;
    }

    logo_texture = IMG_LoadTexture(renderer, "assets/logo.png");
    background_texture = IMG_LoadTexture(renderer, "assets/background.jpg");

    if (background_texture) {
        SDL_SetTextureBlendMode(background_texture, SDL_BLENDMODE_BLEND);
        SDL_SetTextureAlphaMod(background_texture, 80);
    }

    if (!Mix_Init(MIX_INIT_OGG)) {
        SDL_Log("Mix_Init failed: %s", SDL_GetError());
    }

    SDL_AudioSpec desired_spec = {0};
    desired_spec.freq = 44100;
    desired_spec.format = SDL_AUDIO_F32;
    desired_spec.channels = 2;

    if (Mix_OpenAudio(0, &desired_spec) == 0) {
        SDL_Log("Mix_OpenAudio success");
    } else {
        SDL_Log("Mix_OpenAudio failed: %s", SDL_GetError());
    }

    music = Mix_LoadMUS("assets/background1.ogg");
    if (!music) {
        SDL_Log("Failed to load music: %s", SDL_GetError());
    } else {
        //Mix_PlayMusic(music, -1);
    }

    SDL_Event event;
    int running = 1;

    while (running) {
        while (SDL_PollEvent(&event)) {
            if (event.type == SDL_EVENT_QUIT) {
                running = 0;
            }
            if (event.type == SDL_EVENT_JOYSTICK_ADDED) {
                SDL_Joystick *joy = SDL_OpenJoystick(event.jdevice.which);
                if (joy) SDL_Log("Joystick added: %s", SDL_GetJoystickName(joy));
            }
            if (event.type == SDL_EVENT_JOYSTICK_REMOVED) {
                SDL_CloseJoystick(SDL_GetJoystickFromID(event.jdevice.which));
            }
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
            SDL_FRect dst = { (win_w - 200) / 2.0f, 20.0f, 200.0f, 100.0f };
            SDL_RenderTexture(renderer, logo_texture, NULL, &dst);
        }

        if (in_rom_menu)
            draw_rom_menu();
        else
            draw_system_menu();

        SDL_Color sig_color = { 150, 150, 150, 255 };
        const char *signature = "YourSignatureHere";
        render_text(signature, 10, win_h - FONT_SIZE - 10, sig_color);

        SDL_RenderPresent(renderer);
        SDL_Delay(16);
    }

    free_rom_list();
    free_cover_texture();
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
