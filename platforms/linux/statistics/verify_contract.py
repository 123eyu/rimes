#!/usr/bin/env python3
"""Validate the statistics fixtures exported by the actual macOS stores."""
import argparse
import hashlib
import json
from pathlib import Path

root = Path(__file__).resolve().parents[3]
fixture = Path(__file__).resolve().parent / 'resources/native-statistics.json'
parser = argparse.ArgumentParser(description=__doc__)
parser.add_argument('--output', type=Path)
args = parser.parse_args()
value = json.loads(fixture.read_text())
expected = {
    'OfficialPlugins/native/macos/' + name + '.swift' for name in
    ['KeyFrequencyStore', 'TypingSpeedStore', 'DailyInputMetrics', 'StatisticsSettingsViewController']
} | {'Sources/RimeBuffer/' + name + '.swift' for name in
     ['InputTelemetry', 'TypingPracticeTelemetry', 'MetricsChartViews', 'StatisticsLinuxFixtures', 'KeyboardHeatmapView', 'YearHistoryHeatmapView', 'StatisticsDashboardSmoke']}
if value.get('purpose') != 'public-statistics-contract' or value.get('schemaVersion') != 1 or set(value.get('sources', {})) != expected:
    raise SystemExit('Export the actual native statistics fixtures')
for name, digest in value['sources'].items():
    if hashlib.sha256((root / name).read_bytes()).hexdigest() != digest:
        raise SystemExit('Native statistics changed; regenerate and review: ' + name)
if args.output:
    args.output.parent.mkdir(parents=True, exist_ok=True)
    args.output.write_text('#pragma once\nnamespace rimes::native { inline constexpr char kMetricsContract[] = R"metrics('
                          + json.dumps(value, ensure_ascii=False, sort_keys=True) + ')metrics"; }\n')
print('Actual native statistics aggregates, admission rules and keyboard layout are current')
