"""Validate the four downloaded artifacts, then create/update a draft only."""
import hashlib
import json
import os
from pathlib import Path
import subprocess
import sys

PACKAGES = ('sawgod-windows-x86_64.zip', 'sawgod-linux-x86_64.tar.gz',
            'sawgod-macos-arm64.zip', 'sawgod-android-arm64-v8a-release.apk')


def main(folder):
    checksums = []
    for name in PACKAGES:
        package = folder / name
        line = f'{hashlib.sha256(package.read_bytes()).hexdigest()}  {name}\n'
        if (folder / (name + '.sha256')).read_text(encoding='utf-8') != line:
            raise SystemExit(f'Checksum mismatch: {name}')
        checksums.append(line)
    (folder / 'SHA256SUMS.txt').write_text(''.join(checksums), encoding='utf-8', newline='')
    signing = (folder / 'android-signing.txt').read_text(encoding='utf-8').strip()
    if signing not in ('Android APK: release-key signed.',
                       'Android APK: TESTING BUILD, signed with the public debug key; not a production signature.'):
        raise SystemExit('Missing or unrecognized Android signing status')
    body = folder / 'release-body.md'
    body.write_text('SawGod ' + os.environ['RELEASE_VERSION'] + '\n\n'
                    'Fan port of Makai Toushi SaGa for WonderSwan Color. Supply your own legally obtained ROM. '
                    'No ROM or extracted game data is included.\n\n'
                    + signing + '\n\nmacOS: Apple Silicon, ad-hoc signed and not notarized. '
                    'See each package README for first run, controls and storage. '
                    'Android README/notices/source are inside APK assets/.\n\n'
                    'Verify downloads with SHA256SUMS.txt. Each package includes third-party notices, '
                    'license texts and exact FluidSynth/GCEM source with rebuild instructions.\n\n'
                    'DRAFT: finish the device checks before publishing.\n',
                    encoding='utf-8', newline='')
    version = os.environ['RELEASE_VERSION']
    repo = os.environ['GITHUB_REPOSITORY']
    # gh release view may fail for transport/auth errors too; distinguish absence with the list API.
    releases = json.loads(subprocess.check_output(
        ['gh', 'api', '--paginate', '--slurp', f'repos/{repo}/releases?per_page=100'], text=True))
    existing = next((r for page in releases for r in page if r['tag_name'] == version), None)
    if existing and not existing['draft']:
        raise SystemExit('Refusing to overwrite a published release')
    base = ['gh', 'release']
    if existing:
        subprocess.run(base + ['edit', version, '--repo', repo, '--draft=true', '--notes-file', str(body)], check=True)
    else:
        subprocess.run(base + ['create', version, '--repo', repo, '--verify-tag', '--draft',
                               '--title', f'SawGod {version}', '--notes-file', str(body)], check=True)
    assets = [str(folder / name) for name in PACKAGES]
    assets += [str(folder / (name + '.sha256')) for name in PACKAGES]
    assets.append(str(folder / 'SHA256SUMS.txt'))
    subprocess.run(base + ['upload', version, '--repo', repo, '--clobber', *assets], check=True)


if __name__ == '__main__':
    main(Path(sys.argv[1]))
