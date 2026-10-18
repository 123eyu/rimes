#!/usr/bin/env python3
"""Guard actual macOS practice observations and embed only the public article inventory."""
import argparse
import hashlib
import json
from pathlib import Path

root = Path(__file__).resolve().parents[3]
folder = Path(__file__).resolve().parent
parser = argparse.ArgumentParser(description=__doc__)
parser.add_argument('--output', type=Path)
args = parser.parse_args()
value = json.loads((folder/'resources/native-practice.json').read_text())
expected = {'OfficialPlugins/native/macos/'+name+'.swift' for name in
            ['TypingTestArticles','TypingTestSession','TypingTestHistoryStore','TypingTestTextView','TypingSpeedSettingsViewController']} | {
            'Sources/RimeBuffer/'+name+'.swift' for name in ['TypingPracticeTelemetry','TypingTestUISmoke','TypingPracticeLinuxFixtures','TypingPracticeLinuxUISmoke','MetricsChartViews','RimeUI']}
if value.get('purpose') != 'public-typing-practice-contract' or value.get('schemaVersion') != 1 or set(value.get('sources', {})) != expected:
    raise SystemExit('Export the actual native typing practice contract')
for name, digest in value['sources'].items():
    if hashlib.sha256((root/name).read_bytes()).hexdigest() != digest:
        raise SystemExit('Native practice changed; regenerate and review: '+name)
if args.output:
    args.output.parent.mkdir(parents=True, exist_ok=True)
    args.output.write_text('#pragma once\nnamespace rimes::practice { inline constexpr char kNativeArticles[] = R"articles('
                          + json.dumps(value['articles'],ensure_ascii=False,sort_keys=True) + ')articles"; }\n')
print('Actual native typing practice, articles and validation are current')
