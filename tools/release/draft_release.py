"""Validate the downloaded artifacts, then create/update a draft only."""
import hashlib
import json
import os
from pathlib import Path
import subprocess
import sys

from workflow_version import release_flavor

PACKAGES = ('sawgod-windows-x86_64.zip', 'sawgod-linux-x86_64.tar.gz',
            'sawgod-macos-arm64.zip', 'sawgod-android-arm64-v8a-release.apk')
VANILLA_PACKAGES = ('sawgod-vanilla-windows-x86_64.zip', 'sawgod-vanilla-linux-x86_64.tar.gz',
                    'sawgod-vanilla-macos-arm64.zip')
VANILLA_NOTES = Path(__file__).with_name('vanilla_release_notes.md')


def main(folder):
    version = os.environ['RELEASE_VERSION']
    vanilla = release_flavor(version) == 'vanilla'
    packages = VANILLA_PACKAGES if vanilla else PACKAGES
    title = 'sawgod-vanilla' if vanilla else f'SawGod {version}'
    checksums = []
    for name in packages:
        package = folder / name
        line = f'{hashlib.sha256(package.read_bytes()).hexdigest()}  {name}\n'
        if (folder / (name + '.sha256')).read_text(encoding='utf-8') != line:
            raise SystemExit(f'Checksum mismatch: {name}')
        checksums.append(line)
    (folder / 'SHA256SUMS.txt').write_text(''.join(checksums), encoding='utf-8', newline='')
    body = folder / 'release-body.md'
    if vanilla:
        body.write_bytes(VANILLA_NOTES.read_bytes())
    else:
        signing = (folder / 'android-signing.txt').read_text(encoding='utf-8').strip()
        if signing not in ('Android APK: release-key signed.',
                           'Android APK: TESTING BUILD, signed with the public debug key; not a production signature.'):
            raise SystemExit('Missing or unrecognized Android signing status')
        body.write_text('SawGod ' + version + '\n\n'
                        'Fan port of Makai Toushi SaGa for WonderSwan Color. Supply your own legally obtained ROM. '
                        'No ROM or extracted game data is included.\n\n'
                        + signing + '\n\nmacOS: Apple Silicon, ad-hoc signed and not notarized. '
                        'See each package README for first run, controls and storage. '
                        'Android README/notices/source are inside APK assets/.\n\n'
                        'Verify downloads with SHA256SUMS.txt. Each package includes third-party notices, '
                        'license texts, FluidSynth/GCEM source and rebuild instructions.\n\n'
                        'DRAFT: finish the device checks before publishing.\n',
                        encoding='utf-8', newline='')
    repo = os.environ['GITHUB_REPOSITORY']
    # gh release view may fail for transport/auth errors too; distinguish absence with the list API.
    releases = json.loads(subprocess.check_output(
        ['gh', 'api', '--paginate', '--slurp', f'repos/{repo}/releases?per_page=100'], text=True))
    existing = next((r for page in releases for r in page if r['tag_name'] == version), None)
    if existing and not existing['draft']:
        raise SystemExit('Refusing to overwrite a published release')
    base = ['gh', 'release']
    if existing:
        subprocess.run(base + ['edit', version, '--repo', repo, '--draft=true',
                               '--title', title, '--notes-file', str(body)], check=True)
        # Remove leftovers from an earlier draft so retries preserve the exact asset inventory.
        for asset in existing.get('assets', []):
            if asset['name'] not in (*packages, 'SHA256SUMS.txt'):
                subprocess.run(base + ['delete-asset', version, asset['name'], '--repo', repo, '--yes'], check=True)
    else:
        subprocess.run(base + ['create', version, '--repo', repo, '--verify-tag', '--draft',
                               '--title', title, '--notes-file', str(body)], check=True)
    assets = [str(folder / name) for name in packages]
    assets.append(str(folder / 'SHA256SUMS.txt'))
    subprocess.run(base + ['upload', version, '--repo', repo, '--clobber', *assets], check=True)


if __name__ == '__main__':
    main(Path(sys.argv[1]))
