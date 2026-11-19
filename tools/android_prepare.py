#!/usr/bin/env python3
"""Stage offline Android inputs locally, and only authored/licensed APK assets. Never write ref/."""
import hashlib
from pathlib import Path
import shutil
import sys
import tarfile
import zipfile

from paths import ROOT, SAWGOD_REF
sys.path.insert(0, str(ROOT / 'tools/release'))
import notices


def stage_english(out):
    # The English the desktop build last wrote (or the source release's authored copy).
    if not (ROOT / 'assets/text/en.csv').is_file():
        raise RuntimeError('Source release needs authored assets/text/en.csv')
    shutil.copy2(ROOT / 'assets/text/en.csv', out / 'text/en.csv')


def prepare():
    deps = ROOT / 'build/deps'
    deps.mkdir(parents=True, exist_ok=True)
    for name in ('sdl3', 'miniz', 'imgui', 'lzma7z'):
        dest = deps / f'{name}-src'
        source = SAWGOD_REF / 'deps' / dest.name
        if not dest.exists():
            if not source.is_dir():
                raise RuntimeError(f'Missing {source}: provide the pinned CMake dependency sources')
            shutil.copytree(source, dest, ignore=shutil.ignore_patterns('.git'))
    archives = (
        ('fluidsynth-2.6.1.tar.gz', '3d258a3bf97cc20c59eeebfe62c2432fae88adda74d3ad098681c76e0ebf446b'),
        ('gcem.zip', '28159274c54e9640354852e172d10d88eb159f4e7f2fea42edbcd20105ed3526'),
    )
    for name, digest in archives:
        dest = deps / name
        source = SAWGOD_REF / 'fluidsynth' / name
        if not source.exists() and name == 'gcem.zip':
            source = SAWGOD_REF / 'fluidsynth/build' / name
        if not dest.exists():
            shutil.copy2(source, dest)
        if hashlib.sha256(dest.read_bytes()).hexdigest() != digest:
            raise RuntimeError(f'Wrong source archive: {dest}')
    if not (deps / 'fluidsynth-2.6.1').exists():
        with tarfile.open(deps / 'fluidsynth-2.6.1.tar.gz') as archive:
            archive.extractall(deps, filter='data')
    if not (deps / 'gcem-012ae73c6d0a2cb09ffe86475f5c6fba3926e200').exists():
        with zipfile.ZipFile(deps / 'gcem.zip') as archive:
            archive.extractall(deps)
    out = ROOT / 'android/app/build/generated/sawgodAssets'
    if out.exists():
        shutil.rmtree(out)
    out.mkdir(parents=True)
    shutil.copytree(ROOT / 'assets/gb_maps', out / 'gb_maps')
    (out / 'text').mkdir()
    stage_english(out)
    sf = SAWGOD_REF / 'soundfonts/GeneralUser-GS.sf2'
    if hashlib.sha256(sf.read_bytes()).hexdigest() != '9575028c7a1f589f5770fccc8cff2734566af40cd26ed836944e9a5152688cfe':
        raise RuntimeError('Expected the unmodified GeneralUser GS 2.0.3 soundfont')
    (out / 'soundfonts').mkdir()
    shutil.copy2(sf, out / 'soundfonts/GeneralUser-GS.sf2')
    notices.stage('android', out)
    (out / 'source').mkdir()
    for name, _ in archives:
        shutil.copy2(deps / name, out / 'source' / ('fluidsynth-2.6.1.tgz' if name.endswith('.tar.gz') else name))
    shutil.copy2(ROOT / 'tools/release/build_android_fluidsynth.sh', out / 'source/build_android_fluidsynth.sh')
    shutil.copy2(ROOT / 'cmake/AndroidFluidSynth.cmake', out / 'source/AndroidFluidSynth.cmake')
    shutil.copy2(ROOT / 'docs/ANDROID.md', out / 'source/ANDROID.md')
    notices.audit('android', out)
    # Java installs these files into internal storage for std::filesystem and FluidSynth. A content hash
    # makes upgrades refresh assets even when a development APK reuses the same versionCode.
    rows = []
    for folder in ('gb_maps', 'text', 'soundfonts'):
        for file in sorted((out / folder).rglob('*')):
            if file.is_file():
                rows.append(f'{hashlib.sha256(file.read_bytes()).hexdigest()}  {file.relative_to(out).as_posix()}\n')
    (out / 'bundled-files.txt').write_text(''.join(rows), encoding='utf-8', newline='')
    print(f'Android dependencies: {deps}; assets: {out}')


if __name__ == '__main__':
    if len(sys.argv) > 1 and sys.argv[1] == '--list':
        print('\n'.join(notices.staging_list('android')))
    else:
        prepare()
