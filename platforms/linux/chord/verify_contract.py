#!/usr/bin/env python3
"""Check the macOS keymap compiler and rule fixtures before a Linux build."""
import argparse
import hashlib
import json
from pathlib import Path

ROOT = Path(__file__).resolve().parents[3]
DEST = Path(__file__).resolve().parent / 'resources' / 'native-chord.json'
parser = argparse.ArgumentParser(description=__doc__)
parser.add_argument('--import-fixture', type=Path)
parser.add_argument('--output', type=Path)
args = parser.parse_args()
value = json.loads((args.import_fixture or DEST).read_text())
expected = {
    'OfficialPlugins/native/macos/' + name + '.swift' for name in
    ['ChordKeymap', 'ChordController', 'ChordExtension', 'ChordOutputEncoding', 'NativeChordScheme', 'FlyChordLearning',
     'FlyChordLearningSettingsViewController', 'ChordKeymapEditorViewController', 'ChordKeymapActivation']
} | {'Sources/RimeBuffer/ChordLinuxFixtures.swift', 'Sources/RimeBuffer/YoyoEngineSmoke.swift',
     'Sources/RimeBuffer/BuiltInPlugins.swift', 'rime-data/my_combo.schema.yaml'}
if value.get('schemaVersion') != 1 or value.get('purpose') != 'public-chord-contract' or set(value.get('sources', {})) != expected:
    raise SystemExit('Expected an actual macOS chord fixture export')
for name, digest in value['sources'].items():
    if hashlib.sha256((ROOT / name).read_bytes()).hexdigest() != digest:
        raise SystemExit('Native chord source changed; regenerate and review: ' + name)
if args.import_fixture:
    temporary = DEST.with_suffix('.json.edit')
    temporary.write_text(json.dumps(value, ensure_ascii=False, indent=2) + '\n')
    temporary.replace(DEST)
if args.output:
    args.output.parent.mkdir(parents=True, exist_ok=True)
    args.output.write_text('// Generated from the actual native keymap compiler.\n#pragma once\n'
                          'namespace rimes::native { inline constexpr char kChordContract[] = R"chord('
                          + json.dumps(value, ensure_ascii=False, sort_keys=True) + ')chord"; }\n')
print('Native chord profile, compiler, encoding and boundary fixtures are current')
