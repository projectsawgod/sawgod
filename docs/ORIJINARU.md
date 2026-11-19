# ORIJINARU resources

The separate ORIJINARU game engine remains out of scope; the port plays the WSC game. The port imports its
embedded GB Rev 1 art, maps and audio from the player's WSC ROM (banks 0x32–0x33). No external GB ROM is
needed for these resources. The native app hides the original title menu's ORIJINARU option.

The GB picture and our GB CPU/APU music driver run alongside the WSC game, using the extracted resources.
See [configuration](CONFIG.md) for details.

Historical survey and resume notes: git history.
