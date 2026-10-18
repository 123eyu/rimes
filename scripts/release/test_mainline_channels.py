"""Static guardrails for one mainline with independent platform channels."""

from pathlib import Path
import re
import unittest


ROOT = Path(__file__).resolve().parents[2]


def workflow(name):
    return (ROOT / ".github/workflows" / name).read_text()


def event_section(text, event):
    """Read our two-space top-level event blocks without a YAML dependency."""
    match = re.search(
        rf"^  {re.escape(event)}:\n(.*?)(?=^  [a-z_]+:|^permissions:)",
        text,
        re.MULTILINE | re.DOTALL,
    )
    if match is None:
        raise AssertionError(f"Missing event block: {event}")
    return match.group(1)


class MainlineChannelTests(unittest.TestCase):
    def test_platform_checks_follow_main_not_retired_feature_branches(self):
        for name in ("ios.yml", "android.yml", "linux-ime.yml", "windows-ime.yml"):
            with self.subTest(workflow=name):
                text = workflow(name)
                for event in ("push", "pull_request"):
                    block = event_section(text, event)
                    self.assertRegex(block, r"branches:\s*(?:\[main\]|\n\s+- main\n)")
                    if event == "pull_request":
                        self.assertIn("paths:", block)
                    else:
                        # Stable tags require every platform's push evidence
                        # for the exact main SHA, including one-platform fixes.
                        self.assertNotRegex(block, r"\bpaths(?:-ignore)?:")
                trigger = text.split("permissions:", 1)[0]
                self.assertNotIn("cursor/", trigger)
                self.assertNotIn("codex/ios-", trigger)

    def test_native_checks_only_publish_ephemeral_artifacts(self):
        for name in ("linux-ime.yml", "windows-ime.yml"):
            with self.subTest(workflow=name):
                text = workflow(name)
                self.assertIn("contents: read", text)
                self.assertIn("actions/upload-artifact@", text)
                self.assertIn("${{ github.sha }}", text)
                self.assertNotIn("gh release create", text)
                self.assertNotIn("contents: write", text)
                self.assertNotIn("tags:", text.split("permissions:", 1)[0])

    def test_ios_publication_requires_a_separate_tag_and_mainline_commit(self):
        text = workflow("ios-release.yml")
        self.assertIn("tags: ['ios-v*']", text)
        self.assertIn("fetch-depth: 0", text)
        guard = "git merge-base --is-ancestor HEAD origin/main"
        self.assertIn(guard, text)
        self.assertLess(text.index(guard), text.index("Check required signing credentials"))
        self.assertNotIn("workflow_dispatch:", text.split("permissions:", 1)[0])

    def test_shared_dictionary_changes_get_pr_evidence(self):
        for name in ("ios.yml", "windows-ime.yml", "linux-ime.yml"):
            with self.subTest(workflow=name):
                block = event_section(workflow(name), "pull_request")
                self.assertRegex(block, r"['\"]rime-data/\*\*['\"]")
                if name != "ios.yml":
                    self.assertRegex(block, r"['\"]scripts/platform-preview/\*\*['\"]")

    def test_linux_deb_is_required_and_attached_only_after_project_publication(self):
        text = workflow("release.yml")
        self.assertIn('needs: [build_and_smoke, build_linux_deb]', text)
        publish = text.split('  publish_staged_release:', 1)[1].split('\n  attach_linux_deb:', 1)[0]
        self.assertIn('needs: [build_and_smoke, sign_and_stage, build_linux_deb, build_windows_release, build_android_release, verify_signed_install]', publish)
        attach = text.split('  attach_linux_deb:', 1)[1]
        self.assertIn("needs.publish_unsigned_preview.result == 'success'", attach)
        self.assertIn("needs.publish_staged_release.result == 'success'", attach)
        self.assertIn("github.event_name == 'push'", attach)
        self.assertIn('gh release upload', attach)
        self.assertIn('gh release download', attach)
        self.assertNotIn('gh release create', attach)
        self.assertNotIn('--clobber', attach)
        package = workflow('linux-deb.yml')
        self.assertIn('tests/deb_lifecycle.sh', package)
        self.assertNotIn('contents: write', package)

    def test_complete_feed_ignores_only_the_alternate_preview_skip(self):
        job = workflow('release.yml').split('  publish_update_manifest:', 1)[1].split(
            '\n  verify_signed_install:', 1)[0]
        gate = job.split('    if: >-', 1)[1].split('    runs-on:', 1)[0]
        self.assertIn('!cancelled()', gate)
        self.assertIn("needs.build_and_smoke.outputs.is_release == 'true'", gate)
        for producer in ('build_and_smoke', 'build_windows_release', 'build_android_release',
                         'publish_staged_release', 'attach_linux_deb'):
            self.assertIn(f"needs.{producer}.result == 'success'", gate)
        self.assertNotIn('publish_unsigned_preview', gate)
        self.assertIn('bash scripts/release/publish-update-assets.sh', job)


if __name__ == "__main__":
    unittest.main()
