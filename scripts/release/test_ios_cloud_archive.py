import importlib.util
import hashlib
import json
import plistlib
import tempfile
import unittest
from pathlib import Path
from unittest import mock

SPEC = importlib.util.spec_from_file_location('ios_archive', Path(__file__).with_name('create-ios-cloud-archive.py'))
ARCHIVE = importlib.util.module_from_spec(SPEC)
SPEC.loader.exec_module(ARCHIVE)


class CloudArchiveTests(unittest.TestCase):
    def setUp(self):
        temporary = tempfile.TemporaryDirectory()
        self.addCleanup(temporary.cleanup)
        self.root = Path(temporary.name)
        self.app = self.root / 'Products/Applications/RIMES.app'
        self.extension = self.app / 'PlugIns/RIMESKeyboard.appex'
        (self.root / 'Info.plist').write_bytes(plistlib.dumps({'ApplicationProperties': {'ApplicationPath': 'Applications/RIMES.app'}}))
        for path, identifier in ((self.app, 'org.scholay.rimes.ios'), (self.extension, 'org.scholay.rimes.ios.keyboard')):
            path.mkdir(parents=True)
            (path / 'Info.plist').write_bytes(plistlib.dumps({'CFBundleIdentifier': identifier, 'CFBundleVersion': '62', 'CFBundleShortVersionString': '1.1.6', 'UIDeviceFamily': [1], 'CFBundleExecutable': 'RIMES', 'RIMESKeychainGroup': '585J2TL9U6.org.scholay.rimes.ios.shared', 'RIMESAppGroup': 'group.org.scholay.rimes.ios'}))
            (path / 'RIMES').write_bytes(b'executable fixture')
            (path / 'PrivacyInfo.xcprivacy').write_bytes(plistlib.dumps({}))
        model = self.extension / 'Handwriting/handwriting-zh_CN.model'
        model.parent.mkdir()
        model.write_bytes(b'handwriting model fixture')
        expected = hashlib.sha256(model.read_bytes()).hexdigest()
        load_verifier = ARCHIVE.runpy.run_path
        def fixture_verifier(*args, **kwargs):
            verifier = load_verifier(*args, **kwargs)
            verifier['validate_unsigned_app'].__globals__['handwriting_model_sha256'] = lambda: expected
            return verifier
        patcher = mock.patch.object(ARCHIVE.runpy, 'run_path', side_effect=fixture_verifier)
        patcher.start()
        self.addCleanup(patcher.stop)

    def test_complete_unsigned_archive_is_accepted(self):
        ARCHIVE.validate_archive(self.root, '1.1.6', '62')

    def test_archive_with_lost_keychain_prefix_cannot_be_packaged(self):
        path = self.extension / 'Info.plist'
        info = plistlib.loads(path.read_bytes())
        info['RIMESKeychainGroup'] = 'org.scholay.rimes.ios.shared'
        path.write_bytes(plistlib.dumps(info))
        with self.assertRaisesRegex(ValueError, 'Keychain group'):
            ARCHIVE.validate_archive(self.root, '1.1.6', '62')

    def test_wrong_extension_build_is_rejected(self):
        path = self.extension / 'Info.plist'
        info = plistlib.loads(path.read_bytes())
        info['CFBundleVersion'] = '61'
        path.write_bytes(plistlib.dumps(info))
        with self.assertRaisesRegex(ValueError, 'CFBundleVersion'):
            ARCHIVE.validate_archive(self.root, '1.1.6', '62')

    def test_missing_extension_privacy_manifest_is_rejected(self):
        (self.extension / 'PrivacyInfo.xcprivacy').unlink()
        with self.assertRaisesRegex(ValueError, 'privacy manifest'):
            ARCHIVE.validate_archive(self.root, '1.1.6', '62')

    def test_missing_handwriting_model_is_rejected(self):
        (self.extension / 'Handwriting/handwriting-zh_CN.model').unlink()
        with self.assertRaisesRegex(ValueError, 'handwriting model is missing'):
            ARCHIVE.validate_archive(self.root, '1.1.6', '62')

    def test_unexpected_extension_is_rejected(self):
        (self.app / 'PlugIns/Unexpected.appex').mkdir()
        with self.assertRaisesRegex(ValueError, 'exactly the keyboard'):
            ARCHIVE.validate_archive(self.root, '1.1.6', '62')

    def test_invalid_apple_build_numbers_are_rejected(self):
        for build in ('', '0', '10000', '1.100', '1.1.1.1', '62-preview', '62\n'):
            with self.subTest(build=build), self.assertRaises(ValueError):
                ARCHIVE.check_build(build)

    def test_archive_purpose_is_explicit_and_closed(self):
        for purpose in ('app-store', 'device-qa'):
            ARCHIVE.check_purpose(purpose)
        for purpose in ('', 'release', 'device-qa\n'):
            with self.subTest(purpose=purpose), self.assertRaises(ValueError):
                ARCHIVE.check_purpose(purpose)

    def test_invalid_archive_purpose_is_rejected_before_packaging(self):
        with mock.patch.object(ARCHIVE, 'validate_archive') as validate:
            with self.assertRaisesRegex(ValueError, 'archive purpose'):
                ARCHIVE.main([str(self.root), '1.1.6', '62', 'a' * 40,
                              str(self.root / 'output'), 'unexpected'])
            validate.assert_not_called()

    def test_packaged_receipt_identifies_device_qa_and_default_store_purpose(self):
        def fake_ditto(command, **_kwargs):
            Path(command[-1]).write_bytes(b'archive fixture')

        with mock.patch.object(ARCHIVE.subprocess, 'run', side_effect=fake_ditto):
            for purpose in ('app-store', 'device-qa'):
                output = self.root / purpose
                args = [str(self.root), '1.1.6', '62', 'a' * 40, str(output)]
                if purpose == 'device-qa':
                    args.append(purpose)
                ARCHIVE.main(args)
                receipt = json.loads((output / 'archive-provenance.json').read_text())
                self.assertEqual(receipt['purpose'], purpose)
                self.assertEqual(receipt['sha256'], hashlib.sha256(b'archive fixture').hexdigest())
                if purpose == 'device-qa':
                    self.assertIn('device QA only', receipt['signing'])
