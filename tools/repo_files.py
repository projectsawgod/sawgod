"""Repository views shared by source scans and release tools.

Public-only checkouts need no policy file. In a combined checkout the policy
partitions the index; with .git-dev each index is authoritative, even for paths
on the wrong side of the policy.
"""
import fnmatch
import os
from pathlib import Path
import subprocess

ROOT = Path(__file__).resolve().parents[1]
POLICY = 'tools/release/public_excludes.txt'


def git(root, *args, overlay=False, index_env=False, **kwargs):
    # Hooks export index/repository variables. Do not let them select another
    # index when a caller explicitly asks for the other repository.
    env = {k: v for k, v in os.environ.items() if not k.startswith('GIT_')}
    # commit -a/--only use a temporary index. Staged checks must inspect it.
    if index_env and 'GIT_INDEX_FILE' in os.environ:
        env['GIT_INDEX_FILE'] = os.environ['GIT_INDEX_FILE']
    command = ['git', '-C', str(root)]
    if overlay:
        command += ['--git-dir=' + str(Path(root).resolve() / '.git-dev'),
                    '--work-tree=' + str(Path(root).resolve())]
    return subprocess.run(command + list(args), env=env, check=True,
                          stdout=subprocess.PIPE, stderr=subprocess.PIPE, **kwargs).stdout


def split(root=ROOT):
    return (Path(root) / '.git-dev').exists()


def exclusions(policy=None):
    path = Path(policy) if policy is not None else ROOT / POLICY
    if not path.exists():
        return []
    return [s.strip() for s in path.read_text(encoding='utf-8').splitlines()
            if s.strip() and not s.lstrip().startswith('#')]


def excluded(name, patterns):
    return any(name.startswith(p) if p.endswith('/') else fnmatch.fnmatchcase(name, p)
               for p in patterns)


def names(data):
    return {os.fsdecode(n) for n in data.split(b'\0') if n}


def tracked(root=ROOT, overlay=False):
    return names(git(root, 'ls-files', '--cached', '-z', overlay=overlay))


def public(root=ROOT, patterns=None):
    paths = tracked(root)
    if split(root):
        return paths
    policy = exclusions(Path(root) / POLICY) if patterns is None else patterns
    return {p for p in paths if not excluded(p, policy)}


def overlay(root=ROOT, patterns=None):
    if split(root):
        return tracked(root, overlay=True)
    policy = exclusions(Path(root) / POLICY) if patterns is None else patterns
    return {p for p in tracked(root) if excluded(p, policy)}


def both(root=ROOT):
    return tracked(root) | (tracked(root, overlay=True) if split(root) else set())


def untracked(root=ROOT):
    # info/exclude may hide overlay paths, so callers also union the overlay index.
    return names(git(root, 'ls-files', '--others', '--exclude-standard', '-z'))


def staged(root=ROOT, overlay=False, deletions=False):
    return names(git(root, 'diff', '--cached', '--name-only',
                     '--diff-filter=' + ('ACDMRT' if deletions else 'ACMR'), '-z', overlay=overlay, index_env=True))


def dirty(root=ROOT):
    return bool(untracked(root) - both(root)) or any(
        git(root, 'status', '--porcelain', '--untracked-files=no', overlay=side).strip()
        for side in ([False, True] if split(root) else [False]))


def revisions(root=ROOT):
    return {side: git(root, 'rev-parse', 'HEAD', overlay=side == 'overlay').decode().strip()
            for side in (['public', 'overlay'] if split(root) else ['public'])}
