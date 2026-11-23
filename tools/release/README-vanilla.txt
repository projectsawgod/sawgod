SawGod Vanilla — the original WonderSwan Color game

A small SDL3 front end that runs the original Japanese game from SawGod's
static recompilation (partially decompiled for readability), verified routine by
routine against the original by differential tests and fuzzing. Original
Japanese text, menus, sound and cartridge behavior. No game data is included.
Supply your own supported, unmodified Japanese WSC ROM.

Start
  Run sawgod-vanilla (sawgod-vanilla.exe on Windows; sawgod-vanilla.app on macOS).
  Give it a ROM path on the command line, place a .wsc beside the executable
  (beside the .app on macOS), or select the ROM in the open-file dialog.
  Only this project's supported WSC dump is accepted.
  On macOS an ad-hoc signed app may need right-click > Open or an exception
  in System Settings > Privacy & Security.

Saves
  The cartridge's three in-game save slots live in <rom name>.sav beside
  the ROM: game.wsc uses game.sav. This is raw cartridge SRAM, compatible
  with emulator SRAM saves of the same size. Save changes are written
  after a short debounce and again on quit. The ROM folder must be writable.
  There is no autosave and no settings file.

Controls
  WSC X pad: arrow keys; gamepad D-pad or left stick.
  WSC Y pad: W/D/S/A (up/right/down/left); gamepad right stick.
  A: X; gamepad south face button.
  B: Z; gamepad east face button.
  Start: Enter; gamepad Start or north face button.
  Esc: quit immediately. F11 or Alt+Enter: toggle fullscreen.
  The window resizes at integer scales with black letterboxing and nearest
  filtering. --scale N chooses the initial scale; --fullscreen starts fullscreen.

Optional input mapping
  Edit the shipped sawgod-vanilla-input.txt beside the executable (beside the
  .app on macOS). Uncomment a binding to change it; omitted buttons retain
  their defaults. Each nonblank, noncomment line is:
    key a Space
    button b west
    axis x1 lefty-
  A line replaces the keyboard or gamepad bindings for one WSC button;
  "none" clears them. An optional second device/control pair adds a second
  binding of the same input type (keyboard or gamepad), for example:
    button x1 dpup axis lefty-
  Lines starting with # are comments. WSC button names are x1..x4,
  y1..y4, a, b, start. Keys use SDL scancode names without spaces (Up, Return,
  X, Space). Gamepad buttons accept position names: south, east, west, north,
  start, back, dpup, dpright, dpdown, dpleft (also SDL a/b/x/y). Axes: leftx, lefty, rightx,
  righty, lefttrigger, righttrigger, with a trailing + or - for direction.
  Stick deadzone is 40%. Esc, F11 and Alt+Enter are reserved.

Headless recording replay
  sawgod-vanilla game.wsc --frames 15000 --inputs recording.inputs --savesram out.sav
  Input lines are "FRAME buttons", held until the next change; buttons are
  up,right,down,left,up2,right2,down2,left2,a,b,start (comma-separated).
  Changes apply after the numbered frame completes, as in sawgod-headless.
  --sram FILE supplies initial raw SRAM. Headless mode never reads or writes
  the ROM's neighboring .sav automatically. --shot FILE writes the final PNG.

License and reports
  Our code is public domain (CC0); see LICENSE and THIRD_PARTY_NOTICES.txt.
  Report problems: https://github.com/projectsawgod/sawgod/issues
