// Where a launch's saves come from (app/main.cpp; docs/SAVES.md): the save store of the native game, or the
// raw SRAM image of --native none and --sram.
#pragma once

#include <optional>
#include <string>

#include <SDL3/SDL.h>

#include "app/launch_args.h"
#include "emu/wsc.h"
#include "engine/persist.h"
#include "game/save_file.h"

namespace sawgod::app {

// <data folder>/<rom name>.sav (empty: no data folder).
std::string raw_save_path(const std::string &rom);

// Copies the starting SRAM image into the machine. --sram overrides where it comes from; the normal per-user
// save is still where a played session gets written back to, unless --no-save. A recording made from a
// non-fresh save needs that same save file passed with --sram to replay identically.
void load_raw_save(emu::Wsc &wsc, const LaunchArgs &args, const std::string &save_path);

// Attaches the save store and reads it: <data folder>/saves/, or --shot-store. store_dir: where
// the session writes its saves (empty: nowhere, with --no-save, --inputs or --shot-store). Returns 0, or the
// exit code after reporting what is wrong.
int open_save_store(const LaunchArgs &args, game::SaveSlots &slots, std::string &store_dir);

// Writes the session's saves as the player makes them, and what is pending on exit.
//
// --native none: the raw SRAM .sav is written as the player saves (SaveFlusher: once the game's save area
// has settled), and again on exit; each write replaces the file atomically. Nothing is written with
// --no-save or while replaying --inputs (a replay must not overwrite the player's save).
//
// The save store: read at start; a slot's file is rewritten (atomically) as soon as the player saves to
// it, and anything pending again on exit. Nothing is read with --no-save (a fresh start) and nothing
// written with it or while replaying --inputs. The Save Slots option reaches
// the game every frame; the game applies it when a save/load list opens.
class SaveWriter {
  public:
    // window: the parent of the "saving failed" message box.
    explicit SaveWriter(SDL_Window *window) : window_(window) {}

    // Returns 0, or the exit code after reporting what is wrong with the save store.
    int open(emu::Wsc *wsc, const LaunchArgs &args, bool use_store, const std::string &save_path);
    // After each game frame. slot_count: the Save Slots option.
    void frame(emu::Wsc &wsc, int slot_count);
    // On exit: writes whatever is still pending.
    void finish(emu::Wsc *wsc);

  private:
    void failed(const std::string &error);
    void write_store();

    SDL_Window *window_;
    std::optional<SaveFlusher> saver_;
    bool warned_ = false;
    game::SaveSlots *slots_ = nullptr;
    std::string store_dir_;
    int store_retry_ = 0; // frames until a failed write is tried again
};

} // namespace sawgod::app
