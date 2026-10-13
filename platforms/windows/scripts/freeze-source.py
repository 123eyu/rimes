#!/usr/bin/env python3
"""Record the checked-out source bytes used by both Windows architectures."""

import hashlib
import json
from pathlib import Path
import subprocess


def main():
    root = Path(__file__).resolve().parents[3]
    subprocess.run(["git", "diff", "--exit-code", "HEAD", "--"], cwd=root, check=True)
    commit = subprocess.check_output(["git", "rev-parse", "HEAD"], cwd=root, text=True).strip()
    names = subprocess.check_output(
        ["git", "ls-files", "--recurse-submodules", "-z"], cwd=root
    ).decode("utf-8").rstrip("\0").split("\0")
    files = []
    for name in sorted(names):
        path = root / name
        if path.is_symlink():
            files.append({"path": name, "symlink": path.readlink().as_posix()})
            continue
        if not path.is_file():
            raise RuntimeError(f"Source is not a regular file: {name}")
        data = path.read_bytes()
        files.append({"path": name, "bytes": len(data), "sha256": hashlib.sha256(data).hexdigest()})
    manifest = (json.dumps({"commit": commit, "files": files}, ensure_ascii=False,
                           sort_keys=True, separators=(",", ":")) + "\n").encode("utf-8")
    digest = hashlib.sha256(manifest).hexdigest()
    (root / "SOURCE-FILES.json").write_bytes(manifest)
    (root / "SOURCE-COMMIT.txt").write_text(commit + "\n", encoding="ascii")
    (root / "SOURCE-SNAPSHOT.txt").write_text(digest + "\n", encoding="ascii")
    print(json.dumps({"commit": commit, "sourceSnapshot": digest, "files": len(files)}))


if __name__ == "__main__":
    main()
