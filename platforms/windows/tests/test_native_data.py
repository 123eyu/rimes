#!/usr/bin/env python3
"""Regression tests for shared-data closure and non-destructive staging."""

import importlib.util
import json
from pathlib import Path
import tempfile
import unittest
from unittest.mock import patch

SCRIPT = Path(__file__).parents[1] / "scripts/prepare-native-data.py"
SPEC = importlib.util.spec_from_file_location("native_data", SCRIPT)
native = importlib.util.module_from_spec(SPEC)
SPEC.loader.exec_module(native)


class NativeDataTests(unittest.TestCase):
    def setUp(self):
        self.temporary = tempfile.TemporaryDirectory()
        self.addCleanup(self.temporary.cleanup)
        self.root = Path(self.temporary.name)
        self.source = self.root / "source"
        self.source.mkdir()
        (self.source / "rime.lua").write_text("-- fixture\n")
        (self.source / "private.userdb").write_text("must never copy\n")
        self.runtime = self.root / "opencc"
        self.runtime.mkdir()
        self.config = self.runtime / "s2t.json"
        self.config.write_text(json.dumps({"conversion_chain": [{"dict": {
            "type": "ocd2", "file": "STCharacters.ocd2"}}]}))
        (self.runtime / "STCharacters.ocd2").write_bytes(b"fixture dictionary")
        (self.runtime / "unreferenced.ocd2").write_bytes(b"must not copy")
        self.license = self.root / "LICENSE"
        self.license.write_text("fixture license")
        self.output = self.root / "shared"
        policy = {"include": ["rime.lua"], "provenanceGroups": {}, "runtimeRequirements": []}
        result = {"dataRoot": self.source, "included": ["rime.lua"],
                  "external": ["opencc/s2t.json"], "policy": policy}
        self.addCleanup(patch.stopall)
        patch.object(native, "REPO", self.root).start()
        patch.object(native.preview, "load_policy", return_value=policy).start()
        patch.object(native.preview, "validate_repo", return_value=result).start()
        self.chord = self.root / native.CHORD_RESOURCE
        self.chord.parent.mkdir(parents=True)
        self.chord.write_text("schema:\n  schema_id: my_combo\n", encoding="utf-8")
        (self.root / "plugins.lock.json").write_text(json.dumps({
            "revision": "b" * 40,
            "files": {native.CHORD_SOURCE: native.preview.sha256_file(self.chord)}}))

    def stage(self):
        return native.stage(self.root, self.output, self.runtime, self.license, "a" * 40)

    def test_only_reviewed_and_referenced_files_are_staged(self):
        summary = self.stage()
        self.assertEqual(summary["files"], 8)
        self.assertEqual(summary["productSchemas"], list(native.PRODUCT_SCHEMAS))
        self.assertEqual((self.output / "my_combo.schema.yaml").read_bytes(), self.chord.read_bytes())
        self.assertEqual((self.output / "wubi86.custom.yaml").read_text(encoding="utf-8"), native.WINDOWS_PATCHES["wubi86.custom.yaml"])
        self.assertEqual((self.output / "lingxi_wubi.custom.yaml").read_text(encoding="utf-8"), native.WINDOWS_PATCHES["lingxi_wubi.custom.yaml"])
        self.assertEqual(native.verify(self.output), summary)
        self.assertFalse((self.output / "private.userdb").exists())
        self.assertFalse((self.output / "opencc/unreferenced.ocd2").exists())

    def test_changed_plugin_source_is_rejected_before_staging(self):
        self.chord.write_text("unreviewed schema")
        with self.assertRaisesRegex(native.preview.PreviewError, "source pin"):
            self.stage()
        self.assertFalse(self.output.exists())

    def test_chording_cannot_be_removed_from_deployment_manifest(self):
        self.stage()
        path = self.output / native.MANIFEST
        manifest = json.loads(path.read_text())
        manifest["productSchemas"].remove("my_combo")
        path.write_text(json.dumps(manifest))
        with self.assertRaisesRegex(native.preview.PreviewError, "schema set"):
            native.verify(self.output)

    def test_missing_runtime_table_leaves_no_partial_output(self):
        (self.runtime / "STCharacters.ocd2").unlink()
        with self.assertRaises(native.preview.PreviewError):
            self.stage()
        self.assertFalse(self.output.exists())

    def test_traversal_in_opencc_is_rejected(self):
        self.config.write_text(json.dumps({"conversion_chain": [{"file": "../private.txt"}]}))
        with self.assertRaises(native.preview.PreviewError):
            self.stage()
        self.assertFalse(self.output.exists())

    def test_existing_output_is_preserved(self):
        self.output.mkdir()
        marker = self.output / "keep"
        marker.write_text("existing installation")
        with self.assertRaises(native.preview.PreviewError):
            self.stage()
        self.assertEqual(marker.read_text(), "existing installation")

    def test_table_symlinks_are_rejected(self):
        table = self.runtime / "STCharacters.ocd2"
        table.unlink()
        try:
            table.symlink_to(self.license)
        except OSError:
            self.skipTest("symlinks unavailable")
        with self.assertRaises(native.preview.PreviewError):
            self.stage()

    def test_transfer_corruption_is_detected(self):
        self.stage()
        (self.output / "rime.lua").write_text("changed")
        with self.assertRaisesRegex(native.preview.PreviewError, "checksum mismatch"):
            native.verify(self.output)

    def test_extra_user_data_is_detected(self):
        self.stage()
        (self.output / "user.yaml").write_text("user data")
        with self.assertRaisesRegex(native.preview.PreviewError, "inventory mismatch"):
            native.verify(self.output)

    def test_manifest_cannot_omit_a_runtime_table(self):
        self.stage()
        path = self.output / native.MANIFEST
        manifest = json.loads(path.read_text())
        manifest["files"] = [entry for entry in manifest["files"] if not entry["path"].endswith(".ocd2")]
        path.write_text(json.dumps(manifest))
        with self.assertRaisesRegex(native.preview.PreviewError, "file count"):
            native.verify(self.output)

    def test_manifest_is_reproducible(self):
        self.stage()
        first = (self.output / native.MANIFEST).read_bytes()
        self.output = self.root / "second"
        self.stage()
        self.assertEqual(first, (self.output / native.MANIFEST).read_bytes())


if __name__ == "__main__":
    unittest.main()
