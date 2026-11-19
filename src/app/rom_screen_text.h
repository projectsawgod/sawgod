// The ROM screen's text (first-run ROM extraction): shown before any ROM
// is loaded, so it can't use the game's own font or text resources. Every line is drawn in Silver (Poppy
// Works, CC BY 4.0), the whole font baked into src/engine/font_silver.inc by tools/font_bake_silver.py, so
// these strings can change freely. English and Japanese are shown together (each message is a pair). The
// Japanese is a draft awaiting the translator.
#pragma once

namespace sawgod::app::rom_text {

struct Pair {
    const char *en;
    const char *ja;
};

#ifdef __ANDROID__
inline constexpr Pair kChooseOrDrop{"Choose ROM File (A)", "ファイルを選ぶ"};
#else
inline constexpr Pair kChooseOrDrop{"Choose File or Drag and Drop ROM",
                                    "ファイルを選ぶか ROMをドラッグ＆ドロップ"};
#endif
inline constexpr Pair kWanted{"Expected ROM: Makai Toushi SaGa (WonderSwan Color)",
                              "必要なROM：魔界塔士サ・ガ（ワンダースワンカラー版）"};

// Validation results (rom_check.h).
inline constexpr Pair kGameBoy{"This is the Game Boy ROM. Use the WonderSwan Color ROM.",
                               "ゲームボーイ版のROMです。ワンダースワンカラー版のROMを使ってください。"};
// a modified dump or any other ROM (one message, with the SHA-1 it should have)
inline constexpr Pair kInvalid{"Invalid ROM.", "無効なROMです。"};
inline constexpr Pair kNoRomInArchive{"No ROM found inside this archive.",
                                      "このアーカイブにROMがありません。"};
inline constexpr Pair kUnreadable{"This file can't be read.", "このファイルを読み込めません。"};
inline constexpr Pair kCopyFailed{"The ROM couldn't be saved. Check the folder's permissions.",
                                  "ROMを保存できませんでした。フォルダの権限を確認してください。"};
inline constexpr Pair kExpectedSha1{"Expected SHA-1", "正しいSHA-1"}; // over the hash: "en / ja"

// Auto-detected ROM beside the game.
inline constexpr Pair kFoundRom{"Found a ROM next to the game. Use it?",
                                "ゲームの横にROMがあります。使いますか？"};
inline constexpr Pair kYes{"Yes", "はい"};
inline constexpr Pair kNo{"No", "いいえ"};

// First-run extraction.
inline constexpr Pair kSettingUp{"Setting up the game...", "ゲームを準備しています…"};
inline constexpr Pair kSetupFailed{"Setup failed:", "準備に失敗しました："};

} // namespace sawgod::app::rom_text
