"""Fetch pinned build inputs over HTTPS and keep only bytes that match the lock.

platforms/ios/scripts/bootstrap.py (Boost, XcodeGen, data files) and
platforms/android/scripts/build-engine.py (Boost, dictionaries) fetch their
dependencies.lock.json entries with `fetch`. An entry has a `url`, optional `mirrors`
and a `sha256`. The SHA-256 is the trust anchor: a mirror is only another transport
for the same bytes, and nothing that fails the check is kept.
Network-free tests: platforms/android/scripts/test_pinned_download.py.
"""
import hashlib, http.client, os, sys, time, urllib.error, urllib.parse, urllib.request

TIMEOUT = 30           # seconds for each connect or read
WINDOW = 120           # an attempt is dropped when a WINDOW-second stretch averages under MIN_RATE
MIN_RATE = 1 << 10     # bytes per second: a stuck or trickling server goes, a slow link stays
ATTEMPTS = 3           # per source; each pass tries every source once, in lock order
BACKOFF = 5            # seconds before the second pass, doubled before each later one
MAX_BYTES = 256 << 20  # the largest pinned input, Boost 1.86.0, is 126,220,652 bytes

class DownloadError(Exception):
    pass

class HTTPSOnlyRedirects(urllib.request.HTTPRedirectHandler):
    def redirect_request(self, req, fp, code, msg, headers, newurl):
        if urllib.parse.urlsplit(newurl).scheme != 'https':
            raise urllib.error.HTTPError(newurl, code, f'refused non-HTTPS redirect to {newurl}', headers, fp)
        return super().redirect_request(req, fp, code, msg, headers, newurl)

def sha256(path):
    digest = hashlib.sha256()
    with open(path, 'rb') as source:
        while block := source.read(1 << 20): digest.update(block)
    return digest.hexdigest()

def download(opener, url, path, expected, clock=time.monotonic):
    """One attempt: stream to a side file and move it into place only on a SHA-256 match."""
    partial = path.with_name(path.name + '.download')
    try:
        digest, size = hashlib.sha256(), 0
        mark, marked = clock(), 0  # start time and size of the current rate window
        with partial.open('wb') as output, opener.open(url, timeout=TIMEOUT) as response:
            declared = response.headers.get('Content-Length') or ''
            # read1 returns what one socket read delivers, so the rate check also runs
            # while a server trickles bytes just inside TIMEOUT; read() would keep waiting.
            while block := response.read1(1 << 16):
                size += len(block)
                if size > MAX_BYTES: raise DownloadError(f'response is larger than {MAX_BYTES} bytes')
                if (now := clock()) - mark >= WINDOW:
                    if size - marked < MIN_RATE * (now - mark):
                        raise DownloadError(f'received under {MIN_RATE / 1024:g} KiB/s for {now - mark:.0f} s')
                    mark, marked = now, size
                digest.update(block)
                output.write(block)
        if digest.hexdigest() != expected:
            # The hash decides; the declared length only explains a cut-off transfer.
            if declared.isdigit() and int(declared) != size: raise DownloadError(f'received {size} of {declared} bytes')
            raise DownloadError(f'SHA-256 {digest.hexdigest()} does not match the lock')
        os.replace(partial, path)
    finally:
        partial.unlink(missing_ok=True)

def fetch(entry, path, opener=None, sleep=time.sleep, clock=time.monotonic):
    """Return `path` holding exactly the locked bytes of `entry`; a mismatching file is never kept."""
    sources = [entry['url'], *entry.get('mirrors', [])]
    for url in sources:
        parts = urllib.parse.urlsplit(url)
        if parts.scheme != 'https' or not parts.hostname:
            raise RuntimeError(f'Pinned downloads must be HTTPS URLs: {url!r}')
    if path.is_file():
        if sha256(path) == entry['sha256']: return path
        print(f'Discarding {path}: SHA-256 does not match the lock', file=sys.stderr)
        path.unlink()
    path.parent.mkdir(parents=True, exist_ok=True)
    opener = opener or urllib.request.build_opener(HTTPSOnlyRedirects)
    failures = []
    for attempt in range(1, ATTEMPTS + 1):
        if attempt > 1: sleep(BACKOFF * 2 ** (attempt - 2))
        for url in sources:
            try:
                download(opener, url, path, entry['sha256'], clock)
                if failures: print(f'Downloaded {path.name} from {url}', file=sys.stderr)
                return path
            except (OSError, http.client.HTTPException, DownloadError) as error:
                if isinstance(error, urllib.error.HTTPError): error.close()  # release its connection now
                failures.append(f'{url} (attempt {attempt}/{ATTEMPTS}): {error}')
                print(f'Download failed: {failures[-1]}', file=sys.stderr)
    raise RuntimeError(f'No source served {path.name} with the locked SHA-256:\n  ' + '\n  '.join(failures))
