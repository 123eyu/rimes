"""Pinned engine inputs survive a failing host without trusting any source.

platforms/ios/scripts/pinned_download.py fetches the dependencies.lock.json entries for
this Android engine build (Boost, dictionaries) and for the iOS bootstrap (Boost,
XcodeGen, data files). These tests go through build-engine.py's `download` and replay
scripted network outcomes, so they never touch the network.
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
import urllib.parse
import urllib.request


SPEC = importlib.util.spec_from_file_location("rimes_android_engine", Path(__file__).with_name("build-engine.py"))
ENGINE = importlib.util.module_from_spec(SPEC)
SPEC.loader.exec_module(ENGINE)
PINNED = ENGINE.PINNED

GOOD = b"locked archive bytes"
PRIMARY = "https://primary.invalid/boost_1_86_0.tar.bz2"
MIRROR = "https://mirror.invalid/boost_1_86_0.tar.bz2"


class Response(io.BytesIO):
    def __init__(self, body, declared=None, chunk=None):
        super().__init__(body)
        self.headers = {"Content-Length": str(len(body) if declared is None else declared)}
        self.chunk = chunk

    def read1(self, size=-1):
        return super().read1(size if self.chunk is None else self.chunk)


class Opener:
    """Replay outcomes per URL in order, repeating the last one.

    An exception is raised; bytes, or (bytes, declared Content-Length), are served,
    `chunk` bytes per read when it is set.
    """

    def __init__(self, outcomes, chunk=None):
        self.outcomes = {url: list(steps) for url, steps in outcomes.items()}
        self.chunk = chunk
        self.requests = []

    def open(self, url, timeout):
        self.requests.append((url, timeout))
        steps = self.outcomes[url]
        step = steps.pop(0) if len(steps) > 1 else steps[0]
        if isinstance(step, BaseException):
            raise step
        body, declared = step if isinstance(step, tuple) else (step, None)
        return Response(body, declared, self.chunk)


class Clock:
    """A monotonic clock that advances by the next of `ticks`, repeating the last, when read."""

    def __init__(self, *ticks):
        self.ticks, self.now, self.reads = ticks or (0.0,), 0.0, 0

    def __call__(self):
        self.now += self.ticks[min(self.reads, len(self.ticks) - 1)]
        self.reads += 1
        return self.now


class PinnedDownloadTests(unittest.TestCase):
    def setUp(self):
        temporary = tempfile.TemporaryDirectory(prefix="rimes-pinned-download-")
        self.addCleanup(temporary.cleanup)
        self.work = Path(temporary.name)
        self.path = self.work / "boost.tar.bz2"
        self.entry = {"url": PRIMARY, "mirrors": [MIRROR], "sha256": hashlib.sha256(GOOD).hexdigest()}
        self.sleeps = []

    def fetch(self, outcomes, entry=None, chunk=None, clock=None):
        self.opener = Opener(outcomes, chunk)
        with contextlib.redirect_stderr(io.StringIO()) as log:
            try:
                return ENGINE.download(entry or self.entry, self.path, opener=self.opener,
                                       sleep=self.sleeps.append, clock=clock or Clock())
            finally:
                self.log = log.getvalue()

    def files(self):
        return sorted(path.name for path in self.work.iterdir())

    def test_unreachable_primary_falls_back_to_mirror_without_waiting(self):
        self.fetch({PRIMARY: [urllib.error.URLError(TimeoutError("timed out"))], MIRROR: [GOOD]})
        self.assertEqual(self.path.read_bytes(), GOOD)
        self.assertEqual(self.opener.requests, [(PRIMARY, PINNED.TIMEOUT), (MIRROR, PINNED.TIMEOUT)])
        self.assertEqual(self.sleeps, [])
        self.assertEqual(self.files(), [self.path.name])
        self.assertIn("timed out", self.log)

    def test_corrupted_primary_is_discarded_for_matching_mirror(self):
        self.fetch({PRIMARY: [b"tampered archive"], MIRROR: [GOOD]})
        self.assertEqual(self.path.read_bytes(), GOOD)
        self.assertIn("does not match the lock", self.log)

    def test_corrupted_downloads_are_never_kept_and_retries_are_bounded(self):
        with self.assertRaisesRegex(RuntimeError, "locked SHA-256"):
            self.fetch({PRIMARY: [b"tampered archive"], MIRROR: [GOOD[:-1] + b"!"]})
        self.assertEqual(self.files(), [])
        self.assertEqual([url for url, _ in self.opener.requests], [PRIMARY, MIRROR] * PINNED.ATTEMPTS)
        self.assertEqual(self.sleeps, [PINNED.BACKOFF * 2 ** n for n in range(PINNED.ATTEMPTS - 1)])

    def test_transient_failures_are_retried_after_backoff(self):
        unavailable = urllib.error.HTTPError(PRIMARY, 503, "Service Unavailable", http.client.HTTPMessage(), io.BytesIO())
        reset = ConnectionResetError(104, "Connection reset by peer")
        self.fetch({PRIMARY: [unavailable, reset, GOOD]}, dict(self.entry, mirrors=[]))
        self.assertEqual(self.path.read_bytes(), GOOD)
        self.assertEqual(self.sleeps, [PINNED.BACKOFF, PINNED.BACKOFF * 2])
        self.assertIn("503", self.log)
        self.assertIn("Connection reset", self.log)

    def test_truncated_and_oversized_transfers_are_rejected(self):
        single = dict(self.entry, mirrors=[])
        with self.assertRaisesRegex(RuntimeError, f"received 5 of {len(GOOD)} bytes"):
            self.fetch({PRIMARY: [(GOOD[:5], len(GOOD))]}, single)
        with mock.patch.object(PINNED, "MAX_BYTES", len(GOOD) - 1), self.assertRaisesRegex(RuntimeError, "larger than"):
            self.fetch({PRIMARY: [GOOD]}, single)
        self.assertEqual(self.files(), [])

    def paced(self, *rates):
        """Serve four windows' worth of locked bytes, one MIN_RATE chunk per read, at `rates` x MIN_RATE."""
        self.body = bytes(range(256)) * (4 * PINNED.WINDOW * PINNED.MIN_RATE // 256)
        self.clock = Clock(*(1 / rate for rate in rates))
        entry = {"url": PRIMARY, "sha256": hashlib.sha256(self.body).hexdigest()}
        return self.fetch({PRIMARY: [self.body]}, entry, PINNED.MIN_RATE, self.clock)

    def test_trickling_transfers_are_abandoned_even_after_a_fast_start(self):
        fast = (1000,) * (2 * PINNED.WINDOW)  # half the body, read in well under one window
        for name, rates in (("trickle", (0.5,)), ("fast, then a trickle", fast + (0.5,))):
            with self.subTest(name):
                with self.assertRaisesRegex(RuntimeError, "locked SHA-256"):
                    self.paced(*rates)
                self.assertEqual(self.log.count("received under"), PINNED.ATTEMPTS)
                self.assertEqual(self.files(), [])

    def test_slow_but_steady_transfer_is_kept(self):
        self.paced(2)
        self.assertEqual(self.path.read_bytes(), self.body)
        self.assertGreater(self.clock.now, 2 * PINNED.WINDOW)

    def test_cached_file_is_reused_only_when_it_matches(self):
        self.path.write_bytes(GOOD)
        self.fetch({})
        self.assertEqual(self.opener.requests, [])
        self.path.write_bytes(GOOD[:5])
        self.fetch({PRIMARY: [GOOD]})
        self.assertEqual(self.path.read_bytes(), GOOD)
        self.assertIn("Discarding", self.log)

    def test_mismatching_cache_is_not_kept_when_every_source_fails(self):
        self.path.write_bytes(GOOD[:5])
        with self.assertRaisesRegex(RuntimeError, "locked SHA-256"):
            self.fetch({PRIMARY: [ConnectionResetError()], MIRROR: [ConnectionResetError()]})
        self.assertEqual(self.files(), [])

    def test_only_https_sources_and_redirects_are_used(self):
        for url in ("http://archives.boost.io/release/1.86.0/source/boost_1_86_0.tar.bz2",
                    "ftp://mirror.invalid/boost.tar.bz2", "https:///boost.tar.bz2"):
            with self.subTest(url=url), self.assertRaisesRegex(RuntimeError, "HTTPS"):
                self.fetch({}, dict(self.entry, mirrors=[url]))
            self.assertEqual(self.opener.requests, [])
        handler = PINNED.HTTPSOnlyRedirects()
        request, headers = urllib.request.Request(PRIMARY), http.client.HTTPMessage()
        with self.assertRaisesRegex(urllib.error.HTTPError, "non-HTTPS") as refused:
            handler.redirect_request(request, io.BytesIO(), 302, "Found", headers, "http://mirror.invalid/boost.tar.bz2")
        refused.exception.close()
        self.assertEqual(handler.redirect_request(request, io.BytesIO(), 302, "Found", headers, MIRROR).full_url, MIRROR)
        opener = urllib.request.build_opener(PINNED.HTTPSOnlyRedirects)
        redirects = [type(h) for h in opener.handlers if isinstance(h, urllib.request.HTTPRedirectHandler)]
        self.assertEqual(redirects, [PINNED.HTTPSOnlyRedirects])
        # Without a scripted opener, fetch builds exactly that HTTPS-only opener.
        with mock.patch.object(urllib.request, "build_opener", return_value=Opener({PRIMARY: [GOOD]})) as build:
            PINNED.fetch(dict(self.entry, mirrors=[]), self.path)
        build.assert_called_once_with(PINNED.HTTPSOnlyRedirects)
        self.assertEqual(self.path.read_bytes(), GOOD)

    def test_android_lock_entries_are_https_and_pinned(self):
        entries = [ENGINE.LOCK["boost"], *(item for item in ENGINE.LOCK["data"] if item["path"].endswith(".dict.yaml"))]
        self.assertEqual(len(entries), 3)
        for entry in entries:
            with self.subTest(url=entry["url"]):
                for url in [entry["url"], *entry.get("mirrors", [])]:
                    parts = urllib.parse.urlsplit(url)
                    self.assertEqual((parts.scheme, bool(parts.hostname)), ("https", True))
                self.assertRegex(entry["sha256"], "^[0-9a-f]{64}$")


if __name__ == "__main__":
    unittest.main()
