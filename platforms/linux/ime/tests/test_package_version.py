import os
from pathlib import Path
import subprocess
import unittest


ROOT = Path(__file__).resolve().parents[4]
PACKAGE = ROOT / "platforms/linux/ime/scripts/package-deb.sh"


class PackageVersionTests(unittest.TestCase):
    def version(self, value=None):
        env = dict(os.environ)
        env.pop("RIMES_RELEASE_VERSION", None)
        if value is not None:
            env["RIMES_RELEASE_VERSION"] = value
        return subprocess.run([str(PACKAGE), "--print-version"], env=env,
                              capture_output=True, text=True)

    def test_default_tracks_product_version(self):
        result = self.version()
        self.assertEqual(result.returncode, 0, result.stderr)
        self.assertEqual(result.stdout.strip(), (ROOT / "VERSION").read_text().strip())

    def test_preview_uses_debian_ordering(self):
        result = self.version("2.3.4-preview.2")
        self.assertEqual(result.returncode, 0, result.stderr)
        self.assertEqual(result.stdout.strip(), "2.3.4~preview.2")

    def test_formal_version_must_match_product_target(self):
        self.assertNotEqual(self.version("99.0.0").returncode, 0)

    def test_invalid_versions_are_rejected_before_building(self):
        for value in ("1.2", "01.2.3", "1.2.3-preview.0", "1.2.3-preview.x", "1.2.3/../x"):
            with self.subTest(version=value):
                self.assertNotEqual(self.version(value).returncode, 0)


if __name__ == "__main__":
    unittest.main()
