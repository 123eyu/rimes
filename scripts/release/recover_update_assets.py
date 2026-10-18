#!/usr/bin/env python3
"""Authenticate and download artifacts for an already approved stable release.

This read-only preparation never creates releases, executes packages, or signs.
The caller publishes with the same append-only script used by the tag workflow.
"""
import argparse
import hashlib
import json
import os
from pathlib import Path
import re
import shutil
import stat
import subprocess
import zipfile

REPO = "scholay/xsime"
PUBLISHER = "Publish verified Android, Windows and complete update feed"
REQUIRED_JOBS = {
    "Build and runtime-smoke macOS without secrets",
    "Sign, verify, and stage macOS",
    "Install the exact signed package on a disposable Mac",
    "Approve and publish staged macOS release",
    "Attach verified Linux deb to the same release",
    "build_linux_deb / Ubuntu 24.04 amd64 deb installation and removal",
    "build_windows_release / Release x64",
    "build_windows_release / Release x86",
    "build_windows_release / Complete Windows installer",
    "build_windows_release / IME typing x64",
    "build_windows_release / IME typing x86",
    "build_android_release / build",
    "build_android_release / sign-release",
}


def require(condition, message):
    if not condition:
        raise ValueError(message)


def authorize(run, jobs, artifacts, release, latest, tag_commit):
    require(run["repository"]["full_name"] == REPO and
            run["head_repository"]["full_name"] == REPO, "Wrong source repository")
    require(run["path"] == ".github/workflows/release.yml" and
            run["event"] == "push", "Source must be the canonical tag workflow")
    require(run["status"] == "completed" and run["conclusion"] in {"success", "failure"},
            "Source run is unfinished or cancelled")
    tag, commit = run["head_branch"], run["head_sha"]
    require(re.fullmatch(r"v(0|[1-9]\d*)\.(0|[1-9]\d*)\.(0|[1-9]\d*)", tag),
            "Stable tag required")
    require(re.fullmatch(r"[0-9a-f]{40}", commit) and tag_commit == commit,
            "Tag no longer identifies the verified source")
    by_name = {job["name"]: job for job in jobs}
    require(len(by_name) == len(jobs), "Ambiguous job names")
    for name in REQUIRED_JOBS:
        job = by_name.get(name, {})
        require(job.get("status") == "completed" and job.get("conclusion") == "success",
                f"Required source job did not pass: {name}")
    require(all(job["conclusion"] in {"success", "skipped"} or job["name"] == PUBLISHER
                for job in jobs), "A source job failed outside final asset publication")
    require(release["tag_name"] == tag and not release["draft"] and not release["prerelease"],
            "Approved public stable release required")
    require(release["id"] == latest["id"], "Only the latest release may be completed")
    version = tag[1:]
    names = {asset["name"] for asset in release["assets"] if asset["state"] == "uploaded"}
    require({f"RIMES-{version}.pkg", "SHA256SUMS", f"fcitx5-rimes_{version}_amd64.deb",
             f"fcitx5-rimes_{version}_amd64.deb.sha256"} <= names,
            "Approved macOS and Linux assets must already be public")
    selected = {}
    for platform in ("Windows", "Android"):
        name = f"RIMES-{platform}-release-{commit}"
        matches = [asset for asset in artifacts if asset["name"] == name]
        require(len(matches) == 1, f"Missing or ambiguous artifact: {name}")
        asset = matches[0]
        require(not asset["expired"] and 0 < asset["size_in_bytes"] <= 512 * 1024 * 1024,
                f"Expired or oversized artifact: {name}")
        require(asset["workflow_run"]["id"] == run["id"] and
                asset["workflow_run"]["head_sha"] == commit, "Artifact source mismatch")
        require(re.fullmatch(r"sha256:[0-9a-f]{64}", asset.get("digest", "")),
                "Authenticated artifact digest required")
        selected[platform.lower()] = asset
    return {"version": version, "commit": commit, "tag": tag, "artifacts": selected}


def extract_verified(archive, destination, digest):
    with archive.open("rb") as stream:
        require(hashlib.file_digest(stream, "sha256").hexdigest() == digest,
                "Artifact archive digest mismatch")
    with zipfile.ZipFile(archive) as bundle:
        members = bundle.infolist()
        require(0 < len(members) <= 32 and
                sum(member.file_size for member in members) <= 1024 * 1024 * 1024,
                "Unexpected artifact size or member count")
        names = set()
        for member in members:
            name, mode = member.filename, member.external_attr >> 16
            require(name not in names and name not in {"", ".", ".."} and
                    "/" not in name and "\\" not in name and
                    (stat.S_IFMT(mode) in {0, stat.S_IFREG}) and not member.is_dir(),
                    "Artifact must contain unique regular files at its root")
            names.add(name)
        destination.mkdir()
        for member in members:
            with bundle.open(member) as source, (destination / member.filename).open("xb") as target:
                shutil.copyfileobj(source, target)


def api(path):
    return json.loads(subprocess.check_output(["gh", "api", path], text=True))


def pages(path, key):
    data = json.loads(subprocess.check_output(["gh", "api", "--paginate", "--slurp", path], text=True))
    return [item for page in data for item in page[key]]


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--run-id", type=int, required=True)
    args = parser.parse_args()
    require(args.run_id > 0, "Positive source run ID required")
    prefix = f"repos/{REPO}"
    run = api(f"{prefix}/actions/runs/{args.run_id}")
    # Validate tag syntax before placing it into an API path.
    tag = run["head_branch"]
    require(re.fullmatch(r"v\d+\.\d+\.\d+", tag), "Stable source tag required")
    plan = authorize(
        run, pages(f"{prefix}/actions/runs/{args.run_id}/jobs?per_page=100", "jobs"),
        pages(f"{prefix}/actions/runs/{args.run_id}/artifacts?per_page=100", "artifacts"),
        api(f"{prefix}/releases/tags/{tag}"), api(f"{prefix}/releases/latest"),
        api(f"{prefix}/commits/{tag}")["sha"],
    )
    root = Path(os.environ["RUNNER_TEMP"])
    (root / "release-completion-evidence.json").write_text(json.dumps(plan, indent=2) + "\n")
    for platform, artifact in plan["artifacts"].items():
        archive = root / f"{platform}-artifact.zip"
        with archive.open("xb") as stream:
            subprocess.run(["gh", "api", f"{prefix}/actions/artifacts/{artifact['id']}/zip"],
                           stdout=stream, check=True)
        extract_verified(archive, root / f"{platform}-release", artifact["digest"][7:])
    with Path(os.environ["GITHUB_OUTPUT"]).open("a") as stream:
        for key in ("version", "commit", "tag"):
            stream.write(f"{key}={plan[key]}\n")
    print(f"Authenticated source run {args.run_id}, tag {tag}, commit {plan['commit']}")


if __name__ == "__main__":
    main()
