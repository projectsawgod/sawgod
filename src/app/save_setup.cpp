#include "app/save_setup.h"

#include <algorithm>
#include <cstdint>
#include <cstdio>
#include <vector>

#include "app/startup.h"
#include "engine/data_dir.h"
#include "engine/debug_log.h"
#include "game/save_slots.h"
#include "game/session.h"

namespace sawgod::app {

std::string raw_save_path(const std::string &rom) {
    std::string dir = sawgod::data_dir();
    if (dir.empty())
        return {};
    std::string name = rom.substr(rom.find_last_of("/\\") + 1);
    name = name.substr(0, name.find_last_of('.'));
    return dir + name + ".sav";
}

void load_raw_save(emu::Wsc &wsc, const LaunchArgs &args, const std::string &save_path) {
    // --sram overrides where the starting save comes from; the normal per-user save is still where a
    // played session gets written back to, unless --no-save. A recording made from a non-fresh save
    // needs that same save file passed with --sram to replay identically.
    const std::string load_from = !args.sram.empty() ? args.sram : args.no_save ? std::string() : save_path;
    std::vector<uint8_t> sav = load_from.empty() ? std::vector<uint8_t>() : sawgod::app::read_file(load_from);
    std::copy_n(sav.begin(), std::min(sav.size(), wsc.sram.size()), wsc.sram.begin());
    std::fprintf(stderr, "sawgod: starting from save %s%s\n",
                 load_from.empty() ? "(none)" : load_from.c_str(), sav.empty() ? " (not found, fresh)" : "");
}

int open_save_store(const LaunchArgs &args, game::SaveSlots &slots, std::string &store_dir) {
    slots.attach_store();
    const std::string dir = sawgod::data_dir();
    if (!args.no_save && !dir.empty()) {
        store_dir = dir + "saves"; // one store, whatever the ROM file is called
        std::vector<std::string> warnings;
        if (!sawgod::game::read_save_store(store_dir, slots, &warnings))
            std::fprintf(stderr, "sawgod: new save store %s\n", store_dir.c_str());
        for (const std::string &w : warnings)
            sawgod::debug_log::warnf("save", "%s", w.c_str());
        std::fprintf(stderr, "sawgod: saves in %s\n", store_dir.c_str());
    }
    if (!args.inputs.empty())
        store_dir.clear(); // a replay never writes
    if (!args.shot_store.empty()) {
        std::vector<std::string> warnings;
        if (!sawgod::game::read_save_store(args.shot_store, slots, &warnings)) {
            std::fprintf(stderr, "sawgod: cannot read screenshot store %s\n", args.shot_store.c_str());
            return 1;
        }
        for (const auto &warning : warnings)
            std::fprintf(stderr, "sawgod: %s\n", warning.c_str());
        store_dir.clear(); // read-only even with no --inputs
    }
    return 0;
}

int SaveWriter::open(emu::Wsc *wsc, const LaunchArgs &args, bool use_store, const std::string &save_path) {
    // The save area: the last-used slot byte at 0x3300, then three 0x43B-byte slots from 0x3301 and their
    // backup copies 0xCB1 bytes later (load_save_slot_core).
    constexpr size_t kSaveAreaBegin = 0x3300, kSaveAreaEnd = 0x3301 + 2 * 0xCB1;
    if (wsc && !use_store && args.sram.empty() && !args.no_save && args.inputs.empty() &&
        !save_path.empty() && !wsc->sram.empty()) {
        saver_.emplace(save_path, kSaveAreaBegin, kSaveAreaEnd);
        saver_->reset(wsc->sram);
    }

    slots_ = wsc && game::Session::find(*wsc) ? &game::Session::find(*wsc)->save_slots() : nullptr;
    if (use_store)
        if (const int code = open_save_store(args, *slots_, store_dir_))
            return code;
    if (!store_dir_.empty() && slots_->dirty())
        write_store(); // (the import)
    return 0;
}

void SaveWriter::failed(const std::string &error) {
    sawgod::debug_log::criticalf("save", "saving failed: %s", error.c_str());
    if (warned_)
        return;
    warned_ = true; // once per session; stderr gets every failure
    const std::string text =
        "Your save could not be written:\n" + error +
        "\n\nThe previous save file is unchanged. SawGod will try again the next time you "
        "save and when you quit.";
    SDL_ShowSimpleMessageBox(SDL_MESSAGEBOX_ERROR, "SawGod: saving failed", text.c_str(), window_);
}

void SaveWriter::write_store() {
    std::string error;
    const auto write = [](const std::string &path, const std::string &data, std::string *err) {
        return sawgod::WriteFileAtomic(path, data.data(), data.size(), err);
    };
    if (!game::write_save_store(store_dir_, *slots_, write, &error)) {
        failed(error);
        store_retry_ = 600; // tried again in 8 s and on exit
    }
}

void SaveWriter::frame(emu::Wsc &wsc, int slot_count) {
    if (saver_) {
        std::string error;
        if (!saver_->frame(wsc.sram, &error))
            failed(error);
    }
    if (slots_) {
        slots_->request_count(slot_count);
        if (store_retry_ > 0)
            --store_retry_;
        else if (slots_->dirty() && !store_dir_.empty())
            write_store();
    }
}

void SaveWriter::finish(emu::Wsc *wsc) {
    if (saver_) {
        std::string error;
        if (!saver_->flush(wsc->sram, &error))
            failed(error);
    }
    if (slots_ && !store_dir_.empty() && slots_->dirty())
        write_store();
}

} // namespace sawgod::app
