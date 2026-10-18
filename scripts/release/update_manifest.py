#!/usr/bin/env python3
"""Create the update feed only after four tested release packages are present."""
import argparse
import hashlib
import json
from pathlib import Path
import re


def manifest(root: Path, version: str, commit: str, android_build: int):
    if not re.fullmatch(r"(0|[1-9]\d*)\.(0|[1-9]\d*)\.(0|[1-9]\d*)", version):
        raise ValueError("Stable version required")
    if not re.fullmatch(r"[0-9a-f]{40}", commit) or android_build <= 0:
        raise ValueError("Source identity and Android build number required")
    result = {"schemaVersion": 1, "version": version, "sourceCommit": commit, "platforms": {},
              "ios": {"channel": "app-store", "url": "https://apps.apple.com/app/id6814620242"}}
    names = {"macos": f"RIMES-{version}.pkg", "windows": f"RIMES-Windows-{version}-Setup.exe",
             "linux": f"fcitx5-rimes_{version}_amd64.deb", "android": f"RIMES-Android-{version}.apk"}
    for platform, name in names.items():
        file = root / name
        if file.is_symlink() or not file.is_file() or not 0 < file.stat().st_size <= 512 * 1024 * 1024:
            raise ValueError(f"Missing, empty or unsafe package: {name}")
        digest = hashlib.sha256()
        with file.open("rb") as stream:
            for block in iter(lambda: stream.read(1024 * 1024), b""): digest.update(block)
        result["platforms"][platform] = {"url": f"https://github.com/scholay/xsime/releases/download/v{version}/{name}",
                                        "size": file.stat().st_size, "sha256": digest.hexdigest()}
    result["platforms"]["macos"].update(architecture="universal", minimumOS="13.0")
    result["platforms"]["windows"].update(architecture="x64-x86", minimumOS="10.0")
    result["platforms"]["linux"].update(architecture="amd64", distribution="ubuntu-24.04")
    result["platforms"]["android"].update(build=android_build, minimumOS=26, architecture="arm64-v8a,x86_64")
    return result


if __name__ == "__main__":
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("directory", type=Path)
    parser.add_argument("--version", required=True)
    parser.add_argument("--commit", required=True)
    parser.add_argument("--android-build", required=True, type=int)
    args = parser.parse_args()
    data = manifest(args.directory, args.version, args.commit, args.android_build)
    (args.directory / "update-manifest.json").write_text(json.dumps(data, indent=2) + "\n")
