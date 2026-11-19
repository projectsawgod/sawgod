"""Public source exclusions and nested/binary package privacy checks."""
import gzip
import io
import tarfile
import importlib.util
from pathlib import Path
import struct
import subprocess
import tempfile
import unittest
import zipfile
import zlib

SPEC = importlib.util.spec_from_file_location('name_scan', Path(__file__).resolve().parents[1] / 'tools/name_scan.py')
scan = importlib.util.module_from_spec(SPEC)
SPEC.loader.exec_module(scan)
NAME = ('And' + 'rew').encode()
MINGW = (b'Copyright: 2008 ' + NAME + b' Fenn\n'
         b'Copyright: 2009 ' + NAME + b' Nguyen\n'
         b'           2021 ' + NAME + b' Eikum for CodeWeavers\n')
CHANGELOG = (b'thanks to @' + NAME.lower() + b'-sayers)\n') * 3
CHANGELOG_PATH = 'fluidsynth-2.6.1/doc/wiki/ChangeLog.md'


def chunk(kind, payload):
    return struct.pack('>I', len(payload)) + kind + payload + struct.pack('>I', zlib.crc32(kind + payload))


class NameScanTests(unittest.TestCase):
    def test_upstream_notices_pass(self):
        for label in ('licenses/MinGW-w64.txt',
                      'sawgod-windows-x86_64/licenses/MinGW-w64.txt',
                      'sawgod-linux-x86_64/licenses/MinGW-w64.txt',
                      'sawgod-macos-arm64/SawGod.app/Contents/Resources/licenses/MinGW-w64.txt',
                      'assets/licenses/MinGW-w64.txt'):
            with self.subTest(label=label):
                checker = scan.Scanner()
                checker.scan_bytes(MINGW, label)
                self.assertFalse(checker.hits)
                self.assertFalse(checker.errors)

    def test_allowlisted_file_extra_hit_fails(self):
        for extra in (NAME, NAME.upper(), NAME.decode().encode('utf-16le')):
            with self.subTest(extra=extra):
                checker = scan.Scanner()
                checker.scan_bytes(MINGW + extra, 'licenses/MinGW-w64.txt')
                self.assertTrue(checker.hits)

    def test_upstream_counts_and_context_are_pinned(self):
        for data in (MINGW.replace(b' Fenn', b' Someone'),
                     MINGW + MINGW.splitlines(keepends=True)[0], b'clean'):
            with self.subTest(data=data):
                checker = scan.Scanner()
                checker.scan_bytes(data, 'licenses/MinGW-w64.txt')
                self.assertTrue(checker.errors)
        checker = scan.Scanner()
        checker.scan_bytes(MINGW, 'unrelated/MinGW-w64.txt')
        self.assertTrue(checker.hits)
        checker = scan.Scanner()
        checker.scan_bytes(MINGW, 'unrelated!licenses/MinGW-w64.txt')
        self.assertTrue(checker.hits)
        checker = scan.Scanner()
        checker.scan_bytes(gzip.compress(b'clean'), 'licenses/MinGW-w64.txt')
        self.assertTrue(checker.errors)

    def test_owner_email_and_surname_never_exempt(self):
        surname = b'vest' + b'al'
        email = NAME.lower() + surname + b'@example.com'
        for extra in (email, surname, surname.upper(), surname.decode().encode('utf-16be')):
            with self.subTest(extra=extra):
                checker = scan.Scanner()
                checker.scan_bytes(MINGW + extra, 'licenses/MinGW-w64.txt')
                self.assertTrue(checker.hits)
                self.assertFalse(checker.errors)
        checker = scan.Scanner()
        checker.scan_bytes(MINGW.replace(b' Fenn', surname), 'licenses/MinGW-w64.txt')
        self.assertTrue(checker.hits)
        self.assertTrue(checker.errors)

    def test_upstream_changelog_and_count_drift(self):
        for count in (0, 2, 3, 4):
            with self.subTest(count=count):
                checker = scan.Scanner()
                checker.scan_bytes(CHANGELOG.splitlines(keepends=True)[0] * count, CHANGELOG_PATH)
                self.assertFalse(checker.hits)
                self.assertEqual(bool(checker.errors), count != 3)

    def test_upstream_notices_in_nested_archives(self):
        for compression in (zipfile.ZIP_STORED, zipfile.ZIP_DEFLATED):
            with self.subTest(compression=compression):
                tar = io.BytesIO()
                with tarfile.open(fileobj=tar, mode='w') as output:
                    member = tarfile.TarInfo(CHANGELOG_PATH)
                    member.size = len(CHANGELOG)
                    output.addfile(member, io.BytesIO(CHANGELOG))
                archive = io.BytesIO()
                with zipfile.ZipFile(archive, 'w', compression=compression) as output:
                    output.writestr('sawgod-windows-x86_64/licenses/MinGW-w64.txt', MINGW)
                    output.writestr('source/fluidsynth-2.6.1.tar.gz', gzip.compress(tar.getvalue()))
                checker = scan.Scanner()
                checker.scan_bytes(archive.getvalue(), 'release.zip')
                self.assertFalse(checker.hits)
                self.assertFalse(checker.errors)
                checker.scan_bytes(archive.getvalue() + NAME, 'release.zip')
                self.assertTrue(checker.hits)

    def test_zip_directory_payload_is_scanned(self):
        archive = io.BytesIO()
        with zipfile.ZipFile(archive, 'w') as output:
            output.writestr('directory/', NAME)
        checker = scan.Scanner()
        checker.scan_bytes(archive.getvalue(), 'release.zip')
        self.assertTrue(checker.hits)

    def test_tar_containing_zip_uses_tar_member_paths(self):
        for payload in (b'clean', NAME):
            with self.subTest(payload=payload):
                zip_data = io.BytesIO()
                with zipfile.ZipFile(zip_data, 'w', zipfile.ZIP_DEFLATED) as output:
                    output.writestr('content.txt', payload)
                tar_data = io.BytesIO()
                with tarfile.open(fileobj=tar_data, mode='w') as output:
                    for name, data in (('sawgod-linux-x86_64/licenses/MinGW-w64.txt', MINGW),
                                       ('sawgod-linux-x86_64/source/gcem.zip', zip_data.getvalue())):
                        member = tarfile.TarInfo(name)
                        member.size = len(data)
                        output.addfile(member, io.BytesIO(data))
                self.assertTrue(zipfile.is_zipfile(io.BytesIO(tar_data.getvalue())))
                checker = scan.Scanner()
                checker.scan_bytes(gzip.compress(tar_data.getvalue()), 'release.tar.gz')
                self.assertEqual(bool(checker.hits), payload == NAME)
                self.assertFalse(checker.errors)

    def test_public_tree(self):
        with tempfile.TemporaryDirectory() as tmp:
            root = Path(tmp)
            subprocess.run(['git', 'init', '-q', tmp], check=True)
            (root / 'AGENTS.md').write_bytes(NAME)
            (root / 'source.txt').write_bytes(b'clean')
            subprocess.run(['git', '-C', tmp, 'add', '.'], check=True)
            self.assertEqual(scan.main(['--tree', tmp]), 0)
            (root / 'source.txt').write_bytes(NAME.upper())
            self.assertEqual(scan.main(['--tree', tmp]), 1)
            (root / 'source.txt').write_bytes(b'clean')
            (root / 'new.txt').write_bytes(NAME.lower())
            self.assertEqual(scan.main(['--tree', tmp]), 1)

    def test_zip_member(self):
        with tempfile.TemporaryDirectory() as tmp:
            path = Path(tmp) / 'sheet.ods'
            with zipfile.ZipFile(path, 'w', zipfile.ZIP_DEFLATED) as archive:
                archive.writestr('meta.xml', b'<creator>' + NAME + b'vest' + b'al</creator>')
            checker = scan.Scanner()
            checker.scan_path(path)
            self.assertTrue(any('!meta.xml' in hit for hit in checker.hits))
            self.assertFalse(checker.errors)

    def test_png_text(self):
        for kind, payload in [(b'tEXt', b'Author\0' + NAME),
                              (b'zTXt', b'Author\0\0' + zlib.compress(NAME)),
                              (b'iTXt', b'Author\0\x01\0\0\0' + zlib.compress(NAME))]:
            with self.subTest(kind=kind):
                checker = scan.Scanner()
                checker.scan_bytes(scan.PNG + chunk(kind, payload) + chunk(b'IEND', b''), 'image.png')
                self.assertTrue(any('!' + kind.decode() in hit for hit in checker.hits))

    def test_binary_path_and_utf16(self):
        checker = scan.Scanner()
        checker.scan_bytes(('AND' + 'REW').encode('utf-16le'), 'binary')
        self.assertTrue(checker.hits)
        with tempfile.TemporaryDirectory() as tmp:
            path = Path(tmp) / (NAME.decode() + '.txt')
            path.write_bytes(b'clean')
            checker = scan.Scanner()
            checker.scan_path(path)
            self.assertTrue(checker.hits)

    def test_nested_tar_gzip_and_zip_metadata(self):
        archive = io.BytesIO()
        with tarfile.open(fileobj=archive, mode='w') as output:
            member = tarfile.TarInfo('payload.txt')
            member.size = len(NAME)
            output.addfile(member, io.BytesIO(NAME))
        checker = scan.Scanner()
        checker.scan_bytes(gzip.compress(archive.getvalue()), 'source.tar.gz')
        self.assertTrue(any('!gzip!payload.txt' in hit for hit in checker.hits))
        archive = io.BytesIO()
        with zipfile.ZipFile(archive, 'w') as output:
            output.comment = NAME
            output.writestr(NAME.decode() + '.txt', b'clean')
        checker = scan.Scanner()
        checker.scan_bytes(archive.getvalue(), 'package.zip')
        self.assertTrue(any('!comment' in hit for hit in checker.hits))
        self.assertTrue(any('[metadata]' in hit for hit in checker.hits))

    def test_missing_or_corrupt_package_fails(self):
        with tempfile.TemporaryDirectory() as tmp:
            self.assertEqual(scan.main([str(Path(tmp) / 'missing.zip')]), 1)
            path = Path(tmp) / 'broken.zip'
            path.write_bytes(b'PK\x03\x04broken')
            self.assertEqual(scan.main([str(path)]), 1)

    def test_guard_source_is_clean(self):
        checker = scan.Scanner()
        checker.scan_bytes(Path(scan.__file__).read_bytes(), 'guard')
        checker.scan_bytes(Path(__file__).read_bytes(), 'tests')
        self.assertFalse(checker.hits)


if __name__ == '__main__':
    unittest.main()
