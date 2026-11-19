"""Stage only authored assets, executable, soundfont and audited runtime libraries."""
import hashlib
import json
from pathlib import Path
import re
import shutil
import stat
import subprocess
import sys
import tarfile
import zipfile

ROOT = Path(__file__).resolve().parents[2]
sys.path.insert(0, str(ROOT / 'tools'))
from paths import SAWGOD_REF  # noqa: E402
import notices  # noqa: E402


def run(*args):
    return subprocess.check_output(args, text=True)


def stage(target, build):
    dist = ROOT / 'dist'
    name = f'sawgod-{target}-x86_64'
    out = dist / name
    # Staging is recreated; smoke tests always unpack into a separate directory.
    if out.exists():
        shutil.rmtree(out)
    out.mkdir(parents=True)
    exe = 'sawgod.exe' if target == 'windows' else 'sawgod'
    shutil.copy2(build / exe, out / exe)
    strip = 'x86_64-w64-mingw32-strip' if target == 'windows' else 'strip'
    subprocess.run([strip, str(out / exe)], check=True)
    shutil.copytree(ROOT / 'assets/gb_maps', out / 'gb_maps')
    (out / 'text').mkdir()
    shutil.copy2(ROOT / 'assets/text/en.csv', out / 'text/en.csv')
    (out / 'soundfonts').mkdir()
    sf = SAWGOD_REF / 'soundfonts/GeneralUser-GS.sf2'
    if hashlib.sha256(sf.read_bytes()).hexdigest() != '9575028c7a1f589f5770fccc8cff2734566af40cd26ed836944e9a5152688cfe':
        raise RuntimeError('Expected the unmodified GeneralUser GS 2.0.3 soundfont')
    shutil.copy2(sf, out / 'soundfonts/GeneralUser-GS.sf2')
    notices.stage(target, out)
    compiler = 'x86_64-w64-mingw32-g++-posix' if target == 'windows' else 'g++'
    toolchain = run(compiler, '--version')
    packages = ['mingw-w64-common', 'gcc-mingw-w64-x86-64-posix'] if target == 'windows' else ['libstdc++6']
    toolchain += run('dpkg-query', '-W', *packages)
    (out / 'toolchain.txt').write_text(toolchain, encoding='utf-8', newline='')
    source = out / 'source'
    source.mkdir()
    for filename in ('fluidsynth-2.6.1.tar.gz', 'gcem.zip'):
        shutil.copy2(ROOT / 'ref/fluidsynth' / filename, source / filename)
    shutil.copy2(ROOT / 'tools/release/build_fluidsynth.sh', source / 'build_fluidsynth.sh')
    (source / 'README.txt').write_text(
        'Complete source for the bundled, unmodified FluidSynth 2.6.1 and its GCEM headers.\n'
        'On Linux with CMake, Ninja, GCC/G++, Python 3 and (for Windows) MinGW POSIX:\n'
        f'  bash build_fluidsynth.sh {target} "$PWD" "$PWD/build"\n'
        'Replace libfluidsynth-3.dll beside sawgod.exe, or lib/libfluidsynth.so.3.\n'
        'You may modify/replace the library and reverse engineer SawGod to debug those\n'
        'modifications, as provided by LGPL-2.1 section 6. No relinking is needed.\n',
        encoding='utf-8', newline='')
    if target == 'windows':
        fluid = build / 'fluidsynth/src/libfluidsynth-3.dll'
        shutil.copy2(fluid, out / fluid.name)
        subprocess.run([strip, str(out / fluid.name)], check=True)
        # Fail closed: this build intentionally has no non-system DLL dependencies.
        system = {'kernel32.dll', 'msvcrt.dll', 'user32.dll', 'gdi32.dll', 'winmm.dll',
                  'ole32.dll', 'oleaut32.dll', 'advapi32.dll', 'shell32.dll', 'setupapi.dll',
                  'imm32.dll', 'version.dll', 'uuid.dll', 'ws2_32.dll', 'hid.dll',
                  'comdlg32.dll', 'comctl32.dll', 'dinput8.dll', 'dxgi.dll', 'd3d11.dll',
                  'cfgmgr32.dll', 'shlwapi.dll', 'bcrypt.dll', 'dwmapi.dll', 'ntdll.dll'}
        imports = {}
        for binary in sorted(out.glob('*.dll')) + [out / exe]:
            deps = re.findall(r'DLL Name: (\S+)', run('x86_64-w64-mingw32-objdump', '-p', str(binary)))
            imports[binary.name] = deps
            missing = [d for d in deps if d.lower() not in system and not (out / d).exists()]
            if missing:
                raise RuntimeError(f'{binary.name}: unbundled DLLs: {missing}')
    else:
        lib = out / 'lib'
        lib.mkdir()
        shutil.copy2(build / 'fluidsynth/src/libfluidsynth.so.3.6.1', lib / 'libfluidsynth.so.3')
        subprocess.run([strip, str(lib / 'libfluidsynth.so.3')], check=True)
        # SDL and compiler runtimes are static. Only the target OS's glibc/libm/loader remain.
        imports = {}
        for binary in (out / exe, lib / 'libfluidsynth.so.3'):
            deps = run('ldd', str(binary))
            needed = re.findall(r'^\s*(\S+)\s+=>', deps, re.M)
            imports[str(binary.relative_to(out))] = sorted(needed)
            unexpected = set(needed) - {'libc.so.6', 'libm.so.6', 'libpthread.so.0', 'libdl.so.2', 'librt.so.1'}
            if unexpected or 'not found' in deps:
                raise RuntimeError(f'{binary}: unexpected dependencies: {deps}')
        dynamic = run('readelf', '-d', str(out / exe))
        if '$ORIGIN/lib' not in dynamic:
            raise RuntimeError('Executable needs $ORIGIN/lib in its runtime search path')
    notices.audit(target, out)
    # Manifest is useful when checking an unpacked release, and contains no local paths or ROM data.
    manifest = {'platform': target, 'revision': run('git', '-C', str(ROOT), 'rev-parse', 'HEAD').strip(),
                'modified_checkout': bool(run('git', '-C', str(ROOT), 'status', '--porcelain').strip()),
                'runtime_imports': imports,
                'files': {str(p.relative_to(out)): hashlib.sha256(p.read_bytes()).hexdigest()
                          for p in sorted(out.rglob('*')) if p.is_file()}}
    (out / 'manifest.json').write_text(json.dumps(manifest, indent=2) + '\n', encoding='utf-8', newline='')
    if target == 'windows':
        archive = dist / f'{name}.zip'
        with zipfile.ZipFile(archive, 'w', zipfile.ZIP_DEFLATED, compresslevel=9) as z:
            for path in sorted(out.rglob('*')):
                if path.is_file():
                    info = zipfile.ZipInfo.from_file(path, str(path.relative_to(dist)))
                    info.external_attr = (stat.S_IFREG | (0o755 if path.suffix == '.sh' else 0o644)) << 16
                    info.compress_type = zipfile.ZIP_DEFLATED
                    z.writestr(info, path.read_bytes(), compresslevel=9)
    else:
        archive = dist / f'{name}.tar.gz'
        def portable_metadata(info):
            info.mode = 0o755 if info.isdir() or Path(info.name).name in ('sawgod', 'build_fluidsynth.sh') else 0o644
            info.uid = info.gid = 0
            info.uname = info.gname = 'root'
            return info
        with tarfile.open(archive, 'w:gz', compresslevel=9) as tar:
            tar.add(out, arcname=name, filter=portable_metadata)
    checksum = hashlib.sha256(archive.read_bytes()).hexdigest()
    archive.with_name(archive.name + '.sha256').write_text(
        f'{checksum}  {archive.name}\n', encoding='utf-8', newline='')
    print(f'{archive}: {archive.stat().st_size:,} bytes; SHA-256 {checksum}')


if __name__ == '__main__':
    stage(sys.argv[1], Path(sys.argv[2]).resolve())
