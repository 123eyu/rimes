#!/usr/bin/env python3
"""Index actual dual-platform comparison artifacts without granting approval."""
import argparse
from collections import Counter
import hashlib
import json
from pathlib import Path


def digest(path):
    return hashlib.sha256(path.read_bytes()).hexdigest()


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--repo", type=Path, default=Path(__file__).resolve().parents[3])
    parser.add_argument("--output", type=Path, required=True)
    args = parser.parse_args()
    repo = args.repo.resolve()
    reports, statuses = [], Counter()
    for manifest in sorted((repo / "validation").glob("linux-*/**/manifest.json")):
        if not {"comparison", "compare", "diff"}.intersection(manifest.relative_to(repo).parts):
            continue
        entries = json.loads(manifest.read_text())
        if not isinstance(entries, list) or not entries:
            raise ValueError(f"empty/invalid comparison manifest: {manifest}")
        pages = []
        for row in entries:
            status = row["status"]
            files = {}
            for key in ("pair", "difference_4x"):
                image = manifest.parent / row[key]
                if image.is_symlink() or not image.is_file() or image.resolve().parent != manifest.parent.resolve():
                    raise ValueError(f"missing/nonlocal comparison image: {image}")
                files[key] = {"path": str(image.relative_to(repo)), "sha256": digest(image)}
            pages.append({"page": row["page"], "status": status, "files": files})
            statuses[status] += 1
        reports.append({"manifest": str(manifest.relative_to(repo)), "sha256": digest(manifest), "pages": pages})
    result = {
        "schemaVersion": 1,
        "scope": "Existing comparison manifests only; this is not complete state coverage or visual acceptance.",
        "deferredTask": 25,
        "manifestCount": len(reports),
        "pairCount": sum(statuses.values()),
        "statusCounts": dict(statuses),
        "acceptance": "pending: review each discrepancy and cover missing interaction states on the target desktops",
        "reports": reports,
    }
    args.output.parent.mkdir(parents=True, exist_ok=True)
    args.output.write_text(json.dumps(result, ensure_ascii=False, indent=2) + "\n")
    print(f"{len(reports)} comparison manifests; {sum(statuses.values())} pairs; statuses={dict(statuses)}")


if __name__ == "__main__":
    main()
