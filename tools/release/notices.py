"""Single package notice inventory; stage and audit it before archiving/signing."""
from pathlib import Path
import hashlib
import shutil
import sys
import zipfile

ROOT = Path(__file__).resolve().parents[2]
COMMON = [
    ('SDL', '3.4.16', 'zlib', ['SDL3.txt'], 'Window, input, audio and GPU support.'),
    ('SDL HIDAPI', 'SDL 3.4.16 snapshot', 'original HIDAPI permissive license', ['SDL-HIDAPI.txt'], ''),
    ('SDL fdlibm', 'SDL 3.4.16 snapshot', 'Sun permissive license', ['SDL-libm.txt'], ''),
    ('SDL yuv2rgb', 'SDL 3.4.16 snapshot', 'BSD-3-Clause', ['SDL-yuv2rgb.txt'], ''),
    ('miniz', '3.0.2', 'MIT', ['miniz.txt'], ''),
    ('7-Zip C decoder', '26.01', 'public domain', ['7-Zip-C.txt'], 'Decoder subset only; no unRAR.'),
    ('Dear ImGui', '1.92.9b', 'MIT', ['ImGui.txt'], ''),
    ('ProggyClean / ProggyForever fonts', 'ImGui 1.92.9b embedded snapshots', 'MIT', ['Proggy-fonts.txt'],
     'By Tristan Grimmer / Disco Hello; compressed font data embedded by ImGui.'),
    ('stb rect_pack / textedit / truetype in ImGui', '1.01 / 1.14 / 1.26', 'MIT (chosen dual-license option)', ['stb.txt'], ''),
    ('FluidSynth', '2.6.1', 'LGPL-2.1-or-later', ['FluidSynth-LGPL-2.1.txt', 'FluidSynth-NOTICE.txt'],
     'Replaceable shared library. Exact unmodified source and rebuild instructions: source/. '
     'You may modify/replace it and reverse engineer SawGod to debug those modifications (LGPL section 6).'),
    ('GCEM', '012ae73c6d0a2cb09ffe86475f5c6fba3926e200', 'Apache-2.0', ['GCEM-Apache-2.0.txt', 'GCEM-NOTICE.txt'],
     'FluidSynth compile-time math; exact source in source/gcem.zip.'),
    ('GeneralUser GS', '2.0.3', 'GeneralUser GS License 2.0', ['GeneralUser-GS.txt'],
     'By S. Christian Collins; bundled soundfont is unmodified.'),
    ('m5x7', '001.000', 'CC0-1.0', ['m5x7-CC0.md'],
     'By Daniel Linssen; https://managore.itch.io/m5x7 ; rasterized glyphs.'),
    ('Silver', '1.0', 'CC-BY-4.0', ['Silver-CC-BY-4.0.txt'],
     'By Poppy Works / Wolfgang Wozniak; https://poppyworks.itch.io/silver ; '
     'https://creativecommons.org/licenses/by/4.0/ . Glyphs rasterized into bitmap tables '
     'and arranged in the ROM screen and prompts. No endorsement is implied.'),
    ('Cinzel Decorative Bold', '1.002', 'SIL-OFL-1.1', ['CinzelDecorative-OFL.txt'],
     'By Natanael Gama. Only rendered world-card lettering ships (synthetic italic and small caps), '
     'not the font. OFL text retained as a courtesy; rendered artwork does not inherit OFL.'),
    ('Noto Serif CJK JP Bold, Noto Sans CJK JP Bold', '2.002 / 2.004', 'SIL-OFL-1.1', ['NotoSerifCJK-OFL.txt'],
     'By Adobe and Google. Only the rendered kanji line and TM mark of the Enhanced title logo ship, not the fonts. '
     'OFL text retained as a courtesy; rendered artwork does not inherit OFL.'),
    ('SPIRV-Cross MSL support output', 'vulkan-sdk-1.4.328.1', 'Apache-2.0 (conservative notice for generated helpers)',
     ['SPIRV-Cross.txt'], 'Compiler is build-only; generated Metal shader source is embedded. '
     'Authored SPIR-V/DXIL shader outputs carry SawGod\'s license; shader compilers are not bundled.'),
]
PLATFORM = {
    'linux': [('GCC libstdc++ / libgcc', 'build toolchain (see toolchain.txt)', 'GPL-3.0 with Runtime Library Exception 3.1',
               ['GCC-runtime.txt', 'GPL-3.txt'], 'Statically linked. OS glibc, drivers and desktop libraries are not bundled.')],
    'windows': [
        ('GCC libstdc++ / libgcc', 'build toolchain (see toolchain.txt)', 'GPL-3.0 with Runtime Library Exception 3.1',
         ['GCC-runtime.txt', 'GPL-3.txt'], 'Statically linked.'),
        ('MinGW-w64 CRT / winpthreads', 'build toolchain (see toolchain.txt)', 'permissive licenses and upstream exceptions',
         ['MinGW-w64.txt', 'GPL-2.txt'], 'Statically linked; Windows system DLLs are not bundled.')],
    'macos': [],  # Apple system libc++/frameworks are imported, not redistributed.
    'android': [
        ('SDL Java activity / HIDAPI sources', '3.4.16', 'zlib and original HIDAPI permissive license',
         ['SDL3.txt', 'SDL-HIDAPI.txt'], 'Compiled from the pinned SDL Android template.'),
        ('Android NDK libc++_shared / libc++abi / compiler-rt', 'r29 (29.0.14206865)',
         'Apache-2.0 with LLVM Exceptions and retained upstream notices', ['Android-NDK-toolchain-NOTICE.txt', 'Android-NDK-NOTICE.txt'],
         'libc++_shared.so and compiled compiler support. Android Bionic/system libraries are not bundled.')],
}
SOURCE = ['source/fluidsynth-2.6.1.tar.gz', 'source/gcem.zip']
# Android's asset packaging un-gzips *.gz assets, so the APK carries the same exact archive bytes as .tgz.
ANDROID_FLUIDSYNTH = 'source/fluidsynth-2.6.1.tgz'
FLUIDSYNTH_SHA256 = '3d258a3bf97cc20c59eeebfe62c2432fae88adda74d3ad098681c76e0ebf446b'


def sources(target):
    return [ANDROID_FLUIDSYNTH if target == 'android' and p.endswith('.tar.gz') else p for p in SOURCE]


def notice_text(target):
    lines = [f'SawGod: third-party notices ({target})', '',
             'SawGod code: public domain (CC0 1.0); see LICENSE. No ROM or extracted game data is included.',
             'Paths below are relative to this notice file. Full license terms control.', '']
    for component, version, license_name, files, detail in COMMON + PLATFORM[target]:
        lines += [f'{component} | {version} | {license_name}',
                  'License text: ' + ', '.join('licenses/' + f for f in files)]
        if detail:
            lines.append(detail)
        lines.append('')
    return '\n'.join(lines) + '\n'


def required(target):
    files = {'LICENSE', 'README.txt', 'THIRD_PARTY_NOTICES.txt', *sources(target)}
    for _, _, _, texts, _ in COMMON + PLATFORM[target]:
        files.update('licenses/' + name for name in texts)
    if target == 'android':
        files.update(['source/build_android_fluidsynth.sh', 'source/AndroidFluidSynth.cmake', 'source/ANDROID.md'])
    else:
        files.update(['source/build_fluidsynth.sh', 'source/README.txt'])
    if target in ('linux', 'windows'):
        files.add('toolchain.txt')
    return sorted(files)


def audit_payload(names):
    forbidden = {'.wsc', '.ws', '.gb', '.gbc', '.sgz', '.sav', '.srm'}
    bad = [p for p in names if Path(p).suffix.lower() in forbidden or Path(p).name == 'en_fill.csv']
    if bad:
        raise RuntimeError(f'Forbidden release payload: {bad}')


def audit(target, folder):
    audit_payload(str(p.relative_to(folder)) for p in folder.rglob('*') if p.is_file())
    missing = [p for p in required(target) if not (folder / p).is_file() or not (folder / p).stat().st_size]
    if missing:
        raise RuntimeError(f'{target}: missing package notices/source: {missing}')
    if (folder / 'THIRD_PARTY_NOTICES.txt').read_text(encoding='utf-8') != notice_text(target):
        raise RuntimeError(f'{target}: stale third-party notice inventory')
    print(f'{target}: notice audit passed ({len(required(target))} required files)')


def stage(target, folder):
    folder.mkdir(parents=True, exist_ok=True)
    shutil.copytree(ROOT / 'licenses', folder / 'licenses', dirs_exist_ok=True)
    shutil.copy2(ROOT / 'LICENSE', folder / 'LICENSE')
    shutil.copy2(ROOT / f'tools/release/README-{target}.txt', folder / 'README.txt')
    (folder / 'THIRD_PARTY_NOTICES.txt').write_text(notice_text(target), encoding='utf-8', newline='')


def staging_list(target):
    resource = 'SawGod.app/Contents/Resources/' if target == 'macos' else 'assets/' if target == 'android' else ''
    files = set(required(target))
    files.update('licenses/' + p.name for p in (ROOT / 'licenses').iterdir() if p.is_file())
    files.update('gb_maps/' + str(p.relative_to(ROOT / 'assets/gb_maps'))
                 for p in (ROOT / 'assets/gb_maps').rglob('*') if p.is_file())
    files.update(['text/en.csv', 'soundfonts/GeneralUser-GS.sf2'])
    if target == 'android':
        files.add('bundled-files.txt')
        binaries = [f'lib/arm64-v8a/{name}' for name in
                    ('libsawgod.so', 'libSDL3.so', 'libfluidsynth.so', 'libc++_shared.so')]
        binaries += ['AndroidManifest.xml', 'classes.dex', 'resources.arsc']
    elif target == 'macos':
        binaries = ['README.txt', 'manifest.json', 'SawGod.app/Contents/MacOS/SawGod',
                    'SawGod.app/Contents/Frameworks/libfluidsynth.3.dylib', 'SawGod.app/Contents/Info.plist']
        files.add('SawGod.icns')
    else:
        binaries = ['manifest.json', 'sawgod.exe' if target == 'windows' else 'sawgod',
                    'libfluidsynth-3.dll' if target == 'windows' else 'lib/libfluidsynth.so.3']
    return sorted([resource + p for p in files] + binaries)


def audit_apk(apk):
    with zipfile.ZipFile(apk) as archive:
        audit_payload(archive.namelist())
        for path in required('android'):
            if not archive.read('assets/' + path):
                raise RuntimeError(f'Empty APK notice: {path}')
        if hashlib.sha256(archive.read('assets/' + ANDROID_FLUIDSYNTH)).hexdigest() != FLUIDSYNTH_SHA256:
            raise RuntimeError('APK FluidSynth source is not the exact upstream archive')
        if archive.read('assets/THIRD_PARTY_NOTICES.txt').decode('utf-8') != notice_text('android'):
            raise RuntimeError('Stale APK notices')
        expected = {'lib/arm64-v8a/' + name for name in
                    ('libsawgod.so', 'libSDL3.so', 'libfluidsynth.so', 'libc++_shared.so')}
        libraries = {p for p in archive.namelist() if p.startswith('lib/') and p.endswith('.so')}
        if libraries != expected:
            raise RuntimeError(f'APK native libraries changed; re-audit licenses: {libraries}')
    print('android: final APK notice and native-library audit passed')


if __name__ == '__main__':
    if sys.argv[1] == '--apk':
        audit_apk(Path(sys.argv[2]))
    elif sys.argv[1] == '--list':
        print('\n'.join(required(sys.argv[2])))
    else:
        audit(sys.argv[1], Path(sys.argv[2]))
