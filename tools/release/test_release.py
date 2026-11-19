"""ROM/network-free tests for release mutations and fail-closed package auditing."""
import contextlib
import hashlib
import io
import json
import os
import sys
from pathlib import Path
import tempfile
import unittest
from unittest.mock import patch
import zipfile

import draft_release
import notices
import workflow_version
sys.path.insert(0, str(notices.ROOT / 'tools'))
import android_prepare


class ReleaseTests(unittest.TestCase):
    def setUp(self):
        self.temp = tempfile.TemporaryDirectory()
        self.addCleanup(self.temp.cleanup)
        self.folder = Path(self.temp.name)

    def artifacts(self):
        for name in draft_release.PACKAGES:
            data = b'ROM-free test artifact'
            (self.folder / name).write_bytes(data)
            (self.folder / (name + '.sha256')).write_text(
                f'{hashlib.sha256(data).hexdigest()}  {name}\n', encoding='utf-8', newline='')
        (self.folder / 'android-signing.txt').write_text(
            'Android APK: TESTING BUILD, signed with the public debug key; not a production signature.\n',
            encoding='utf-8', newline='')

    def release(self, existing, bad_checksum=False):
        self.artifacts()
        if bad_checksum:
            (self.folder / draft_release.PACKAGES[0]).write_bytes(b'corrupted')
        with patch.dict(os.environ, RELEASE_VERSION='v1.0.0', GITHUB_REPOSITORY='example/sawgod'), \
                patch('subprocess.check_output', return_value=json.dumps([existing])), \
                patch('subprocess.run') as run, contextlib.redirect_stdout(io.StringIO()):
            draft_release.main(self.folder)
            return [c.args[0] for c in run.call_args_list]

    def test_draft_contains_all_packages_and_checksums(self):
        calls = self.release([])
        self.assertIn('--draft', calls[0])
        self.assertIn('--verify-tag', calls[0])
        self.assertIn('create', calls[0])
        uploads = calls[1]
        self.assertEqual(sum(str(self.folder) in arg for arg in uploads), 9)
        self.assertIn('TESTING BUILD', (self.folder / 'release-body.md').read_text(encoding='utf-8'))

    def test_existing_draft_updates_without_publishing(self):
        calls = self.release([{'tag_name': 'v1.0.0', 'draft': True}])
        self.assertIn('edit', calls[0])
        self.assertIn('--draft=true', calls[0])

    def test_published_release_cannot_be_changed(self):
        with patch('subprocess.run') as run:
            with self.assertRaisesRegex(SystemExit, 'published'):
                self.release([{'tag_name': 'v1.0.0', 'draft': False}])
            run.assert_not_called()

    def test_checksum_corruption_stops_before_gh(self):
        with self.assertRaisesRegex(SystemExit, 'Checksum mismatch'):
            self.release([], bad_checksum=True)

    def test_missing_license_rejected_in_every_package(self):
        for target in notices.PLATFORM:
            folder = self.folder / target
            notices.stage(target, folder)
            for filename in notices.required(target):
                path = folder / filename
                if not path.exists():
                    path.parent.mkdir(parents=True, exist_ok=True)
                    path.write_text('test fixture\n', encoding='utf-8', newline='')
            with contextlib.redirect_stdout(io.StringIO()):
                notices.audit(target, folder)
            (folder / 'licenses/SDL3.txt').unlink()
            with self.assertRaisesRegex(RuntimeError, 'SDL3.txt'):
                notices.audit(target, folder)

    def test_android_source_export_keeps_authored_english(self):
        root = self.folder / 'export'
        english = root / 'assets/text/en.csv'
        english.parent.mkdir(parents=True)
        content = 'key,text\nui_test,Authored English\n'
        english.write_text(content, encoding='utf-8', newline='')
        output = self.folder / 'assets'
        (output / 'text').mkdir(parents=True)
        with patch.object(android_prepare, 'ROOT', root), patch('subprocess.run') as run:
            android_prepare.stage_english(output)
            run.assert_not_called()
        self.assertEqual((output / 'text/en.csv').read_text(encoding='utf-8'), content)
        self.assertEqual(english.read_text(encoding='utf-8'), content)

    def test_rom_and_comparison_exports_are_forbidden(self):
        for name in ('assets/rom.wsc', 'wsc.sgz', 'text/en_fill.csv'):
            with self.assertRaisesRegex(RuntimeError, 'Forbidden release payload'):
                notices.audit_payload([name])

    def test_missing_notice_index_rejected(self):
        for target in notices.PLATFORM:
            folder = self.folder / target
            notices.stage(target, folder)
            (folder / 'THIRD_PARTY_NOTICES.txt').unlink()
            with self.assertRaisesRegex(RuntimeError, 'THIRD_PARTY_NOTICES.txt'):
                notices.audit(target, folder)

    @patch.object(notices, 'FLUIDSYNTH_SHA256', hashlib.sha256(b'test fixture').hexdigest())
    def test_apk_audits_final_assets_and_library_inventory(self):
        apk = self.folder / 'test.apk'
        with zipfile.ZipFile(apk, 'w') as archive:
            for name in notices.required('android'):
                archive.writestr('assets/' + name, notices.notice_text('android')
                                 if name == 'THIRD_PARTY_NOTICES.txt' else 'test fixture')
            for name in ('libsawgod.so', 'libSDL3.so', 'libfluidsynth.so', 'libc++_shared.so'):
                archive.writestr('lib/arm64-v8a/' + name, 'test fixture')
        with contextlib.redirect_stdout(io.StringIO()):
            notices.audit_apk(apk)
        with zipfile.ZipFile(apk, 'a') as archive:
            archive.writestr('lib/arm64-v8a/libunexpected.so', 'test fixture')
        with self.assertRaisesRegex(RuntimeError, 're-audit licenses'):
            notices.audit_apk(apk)

    def test_apk_fluidsynth_source_must_be_exact_archive(self):
        apk = self.folder / 'test.apk'
        with zipfile.ZipFile(apk, 'w') as archive:
            for name in notices.required('android'):
                archive.writestr('assets/' + name, notices.notice_text('android')
                                 if name == 'THIRD_PARTY_NOTICES.txt' else 'test fixture')
        with self.assertRaisesRegex(RuntimeError, 'exact upstream archive'):
            notices.audit_apk(apk)

    def test_version_rejects_partial_build_or_wrong_commit(self):
        env = dict(GITHUB_REF_TYPE='branch', GITHUB_REF_NAME='master', GITHUB_SHA='abc',
                   REQUESTED_VERSION='v1.0.0', REQUESTED_PLATFORMS='linux',
                   GITHUB_OUTPUT=str(self.folder / 'output'))
        with patch.dict(os.environ, env), patch('subprocess.run'), \
                patch('subprocess.check_output', return_value='wrong\n'):
            with self.assertRaisesRegex(SystemExit, 'platforms=all'):
                workflow_version.main()
            os.environ['REQUESTED_PLATFORMS'] = 'all'
            with self.assertRaisesRegex(SystemExit, 'tag and checkout'):
                workflow_version.main()

    def test_version_uses_matching_tag(self):
        env = dict(GITHUB_REF_TYPE='tag', GITHUB_REF_NAME='v1.0.0', GITHUB_SHA='abc',
                   REQUESTED_VERSION='', REQUESTED_PLATFORMS='',
                   GITHUB_OUTPUT=str(self.folder / 'output'))
        with patch.dict(os.environ, env), patch('subprocess.run'), \
                patch('subprocess.check_output', return_value='abc\n'):
            workflow_version.main()
        self.assertEqual((self.folder / 'output').read_text(encoding='utf-8'), 'version=v1.0.0\nrelease=true\n')


if __name__ == '__main__':
    unittest.main()
