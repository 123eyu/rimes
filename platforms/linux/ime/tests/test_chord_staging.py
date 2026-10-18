import importlib.util
import json
from pathlib import Path
import tempfile
import unittest


REPO = Path(__file__).resolve().parents[4]
SPEC = importlib.util.spec_from_file_location('linux_stage_data', REPO / 'platforms/linux/ime/scripts/stage-data.py')
STAGE = importlib.util.module_from_spec(SPEC)
SPEC.loader.exec_module(STAGE)


class ChordDataStaging(unittest.TestCase):
    def test_native_presets_match_reviewed_source(self):
        lock = STAGE.verified_presets(REPO)
        self.assertEqual(lock['schemas'], ['yoyo-yx', 'yoyo-yx-hm'])
        self.assertEqual(len(lock['files']), 12)

    def test_changed_file_cannot_silently_ship(self):
        with tempfile.TemporaryDirectory() as temporary:
            root = Path(temporary)
            policy = root / 'scripts/platform-preview/policy.json'
            policy.parent.mkdir(parents=True)
            policy.write_bytes((REPO / 'scripts/platform-preview/policy.json').read_bytes())
            notice = root / 'rime-data/licenses/yoyo-SOURCE.md'
            notice.parent.mkdir(parents=True)
            notice.write_text('unexpected replacement')
            with self.assertRaisesRegex(ValueError, 'data changed'):
                STAGE.verified_presets(root)

    def test_full_stage_keeps_default_selection_and_exact_inventory(self):
        with tempfile.TemporaryDirectory() as temporary:
            output = Path(temporary) / 'data'
            STAGE.stage(REPO, output)
            policy = json.loads((REPO / 'scripts/platform-preview/policy.json').read_text())
            lock = json.loads((output / 'linux-native-chord-manifest.json').read_text())
            expected = set(policy['include']) | set(lock['files']) | {'linux-native-chord-manifest.json'}
            actual = {str(path.relative_to(output)) for path in output.rglob('*') if path.is_file()}
            self.assertEqual(actual, expected)
            for name in ('default.yaml', 'default.custom.yaml'):
                self.assertEqual((output / name).read_bytes(), (REPO / 'rime-data' / name).read_bytes())
            for name in lock['files']:
                self.assertEqual((output / name).read_bytes(), (REPO / 'rime-data' / name).read_bytes())


if __name__ == '__main__':
    unittest.main()
