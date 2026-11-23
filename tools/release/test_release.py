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

    def artifacts(self, version):
        packages = draft_release.VANILLA_PACKAGES if version == 'vanilla' else draft_release.PACKAGES
        for name in packages:
            data = b'ROM-free test artifact'
            (self.folder / name).write_bytes(data)
            (self.folder / (name + '.sha256')).write_text(
                f'{hashlib.sha256(data).hexdigest()}  {name}\n', encoding='utf-8', newline='')
        if version == 'vanilla':
            (self.folder / 'android-signing.txt').unlink(missing_ok=True)
            return
        (self.folder / 'android-signing.txt').write_text(
            'Android APK: TESTING BUILD, signed with the public debug key; not a production signature.\n',
            encoding='utf-8', newline='')

    def release(self, existing, version='v1.1.0'):
        self.artifacts(version)
        with patch.dict(os.environ, RELEASE_VERSION=version, GITHUB_REPOSITORY='example/sawgod'), \
                patch('subprocess.check_output', return_value=json.dumps([existing])), \
                patch('subprocess.run') as run, contextlib.redirect_stdout(io.StringIO()):
            draft_release.main(self.folder)
            return [c.args[0] for c in run.call_args_list]

    def test_draft_contains_only_selected_packages_and_combined_checksums(self):
        for version, packages, title in [('v1.1.0', draft_release.PACKAGES, 'SawGod v1.1.0'),
                                         ('vanilla', draft_release.VANILLA_PACKAGES, 'sawgod-vanilla')]:
            with self.subTest(version=version):
                calls = self.release([], version=version)
                self.assertIn('--draft', calls[0])
                self.assertIn('--verify-tag', calls[0])
                self.assertIn('create', calls[0])
                self.assertEqual(calls[0][calls[0].index('--title') + 1], title)
                uploads = calls[-1]
                self.assertEqual(uploads[uploads.index('--clobber') + 1:],
                                 [str(self.folder / name) for name in (*packages, 'SHA256SUMS.txt')])
                sums = (self.folder / 'SHA256SUMS.txt').read_text(encoding='utf-8')
                self.assertEqual([line.split('  ')[1] for line in sums.splitlines()], list(packages))
                body = (self.folder / 'release-body.md').read_bytes()
                if version == 'vanilla':
                    self.assertEqual(body, draft_release.VANILLA_NOTES.read_bytes())
                else:
                    self.assertIn(b'TESTING BUILD', body)
                    self.assertNotIn(b'vanilla', body)

    def test_existing_draft_updates_without_publishing_and_removes_stale_assets(self):
        for version in ('v1.1.0', 'vanilla'):
            with self.subTest(version=version):
                packages = draft_release.VANILLA_PACKAGES if version == 'vanilla' else draft_release.PACKAGES
                stale = draft_release.PACKAGES if version == 'vanilla' else draft_release.VANILLA_PACKAGES
                names = [*packages, *stale, packages[0] + '.sha256', 'SHA256SUMS.txt']
                calls = self.release([{'tag_name': version, 'draft': True,
                                       'assets': [{'name': name} for name in names]}], version=version)
                self.assertIn('edit', calls[0])
                self.assertIn('--draft=true', calls[0])
                self.assertIn('--title', calls[0])
                self.assertEqual([call[4] for call in calls if call[2] == 'delete-asset'],
                                 [*stale, packages[0] + '.sha256'])

    def test_published_release_cannot_be_changed(self):
        for version in ('v1.1.0', 'vanilla'):
            self.artifacts(version)
            existing = [{'tag_name': version, 'draft': False}]
            with self.subTest(version=version), \
                    patch.dict(os.environ, RELEASE_VERSION=version, GITHUB_REPOSITORY='example/sawgod'), \
                    patch('subprocess.check_output', return_value=json.dumps([existing])), \
                    patch('subprocess.run') as run:
                with self.assertRaisesRegex(SystemExit, 'published'):
                    draft_release.main(self.folder)
                run.assert_not_called()

    def test_checksum_corruption_stops_before_gh(self):
        for version in ('v1.1.0', 'vanilla'):
            self.artifacts(version)
            packages = draft_release.VANILLA_PACKAGES if version == 'vanilla' else draft_release.PACKAGES
            (self.folder / packages[0]).write_bytes(b'corrupted')
            with self.subTest(version=version), patch.dict(os.environ, RELEASE_VERSION=version), \
                    patch('subprocess.check_output') as gh, patch('subprocess.run') as run:
                with self.assertRaisesRegex(SystemExit, 'Checksum mismatch'):
                    draft_release.main(self.folder)
                gh.assert_not_called()
                run.assert_not_called()

    def test_sawgod_requires_recognized_android_signing_status(self):
        self.artifacts('v1.1.0')
        for status in ('', 'unknown'):
            (self.folder / 'android-signing.txt').write_text(status, encoding='utf-8', newline='')
            with patch.dict(os.environ, RELEASE_VERSION='v1.1.0'), patch('subprocess.check_output') as gh:
                with self.assertRaisesRegex(SystemExit, 'Android signing status'):
                    draft_release.main(self.folder)
                gh.assert_not_called()

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

    def workflow_env(self, tag='', requested='', platforms='', sha='abc'):
        return dict(GITHUB_REF_TYPE='tag' if tag else 'branch', GITHUB_REF_NAME=tag or 'main',
                    GITHUB_SHA=sha, REQUESTED_VERSION=requested, REQUESTED_PLATFORMS=platforms,
                    GITHUB_OUTPUT=str(self.folder / 'output'))

    def test_version_rejects_partial_build_or_wrong_commit(self):
        for version in ('v1.1.0', 'vanilla'):
            with self.subTest(version=version), \
                    patch.dict(os.environ, self.workflow_env(requested=version, platforms='linux')), \
                    patch('subprocess.run'), patch('subprocess.check_output', return_value='wrong\n'):
                with self.assertRaisesRegex(SystemExit, 'platforms=all'):
                    workflow_version.main()
                os.environ['REQUESTED_PLATFORMS'] = 'all'
                with self.assertRaisesRegex(SystemExit, 'tag and checkout'):
                    workflow_version.main()

    def test_version_uses_matching_tag_or_manual_request(self):
        for version, flavor in [('v1.1.0', 'sawgod'), ('v1.1.0-rc.1', 'sawgod'), ('vanilla', 'vanilla')]:
            for manual in (False, True):
                with self.subTest(version=version, manual=manual):
                    output = self.folder / 'output'
                    output.write_text('', encoding='utf-8', newline='')
                    env = self.workflow_env(requested=version, platforms='all') if manual else self.workflow_env(tag=version)
                    with patch.dict(os.environ, env), patch('subprocess.run') as fetch, \
                            patch('subprocess.check_output', return_value='abc\n') as rev:
                        workflow_version.main()
                    fetch.assert_called_once_with(['git', 'fetch', '--depth=1', 'origin',
                                                  f'refs/tags/{version}:refs/tags/{version}'], check=True)
                    rev.assert_called_once_with(['git', 'rev-parse', version + '^{commit}'], text=True)
                    build_version = version if flavor == 'sawgod' else ''
                    self.assertEqual(output.read_text(encoding='utf-8'),
                                     f'version={version}\nrelease=true\nflavor={flavor}\nbuild_version={build_version}\n')

    def test_version_rejects_other_tags_before_git(self):
        for version in ('foo', 'vanilla-v1.0.0', 'vanilla/extra', 'v1.1', '1.1.0', 'v1.1.0/extra'):
            for manual in (False, True):
                with self.subTest(version=version, manual=manual), \
                        patch.dict(os.environ, self.workflow_env(requested=version) if manual
                                   else self.workflow_env(tag=version)), patch('subprocess.run') as fetch:
                    with self.assertRaisesRegex(SystemExit, 'Use vanilla or a version'):
                        workflow_version.main()
                    fetch.assert_not_called()

    def test_version_rejects_mismatched_selected_tag(self):
        for tag, requested in [('v1.1.0', 'vanilla'), ('vanilla', 'v1.1.0'), ('foo', 'vanilla')]:
            with self.subTest(tag=tag, requested=requested), \
                    patch.dict(os.environ, self.workflow_env(tag=tag, requested=requested)), \
                    patch('subprocess.run') as fetch:
                with self.assertRaisesRegex(SystemExit, 'match the selected tag'):
                    workflow_version.main()
                fetch.assert_not_called()

    def test_branch_and_pr_do_not_release_or_fetch(self):
        for event in ('push', 'pull_request', 'workflow_dispatch'):
            output = self.folder / 'output'
            output.write_text('', encoding='utf-8', newline='')
            with patch.dict(os.environ, self.workflow_env() | {'GITHUB_EVENT_NAME': event}), \
                    patch('subprocess.run') as fetch:
                workflow_version.main()
            fetch.assert_not_called()
            self.assertEqual(output.read_text(encoding='utf-8'),
                             'version=\nrelease=false\nflavor=sawgod\nbuild_version=\n')


if __name__ == '__main__':
    unittest.main()
