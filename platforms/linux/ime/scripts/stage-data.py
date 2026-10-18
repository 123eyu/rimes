#!/usr/bin/env python3
"""Stage the Linux full edition, including the two bundled native chord presets."""
import argparse
import hashlib
import json
from pathlib import Path
import shutil
import subprocess
import sys


ROOT = Path(__file__).resolve().parents[4]
LOCK = ROOT / 'platforms/linux/chord/resources/native-presets.lock.json'


def verified_presets(repo):
    lock = json.loads(LOCK.read_text())
    policy = json.loads((repo / 'scripts/platform-preview/policy.json').read_text())
    files = lock['files']
    if lock.get('formatVersion') != 1 or set(files) != set(policy['exclude']['nativeChordSchemes']):
        raise ValueError('Native chord data inventory changed; review and update the Linux lock')
    for name, digest in files.items():
        source = repo / 'rime-data' / name
        if source.is_symlink() or not source.is_file() or source.stat().st_size > 32 * 1024 * 1024:
            raise ValueError('Invalid native chord data file: ' + name)
        if hashlib.sha256(source.read_bytes()).hexdigest() != digest:
            raise ValueError('Native chord data changed; review and update the Linux lock: ' + name)
    return lock


def stage(repo, output):
    lock = verified_presets(repo)
    # The shared validator also rejects symlinks and unclassified files anywhere
    # in rime-data. Keep the existing data-only preview inventory unchanged.
    subprocess.run([sys.executable, str(repo / 'scripts/platform-preview/preview.py'),
                    'stage', '--repo-root', str(repo), '--output-dir', str(output)], check=True)
    for name in lock['files']:
        destination = output / name
        destination.parent.mkdir(parents=True, exist_ok=True)
        shutil.copyfile(repo / 'rime-data' / name, destination, follow_symlinks=False)
        if hashlib.sha256(destination.read_bytes()).hexdigest() != lock['files'][name]:
            raise ValueError('Staged native chord file differs: ' + name)
    (output / 'linux-native-chord-manifest.json').write_text(
        json.dumps(lock, ensure_ascii=False, indent=2, sort_keys=True) + '\n')
    print(f'Linux full data staged; {len(lock["files"])} verified native chord files added')


if __name__ == '__main__':
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--repo-root', type=Path, default=ROOT)
    parser.add_argument('--output-dir', type=Path, required=True)
    args = parser.parse_args()
    try:
        stage(args.repo_root, args.output_dir)
    except (ValueError, OSError, subprocess.CalledProcessError) as error:
        raise SystemExit(str(error)) from error
