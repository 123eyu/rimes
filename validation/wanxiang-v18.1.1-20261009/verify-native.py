#!/usr/bin/env python3
"""Cross-check the portable package with the desktop engine, in temporary directories.

Run with build/input-schemes/venv/bin/python from any directory. The desktop Lua
plugin has no mobile error recorder; success requires clean error-level stderr,
successful deployment/loading, and every expected candidate/commit.
"""
import hashlib
import json
from pathlib import Path
import re
import subprocess
import sys
import tempfile

REPO = Path(__file__).resolve().parents[2]
sys.path.insert(0, str(REPO / "scripts/input-schemes"))
import scheme_build as build

output = build.DEFAULT_OUTPUT
entry = next(e for e in build.load_sources()["packages"] if e["id"] == "wanxiang")
package = json.loads((output / "reports/wanxiang.json").read_text())
archive = output / "packages" / package["archive"]
frameworks = REPO / "Vendor/rime/Frameworks"
plugin = frameworks / "rime-plugins/librime-lua.dylib"
library = frameworks / "librime.1.dylib"
probe = output / "bin/native_probe"
command = ["xcrun", "clang++", "-std=c++17", "-O2",
           '-DRIMES_NATIVE_LUA_PLUGIN="' + str(plugin) + '"',
           "-I" + str(build.ENGINE_HEADERS), str(REPO / "scripts/input-schemes/host_probe.cc"),
           str(library), "-Wl,-rpath," + str(frameworks), "-o", str(probe)]
subprocess.run(command, check=True)
harness = build.importer_harness(output, build.importer_source(None))
rows, logs = [], []
with tempfile.TemporaryDirectory(dir=output, prefix="wanxiang-native-") as temp:
    response = build.run([str(harness), "--stage", temp, str(archive)])
    review = build.json_lines(response.stdout)[0]
    staged = review["staged"]
    for mode, args in [("deploy", staged["deployment"]), ("load", staged["selected"])]:
        response = build.run([str(probe), staged["root"], mode, *args], timeout=600)
        steps = build.json_lines(response.stdout)
        passed = response.returncode == 0 and bool(steps) and all(x["ok"] and x.get("compiled", True) for x in steps)
        rows.append({"mode": mode, "passed": passed, "results": steps})
        logs.append(response.stderr)
        print(mode, passed, flush=True)
    for case in entry["probes"]:
        response = build.run([str(probe), staged["root"], "type", case["schema"], case["keys"], *case.get("options", [])], timeout=60)
        typed = build.json_lines(response.stdout)[0]
        seen = typed.get("candidates", []) + [typed.get("commit", "")]
        passed = response.returncode == 0 and typed["ok"] and any(case["expect"] in text for text in seen)
        rows.append({**case, "passed": passed, "result": typed})
        logs.append(response.stderr)
        print(case["keys"], case.get("options", []), passed, flush=True)

stderr = "\n".join(logs)
errors = [line for line in stderr.splitlines() if re.match(r"^[EF]\d{8} ", line)]
def digest(path):
    return hashlib.sha256(path.read_bytes()).hexdigest()
report = {"archive": archive.name, "sha256": digest(archive),
          "engine": "macOS bundled librime 1.16.0 + librime-lua",
          "librarySHA256": digest(library), "luaPluginSHA256": digest(plugin),
          "luaDiagnosticsAvailable": False, "errorLevelLogs": errors,
          "stderr": stderr, "rows": rows,
          "passed": all(x["passed"] for x in rows) and not errors}
path = output / "wanxiang-v18.1.1/native-verification.json"
path.write_text(json.dumps(report, ensure_ascii=False, indent=2) + "\n")
print("native passed", report["passed"])
sys.exit(0 if report["passed"] else 1)
