#include "bamboo_engine_adapter.h"
#include "config_store.h"
#include "font_loader.h"
#include "key_mapper.h"
#include "ui_views.h"

#include <SDL3/SDL.h>
#include <imgui.h>
#include <imgui_impl_sdl3.h>
#include <imgui_impl_sdlrenderer3.h>
#include <string>

int main() {
    // Khớp với areca-settings.desktop để Wayland compositor tìm đúng icon.
    if (!SDL_SetHint(SDL_HINT_APP_ID, "areca-settings")) {
        SDL_Log("Không thể đặt application ID: %s", SDL_GetError());
    }
    SDL_SetHint(SDL_HINT_APP_NAME, "Areca Settings");

    if (!SDL_Init(SDL_INIT_VIDEO)) {
        SDL_Log("Không thể khởi tạo SDL: %s", SDL_GetError());
        return 1;
    }
    SDL_Window* window =
        SDL_CreateWindow("Areca Settings", 960, 680, SDL_WINDOW_RESIZABLE | SDL_WINDOW_HIGH_PIXEL_DENSITY);
    if (!window) {
        SDL_Log("Không thể tạo cửa sổ: %s", SDL_GetError());
        SDL_Quit();
        return 1;
    }
    SDL_SetWindowMinimumSize(window, 760, 480);
    SDL_Renderer* renderer = SDL_CreateRenderer(window, nullptr);
    if (!renderer) {
        SDL_Log("Không thể tạo SDL renderer: %s", SDL_GetError());
        SDL_DestroyWindow(window);
        SDL_Quit();
        return 1;
    }
    SDL_SetRenderVSync(renderer, 1);

    IMGUI_CHECKVERSION();
    ImGui::CreateContext();
    ImGuiIO& io = ImGui::GetIO();
    io.IniFilename = nullptr;
    areca::settings::loadVietnameseFont();
    ImGui::StyleColorsDark();
    if (!ImGui_ImplSDL3_InitForSDLRenderer(window, renderer) || !ImGui_ImplSDLRenderer3_Init(renderer)) {
        SDL_Log("Không thể khởi tạo Dear ImGui: %s", SDL_GetError());
        ImGui::DestroyContext();
        SDL_DestroyRenderer(renderer);
        SDL_DestroyWindow(window);
        SDL_Quit();
        return 1;
    }

    areca::settings::ConfigStore config;
    config.load();
    const auto inputMethods = areca::BambooEngineAdapter::inputMethodNames();
    const auto charsets = areca::BambooEngineAdapter::charsetNames();
    bool listeningShortcut = false;
    std::string status;
    bool running = true;
    while (running) {
        SDL_Event event;
        while (SDL_PollEvent(&event)) {
            if (listeningShortcut && event.type == SDL_EVENT_KEY_DOWN && !event.key.repeat) {
                if (event.key.key == SDLK_ESCAPE) {
                    listeningShortcut = false;
                } else {
                    const std::string fcitxKeyName = areca::settings::sdlToFcitx(event.key.key, event.key.mod);
                    if (!fcitxKeyName.empty()) {
                        fcitx::Key key(fcitxKeyName);
                        if (key.isValid()) {
                            config.main.switchModeKey.setValue(fcitx::KeyList{key});
                            listeningShortcut = false;
                        }
                    }
                }
                continue;
            }
            ImGui_ImplSDL3_ProcessEvent(&event);
            if (event.type == SDL_EVENT_QUIT) {
                running = false;
            }
        }
        ImGui_ImplSDLRenderer3_NewFrame();
        ImGui_ImplSDL3_NewFrame();
        ImGui::NewFrame();
        areca::settings::drawWindow(config, inputMethods, charsets, listeningShortcut, status, running);
        ImGui::Render();

        SDL_SetRenderDrawColorFloat(renderer, 0.08F, 0.09F, 0.11F, 1.0F);
        SDL_RenderClear(renderer);
        ImGui_ImplSDLRenderer3_RenderDrawData(ImGui::GetDrawData(), renderer);
        SDL_RenderPresent(renderer);
    }

    ImGui_ImplSDLRenderer3_Shutdown();
    ImGui_ImplSDL3_Shutdown();
    ImGui::DestroyContext();
    SDL_DestroyRenderer(renderer);
    SDL_DestroyWindow(window);
    SDL_Quit();
    return 0;
}
