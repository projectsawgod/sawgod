#pragma once
// A bounded native animation driver: execute each authored VBlank callout as one completed raster.
// No gameplay, altered animation code, wall-clock waits or video capture.
#include "game/bank36_misc.h"
#include "game/computed_handlers.h"
#include "game/presentation_scene.h"
#include "game/session.h"
#include <stdexcept>

namespace battle_test {
using namespace sawgod;
using Body = void (*)(game::Machine &);
struct Driver {
    emu::Wsc w;
    game::Machine m;
    game::SceneObserver observer;
    std::unique_ptr<game::Fiber> fiber;
    int waits = 0;
    Driver(const std::vector<uint8_t> &rom, Body body, int palette = 0, int item = 0) : w(rom, 0), m(w) {
        w.io.fill(0);
        w.io[0xC0] = w.io[0xC1] = w.io[0xC2] = w.io[0xC3] = 0xFF;
        w.io[0x60] = 0xE0;
        w.io[0x16] = 158;
        w.io[0] = 7;
        w.io[4] = 1; // native DMA sprite destination
        w.io[7] = 0x32;
        w.ram[0xD6D4] = 3;
        w.ram[0xDC00] = 4;
        w.ram[0xDC12] = uint8_t(palette);
        w.ram[0xDBFA] = 1; // innate/direct-item selection
        w.ram[0xDBFC] = w.ram[0xDBFD] = uint8_t(item);
        w.ram[0xDBFE] = 0;
        w.ram[0xDBFF] = 4;
        m.ss() = m.ds() = 0;
        m.cs() = 0x7000;
        m.sp() = 0xFD00; // above VRAM: full-screen effects overwrite tiles through BFFF
        w.capture_layers(true);
        observer.install(w);
        game::hooks::emit(game::hooks::Event::BattleStart, m);
        fiber = std::make_unique<game::Fiber>([this, body] { body(m); });
    }
    bool step() {
        for (;;) {
            if (fiber->resume())
                return false;
            // Raster setup waits for the visible scanline before the next VBlank;
            // it changes no authored frame count in this bounded drawing driver.
            if (m.cs() != 0x4000 || w.cpu.s.ip != 0x51)
                break;
            m.retf();
        }
        if (++waits > 2048)
            throw std::runtime_error("native animation exceeded VBlank bound");
        if (m.cs() != 0x4000 || w.cpu.s.ip != 9)
            throw std::runtime_error("unexpected animation callout");
        m.retf();
        // The caller's registers remain parked while the raster completes.
        const auto parked = w.cpu.s;
        w.cpu.s.halted = true;
        w.cpu.s.flags = 0;
        w.run_frame();
        w.cpu.s = parked;
        return true;
    }
};
} // namespace battle_test
