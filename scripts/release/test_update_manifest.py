import hashlib
from pathlib import Path
import tempfile
import unittest
from update_manifest import manifest


class UpdateManifestTests(unittest.TestCase):
    def test_feed_requires_every_platform_and_hashes_exact_packages(self):
        with tempfile.TemporaryDirectory() as temp:
            root = Path(temp)
            names = ["RIMES-1.2.3.pkg", "RIMES-Windows-1.2.3-Setup.exe", "fcitx5-rimes_1.2.3_amd64.deb", "RIMES-Android-1.2.3.apk"]
            for name in names[:-1]: (root / name).write_bytes(name.encode())
            with self.assertRaises(ValueError): manifest(root, "1.2.3", "a" * 40, 20)
            (root / names[-1]).write_bytes(b"apk")
            feed = manifest(root, "1.2.3", "a" * 40, 20)
            self.assertEqual(set(feed["platforms"]), {"macos", "windows", "android", "linux"})
            self.assertEqual(feed["platforms"]["android"]["sha256"], hashlib.sha256(b"apk").hexdigest())
            self.assertEqual(feed["platforms"]["android"]["build"], 20)
            (root / names[0]).write_bytes(b"")
            with self.assertRaises(ValueError): manifest(root, "1.2.3", "a" * 40, 20)

    def test_no_preview_downgrade_or_unbound_source(self):
        for version, commit in [("1.2.3-preview.1", "a" * 40), ("01.2.3", "a" * 40), ("1.2.3", "main")]:
            with self.assertRaises(ValueError): manifest(Path("/nonexistent"), version, commit, 20)


if __name__ == "__main__": unittest.main()
