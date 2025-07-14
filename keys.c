#include <SDL3/SDL.h>
#include <stdio.h>

int main(int argc, char *argv[]) {
    // Initialize SDL. In SDL3, SDL_Init returns SDL_TRUE (1) on success.
    if (!SDL_Init(SDL_INIT_VIDEO)) {
        fprintf(stderr, "SDL could not initialize! SDL_Error: %s\n", SDL_GetError());
        return 1;
    }

    // Create a window
    SDL_Window *window = SDL_CreateWindow("SDL3 Keyboard Event var_dump", 800, 600, 0);
    if (window == NULL) {
        fprintf(stderr, "Window could not be created! SDL_Error: %s\n", SDL_GetError());
        SDL_Quit();
        return 1;
    }

    // Create a renderer (minimal, just to keep the window open)
    SDL_Renderer *renderer = SDL_CreateRenderer(window, NULL);
    if (renderer == NULL) {
        fprintf(stderr, "Renderer could not be created! SDL_Error: %s\n", SDL_GetError());
        SDL_DestroyWindow(window);
        SDL_Quit();
        return 1;
    }

    SDL_Event event;
    int running = 1;

    printf("--- SDL3 Keyboard Event var_dump ---\n");
    printf("Press any key to dump its SDL_KeyboardEvent structure.\n");
    printf("Press ESC or close the window to quit.\n\n");

    // Event loop
    while (running) {
        while (SDL_PollEvent(&event)) {
            // Handle quit event (e.g., closing the window)
            if (event.type == SDL_EVENT_QUIT) {
                running = 0;
            }
            // Handle key down events to inspect SDL_KeyboardEvent
            else if (event.type == SDL_EVENT_KEY_DOWN) {
                printf("--- Dumping SDL_KeyboardEvent (event.key) ---\n");

                // Common event fields (from SDL_CommonEvent, embedded in SDL_KeyboardEvent)
                printf("  Type (event.key.type): %u\n", event.key.type);
                printf("    (Expected: %u for SDL_EVENT_KEY_DOWN)\n", SDL_EVENT_KEY_DOWN);
                printf("  Timestamp (event.key.timestamp): %llu\n", event.key.timestamp); // Uint64, use %llu

                // SDL_KeyboardEvent specific fields
                printf("  Window ID (event.key.windowID): %u\n", event.key.windowID);
                printf("  Repeat (event.key.repeat): %s\n", (event.key.repeat) ? "True" : "False");

                // Key identification fields
                //printf("  Keycode (event.key.keycode): %d\n", event.key.keycode);
                //printf("    (Name: '%s')\n", SDL_GetKeyName(event.key.keycode));
                printf("  Scancode (event.key.scancode): %d\n", event.key.scancode);
                printf("    (Name: '%s')\n", SDL_GetScancodeName(event.key.scancode));

                // Modifier state
                printf("  Modifiers (event.key.mod): %u\n", event.key.mod);
                printf("    - Shift: %s\n", (event.key.mod & SDL_KMOD_SHIFT) ? "True" : "False");
                printf("    - Ctrl: %s\n", (event.key.mod & SDL_KMOD_CTRL) ? "True" : "False");
                printf("    - Alt: %s\n", (event.key.mod & SDL_KMOD_ALT) ? "True" : "False");
                printf("    - GUI (Meta/Windows/Command): %s\n", (event.key.mod & SDL_KMOD_GUI) ? "True" : "False");
                printf("    - Num Lock: %s\n", (event.key.mod & SDL_KMOD_NUM) ? "True" : "False");
                printf("    - Caps Lock: %s\n", (event.key.mod & SDL_KMOD_CAPS) ? "True" : "False");

                printf("--------------------------------------\n\n");

                // If ESC is pressed, quit the program
                //if (event.key.keycode == SDLK_ESCAPE) {
                //    running = 0;
                //}
            }
            // Also dump SDL_TextInputEvent if text input is active (e.g., after SDL_StartTextInput)
            else if (event.type == SDL_EVENT_TEXT_INPUT) {
                printf("--- Dumping SDL_TextInputEvent (event.text) ---\n");
                printf("  Type (event.text.type): %u\n", event.text.type);
                printf("    (Expected: %u for SDL_EVENT_TEXT_INPUT)\n", SDL_EVENT_TEXT_INPUT);
                printf("  Timestamp (event.text.timestamp): %llu\n", event.text.timestamp);
                printf("  Window ID (event.text.windowID): %u\n", event.text.windowID);
                printf("  Text (event.text.text): \"%s\"\n", event.text.text);
                printf("--------------------------------------\n\n");
            }
        }

        // Minimal rendering loop to keep the window responsive
        SDL_SetRenderDrawColor(renderer, 0, 0, 0, 255);
        SDL_RenderClear(renderer);
        SDL_RenderPresent(renderer);
        SDL_Delay(10);
    }

    // Clean up
    SDL_DestroyRenderer(renderer);
    SDL_DestroyWindow(window);
    SDL_Quit();

    return 0;
}