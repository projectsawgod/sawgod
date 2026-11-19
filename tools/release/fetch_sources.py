"""Prepare pinned redistributable Android build inputs; never fetch ROM material."""
from pathlib import Path
import subprocess

ROOT = Path(__file__).resolve().parents[2]
DEPS = {
    'sdl3': ('libsdl-org/SDL', 'release-3.4.16'),
    'miniz': ('richgel999/miniz', '3.0.2'),
    'imgui': ('ocornut/imgui', 'v1.92.9b'),
    'lzma7z': ('ip7z/7zip', '26.01'),
}


def main():
    folder = ROOT / 'ref/deps'
    folder.mkdir(parents=True, exist_ok=True)
    for name, (repo, tag) in DEPS.items():
        dest = folder / f'{name}-src'
        if not dest.exists():
            subprocess.run(['git', 'clone', '--depth', '1', '--branch', tag,
                            f'https://github.com/{repo}.git', str(dest)], check=True)
        expected = subprocess.check_output(['git', '-C', str(dest), 'rev-parse', tag + '^{commit}'])
        actual = subprocess.check_output(['git', '-C', str(dest), 'rev-parse', 'HEAD'])
        if expected != actual:
            raise RuntimeError(f'Wrong dependency revision: {name}')
    subprocess.run(['bash', str(ROOT / 'tools/release/build_fluidsynth.sh'), 'linux',
                    str(ROOT / 'ref/fluidsynth'), str(ROOT / 'build/fetch-fluid'), '--fetch-only'], check=True)
    sources = ROOT / 'ref/SOURCES.md'
    if not sources.exists():
        sources.write_text('# Redistributable release build inputs\n\n'
                           'Dependency tags and upstream URLs: tools/release/fetch_sources.py.\n'
                           'FluidSynth/GCEM URLs and SHA-256: tools/release/build_fluidsynth.sh.\n'
                           'GeneralUser GS URL and SHA-256: tools/release/fetch_soundfont.py.\n'
                           'License texts: licenses/ and package THIRD_PARTY_NOTICES.txt.\n',
                           encoding='utf-8', newline='')


if __name__ == '__main__':
    main()
