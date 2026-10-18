#!/usr/bin/env python3
"""Verify public fixtures exported by the production macOS Codex connector."""
import argparse
import hashlib
import json
from pathlib import Path

ROOT = Path(__file__).resolve().parents[3]
DESTINATION = Path(__file__).resolve().parent / "resources/native-codex.json"
parser = argparse.ArgumentParser(description=__doc__)
parser.add_argument("--import-fixture", type=Path)
parser.add_argument("--output", type=Path)
parser.add_argument("--import-settings", type=Path)
args = parser.parse_args()
data = json.loads((args.import_fixture or DESTINATION).read_text())
expected = {"Sources/RimeBuffer/" + name for name in
            ("AITextPlugins.swift", "AITextCodexAppServer.swift", "AITextCodexLogin.swift", "AITextSkills.swift")}
if data.get("purpose") != "public-codex-channel-contract" or data.get("schemaVersion") != 1 or set(data.get("sources", {})) != expected:
    raise SystemExit("Expected the actual native Codex export")
for path, digest in data["sources"].items():
    if hashlib.sha256((ROOT / path).read_bytes()).hexdigest() != digest:
        raise SystemExit("Native Codex source changed; regenerate and review: " + path)
if args.import_fixture:
    DESTINATION.parent.mkdir(parents=True, exist_ok=True)
    temporary = DESTINATION.with_suffix(".json.edit")
    temporary.write_text(json.dumps(data, ensure_ascii=False, indent=2) + "\n")
    temporary.replace(DESTINATION)
settings_path = DESTINATION.with_name("native-codex-settings.json")
settings = json.loads((args.import_settings or settings_path).read_text())
if settings.get("purpose") != "public-codex-settings" or settings.get("schemaVersion") != 1 or set(settings.get("sources", {})) != {"Sources/RimeBuffer/SettingsWindow.swift"}:
    raise SystemExit("Expected actual native Codex settings export")
for path, digest in settings["sources"].items():
    if hashlib.sha256((ROOT / path).read_bytes()).hexdigest() != digest:
        raise SystemExit("Native Codex settings changed; regenerate actual render: " + path)
if args.import_settings:
    temporary = settings_path.with_suffix(".json.edit")
    temporary.write_text(json.dumps(settings, ensure_ascii=False, indent=2) + "\n")
    temporary.replace(settings_path)
data["settings"] = settings
if args.output:
    args.output.parent.mkdir(parents=True, exist_ok=True)
    args.output.write_text('// Generated from actual macOS Codex builders. Do not edit.\n#pragma once\n'
        'namespace rimes::native { inline constexpr char kCodexContract[] = R"codex('
        + json.dumps(data, ensure_ascii=False, sort_keys=True) + ')codex"; }\n')
print("Native Codex requests, isolation and parser fixtures are current")
