import hashlib
import importlib.util
from pathlib import Path
import shutil
import subprocess
import tempfile
import unittest

SPEC = importlib.util.spec_from_file_location(
    "previous_deb", Path(__file__).resolve().parents[1] / "scripts/fetch-previous-deb.py")
MODULE = importlib.util.module_from_spec(SPEC)
SPEC.loader.exec_module(MODULE)


def release(tag, architecture="amd64", checksum=True, **fields):
    name = f"fcitx5-rimes_{tag[1:].replace('-preview.', '~preview.')}_{architecture}.deb"
    return dict(tag_name=tag, published_at="2026-10-18T00:00:00Z", draft=False,
                assets=[{"name": name}, *([{"name": name + ".sha256"}] if checksum else [])], **fields)


@unittest.skipUnless(shutil.which("dpkg") and shutil.which("dpkg-deb"), "requires Debian package tools")
class PreviousDebTests(unittest.TestCase):
    def test_selects_highest_older_version_with_matching_architecture_and_checksum(self):
        rows = [release("v1.1.3"), release("v1.1.4"), release("v1.1.5-preview.2"),
                release("v1.1.5"), release("v1.2.0"), release("v1.1.5-preview.10", checksum=False),
                release("v1.1.5-preview.9", "arm64"), release("windows-v1.1.5")]
        draft = release("v1.1.5-preview.8")
        draft["draft"] = True
        rows.append(draft)
        self.assertEqual(MODULE.select_release(rows, "1.1.5", "amd64")[0], "1.1.5~preview.2")
        self.assertEqual(MODULE.select_release(rows, "1.1.5~preview.1", "amd64")[0], "1.1.4")
        self.assertIsNone(MODULE.select_release(rows, "1.1.3", "amd64"))

    def test_rejects_changed_bytes_and_mismatched_package_identity(self):
        with tempfile.TemporaryDirectory() as directory:
            root = Path(directory)
            (root / "payload/DEBIAN").mkdir(parents=True)
            (root / "payload/DEBIAN/control").write_text(
                "Package: fcitx5-rimes\nVersion: 1.1.4\nArchitecture: all\nMaintainer: Test <test@example.invalid>\nDescription: Public fixture\n")
            package = root / "fcitx5-rimes_1.1.4_all.deb"
            subprocess.run(["dpkg-deb", "--root-owner-group", "-b", str(root / "payload"), str(package)],
                           check=True, stdout=subprocess.DEVNULL)
            digest = hashlib.sha256(package.read_bytes()).hexdigest()
            (root / (package.name + ".sha256")).write_text(f"{digest}  {package.name}\n")
            MODULE.verify(package, "1.1.4", "all")
            with self.assertRaisesRegex(ValueError, "metadata"):
                MODULE.verify(package, "1.1.3", "all")
            package.write_bytes(package.read_bytes() + b"modified")
            with self.assertRaisesRegex(ValueError, "checksum"):
                MODULE.verify(package, "1.1.4", "all")


if __name__ == "__main__":
    unittest.main()
