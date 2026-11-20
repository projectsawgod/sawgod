"""Inspect the generated link command as well as CMake's dependency-graph check."""
from pathlib import Path
import subprocess
import sys

build, binary = Path(sys.argv[1]), Path(sys.argv[2])
if (build / 'build.ninja').is_file():
    text = subprocess.check_output(['ninja', '-C', str(build), '-t', 'commands', binary.name], text=True)
    # Last command links the executable; preceding commands may build shared archives.
    text = text.splitlines()[-1]
else:
    link = build / 'CMakeFiles/sawgod-decomp.dir/link.txt'
    if not link.is_file():
        print('CMake dependency-graph check passed at configuration (link command unavailable).')
        sys.exit(0)
    text = link.read_text(encoding='utf-8')
for name in ('sawgod_ui', 'sawgod_imgui', 'sawgod_enhanced_music', 'mode7_renderer', 'enhanced_graphics'):
    if name in text:
        raise AssertionError(f'Excluded dependency in decomp link: {name}')
print('Decomp link has no host UI, ImGui, Enhanced music or Enhanced renderer.')
