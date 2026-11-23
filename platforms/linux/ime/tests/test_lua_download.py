"""The pinned Lua download survives a failing host without trusting any source.

platforms/ios/scripts/prepare-lua.py feeds the Linux, iOS and Android engines.
These tests replay scripted network outcomes, so they never touch the network.
"""

import contextlib
import hashlib
import http.client
import importlib.util
import io
from pathlib import Path
import tempfile
import unittest
from unittest import mock
import urllib.error
import urllib.request


REPO = Path(__file__).resolve().parents[4]
SPEC = importlib.util.spec_from_file_location("rimes_lua_policy", REPO / "platforms/ios/scripts/prepare-lua.py")
POLICY = importlib.util.module_from_spec(SPEC)
SPEC.loader.exec_module(POLICY)

GOOD = b"locked Lua archive bytes"
PRIMARY = "https://primary.invalid/lua-5.4.9.tar.gz"
MIRROR = "https://mirror.invalid/lua-5.4.9.tar.gz"


class Response(io.BytesIO):
    def __init__(self, body, declared=None):
        super().__init__(body)
        self.headers = {"Content-Length": str(len(body) if declared is None else declared)}


class Opener:
    """Replay outcomes per URL in order, repeating the last one.

    An exception is raised; bytes, or (bytes, declared Content-Length), are served.
    """

    def __init__(self, outcomes):
        self.outcomes = {url: list(steps) for url, steps in outcomes.items()}
        self.requests = []

    def open(self, url, timeout):
        self.requests.append((url, timeout))
        steps = self.outcomes[url]
        step = steps.pop(0) if len(steps) > 1 else steps[0]
        if isinstance(step, BaseException):
            raise step
        return Response(*step) if isinstance(step, tuple) else Response(step)


class LuaDownloadTests(unittest.TestCase):
    def setUp(self):
        temporary = tempfile.TemporaryDirectory(prefix="rimes-lua-download-")
        self.addCleanup(temporary.cleanup)
        self.work = Path(temporary.name)
        self.archive = self.work / "lua-5.4.9.tar.gz"
        self.entry = {"url": PRIMARY, "mirrors": [MIRROR], "sha256": hashlib.sha256(GOOD).hexdigest()}
        self.sleeps = []

    def fetch(self, outcomes, entry=None):
        self.opener = Opener(outcomes)
        with contextlib.redirect_stderr(io.StringIO()) as log:
            try:
                return POLICY.fetch_lua(entry or self.entry, self.archive, self.opener, self.sleeps.append)
            finally:
                self.log = log.getvalue()

    def files(self):
        return sorted(path.name for path in self.work.iterdir())

    def test_unreachable_primary_falls_back_to_mirror_without_waiting(self):
        self.fetch({PRIMARY: [urllib.error.URLError(TimeoutError("timed out"))], MIRROR: [GOOD]})
        self.assertEqual(self.archive.read_bytes(), GOOD)
        self.assertEqual(self.opener.requests, [(PRIMARY, POLICY.TIMEOUT), (MIRROR, POLICY.TIMEOUT)])
        self.assertEqual(self.sleeps, [])
        self.assertEqual(self.files(), [self.archive.name])
        self.assertIn("timed out", self.log)

    def test_corrupted_primary_is_discarded_for_matching_mirror(self):
        self.fetch({PRIMARY: [b"tampered archive"], MIRROR: [GOOD]})
        self.assertEqual(self.archive.read_bytes(), GOOD)
        self.assertIn("does not match the lock", self.log)

    def test_corrupted_downloads_are_never_kept_and_retries_are_bounded(self):
        with self.assertRaisesRegex(RuntimeError, "locked SHA-256"):
            self.fetch({PRIMARY: [b"tampered archive"], MIRROR: [GOOD[:-1] + b"!"]})
        self.assertEqual(self.files(), [])
        self.assertEqual([url for url, _ in self.opener.requests], [PRIMARY, MIRROR] * POLICY.ATTEMPTS)
        self.assertEqual(self.sleeps, [POLICY.BACKOFF * 2 ** n for n in range(POLICY.ATTEMPTS - 1)])

    def test_connection_reset_is_retried_after_backoff(self):
        reset = ConnectionResetError(104, "Connection reset by peer")
        self.fetch({PRIMARY: [reset, GOOD]}, dict(self.entry, mirrors=[]))
        self.assertEqual(self.archive.read_bytes(), GOOD)
        self.assertEqual(self.sleeps, [POLICY.BACKOFF])

    def test_truncated_oversized_and_stalled_transfers_are_rejected(self):
        single = dict(self.entry, mirrors=[])
        with self.assertRaisesRegex(RuntimeError, f"received 5 of {len(GOOD)} bytes"):
            self.fetch({PRIMARY: [(GOOD[:5], len(GOOD))]}, single)
        with mock.patch.object(POLICY, "MAX_BYTES", len(GOOD) - 1), self.assertRaisesRegex(RuntimeError, "larger than"):
            self.fetch({PRIMARY: [GOOD]}, single)
        with mock.patch.object(POLICY, "DEADLINE", -1), self.assertRaisesRegex(RuntimeError, "longer than"):
            self.fetch({PRIMARY: [GOOD]}, single)
        self.assertEqual(self.files(), [])

    def test_cached_archive_is_reused_only_when_it_matches(self):
        self.archive.write_bytes(GOOD)
        self.fetch({})
        self.assertEqual(self.opener.requests, [])
        self.archive.write_bytes(GOOD[:5])
        self.fetch({PRIMARY: [GOOD]})
        self.assertEqual(self.archive.read_bytes(), GOOD)
        self.assertIn("Discarding", self.log)

    def test_only_https_sources_and_redirects_are_used(self):
        for url in ("http://www.lua.org/ftp/lua-5.4.9.tar.gz", "ftp://mirror.invalid/lua.tar.gz", "https:///lua.tar.gz"):
            with self.subTest(url=url), self.assertRaisesRegex(RuntimeError, "HTTPS"):
                self.fetch({}, dict(self.entry, mirrors=[url]))
            self.assertEqual(self.opener.requests, [])
        handler = POLICY.HTTPSOnlyRedirects()
        request, headers = urllib.request.Request(PRIMARY), http.client.HTTPMessage()
        with self.assertRaisesRegex(urllib.error.HTTPError, "non-HTTPS") as refused:
            handler.redirect_request(request, io.BytesIO(), 302, "Found", headers, "http://mirror.invalid/lua.tar.gz")
        refused.exception.close()
        self.assertEqual(handler.redirect_request(request, io.BytesIO(), 302, "Found", headers, MIRROR).full_url, MIRROR)
        opener = urllib.request.build_opener(POLICY.HTTPSOnlyRedirects)
        redirects = [type(h) for h in opener.handlers if isinstance(h, urllib.request.HTTPRedirectHandler)]
        self.assertEqual(redirects, [POLICY.HTTPSOnlyRedirects])


if __name__ == "__main__":
    unittest.main()
