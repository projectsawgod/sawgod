#!/usr/bin/env python3
"""Reject personal identifiers in the public source tree and staged release packages.

Default: git ls-files (cached plus new, nonignored files), minus DEV_ONLY. Explicit
paths scan files/directories, including nested ZIP/ODS/APK, tar/gzip and PNG text.
Patterns are assembled from fragments so this guard does not match its own source.
"""
import argparse
import gzip
import io
import os
from pathlib import Path
import struct
import subprocess
import sys
import tarfile
import zipfile
import zlib

DEV_ONLY = ('AGENTS.md', 'CLAUDE.md', 'PLAN.md', 'docs/milestones/')
PERSON = 'and' + 'rew'
SURNAME = 'vest' + 'al'
TOKENS = (PERSON, SURNAME, PERSON + SURNAME + '@' + 'gmail' + '.com')  # first name, surname, e-mail
PATTERNS = tuple((token, token.encode(encoding)) for token in TOKENS
                 for encoding in ('utf-8', 'utf-16le', 'utf-16be'))
PNG = b'\x89PNG\r\n\x1a\n'

# (file or member path, exact UTF-8 context, expected count, reason).
# Only first-name hits within these contexts are exempt; other tokens always fail.
UPSTREAM_ALLOWLIST = (
    ('licenses/MinGW-w64.txt', 'Copyright: 2008 ' + PERSON.title() + ' Fenn\n', 1,
     'Preserve the upstream xinput.h copyright notice verbatim.'),
    ('licenses/MinGW-w64.txt', 'Copyright: 2009 ' + PERSON.title() + ' Nguyen\n', 1,
     'Preserve the upstream t2embapi.h copyright notice verbatim.'),
    ('licenses/MinGW-w64.txt', '           2021 ' + PERSON.title() + ' Eikum for CodeWeavers\n', 1,
     'Preserve the upstream windows.media.devices.idl copyright notice verbatim.'),
    ('fluidsynth-2.6.1/doc/wiki/ChangeLog.md', '@' + PERSON + '-sayers', 3,
     'Preserve all three upstream FluidSynth contributor credits verbatim.'),
)


def upstream_path(label):
    """Resolve known package wrappers; the file/member path itself stays exact."""
    path = label
    for prefix in ('sawgod-linux-x86_64/', 'sawgod-windows-x86_64/',
                   'sawgod-macos-arm64/'):
        if path.startswith(prefix):
            path = path[len(prefix):]
            break
    for prefix in ('SawGod.app/Contents/Resources/', 'assets/'):
        if path.startswith(prefix):
            path = path[len(prefix):]
            break
    return path


def excluded(name):
    return any(name.startswith(p) if p.endswith('/') else name == p for p in DEV_ONLY)


def public_files(root):
    names = subprocess.check_output(
        ['git', '-C', str(root), 'ls-files', '--cached', '--others', '--exclude-standard', '-z'])
    return [root / os.fsdecode(n) for n in sorted(set(names.split(b'\0'))) if n
            and not excluded(os.fsdecode(n)) and (root / os.fsdecode(n)).exists()]


class Scanner:
    def __init__(self):
        self.hits = []
        self.errors = []
        self.files = 0

    def check(self, data, label, upstream=None):
        first_name_data = data
        if upstream is not None:
            for path, context, expected, _reason in UPSTREAM_ALLOWLIST:
                if upstream_path(upstream) != path:
                    continue
                context = context.encode('utf-8')
                count = data.count(context)
                if count != expected:
                    self.errors.append(f'{label}: upstream context count {count}, expected {expected}: '
                                       + context.decode('utf-8').strip())
                first_name_data = first_name_data.replace(context, b'\0' * len(context))
        lowered = data.lower()
        first_name_lowered = first_name_data.lower()
        for token, pattern in PATTERNS:
            searchable = first_name_lowered if token == PERSON else lowered
            offset = searchable.find(pattern)
            if offset >= 0:
                self.hits.append(f'{label}: byte {offset}: {token}')
                break

    def scan_bytes(self, data, label, depth=0, member_path=None):
        if depth > 16:
            raise ValueError('archive nesting exceeds 16')
        # Member paths are separate from display labels: a literal '!' in a filename
        # must never turn an unrelated path into an allowlisted one.
        path = label if member_path is None else member_path
        upstream = any(upstream_path(path) == entry[0] for entry in UPSTREAM_ALLOWLIST)
        if upstream:
            self.check(data, label, upstream=path)
        stream = io.BytesIO(data)
        # A tar containing a ZIP near its end can also satisfy is_zipfile(): the ZIP
        # reader accepts leading/trailing bytes. Prefer the enclosing tar's structure.
        is_tar = (len(data) >= 512 and not data.startswith(b'\x1f\x8b')
                  and tarfile.is_tarfile(io.BytesIO(data)))
        if not is_tar and zipfile.is_zipfile(stream):
            container = bytearray(data)
            with zipfile.ZipFile(stream) as archive:
                self.check(archive.comment, label + '!comment')
                for entry in archive.infolist():
                    member = label + '!' + entry.filename
                    self.check(entry.filename.encode('utf-8') + entry.comment + entry.extra,
                               member + ' [metadata]')
                    # Scan each expanded payload under its own path. Keep raw headers,
                    # metadata and trailing bytes for the container check below.
                    name_size, extra_size = struct.unpack_from('<HH', data, entry.header_offset + 26)
                    start = entry.header_offset + 30 + name_size + extra_size
                    end = start + entry.compress_size
                    container[start:end] = b'\0' * (end - start)
                    self.scan_bytes(archive.read(entry), member, depth + 1, entry.filename)
            self.check(container, label)
        elif data.startswith((b'PK\x03\x04', b'PK\x05\x06', b'PK\x07\x08')):
            raise ValueError('invalid ZIP archive')
        elif data.startswith(b'\x1f\x8b'):
            self.check(data, label)
            self.scan_bytes(gzip.decompress(data), label + '!gzip', depth + 1, path)
        elif is_tar:
            container = bytearray(data)
            stream.seek(0)
            with tarfile.open(fileobj=stream, mode='r:') as archive:
                for entry in archive:
                    member = label + '!' + entry.name
                    self.check(str((entry.name, entry.linkname, entry.uname, entry.gname,
                                    entry.pax_headers)).encode('utf-8'), member + ' [metadata]')
                    if entry.isfile():
                        with archive.extractfile(entry) as content:
                            self.scan_bytes(content.read(), member, depth + 1, entry.name)
                        start, end = entry.offset_data, entry.offset_data + entry.size
                        container[start:end] = b'\0' * (end - start)
            self.check(container, label)
        elif data[257:262] == b'ustar':
            raise ValueError('invalid tar archive')
        elif data.startswith(PNG):
            self.check(data, label)
            offset = len(PNG)
            while offset < len(data):
                size, kind = struct.unpack_from('>I4s', data, offset)
                end = offset + 12 + size
                if end > len(data):
                    raise ValueError('truncated PNG chunk')
                payload = data[offset + 8:offset + 8 + size]
                if kind == b'zTXt':
                    keyword, compressed = payload.split(b'\0', 1)
                    if not compressed or compressed[0] != 0:
                        raise ValueError('unsupported PNG compression')
                    self.check(keyword + zlib.decompress(compressed[1:]), label + '!zTXt')
                elif kind == b'iTXt':
                    keyword, rest = payload.split(b'\0', 1)
                    flag, method = rest[:2]
                    language, translated, text = rest[2:].split(b'\0', 2)
                    if flag:
                        if method != 0:
                            raise ValueError('unsupported PNG compression')
                        text = zlib.decompress(text)
                    self.check(keyword + language + translated + text, label + '!iTXt')
                elif kind == b'tEXt':
                    self.check(payload, label + '!tEXt')
                offset = end
        elif not upstream:
            self.check(data, label)

    def scan_file(self, path, label):
        self.files += 1
        self.check(label.encode('utf-8'), label + ' [path]')
        try:
            if path.is_symlink():
                self.check(os.readlink(path).encode('utf-8'), label + ' [symlink]')
            if path.is_file():
                self.scan_bytes(path.read_bytes(), label)
            else:
                raise ValueError('not a regular file')
        except (OSError, ValueError, EOFError, struct.error, zlib.error,
                zipfile.BadZipFile, tarfile.TarError, RuntimeError) as error:
            self.errors.append(f'{label}: cannot scan: {error}')

    def scan_path(self, path):
        if path.is_dir():
            for child in sorted(path.rglob('*')):
                relative = child.relative_to(path).as_posix()
                self.check(relative.encode('utf-8'), relative + ' [path]')
                if child.is_file() or child.is_symlink():
                    self.scan_file(child, relative)
        else:
            self.scan_file(path, path.name)


def main(argv=None):
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('paths', nargs='*', type=Path, help='packages or staging directories')
    parser.add_argument('--tree', type=Path, help='Git checkout (default: repository containing this tool)')
    args = parser.parse_args(argv)
    scan = Scanner()
    if args.tree or not args.paths:
        root = (args.tree or Path(__file__).resolve().parents[1]).resolve()
        for path in public_files(root):
            scan.scan_file(path, path.relative_to(root).as_posix())
    for path in args.paths:
        scan.scan_path(path)
    for message in scan.hits + scan.errors:
        print(message)
    print(f'name scan: {scan.files} files, {len(scan.hits)} hits, {len(scan.errors)} errors')
    return int(bool(scan.hits or scan.errors))


if __name__ == '__main__':
    sys.exit(main())
