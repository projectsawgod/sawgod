SawGod for Linux x86_64

SawGod is a fan port of Makai Toushi SaGa for WonderSwan Color. You need your
own legally obtained Japanese WonderSwan Color ROM. No game data is included.
SawGod is not affiliated with or endorsed by the original game's rights holders.

Extract the entire tar.gz to a writable folder and run ./sawgod. Packages
built on Ubuntu 24.04 require glibc 2.39 or newer and working desktop/audio/GPU
drivers. On first run choose Browse or drop a .wsc, .zip or .7z onto the window.
The ROM is checked, copied into rom/, then extracted locally. Later launches
reuse that copy. You may also run ./sawgod --rom /absolute/path/to/game.wsc.

Saves (saves/), config (sawgod.toml), mods and imported data live beside the
executable if writable; otherwise in $XDG_DATA_HOME/sawgod/sawgod/ (default
~/.local/share/sawgod/sawgod/). Keep that folder when upgrading. SAWGOD_HOME
can override it. No installer or administrator privileges are required.

Default keyboard: arrows move; X confirms/interacts; Z cancels/runs; Enter is
Start. W/D/S/A are the secondary directional pad. Esc or F1 opens Options;
G/H steps Swap down/up; hold Tab for fast-forward. M toggles the minimap;
C toggles encounters. Bindings can be changed in Options.

Default gamepad (buttons by position): D-pad/left stick moves; bottom face
button confirms; right face button cancels/runs; Start is Start; top face
button opens Options. Right stick is the secondary directional pad. L1/R1
steps Swap down/up; hold R2 for fast-forward. Click right stick for minimap;
click left stick for encounters. Face-button labels vary by controller.

Graphics and music can each use GB, WSC or Enhanced in Options. GB uses the
Game Boy presentation; WSC uses the color game's presentation; Enhanced adds
host-rendered effects and soundfont music. Swap (G/H or L1/R1) steps the chosen
modes together; each session starts at Enhanced. Enhanced graphics needs a
supported GPU/driver; WSC and GB remain available through the fallback renderer.

On laptops with two GPUs, SawGod uses the system's default GPU (the one that
drives the display), like other Linux apps; that keeps motion smooth. To use
the other GPU, launch it the usual way: prime-run ./sawgod (NVIDIA) or
DRI_PRIME=1 ./sawgod.

SawGod code is public domain (CC0 1.0, LICENSE); other components keep their own terms.
See THIRD_PARTY_NOTICES.txt and licenses/ for credits, font attribution and
license texts. Exact FluidSynth/GCEM source and rebuild instructions are in
source/; FluidSynth is a replaceable shared library under LGPL-2.1-or-later.

Report problems: https://github.com/projectsawgod/sawgod/issues
Include OS/device, version, graphics/music modes and steps to reproduce,
and attach the newest debug log: logs/ in the data folder above (one small
text file per session, problems only). Never attach your ROM, extracted
game data or credentials.
