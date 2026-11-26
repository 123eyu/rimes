#!/usr/bin/env python3
"""Fetch the original compatibility fixture; never rewrite its source files."""
import argparse
import hashlib
from pathlib import Path
import sys
import time
import urllib.error
import urllib.request

URL = 'https://github.com/amzxyz/rime-wanxiang/releases/download/v18.1.1/rime-wanxiang-base.zip'
SHA256 = 'abd00fd2b4135453e53b81d8ac893301e808fdb52290ea72977714dfc67400a2'
ATTEMPTS = 4


def digest(path):
    with path.open('rb') as source:
        return hashlib.file_digest(source, 'sha256').hexdigest()


def download(temporary):
    with urllib.request.urlopen(URL, timeout=60) as response, temporary.open('wb') as output:
        size = 0
        while block := response.read(1024 * 1024):
            size += len(block)
            if size > 256 * 1024 * 1024:
                raise RuntimeError('Fixture exceeds the original archive limit')
            output.write(block)


def fetch(path):
    if path.is_file() and digest(path) == SHA256:
        return path
    path.parent.mkdir(parents=True, exist_ok=True)
    temporary = path.with_suffix('.download')
    try:
        for attempt in range(1, ATTEMPTS + 1):
            try:
                download(temporary)
                break
            except (urllib.error.URLError, TimeoutError) as error:
                if attempt == ATTEMPTS:
                    raise
                print(f'fixture download attempt {attempt} failed: {error}', file=sys.stderr)
                time.sleep(attempt * 5)
        if digest(temporary) != SHA256:
            raise RuntimeError('Upstream fixture SHA-256 changed; review the new source instead of rewriting this ZIP')
        temporary.replace(path)
    finally:
        temporary.unlink(missing_ok=True)
    return path


if __name__ == '__main__':
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('output', type=Path)
    args = parser.parse_args()
    print(fetch(args.output).resolve())
