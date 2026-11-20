// Minimal SDL shell for the original cartridge configuration.
#include "app/audio_output.h"
#include "app/input_movie.h"
#include "decomp/input.h"
#include "emu/png.h"
#include "engine/assets.h"
#include "engine/persist.h"
#include "engine/rom_check.h"
#include "game/setup.h"
#include <SDL3/SDL.h>
#include <SDL3/SDL_main.h>
#include <algorithm>
#include <atomic>
#include <cctype>
#include <charconv>
#include <cstdio>
#include <filesystem>
#include <fstream>
#include <iterator>
#include <stdexcept>
#include <string_view>

namespace {
namespace fs = std::filesystem;
using sawgod::emu::Wsc;
struct Args {
    fs::path rom, sram, savesram, inputs, goldendir, shot;
    int frames = -1, scale = 0, goldenevery = 0;
    bool fullscreen = false;
};
int integer(const std::string &s) {
    int value = -1;
    const auto result = std::from_chars(s.data(), s.data() + s.size(), value);
    if (result.ec != std::errc() || result.ptr != s.data() + s.size() || value < 0)
        throw std::runtime_error("Expected a nonnegative integer.");
    return value;
}
fs::path from_utf8(std::string_view value) { return fs::path(std::u8string(value.begin(), value.end())); }
std::string utf8(const fs::path &path) {
    const auto value = path.u8string();
    return {value.begin(), value.end()};
}
std::vector<uint8_t> read(const fs::path &path) {
    std::ifstream file(path, std::ios::binary);
    if (!file)
        throw std::runtime_error("Cannot read " + utf8(path));
    return {std::istreambuf_iterator<char>(file), {}};
}
void write(const fs::path &path, const std::vector<uint8_t> &bytes) {
    std::string error;
    if (!sawgod::WriteFileAtomic(utf8(path), bytes, &error))
        throw std::runtime_error(error);
}
void picture(const Wsc &w, const fs::path &path) {
    const auto pixels = w.framebuffer_rgb(Wsc::ColorMode::Lcd);
    write(path, sawgod::emu::encode_png(pixels.data(), Wsc::kWidth, Wsc::kHeight));
}
void golden(const Wsc &w, const fs::path &dir, int frame) {
    fs::create_directories(dir);
    char number[32];
    std::snprintf(number, sizeof number, "%06d", frame);
    const std::string prefix = number;
    write(dir / (prefix + ".ram"), {w.ram.begin(), w.ram.end()});
    write(dir / (prefix + ".sram"), w.sram);
    picture(w, dir / (prefix + ".png"));
}
void load_sram(Wsc &w, const fs::path &path) {
    if (!fs::exists(path))
        return;
    auto bytes = read(path);
    if (bytes.size() != w.sram.size())
        throw std::runtime_error("SRAM file has the wrong size: " + utf8(path));
    w.sram = std::move(bytes);
}
struct Dialog {
    std::atomic<bool> done{false};
    std::string path;
    bool failed = false;
};
void SDLCALL picked(void *userdata, const char *const *files, int) {
    auto &result = *static_cast<Dialog *>(userdata);
    result.failed = files == nullptr;
    if (files && files[0])
        result.path = files[0];
    result.done.store(true);
}
fs::path choose_rom(const fs::path &base) {
    std::vector<fs::path> candidates;
    for (const auto &entry : fs::directory_iterator(base)) {
        auto extension = entry.path().extension().string();
        std::transform(extension.begin(), extension.end(), extension.begin(),
                       [](unsigned char c) { return char(std::tolower(c)); });
        if (entry.is_regular_file() && extension == ".wsc")
            candidates.push_back(entry.path());
    }
    if (!candidates.empty()) {
        std::sort(candidates.begin(), candidates.end());
        return candidates.front();
    }
    Dialog result;
    const SDL_DialogFileFilter filter{"WonderSwan Color ROM", "wsc"};
    SDL_ShowOpenFileDialog(picked, &result, nullptr, &filter, 1, nullptr, false);
    while (!result.done.load()) {
        SDL_PumpEvents();
        SDL_Delay(10);
    }
    if (result.failed)
        throw std::runtime_error("Could not open the ROM picker.");
    return from_utf8(result.path);
}
int replay(const Args &args, Wsc &w) {
    if (!args.sram.empty())
        load_sram(w, args.sram);
    if (!args.inputs.empty() && !fs::is_regular_file(args.inputs))
        throw std::runtime_error("Cannot read input recording.");
    const auto changes = args.inputs.empty() ? std::vector<std::pair<uint64_t, uint16_t>>{}
                                             : sawgod::app::read_input_movie(utf8(args.inputs));
    size_t next = 0;
    uint16_t held = 0;
    for (int frame = 1; frame <= args.frames; ++frame) {
        w.set_keys(held);
        w.run_frame();
        w.apu.take();
        // Same frame numbering as sawgod-headless: apply changes after that frame completes.
        while (next < changes.size() && changes[next].first <= uint64_t(frame))
            held = changes[next++].second;
        if (!args.goldendir.empty() && args.goldenevery && frame % args.goldenevery == 0)
            golden(w, args.goldendir, frame);
    }
    if (!args.savesram.empty())
        write(args.savesram, w.sram);
    if (!args.shot.empty())
        picture(w, args.shot);
    std::vector<uint8_t> pixels;
    for (const auto pixel : w.framebuffer()) {
        pixels.push_back(uint8_t(pixel));
        pixels.push_back(uint8_t(pixel >> 8));
    }
    std::printf("frames %d\nram %s\nsram %s\npicture %s\n", args.frames,
                sawgod::to_hex(sawgod::sha1(w.ram.data(), w.ram.size())).c_str(),
                sawgod::to_hex(sawgod::sha1(w.sram.data(), w.sram.size())).c_str(),
                sawgod::to_hex(sawgod::sha1(pixels.data(), pixels.size())).c_str());
    return 0;
}
struct Sdl {
    ~Sdl() { SDL_Quit(); }
};
int desktop(Args args) {
    if (!SDL_Init(SDL_INIT_VIDEO | SDL_INIT_AUDIO | SDL_INIT_GAMEPAD))
        throw std::runtime_error(SDL_GetError());
    Sdl sdl;
    try {
        const char *base_string = SDL_GetBasePath();
        if (!base_string)
            throw std::runtime_error(SDL_GetError());
        const auto base = from_utf8(base_string);
        if (args.rom.empty())
            args.rom = choose_rom(base);
        if (args.rom.empty())
            return 0; // Picker canceled.
        auto rom = read(args.rom);
        if (sawgod::classify_rom(rom) != sawgod::RomVerdict::Ok)
            throw std::runtime_error("This is not the supported Japanese Makai Toushi SaGa WSC ROM.\n"
                                     "Use an unmodified dump of the supported cartridge.");
        auto w = sawgod::game::make_decomp_game(std::move(rom));
        auto save = args.rom;
        save.replace_extension(".sav");
        load_sram(*w, save);
        // Persist every cartridge byte, including ORIJINARU and SRAM used as work memory.
        // A two-second bound prevents continuous writes from postponing persistence forever.
        sawgod::SaveFlusher flusher(utf8(save), 0, w->sram.size(), 30, &sawgod::WriteFileAtomic, 151);
        flusher.reset(w->sram);
        if (!args.scale) {
            SDL_Rect usable{};
            args.scale =
                SDL_GetDisplayUsableBounds(SDL_GetPrimaryDisplay(), &usable)
                    ? std::clamp(std::min(usable.w / Wsc::kWidth, (usable.h - 64) / Wsc::kHeight), 1, 4)
                    : 3;
        }
        using Window = std::unique_ptr<SDL_Window, decltype(&SDL_DestroyWindow)>;
        using Renderer = std::unique_ptr<SDL_Renderer, decltype(&SDL_DestroyRenderer)>;
        using Texture = std::unique_ptr<SDL_Texture, decltype(&SDL_DestroyTexture)>;
        Window window(SDL_CreateWindow("SawGod Decomp", Wsc::kWidth * args.scale, Wsc::kHeight * args.scale,
                                       SDL_WINDOW_RESIZABLE | (args.fullscreen ? SDL_WINDOW_FULLSCREEN : 0)),
                      SDL_DestroyWindow);
        if (!window)
            throw std::runtime_error(SDL_GetError());
        SDL_SetWindowMinimumSize(window.get(), Wsc::kWidth, Wsc::kHeight);
        Renderer renderer(SDL_CreateRenderer(window.get(), nullptr), SDL_DestroyRenderer);
        if (!renderer)
            throw std::runtime_error(SDL_GetError());
        SDL_SetRenderVSync(renderer.get(), 0);
        Texture texture(SDL_CreateTexture(renderer.get(), SDL_PIXELFORMAT_XRGB8888,
                                          SDL_TEXTUREACCESS_STREAMING, Wsc::kWidth, Wsc::kHeight),
                        SDL_DestroyTexture);
        if (!texture)
            throw std::runtime_error(SDL_GetError());
        SDL_SetTextureScaleMode(texture.get(), SDL_SCALEMODE_NEAREST);
        sawgod::app::AudioOutput audio;
        if (!audio.open())
            throw std::runtime_error(SDL_GetError());
        sawgod::decomp::Input input;
        input.load(base / "sawgod-decomp-input.txt");
        bool running = true;
        uint64_t next_frame = SDL_GetTicksNS();
        while (running) {
            SDL_Event event;
            while (SDL_PollEvent(&event)) {
                input.event(event);
                if (event.type == SDL_EVENT_QUIT ||
                    (event.type == SDL_EVENT_KEY_DOWN && event.key.scancode == SDL_SCANCODE_ESCAPE))
                    running = false;
                if (event.type == SDL_EVENT_KEY_DOWN && !event.key.repeat &&
                    (event.key.scancode == SDL_SCANCODE_F11 ||
                     (event.key.scancode == SDL_SCANCODE_RETURN && (event.key.mod & SDL_KMOD_ALT))))
                    SDL_SetWindowFullscreen(window.get(),
                                            !(SDL_GetWindowFlags(window.get()) & SDL_WINDOW_FULLSCREEN));
            }
            if (!running)
                break;
            const auto now = SDL_GetTicksNS();
            constexpr auto interval = sawgod::game::kWscFrameNs;
            if (now > next_frame + interval * 8)
                next_frame = now;
            if (now < next_frame) {
                SDL_DelayPrecise(std::min(next_frame - now, interval));
                continue;
            }
            int ran = 0;
            while (next_frame <= now && ran++ < 4) {
                // Do not send fullscreen shortcuts or unfocused keyboard state to the cartridge.
                const bool focused = SDL_GetWindowFlags(window.get()) & SDL_WINDOW_INPUT_FOCUS;
                const bool alt = SDL_GetModState() & SDL_KMOD_ALT;
                w->set_keys(focused && !alt ? input.held() : 0);
                w->run_frame();
                auto samples = w->apu.take();
                audio.submit(samples, SDL_GetTicksNS());
                std::string error;
                if (!flusher.frame(w->sram, &error))
                    SDL_ShowSimpleMessageBox(SDL_MESSAGEBOX_ERROR, "Save error", error.c_str(), window.get());
                next_frame += interval;
            }
            const auto pixels = w->framebuffer_rgb(Wsc::ColorMode::Lcd);
            SDL_UpdateTexture(texture.get(), nullptr, pixels.data(), Wsc::kWidth * sizeof(uint32_t));
            int width = 0, height = 0;
            SDL_GetCurrentRenderOutputSize(renderer.get(), &width, &height);
            const int scale = std::max(1, std::min(width / Wsc::kWidth, height / Wsc::kHeight));
            const SDL_FRect dest{float(width - Wsc::kWidth * scale) / 2,
                                 float(height - Wsc::kHeight * scale) / 2, float(Wsc::kWidth * scale),
                                 float(Wsc::kHeight * scale)};
            SDL_SetRenderDrawColor(renderer.get(), 0, 0, 0, 255);
            SDL_RenderClear(renderer.get());
            SDL_RenderTexture(renderer.get(), texture.get(), nullptr, &dest);
            SDL_RenderPresent(renderer.get());
        }
        std::string error;
        if (!flusher.flush(w->sram, &error))
            throw std::runtime_error(error);
        return 0;
    } catch (const std::exception &error) {
        SDL_ShowSimpleMessageBox(SDL_MESSAGEBOX_ERROR, "SawGod Decomp", error.what(), nullptr);
        throw;
    }
}
} // namespace
int main(int argc, char **argv) {
    try {
        Args args;
        for (int i = 1; i < argc; ++i) {
            const std::string flag = argv[i];
            auto value = [&]() -> std::string {
                if (++i >= argc)
                    throw std::runtime_error("Missing value for " + flag);
                return argv[i];
            };
            if (flag == "--frames")
                args.frames = integer(value());
            else if (flag == "--scale") {
                args.scale = integer(value());
                if (args.scale < 1 || args.scale > 32)
                    throw std::runtime_error("Scale must be 1 through 32.");
            } else if (flag == "--fullscreen")
                args.fullscreen = true;
            else if (flag == "--inputs")
                args.inputs = from_utf8(value());
            else if (flag == "--sram")
                args.sram = from_utf8(value());
            else if (flag == "--savesram")
                args.savesram = from_utf8(value());
            else if (flag == "--goldendir")
                args.goldendir = from_utf8(value());
            else if (flag == "--goldenevery")
                args.goldenevery = integer(value());
            else if (flag == "--shot")
                args.shot = from_utf8(value());
            else if (flag == "--help") {
                std::puts("sawgod-decomp [ROM.wsc] [--scale N] [--fullscreen]\n"
                          "Replay: ROM.wsc --frames N [--inputs FILE] [--sram FILE] [--savesram FILE]\n"
                          "        [--goldendir DIR --goldenevery N] [--shot FILE]");
                return 0;
            } else if (!flag.starts_with('-') && args.rom.empty())
                args.rom = from_utf8(flag);
            else
                throw std::runtime_error("Unknown argument: " + flag);
        }
        if (args.frames >= 0) {
            if (args.rom.empty())
                throw std::runtime_error("Replay mode requires a ROM path.");
            auto rom = read(args.rom);
            if (sawgod::classify_rom(rom) != sawgod::RomVerdict::Ok)
                throw std::runtime_error("Unsupported ROM.");
            auto w = sawgod::game::make_decomp_game(std::move(rom));
            return replay(args, *w);
        }
        if (!args.inputs.empty() || !args.sram.empty() || !args.savesram.empty() || !args.goldendir.empty() ||
            args.goldenevery || !args.shot.empty())
            throw std::runtime_error("Replay options require --frames.");
        return desktop(args);
    } catch (const std::exception &error) {
        std::fprintf(stderr, "sawgod-decomp: %s\n", error.what());
        return 1;
    }
}
