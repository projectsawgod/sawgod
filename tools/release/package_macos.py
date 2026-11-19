"""Stage, audit and ad-hoc sign the arm64 app; run only on macOS."""
import hashlib
import json
from pathlib import Path
import plistlib
import re
import shutil
import subprocess
import sys

from package import ROOT, SAWGOD_REF
import notices


def run(*args):
    return subprocess.check_output([str(a) for a in args], text=True).strip()


def system_library(name):
    return name.startswith(('/usr/lib/', '/System/Library/'))


def imports(binary):
    return [line.strip().split(' (compatibility version')[0]
            for line in run('otool', '-L', binary).splitlines()[1:]]


def rpaths(binary):
    return re.findall(r'cmd LC_RPATH\s+cmdsize \d+\s+path (.*?) \(offset', run('otool', '-l', binary))


def bundle_libraries(app, fluid):
    frameworks = app / 'Contents/Frameworks'
    frameworks.mkdir(exist_ok=True)
    executable = app / 'Contents/MacOS/SawGod'
    # Keep original locations while resolving recursive @loader_path/@rpath references.
    pending = [(executable, executable), (fluid, frameworks / 'libfluidsynth.3.dylib')]
    copied = {}
    while pending:
        source, dest = pending.pop(0)
        source = source.resolve()
        if dest in copied:
            if copied[dest] != source:
                raise RuntimeError(f'Conflicting dylibs named {dest.name}')
            continue
        copied[dest] = source
        deps, search = imports(source), rpaths(source)
        if source != dest.resolve():
            shutil.copy2(source, dest)
        dest.chmod(dest.stat().st_mode | 0o200)
        if dest.suffix == '.dylib':
            run('install_name_tool', '-id', '@rpath/' + dest.name, dest)
        for index, dep in enumerate(deps):
            if dest.suffix == '.dylib' and index == 0:
                continue  # otool lists the dylib's own install ID first.
            if system_library(dep):
                continue
            candidates = []
            def expand(path):
                return Path(path.replace('@loader_path', str(source.parent))
                            .replace('@executable_path', str(executable.parent)))
            if dep.startswith('@rpath/'):
                candidates = [expand(p) / dep[len('@rpath/'):] for p in search]
            else:
                candidates = [expand(dep)]
            found = next((p.resolve() for p in candidates if p.is_file()), None)
            if found is None:
                raise RuntimeError(f'{source}: unresolved dependency {dep}')
            # otool -L includes a dylib's own install ID.
            if found == source and dest.suffix == '.dylib':
                continue
            bundled = frameworks / found.name
            run('install_name_tool', '-change', dep, '@rpath/' + bundled.name, dest)
            pending.append((found, bundled))
        # Eliminate build-machine/Homebrew search paths; all runtime imports are now local.
        for path in rpaths(dest):
            run('install_name_tool', '-delete_rpath', path, dest)
        run('install_name_tool', '-add_rpath', '@executable_path/../Frameworks', dest)
    audit = {}
    for binary in [executable, *sorted(frameworks.glob('*.dylib'))]:
        if run('lipo', '-archs', binary) != 'arm64':
            raise RuntimeError(f'Expected arm64-only binary: {binary}')
        deps = imports(binary)
        for dep in deps:
            if not system_library(dep) and not (dep.startswith('@rpath/') and
                                                (frameworks / dep[len('@rpath/'):]).is_file()):
                raise RuntimeError(f'{binary}: unbundled dependency {dep}')
        audit[str(binary.relative_to(app))] = deps
    return audit


def stage(build):
    dist = ROOT / 'dist'
    name = 'sawgod-macos-arm64'
    out = dist / name
    if out.exists():
        shutil.rmtree(out)
    out.mkdir(parents=True)
    app = out / 'SawGod.app'
    shutil.copytree(build / 'SawGod.app', app, symlinks=True)
    with (app / 'Contents/Info.plist').open('rb') as stream:
        info = plistlib.load(stream)
    resources = app / 'Contents/Resources'
    (resources / 'soundfonts').mkdir(exist_ok=True)
    sf = SAWGOD_REF / 'soundfonts/GeneralUser-GS.sf2'
    if hashlib.sha256(sf.read_bytes()).hexdigest() != '9575028c7a1f589f5770fccc8cff2734566af40cd26ed836944e9a5152688cfe':
        raise RuntimeError('Expected the unmodified GeneralUser GS 2.0.3 soundfont')
    shutil.copy2(sf, resources / 'soundfonts/GeneralUser-GS.sf2')
    # Development-only comparison text must never ship.
    (resources / 'text/en_fill.csv').unlink(missing_ok=True)
    (resources / 'LICENSES.md').unlink(missing_ok=True)
    notices.stage('macos', resources)
    shutil.copy2(resources / 'README.txt', out / 'README.txt')
    source = resources / 'source'
    source.mkdir()
    for filename in ('fluidsynth-2.6.1.tar.gz', 'gcem.zip'):
        shutil.copy2(ROOT / 'ref/fluidsynth' / filename, source / filename)
    shutil.copy2(ROOT / 'tools/release/build_fluidsynth.sh', source / 'build_fluidsynth.sh')
    (source / 'README.txt').write_text(
        'Complete unmodified FluidSynth 2.6.1 (LGPL-2.1-or-later) and GCEM (Apache-2.0).\n'
        'With Xcode command-line tools, CMake, Ninja and Python 3 installed:\n'
        '  bash build_fluidsynth.sh macos "$PWD" "$PWD/build"\n'
        'Replace Contents/Frameworks/libfluidsynth.3.dylib with the rebuilt dylib;\n'
        'set its ID to @rpath/libfluidsynth.3.dylib with install_name_tool.\n'
        'Ad-hoc sign the dylib and app again after replacing it.\n'
        'You may modify/replace this library and reverse engineer SawGod to debug\n'
        'those modifications under LGPL-2.1 section 6; no relinking is needed.\n',
        encoding='utf-8', newline='')
    fluid = next((build / 'fluidsynth/src').glob('libfluidsynth.3*.dylib'))
    audit = bundle_libraries(app, fluid)
    bundled = {p.name for p in (app / 'Contents/Frameworks').glob('*.dylib')}
    if bundled != {'libfluidsynth.3.dylib'}:
        raise RuntimeError(f'Bundled libraries changed; re-audit licenses: {bundled}')
    notices.audit('macos', resources)
    for dylib in sorted((app / 'Contents/Frameworks').glob('*.dylib')):
        run('codesign', '--force', '--sign', '-', dylib)
    run('codesign', '--force', '--sign', '-', app)
    run('codesign', '--verify', '--deep', '--strict', app)
    manifest = {'platform': 'macos', 'architecture': 'arm64',
                'minimum_macos': info['LSMinimumSystemVersion'],
                'version': info['CFBundleShortVersionString'],
                'revision': run('git', '-C', ROOT, 'rev-parse', 'HEAD'),
                'modified_checkout': bool(run('git', '-C', ROOT, 'status', '--porcelain')),
                'runtime_imports': audit,
                'files': {str(p.relative_to(out)): hashlib.sha256(p.read_bytes()).hexdigest()
                          for p in sorted(out.rglob('*')) if p.is_file()}}
    (out / 'manifest.json').write_text(json.dumps(manifest, indent=2) + '\n', encoding='utf-8', newline='')
    archive = dist / f'{name}.zip'
    # ditto preserves executable permissions, symlinks and the signed bundle layout.
    run('ditto', '-c', '-k', '--sequesterRsrc', '--keepParent', out, archive)
    checksum = hashlib.sha256(archive.read_bytes()).hexdigest()
    archive.with_name(archive.name + '.sha256').write_text(
        f'{checksum}  {archive.name}\n', encoding='utf-8', newline='')
    print(f'{archive}: {archive.stat().st_size:,} bytes; SHA-256 {checksum}')


if __name__ == '__main__':
    if sys.argv[1] == '--list':
        print('\n'.join(notices.staging_list('macos')))
    else:
        stage(Path(sys.argv[1]).resolve())
