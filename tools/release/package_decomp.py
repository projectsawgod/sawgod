"""Stage the original-cartridge desktop executable and only its applicable notices."""
import hashlib
import json
from pathlib import Path
import shutil
import subprocess
import sys
import tarfile
import zipfile

from package import ROOT, run
import notices
import repo_files as repo


def stage(target, build):
    if target not in ('linux', 'windows', 'macos'):
        raise ValueError('Desktop platforms only')
    name = f'sawgod-decomp-{target}-' + ('arm64' if target == 'macos' else 'x86_64')
    out = ROOT / 'dist' / name
    if out.exists():
        shutil.rmtree(out)
    out.mkdir(parents=True)
    if target == 'macos':
        app = out / 'sawgod-decomp.app'
        shutil.copytree(build / app.name, app, symlinks=True)
        binary = app / 'Contents/MacOS/sawgod-decomp'
        deps = run('otool', '-L', str(binary)).splitlines()[1:]
        if any(not line.strip().startswith(('/usr/lib/', '/System/Library/')) for line in deps):
            raise RuntimeError('Unexpected non-system dependency in decomp app')
        subprocess.run(['codesign', '--force', '--sign', '-', str(app)], check=True)
        subprocess.run(['codesign', '--verify', '--deep', '--strict', str(app)], check=True)
    else:
        binary = out / ('sawgod-decomp.exe' if target == 'windows' else 'sawgod-decomp')
        shutil.copy2(build / binary.name, binary)
        strip = 'x86_64-w64-mingw32-strip' if target == 'windows' else 'strip'
        subprocess.run([strip, str(binary)], check=True)
        if target == 'linux':
            deps = run('ldd', str(binary))
            allowed = {'libc.so.6', 'libm.so.6', 'libpthread.so.0', 'libdl.so.2', 'librt.so.1'}
            for line in deps.splitlines():
                if '=>' in line and line.split()[0] not in allowed:
                    raise RuntimeError('Unexpected decomp dependency: ' + line)
            if 'not found' in deps:
                raise RuntimeError(deps)
        else:
            deps = run('x86_64-w64-mingw32-objdump', '-p', str(binary))
            # Statically linked compiler/SDL runtimes; only OS DLLs may remain.
            import re
            system = {'kernel32.dll', 'msvcrt.dll', 'user32.dll', 'gdi32.dll', 'winmm.dll',
                      'ole32.dll', 'oleaut32.dll', 'advapi32.dll', 'shell32.dll', 'setupapi.dll',
                      'imm32.dll', 'version.dll', 'uuid.dll', 'ws2_32.dll', 'hid.dll',
                      'comdlg32.dll', 'comctl32.dll', 'dinput8.dll', 'dxgi.dll', 'd3d11.dll',
                      'cfgmgr32.dll', 'shlwapi.dll', 'bcrypt.dll', 'dwmapi.dll', 'ntdll.dll'}
            deps = re.findall(r'DLL Name: (\S+)', deps)
            for dep in deps:
                if dep.lower() not in system:
                    raise RuntimeError('Unexpected decomp DLL: ' + dep)
    notices.stage_decomp(target, out)
    if target != 'macos':
        compiler = 'x86_64-w64-mingw32-g++-posix' if target == 'windows' else 'g++'
        (out / 'toolchain.txt').write_text(run(compiler, '--version'), encoding='utf-8', newline='')
    notices.audit_decomp(target, out)
    manifest = {'platform': target, 'flavor': 'decomp', 'revision': repo.revisions(ROOT)['public'],
                'modified_checkout': repo.dirty(ROOT), 'runtime_imports': deps,
                'files': {str(p.relative_to(out)): hashlib.sha256(p.read_bytes()).hexdigest()
                          for p in sorted(out.rglob('*')) if p.is_file()}}
    (out / 'manifest.json').write_text(json.dumps(manifest, indent=2) + '\n', encoding='utf-8', newline='')
    if target == 'linux':
        archive = out.with_suffix('.tar.gz')
        def metadata(info):
            info.mode = 0o755 if info.isdir() or Path(info.name).name == 'sawgod-decomp' else 0o644
            info.uid = info.gid = 0
            info.uname = info.gname = 'root'
            return info
        with tarfile.open(archive, 'w:gz') as tar:
            tar.add(out, arcname=name, filter=metadata)
    else:
        archive = out.with_suffix('.zip')
        if target == 'macos':
            subprocess.run(['ditto', '-c', '-k', '--sequesterRsrc', '--keepParent', str(out), str(archive)], check=True)
        else:
            with zipfile.ZipFile(archive, 'w', zipfile.ZIP_DEFLATED) as zip:
                for path in sorted(out.rglob('*')):
                    if path.is_file():
                        zip.write(path, str(path.relative_to(out.parent)))
    checksum = hashlib.sha256(archive.read_bytes()).hexdigest()
    archive.with_name(archive.name + '.sha256').write_text(
        f'{checksum}  {archive.name}\n', encoding='utf-8', newline='')
    print(f'{archive}: {archive.stat().st_size:,} bytes; SHA-256 {checksum}')


if __name__ == '__main__':
    stage(sys.argv[1], Path(sys.argv[2]).resolve())
