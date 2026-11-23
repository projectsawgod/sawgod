"""Validate release intent before allocating build runners. Manual retries use an existing tag."""
import os
import re
import subprocess


def release_flavor(version):
    if version == 'vanilla':
        return 'vanilla'
    if re.fullmatch(r'v[0-9]+\.[0-9]+\.[0-9]+(?:-[0-9A-Za-z.-]+)?', version):
        return 'sawgod'
    raise SystemExit('Use vanilla or a version such as v1.1.0 or v1.1.0-rc.1')


def main():
    tag = os.environ['GITHUB_REF_NAME'] if os.environ['GITHUB_REF_TYPE'] == 'tag' else ''
    version = os.environ.get('REQUESTED_VERSION', '') or tag
    flavor = release_flavor(version) if version else 'sawgod'
    if version:
        if os.environ.get('REQUESTED_PLATFORMS', 'all') not in ('', 'all'):
            raise SystemExit('A draft release requires platforms=all')
        if tag and tag != version:
            raise SystemExit('Version must match the selected tag')
        subprocess.run(['git', 'fetch', '--depth=1', 'origin', f'refs/tags/{version}:refs/tags/{version}'], check=True)
        commit = subprocess.check_output(['git', 'rev-parse', version + '^{commit}'], text=True).strip()
        if commit != os.environ['GITHUB_SHA']:
            raise SystemExit('Run against the existing version tag (--ref); tag and checkout must match')
    with open(os.environ['GITHUB_OUTPUT'], 'a', encoding='utf-8', newline='') as stream:
        build_version = version if flavor == 'sawgod' else ''
        stream.write(f'version={version}\nrelease={str(bool(version)).lower()}\n'
                     f'flavor={flavor}\nbuild_version={build_version}\n')


if __name__ == '__main__':
    main()
