"""Requested release smoke checks, isolated from development data. Does not run the private verification suite."""
import hashlib
import json
import os
from pathlib import Path
import shutil
import subprocess
import sys
import tarfile
import tempfile
import time
import zipfile

ROOT = Path(__file__).resolve().parents[2]
sys.path.insert(0, str(ROOT / 'tools'))
from paths import SAWGOD_ROM  # noqa: E402


def pe_stack_reserve(exe):
    """SizeOfStackReserve from a PE32+ header: the main thread's stack on Windows."""
    data = exe.read_bytes()[:4096]
    optional = int.from_bytes(data[0x3c:0x40], 'little') + 24
    assert data[optional:optional + 2] == b'\x0b\x02', 'Not a PE32+ executable'
    return int.from_bytes(data[optional + 72:optional + 80], 'little')


def check(target):
    if target not in ('linux', 'windows'):
        raise ValueError('Expected linux or windows')
    # Wine can't build a prefix on an NTFS/exFAT drive (no symlinks): SAWGOD_RELEASE_SCRATCH puts the work on another
    scratch = Path(os.environ.get('SAWGOD_RELEASE_SCRATCH') or ROOT / 'tmp')
    scratch.mkdir(parents=True, exist_ok=True)
    scratch.mkdir(exist_ok=True)
    work = Path(tempfile.mkdtemp(prefix=f'release-{target}-', dir=scratch))
    artifact = ROOT / 'dist' / f'sawgod-{target}-x86_64'
    archive = artifact.with_name(artifact.name + ('.zip' if target == 'windows' else '.tar.gz'))
    if target == 'windows':
        with zipfile.ZipFile(archive) as z:
            z.extractall(work)
    else:
        with tarfile.open(archive) as tar:
            tar.extractall(work, filter='data')
    package = work / artifact.name
    # Spaces in the install path and an unrelated current directory catch common portable-path bugs.
    package = package.rename(work / 'Portable Game')
    cwd = work / 'unrelated'
    cwd.mkdir()
    env = {k: v for k, v in os.environ.items()
           if not k.startswith('SAWGOD_') and k not in ('APPIMAGE', 'APPDIR', 'FLATPAK_ID', 'LD_LIBRARY_PATH')}
    env.update(SDL_VIDEODRIVER='dummy', SDL_AUDIODRIVER='dummy', SDL_RENDER_DRIVER='software',
               XDG_DATA_HOME=str(work / 'os-data'))
    prefix = []
    results = {'archive': str(archive), 'bytes': archive.stat().st_size, 'work': str(work),
               'passed': False, 'steps': {}}

    def run(name, command, timeout=90, idle=False, overrides=None):
        started = time.monotonic()
        log = work / f'{name}.log'
        with log.open('w', encoding='utf-8', newline='') as f:
            try:
                proc = subprocess.run(command, cwd=cwd, env=env | (overrides or {}),
                                      stdout=f, stderr=subprocess.STDOUT, timeout=timeout)
                status = proc.returncode
            except subprocess.TimeoutExpired:
                status = 'timeout'
        elapsed = round(time.monotonic() - started, 3)
        results['steps'][name] = {'seconds': elapsed, 'status': status, 'log': str(log)}
        print(f'{target}: {name}: {status}, {elapsed}s', flush=True)
        if (idle and status != 'timeout') or (not idle and status != 0):
            raise RuntimeError(f'{name} failed: {log.read_text(encoding="utf-8")}')
        return log.read_text(encoding='utf-8', errors='replace')

    def winpath(path):
        return 'Z:' + str(path).replace('/', '\\')

    try:
        if target == 'windows':
            # Debian's generic launcher may invoke a 32-bit loader even for WINEARCH=win64.
            # Use the native 64-bit loader when available; still use an entirely fresh prefix.
            wine = shutil.which('wine64')
            if not wine and Path('/usr/lib/wine/wine64').exists():
                wine = '/usr/lib/wine/wine64'
            wine = wine or 'wine'
            env.update(WINEPREFIX=str(work / 'wine'), WINEARCH='win64', WINEDEBUG='-all',
                       WINEDLLOVERRIDES='mscoree,mshtml=')
            if Path('/usr/lib/wine/wineserver64').exists():
                env.update(WINESERVER='/usr/lib/wine/wineserver64', WINELOADER=wine)
            prefix = [wine]
            run('wine-init', prefix + ['wineboot.exe', '-u'], timeout=120)
        exe = package / ('sawgod.exe' if target == 'windows' else 'sawgod')
        command = prefix + [str(exe)]
        if target == 'windows':
            # Wine runs a too-small main-thread stack without complaint; Windows overflowed on first run.
            reserve = pe_stack_reserve(exe)
            results['stack_reserve'] = reserve
            assert reserve >= 8 << 20, f'sawgod.exe stack reserve is {reserve} bytes; main() needs 8 MiB'
        assert not (package / 'CMakeCache.txt').exists()
        assert not (package / 'rom').exists()
        assert not (package / 'wsc.sgz').exists()
        # With no ROM and no game art, the interactive ROM screen must remain alive.
        run('rom-screen-idle', command, timeout=5, idle=True)
        assert not (package / 'rom').exists()
        rom = winpath(SAWGOD_ROM) if target == 'windows' else str(SAWGOD_ROM)
        run('extract', command + ['--rom', rom, '--frames', '1', '--renderer', 'legacy'])
        stored = list((package / 'rom').glob('*.wsc'))
        assert len(stored) == 1
        assert hashlib.sha1(stored[0].read_bytes()).digest() == hashlib.sha1(SAWGOD_ROM.read_bytes()).digest()
        stamps = {}
        for name in ('wsc.sgz', 'gb.sgz'):
            path = package / name
            with zipfile.ZipFile(path) as z:
                assert z.testzip() is None
                assert len(z.namelist()) > 10
                results[name] = {'bytes': path.stat().st_size, 'entries': len(z.namelist())}
            stamps[name] = path.stat().st_mtime_ns
        config = (package / 'sawgod.toml').read_text(encoding='utf-8')
        assert 'soundfonts/GeneralUser-GS.sf2' in config
        assert str(ROOT) not in config
        shot = work / 'stored-rom.png'
        shotarg = winpath(shot) if target == 'windows' else str(shot)
        trace = {'WINEDEBUG': '+loaddll'} if target == 'windows' else {'LD_DEBUG': 'libs'}
        log = run('stored-rom-frame', command + ['--shot-overlay', shotarg, '--frames', '120',
                                                 '--renderer', 'legacy'], overrides=trace)
        assert shot.read_bytes()[:8] == b'\x89PNG\r\n\x1a\n' and shot.stat().st_size > 1000
        if target == 'linux':
            assert str(package / 'lib/libfluidsynth.so.3') in log
        else:
            assert 'libfluidsynth-3.dll' in log
        assert 'Enhanced music uses WSC' not in log
        for name, mtime in stamps.items():
            assert (package / name).stat().st_mtime_ns == mtime, 'Cached archive was unexpectedly rebuilt'
        assert not list((work / 'os-data').rglob('*.sgz')), 'Portable data went to the OS data folder'
        # Exercise the actual DLL/.so and actual soundfont, even if the screenshot did not reach a song.
        probe = work / ('audio-probe.exe' if target == 'windows' else 'audio-probe')
        compiler = 'x86_64-w64-mingw32-gcc-posix' if target == 'windows' else 'cc'
        flags = ['-static', '-static-libgcc'] if target == 'windows' else ['-ldl', '-lm']
        run('build-audio-probe', [compiler, '-O2', str(ROOT / 'tools/release/audio_probe.c'),
                                  '-o', str(probe)] + flags)
        fluid = package / ('libfluidsynth-3.dll' if target == 'windows' else 'lib/libfluidsynth.so.3')
        sf = package / 'soundfonts/GeneralUser-GS.sf2'
        args = [winpath(fluid), winpath(sf)] if target == 'windows' else [str(fluid), str(sf)]
        run('bundled-audio', prefix + [str(probe)] + args)
        # Move the installation after config has been saved: the default must remain portable.
        moved = package.rename(work / 'Moved Game')
        moved_exe = moved / exe.name
        log = run('relocated', prefix + [str(moved_exe), '--frames', '1', '--renderer', 'legacy'])
        assert 'Enhanced music uses WSC' not in log
        results['passed'] = True
    finally:
        (work / 'results.json').write_text(json.dumps(results, indent=2) + '\n', encoding='utf-8', newline='')
        if target == 'windows':
            subprocess.run([env.get('WINESERVER', 'wineserver'), '-k'], env=env, check=False)
        print(f'Results: {work / "results.json"}', flush=True)
    return results


if __name__ == '__main__':
    check(sys.argv[1])
