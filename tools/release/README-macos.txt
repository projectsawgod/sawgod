SawGod for Apple Silicon (arm64), macOS 11.0 or later

SawGod is a fan port of Makai Toushi SaGa for WonderSwan Color. You need your
own legally obtained Japanese WonderSwan Color ROM. No game data is included.
SawGod is not affiliated with or endorsed by the original game's rights holders.

Unzip and drag SawGod.app to Applications, then open it. The app is ad-hoc
signed, without Developer ID signing or notarization. If blocked, right-click
SawGod.app > Open. On newer macOS, launch once, then use System Settings >
Privacy & Security > Open Anyway. Only approve a trusted download.

On first run choose Browse or drop a .wsc, .zip or .7z onto the window. The
ROM is checked, copied and extracted locally; later launches reuse the copy.
Saves (saves/), config (sawgod.toml), mods and imported data live in
~/Library/Application Support/sawgod/sawgod/ (or SAWGOD_HOME). Keep that folder
when upgrading. The app bundle does not need to be writable. License texts
and source/ below are inside SawGod.app/Contents/Resources/; in Finder use
Show Package Contents. No Homebrew installation is needed to play.

Default keyboard: arrows move; X confirms/interacts; Z cancels/runs; Enter is
Start. W/D/S/A are the secondary directional pad. Esc or F1 opens Options;
G/H steps Swap down/up; hold Tab for fast-forward. M toggles the minimap;
C toggles encounters. Bindings can be changed in Options.

Default gamepad (buttons by position): D-pad/left stick moves; bottom face
button confirms; right face button cancels/runs; Start or the top face
button is Start; Select (Back/View) opens Options. Right stick is the secondary directional pad. L1/R1
steps Swap down/up; hold R2 for fast-forward. Click right stick for minimap;
click left stick for encounters. Face-button labels vary by controller.

Graphics and music can each use GB, WSC or Enhanced in Options. GB uses the
Game Boy presentation; WSC uses the color game's presentation; Enhanced adds
host-rendered effects and soundfont music. Swap (G/H or L1/R1) steps the chosen
modes together; each session starts at Enhanced. Enhanced graphics needs a
supported GPU/driver; WSC and GB remain available through the fallback renderer.

SawGod code is public domain (CC0 1.0, LICENSE); other components keep their own terms.
See THIRD_PARTY_NOTICES.txt and licenses/ for credits, font attribution and
license texts. Exact FluidSynth/GCEM source and rebuild instructions are in
source/; FluidSynth is a replaceable shared library under LGPL-2.1-or-later.

Report problems: https://github.com/projectsawgod/sawgod/issues
Include OS/device, version, graphics/music modes and steps to reproduce,
and attach the newest debug log: logs/ in the data folder above (one small
text file per session, problems only). Never attach your ROM, extracted
game data or credentials.
