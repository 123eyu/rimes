"""Recovery must not turn unapproved, cancelled or different-source runs into releases."""
import copy
import hashlib
from pathlib import Path
import stat
import tempfile
import unittest
import zipfile

from recover_update_assets import authorize, extract_verified, REQUIRED_JOBS


class RecoveryTests(unittest.TestCase):
    def setUp(self):
        commit = "a" * 40
        self.args = [
            {"id": 123, "repository": {"full_name": "scholay/xsime"},
             "head_repository": {"full_name": "scholay/xsime"},
             "path": ".github/workflows/release.yml", "event": "push",
             "status": "completed", "conclusion": "success", "head_branch": "v1.1.6",
             "head_sha": commit},
            [{"name": name, "status": "completed", "conclusion": "success"}
             for name in REQUIRED_JOBS] +
            [{"name": "Passively verify and publish unsigned preview", "status": "completed",
              "conclusion": "skipped"}],
            [{"id": index, "name": f"RIMES-{platform}-release-{commit}", "expired": False,
              "size_in_bytes": 1024, "digest": "sha256:" + "b" * 64,
              "workflow_run": {"id": 123, "head_sha": commit}}
             for index, platform in enumerate(("Windows", "Android"), start=1)],
            {"id": 456, "tag_name": "v1.1.6", "draft": False, "prerelease": False,
             "assets": [{"name": name, "state": "uploaded"} for name in
                        ("RIMES-1.1.6.pkg", "SHA256SUMS", "fcitx5-rimes_1.1.6_amd64.deb",
                         "fcitx5-rimes_1.1.6_amd64.deb.sha256")]},
            {"id": 456}, commit,
        ]

    def test_completed_approved_source_accepts_intentionally_skipped_preview(self):
        plan = authorize(*self.args)
        self.assertEqual(plan["commit"], "a" * 40)
        self.assertEqual(set(plan["artifacts"]), {"windows", "android"})

    def test_every_required_job_must_have_run_and_passed(self):
        for name in REQUIRED_JOBS:
            for state in ("skipped", "failure", "cancelled"):
                with self.subTest(name=name, state=state):
                    args = copy.deepcopy(self.args)
                    next(job for job in args[1] if job["name"] == name)["conclusion"] = state
                    with self.assertRaises(ValueError):
                        authorize(*args)

    def test_source_and_release_identity_fail_closed(self):
        mutations = [
            lambda a: a[0].update(event="pull_request"),
            lambda a: a[0].update(path=".github/workflows/ci.yml"),
            lambda a: a[0].update(conclusion="cancelled"),
            lambda a: a[0].update(status="in_progress"),
            lambda a: a[0].update(head_branch="v1.1.6-preview.1"),
            lambda a: a[0]["head_repository"].update(full_name="someone/fork"),
            lambda a: a.__setitem__(5, "c" * 40),
            lambda a: a[3].update(draft=True),
            lambda a: a[3].update(prerelease=True),
            lambda a: a[3].update(tag_name="v1.1.5"),
            lambda a: a[3].update(assets=[]),
            lambda a: a[4].update(id=999),
            lambda a: a[2][0].update(expired=True),
            lambda a: a[2][0].update(digest=""),
            lambda a: a[2][0]["workflow_run"].update(id=124),
            lambda a: a[2][0]["workflow_run"].update(head_sha="c" * 40),
            lambda a: a[2].append(copy.deepcopy(a[2][0])),
        ]
        for index, mutate in enumerate(mutations):
            with self.subTest(index=index):
                args = copy.deepcopy(self.args)
                mutate(args)
                with self.assertRaises(ValueError):
                    authorize(*args)

    def test_archive_hash_paths_and_symlinks(self):
        for name, mode in (("BUILD-INFO.json", stat.S_IFREG),
                           ("../escape", stat.S_IFREG), ("/escape", stat.S_IFREG),
                           ("link", stat.S_IFLNK)):
            with self.subTest(name=name), tempfile.TemporaryDirectory() as directory:
                root = Path(directory)
                archive = root / "source.zip"
                with zipfile.ZipFile(archive, "w") as bundle:
                    info = zipfile.ZipInfo(name)
                    info.external_attr = (mode | 0o644) << 16
                    bundle.writestr(info, b"fixture")
                digest = hashlib.sha256(archive.read_bytes()).hexdigest()
                with self.assertRaises(ValueError):
                    extract_verified(archive, root / "bad-digest", "0" * 64)
                if name == "BUILD-INFO.json":
                    extract_verified(archive, root / "valid", digest)
                    self.assertEqual((root / "valid" / name).read_bytes(), b"fixture")
                else:
                    with self.assertRaises(ValueError):
                        extract_verified(archive, root / "unsafe", digest)
                    self.assertFalse((root / "unsafe").exists())


if __name__ == "__main__":
    unittest.main()
