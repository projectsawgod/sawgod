SawGod for Android arm64, Android 8.0 or later (gamepad required)

SawGod is a fan port of Makai Toushi SaGa for WonderSwan Color. You need your
own legally obtained Japanese WonderSwan Color ROM. No game data is included.
SawGod is not affiliated with or endorsed by the original game's rights holders.

Open the APK on your device. When prompted, allow installing from this source,
then install and open SawGod. Only allow trusted downloads; you can turn that
permission off afterward. A debug-key package is a testing build; changing
signing keys requires uninstalling, which deletes internal data.

Connect a gamepad; there are no gameplay touch controls. On first run press
the bottom face button to choose your legally obtained ROM in the system file
picker (.wsc, .zip or .7z). SawGod verifies, copies and extracts it locally.
Cancel or retry if needed; later launches reuse the imported copy.

Saves (saves/), config (sawgod.toml), mods and imported data are in the app's
private internal files directory (/data/user/0/org.sawgod.app/files/). They
survive updates signed with the same key. Uninstalling or clearing app data
deletes them; automatic cloud backup is disabled. Notices and source/ are in
APK assets/ (an APK can be opened as a zip).

Default gamepad (buttons by position): D-pad/left stick moves; bottom face
button confirms; right face button cancels/runs; Start or the top face
button is Start; Select (Back/View) opens Options. Right stick is the secondary directional pad. L1/R1
steps Swap down/up; hold R2 for fast-forward. Click right stick for minimap;
click left stick for encounters. Face-button labels vary by controller.

Graphics and music can each use GB, WSC or Enhanced in Options. GB uses the
Game Boy presentation; WSC uses the color game's presentation; Enhanced adds
host-rendered effects and soundfont music. Swap (L1/R1) steps the chosen
modes together; each session starts at Enhanced. Enhanced graphics needs a
supported GPU/driver; WSC and GB remain available through the fallback renderer.

SawGod code is public domain (CC0 1.0, LICENSE); other components keep their own terms.
See THIRD_PARTY_NOTICES.txt and licenses/ for credits, font attribution and
license texts. Exact FluidSynth/GCEM source and rebuild instructions are in
source/; FluidSynth is a replaceable shared library under LGPL-2.1-or-later.

Report problems: https://github.com/projectsawgod/sawgod/issues
Include OS/device, version, graphics/music modes and steps to reproduce.
Never attach your ROM, extracted game data or credentials.
