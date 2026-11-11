"""bootstrap.py keeps only locked bytes and never replaces a committed file.

The shared download policy (mirrors, retries, HTTPS-only redirects, size and rate
limits) is tested in platforms/android/scripts/test_pinned_download.py. These tests
replay scripted network outcomes, so they never touch the network.
"""

import contextlib
import hashlib
import importlib.util
import io
from pathlib import Path
import tempfile
import unittest
from unittest import mock
import urllib.parse


SCRIPTS = Path(__file__).resolve().parents[1]
REPO = SCRIPTS.parents[2]
SPEC = importlib.util.spec_from_file_location("rimes_ios_bootstrap", SCRIPTS / "bootstrap.py")
BOOTSTRAP = importlib.util.module_from_spec(SPEC)
SPEC.loader.exec_module(BOOTSTRAP)

GOOD = b"locked license text"
PRIMARY = "https://primary.invalid/LICENSE"
MIRROR = "https://mirror.invalid/LICENSE"


class Response(io.BytesIO):
    def __init__(self, body):
        super().__init__(body)
        self.headers = {"Content-Length": str(len(body))}


class Opener:
    """Replay outcomes per URL in order, repeating the last one; an exception is raised."""

    def __init__(self, outcomes):
        self.outcomes = {url: list(steps) for url, steps in outcomes.items()}
        self.requests = []

    def open(self, url, timeout):
        self.requests.append(url)
        steps = self.outcomes[url]
        step = steps.pop(0) if len(steps) > 1 else steps[0]
        if isinstance(step, BaseException):
            raise step
        return Response(step)


class BootstrapDownloadTests(unittest.TestCase):
    def setUp(self):
        temporary = tempfile.TemporaryDirectory(prefix="rimes-bootstrap-download-")
        self.addCleanup(temporary.cleanup)
        self.work = Path(temporary.name)
        self.path = self.work / "LICENSE"
        self.entry = {"url": PRIMARY, "sha256": hashlib.sha256(GOOD).hexdigest()}
        self.sleeps = []

    def download(self, outcomes, committed, entry=None):
        self.opener = Opener(outcomes)
        with mock.patch.object(BOOTSTRAP, "tracked", return_value=committed), \
                contextlib.redirect_stderr(io.StringIO()) as log:
            try:
                return BOOTSTRAP.download(entry or self.entry, self.path, opener=self.opener, sleep=self.sleeps.append)
            finally:
                self.log = log.getvalue()

    def files(self):
        return sorted(path.name for path in self.work.iterdir())

    def test_bootstrap_inputs_are_https_and_pinned(self):
        lock = BOOTSTRAP.LOCK
        for entry in [lock["boost"], lock["xcodegen"], *lock["data"]]:
            with self.subTest(url=entry["url"]):
                for url in [entry["url"], *entry.get("mirrors", [])]:
                    parts = urllib.parse.urlsplit(url)
                    self.assertEqual((parts.scheme, bool(parts.hostname)), ("https", True))
                self.assertRegex(entry["sha256"], "^[0-9a-f]{64}$")

    def test_committed_files_are_told_apart_from_downloads(self):
        self.assertTrue(BOOTSTRAP.tracked(REPO / "platforms/ios/Licenses/rapidjson-1.1.0-MIT.txt"))
        self.assertFalse(BOOTSTRAP.tracked(REPO / "platforms/ios/Resources/EngineData/pinyin_simp.dict.yaml"))
        self.assertFalse(BOOTSTRAP.tracked(BOOTSTRAP.WORK / "boost.tar.bz2"))

    def test_committed_file_that_differs_from_the_lock_is_reported_not_replaced(self):
        self.path.write_bytes(b"locally edited license text")
        with self.assertRaisesRegex(RuntimeError, "committed but differs from the lock"):
            self.download({PRIMARY: [GOOD]}, committed=True)
        self.assertEqual(self.path.read_bytes(), b"locally edited license text")
        self.assertEqual(self.opener.requests, [])
        self.path.write_bytes(GOOD)
        self.download({}, committed=True)
        self.assertEqual(self.opener.requests, [])

    def test_downloaded_file_that_differs_is_discarded_and_fetched_again(self):
        self.path.write_bytes(GOOD[:5])
        self.download({PRIMARY: [GOOD]}, committed=False)
        self.assertEqual(self.path.read_bytes(), GOOD)
        self.assertIn("Discarding", self.log)

    def test_transient_failure_is_retried_after_backoff(self):
        self.download({PRIMARY: [ConnectionResetError(54, "Connection reset by peer"), GOOD]}, committed=False)
        self.assertEqual(self.path.read_bytes(), GOOD)
        self.assertEqual(self.opener.requests, [PRIMARY, PRIMARY])
        self.assertEqual(self.sleeps, [BOOTSTRAP.PINNED.BACKOFF])

    def test_corrupted_downloads_are_never_kept_and_lock_mirrors_are_tried(self):
        entry = dict(self.entry, mirrors=[MIRROR])
        with self.assertRaisesRegex(RuntimeError, "locked SHA-256"):
            self.download({PRIMARY: [b"tampered"], MIRROR: [GOOD[:-1] + b"!"]}, committed=False, entry=entry)
        self.assertEqual(self.files(), [])
        self.assertEqual(self.opener.requests, [PRIMARY, MIRROR] * BOOTSTRAP.PINNED.ATTEMPTS)


if __name__ == "__main__":
    unittest.main()
