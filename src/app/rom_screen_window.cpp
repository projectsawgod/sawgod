// The ROM screen's window (first_run.h run_rom_screen): its own small SDL window and renderer, closed before
// the game opens its own.
#include <algorithm>
#include <atomic>
#include <filesystem>
#include <memory>
#include <mutex>
#include <thread>
#include <vector>

#include <SDL3/SDL.h>

#include "app/first_run.h"
#include "app/rom_screen.h"
#include "app/splash.h"
#include "engine/assets.h"
#include "engine/rom_check.h"

namespace sawgod::app {

namespace {

// The picker's answer arrives on SDL's callback, possibly on another thread.
struct Picked {
    std::mutex lock;
    bool ready = false;
    std::string path; // "" when cancelled
};

void SDLCALL picked(void *user, const char *const *files, int) {
    std::unique_ptr<std::shared_ptr<Picked>> holder(static_cast<std::shared_ptr<Picked> *>(user));
    Picked &p = **holder;
    std::lock_guard<std::mutex> g(p.lock);
    p.ready = true;
    p.path = files && files[0] ? files[0] : "";
}

const rom_text::Pair *message_for(RomVerdict v) {
    switch (v) {
    case RomVerdict::GameBoy:
        return &rom_text::kGameBoy;
    case RomVerdict::Modified:
    case RomVerdict::Unsupported:
        return &rom_text::kInvalid;
    case RomVerdict::NoRomInArchive:
        return &rom_text::kNoRomInArchive;
    case RomVerdict::Unreadable:
        return &rom_text::kUnreadable;
    case RomVerdict::Ok:
        break;
    }
    return nullptr;
}

} // namespace

std::string run_rom_screen(const std::string &data_dir, const std::string &exe_dir,
                           const std::string &gb_maps_dir) {
    if (!SDL_InitSubSystem(SDL_INIT_VIDEO | SDL_INIT_GAMEPAD)) {
        SDL_Log("ROM screen: %s", SDL_GetError());
        return {};
    }
#ifdef __ANDROID__
    const SDL_WindowFlags flags = SDL_WINDOW_FULLSCREEN;
#else
    const SDL_WindowFlags flags = SDL_WINDOW_RESIZABLE;
#endif
    SDL_Window *window = SDL_CreateWindow("SawGod", kRomScreenW * 2, kRomScreenH * 2, flags);
    if (window)
        set_window_icon(window);
    SDL_Renderer *renderer = window ? SDL_CreateRenderer(window, nullptr) : nullptr;
    if (!renderer) {
        SDL_Log("ROM screen: %s", SDL_GetError());
        if (window)
            SDL_DestroyWindow(window);
        return {};
    }
    SDL_SetRenderVSync(renderer, 1);
    SDL_Texture *texture = SDL_CreateTexture(renderer, SDL_PIXELFORMAT_XRGB8888, SDL_TEXTUREACCESS_STREAMING,
                                             kRomScreenW, kRomScreenH);
    SDL_SetTextureScaleMode(texture, SDL_SCALEMODE_NEAREST);
    // the canvas at the largest whole scale that fits, centered on black; mouse positions map back to it
    SDL_SetRenderLogicalPresentation(renderer, kRomScreenW, kRomScreenH,
                                     SDL_LOGICAL_PRESENTATION_INTEGER_SCALE);
    int logo_w = 0, logo_h = 0, logo_tw = 0, logo_th = 0;
    const std::vector<uint32_t> logo_px = load_logo(Logo::OnBlack, &logo_w, &logo_h);
    SDL_Texture *logo = nullptr; // logo_px averaged down to the logo's size in window pixels

    RomScreenState state;
    std::string found = detect_rom_beside(exe_dir, data_dir);
    if (!found.empty())
        state.mode = RomScreenState::Mode::Found;
    // shared with SDL's callback, which may arrive after this screen closes (the dialog still open)
    const auto pick = std::make_shared<Picked>();
    bool picker_open = false;
    std::atomic<float> progress{0};
    std::atomic<bool> working_done{false};
    std::string work_error, result;
    std::thread worker;
    std::vector<uint8_t> rom;
    std::string stored;

    // An offered file: check it, store it, start building.
    const auto offer = [&](const std::string &path) {
        RomCheck c = check_rom_selection(path);
        if (c.verdict != RomVerdict::Ok) {
            state.mode = RomScreenState::Mode::Choose;
            state.message = message_for(c.verdict);
            // the SHA-1 it should have, for a ROM that isn't it (not for a Game Boy ROM or an unreadable
            // file)
            const bool invalid = c.verdict == RomVerdict::Modified || c.verdict == RomVerdict::Unsupported;
            state.expected_sha1 = invalid ? kBaselineSha1 : "";
            return;
        }
        std::string error;
        stored = store_rom(data_dir, c.name, c.rom, &error);
        if (stored.empty()) {
            state.mode = RomScreenState::Mode::Choose;
            state.message = &rom_text::kCopyFailed;
            state.expected_sha1.clear();
            SDL_Log("ROM screen: %s", error.c_str());
            return;
        }
        rom = std::move(c.rom);
        state.mode = RomScreenState::Mode::Working;
        state.message = nullptr;
        worker = std::thread([&, sha1 = c.sha1] {
            if (!ensure_archives(
                    data_dir, rom, sha1, gb_maps_dir, [&](float f) { progress = f; }, &work_error))
                SDL_Log("setup: %s", work_error.c_str());
            working_done = true;
        });
    };
    const auto open_picker = [&] {
        if (picker_open)
            return;
        picker_open = true;
        [[maybe_unused]] static const SDL_DialogFileFilter filters[] = {
            {"WonderSwan Color ROM (.wsc, .zip, .7z)", "wsc;zip;7z"}, {"All files", "*"}};
        // the callback holds its own reference: freed by whichever of the two finishes last
#ifdef __ANDROID__
        // Providers often label ROMs as application/octet-stream: allow all types in SAF.
        SDL_ShowOpenFileDialog(picked, new std::shared_ptr<Picked>(pick), window, nullptr, 0, nullptr, false);
#else
        SDL_ShowOpenFileDialog(picked, new std::shared_ptr<Picked>(pick), window, filters, 2, nullptr, false);
#endif
    };
    const auto to_canvas = [&](float wx, float wy, int *cx, int *cy) {
        float lx = 0, ly = 0;
        SDL_RenderCoordinatesFromWindow(renderer, wx, wy, &lx, &ly);
        *cx = int(lx);
        *cy = int(ly);
    };

    std::vector<SDL_Gamepad *> pads;
    bool quit = false;
    while (!quit) {
        SDL_Event e;
        while (SDL_PollEvent(&e)) {
            const bool choosing = state.mode == RomScreenState::Mode::Choose;
            const bool finding = state.mode == RomScreenState::Mode::Found;
            switch (e.type) {
            case SDL_EVENT_QUIT:
                quit = true;
                break;
            case SDL_EVENT_DROP_BEGIN:
                state.drop_hot = choosing;
                break;
            case SDL_EVENT_DROP_COMPLETE:
                state.drop_hot = false;
                break;
            case SDL_EVENT_DROP_FILE:
                state.drop_hot = false;
                if (choosing || finding)
                    offer(e.drop.data ? e.drop.data : "");
                break;
            case SDL_EVENT_MOUSE_MOTION: {
                int x, y;
                to_canvas(e.motion.x, e.motion.y, &x, &y);
                if (choosing)
                    state.box_hot = rom_screen_box().contains(x, y);
                if (finding)
                    state.found_choice = rom_screen_no().contains(x, y)    ? 1
                                         : rom_screen_yes().contains(x, y) ? 0
                                                                           : state.found_choice;
                break;
            }
            case SDL_EVENT_MOUSE_BUTTON_UP:
                if (e.button.button == SDL_BUTTON_LEFT) {
                    int x, y;
                    to_canvas(e.button.x, e.button.y, &x, &y);
                    if (choosing && rom_screen_box().contains(x, y))
                        open_picker();
                    else if (finding && rom_screen_yes().contains(x, y))
                        offer(found);
                    else if (finding && rom_screen_no().contains(x, y))
                        state.mode = RomScreenState::Mode::Choose;
                }
                break;
            case SDL_EVENT_KEY_DOWN:
                if (e.key.key == SDLK_RETURN || e.key.key == SDLK_KP_ENTER || e.key.key == SDLK_SPACE) {
                    if (choosing)
                        open_picker();
                    else if (finding && state.found_choice == 0)
                        offer(found);
                    else if (finding)
                        state.mode = RomScreenState::Mode::Choose;
                } else if (finding && (e.key.key == SDLK_LEFT || e.key.key == SDLK_RIGHT)) {
                    state.found_choice ^= 1;
                } else if (e.key.key == SDLK_TAB && choosing) {
                    state.box_hot = !state.box_hot;
                } else if (e.key.key == SDLK_ESCAPE && finding) {
                    state.mode = RomScreenState::Mode::Choose;
                }
                break;
            case SDL_EVENT_GAMEPAD_ADDED:
                if (SDL_Gamepad *g = SDL_OpenGamepad(e.gdevice.which))
                    pads.push_back(g);
                break;
            case SDL_EVENT_GAMEPAD_BUTTON_DOWN:
                if (e.gbutton.button == SDL_GAMEPAD_BUTTON_SOUTH) {
                    if (choosing)
                        open_picker();
                    else if (finding && state.found_choice == 0)
                        offer(found);
                    else if (finding)
                        state.mode = RomScreenState::Mode::Choose;
                } else if (e.gbutton.button == SDL_GAMEPAD_BUTTON_EAST && finding) {
                    state.mode = RomScreenState::Mode::Choose;
                } else if (finding && (e.gbutton.button == SDL_GAMEPAD_BUTTON_DPAD_LEFT ||
                                       e.gbutton.button == SDL_GAMEPAD_BUTTON_DPAD_RIGHT)) {
                    state.found_choice ^= 1;
                }
                break;
            default:
                break;
            }
        }
        {
            std::string path;
            bool ready = false;
            {
                std::lock_guard<std::mutex> g(pick->lock);
                ready = pick->ready;
                path = pick->path;
                pick->ready = false;
            }
            if (ready)
                picker_open = false;
            if (ready && !path.empty() && state.mode == RomScreenState::Mode::Choose)
                offer(path);
        }
        if (state.mode == RomScreenState::Mode::Working) {
            state.progress = progress;
            if (working_done && worker.joinable()) { // once: a failed setup stays on its message
                worker.join();
                if (work_error.empty()) {
                    result = stored;
                    break;
                }
                state.message = &rom_text::kSetupFailed;
                state.detail = work_error.substr(0, 60);
            }
        }
        const std::vector<uint32_t> px = draw_rom_screen(state);
        SDL_UpdateTexture(texture, nullptr, px.data(), kRomScreenW * 4);
        SDL_SetRenderDrawColor(renderer, 0, 0, 0, 255);
        SDL_RenderClear(renderer);
        SDL_RenderTexture(renderer, texture, nullptr, nullptr);
        // the logo at the window's own resolution (the canvas is scaled by whole steps; the logo is not pixel
        // art)
        SDL_FRect shown{};
        SDL_GetRenderLogicalPresentationRect(renderer, &shown);
        const RomScreenRect place = rom_screen_logo();
        const float scale = shown.w / float(kRomScreenW);
        const int tw = std::min(logo_w, int(float(place.w) * scale + 0.5f));
        const int th = std::min(logo_h, int(float(place.h) * scale + 0.5f));
        if (!logo_px.empty() && tw > 0 && th > 0 && (tw != logo_tw || th != logo_th)) {
            SDL_DestroyTexture(logo);
            const std::vector<uint32_t> px = scale_rom_screen_logo(logo_px, logo_w, logo_h, tw, th);
            logo = SDL_CreateTexture(renderer, SDL_PIXELFORMAT_ARGB8888, SDL_TEXTUREACCESS_STATIC, tw, th);
            if (logo) {
                SDL_UpdateTexture(logo, nullptr, px.data(), tw * 4);
                SDL_SetTextureBlendMode(logo, SDL_BLENDMODE_BLEND_PREMULTIPLIED);
            }
            logo_tw = tw;
            logo_th = th;
        }
        if (logo) {
            const SDL_FRect at{float(place.x), float(place.y), float(place.w), float(place.h)};
            SDL_RenderTexture(renderer, logo, nullptr, &at);
        }
        SDL_RenderPresent(renderer);
    }
    if (worker.joinable()) // closed mid-build: let it finish (an atomic write leaves no half archive)
        worker.join();
    for (SDL_Gamepad *g : pads)
        SDL_CloseGamepad(g);
    SDL_DestroyTexture(logo);
    SDL_DestroyTexture(texture);
    SDL_DestroyRenderer(renderer);
    SDL_DestroyWindow(window);
    SDL_QuitSubSystem(SDL_INIT_VIDEO | SDL_INIT_GAMEPAD);
    return result;
}

} // namespace sawgod::app
