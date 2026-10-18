#!/usr/bin/env python3
"""Check request builders and real widget states exported by the macOS Claude channel."""
import argparse
import hashlib
import json
from pathlib import Path

ROOT = Path(__file__).resolve().parents[3]
RESOURCES = Path(__file__).resolve().parent / 'resources'
parser = argparse.ArgumentParser(description=__doc__)
parser.add_argument('--import-fixture', type=Path)
parser.add_argument('--import-settings', type=Path)
parser.add_argument('--output', type=Path)
args = parser.parse_args()
exports = []
for kind, source, paths in [
    ('channel-contract', args.import_fixture, ['AITextPlugins.swift', 'AITextClaudeLogin.swift', 'AITextClaudeLinuxFixtures.swift']),
    ('settings', args.import_settings, ['SettingsWindow.swift']),
]:
    destination = RESOURCES / ('native-claude.json' if kind == 'channel-contract' else 'native-claude-settings.json')
    value = json.loads((source or destination).read_text())
    expected = {'Sources/RimeBuffer/' + path for path in paths}
    if value.get('schemaVersion') != 1 or value.get('purpose') != 'public-claude-' + kind or set(value.get('sources', {})) != expected:
        raise SystemExit('Expected actual native Claude export')
    for name, digest in value['sources'].items():
        if hashlib.sha256((ROOT / name).read_bytes()).hexdigest() != digest:
            raise SystemExit('Native Claude source changed; regenerate and review: ' + name)
    if source:
        temporary = destination.with_suffix('.json.edit')
        temporary.write_text(json.dumps(value, ensure_ascii=False, indent=2) + '\n')
        temporary.replace(destination)
    exports.append(value)
value = exports[0]
value['settings'] = exports[1]
if args.output:
    args.output.parent.mkdir(parents=True, exist_ok=True)
    args.output.write_text('// Generated from actual native Claude builders. Do not edit.\n#pragma once\n'
                          'namespace rimes::native { inline constexpr char kClaudeContract[] = R"claude('
                          + json.dumps(value, ensure_ascii=False, sort_keys=True) + ')claude"; }\n')
print('Native Claude requests, login and parser fixtures are current')
