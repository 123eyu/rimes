#!/usr/bin/env python3
"""Fetch the newest strictly older deb from this project's published v* releases.

Print its local path, or print nothing when no older package exists. API and
integrity failures are fatal; they must not silently become fixture coverage.
"""
import argparse
import functools
import hashlib
import json
from pathlib import Path
import re
import subprocess
import sys
import tempfile

REPOSITORY = "scholay/xsime"
TAG = re.compile(r"v((?:0|[1-9][0-9]*)\.(?:0|[1-9][0-9]*)\.(?:0|[1-9][0-9]*)(?:-preview\.[1-9][0-9]*)?)")


def older(left, right):
    result = subprocess.run(["dpkg", "--compare-versions", left, "lt", right], check=False)
    if result.returncode not in (0, 1):
        raise ValueError("Invalid Debian version comparison")
    return result.returncode == 0


def select_release(releases, version, architecture):
    candidates = []
    for release in releases:
        match = TAG.fullmatch(release.get("tag_name", ""))
        if release.get("draft") or not release.get("published_at") or not match:
            continue
        package_version = match[1].replace("-preview.", "~preview.")
        name = f"fcitx5-rimes_{package_version}_{architecture}.deb"
        names = {asset["name"] for asset in release.get("assets", [])}
        if {name, name + ".sha256"} <= names and older(package_version, version):
            candidates.append((package_version, release["tag_name"], name))
    compare = lambda a, b: -1 if older(a[0], b[0]) else 1 if older(b[0], a[0]) else 0
    return max(candidates, key=functools.cmp_to_key(compare)) if candidates else None


def field(package, name):
    return subprocess.check_output(["dpkg-deb", "-f", str(package), name], text=True).strip()


def verify(package, version, architecture):
    checksum = package.with_name(package.name + ".sha256").read_text()
    match = re.fullmatch(r"([0-9a-f]{64}) [ *]" + re.escape(package.name) + r"\n?", checksum)
    if not match or hashlib.sha256(package.read_bytes()).hexdigest() != match[1]:
        raise ValueError("Previous release checksum mismatch")
    if (field(package, "Package"), field(package, "Version"), field(package, "Architecture")) != (
            "fcitx5-rimes", version, architecture):
        raise ValueError("Previous release package metadata disagrees with its asset name")


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("current", type=Path)
    parser.add_argument("output", type=Path)
    args = parser.parse_args()
    if field(args.current, "Package") != "fcitx5-rimes":
        raise ValueError("Expected a fcitx5-rimes package")
    version, architecture = field(args.current, "Version"), field(args.current, "Architecture")
    pages = json.loads(subprocess.check_output([
        "gh", "api", f"repos/{REPOSITORY}/releases?per_page=100", "--paginate", "--slurp"], text=True))
    selected = select_release([release for page in pages for release in page], version, architecture)
    if selected is None:
        print(f"No published older {architecture} deb exists; use the controlled upgrade fixture.", file=sys.stderr)
        return
    old_version, tag, name = selected
    output = args.output.resolve()
    output.mkdir(parents=True, exist_ok=True)
    with tempfile.TemporaryDirectory(prefix=".download-", dir=output) as directory:
        subprocess.run(["gh", "release", "download", tag, "--repo", REPOSITORY,
                        "--pattern", name, "--pattern", name + ".sha256", "--dir", directory], check=True)
        package = Path(directory) / name
        verify(package, old_version, architecture)
        for filename in (name, name + ".sha256"):
            destination = output / filename
            if destination.exists():
                raise FileExistsError(destination)
        for filename in (name, name + ".sha256"):
            (Path(directory) / filename).rename(output / filename)
    print(f"Historical upgrade input: {tag}, {name}", file=sys.stderr)
    print(output / name)


if __name__ == "__main__":
    main()
