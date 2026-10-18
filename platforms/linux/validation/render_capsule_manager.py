#!/usr/bin/env python3
"""Render the real, inactive Capsule manager in an isolated X11 session."""
import argparse
import json
import os
from pathlib import Path
import subprocess
import tempfile


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--binary", type=Path, required=True)
    parser.add_argument("--output", type=Path, required=True)
    args = parser.parse_args()
    binary = args.binary.resolve(strict=True)
    output = args.output.resolve()
    output.mkdir(parents=True, exist_ok=True)
    bus_config = Path(__file__).resolve().parents[1] / "ime/tests/portal_fixture_bus.conf"
    # A bare Xvfb display ignores the no-focus hint and reports the manager as
    # active. Wait for a real WM before mapping it, just as on a desktop.
    session = """
set -eu
xfwm4 --compositor=off --sm-client-disable >/dev/null 2>&1 &
wm=$!
trap 'kill "$wm" 2>/dev/null || true' EXIT
ready=0
for attempt in $(seq 1 100); do
    if xprop -root _NET_SUPPORTING_WM_CHECK 2>/dev/null | grep -q 'window id # 0x'; then
        ready=1
        break
    fi
    kill -0 "$wm"
    sleep 0.05
done
test "$ready" = 1
"$@"
"""
    with tempfile.TemporaryDirectory(prefix="xsime-capsule-visual-") as root:
        for theme in ("night", "day", "quiet", "rasta"):
            env = os.environ.copy()
            env.update(GDK_SCALE="2", GDK_DPI_SCALE="1", GDK_BACKEND="x11",
                       RIMES_USER_DIR=str(Path(root) / theme))
            png = output / f"{theme}.png"
            subprocess.run([
                "dbus-run-session", f"--config-file={bus_config}", "--",
                "xvfb-run", "-a", "-s", "-screen 0 2560x1600x24",
                "sh", "-c", session, "capsule-manager-render", str(binary),
                "--manager-preview", "--theme", theme, "--render", str(png)
            ], env=env, check=True, timeout=45)
            geometry = json.loads(png.with_suffix(".png.json").read_text())
            if (geometry["width"], geometry["height"], geometry["selectedRow"],
                    geometry["keyWindow"], geometry["scale"], geometry["appearance"]) != (
                    940, 660, 0, False, 2, "light" if theme == "day" else "dark"):
                raise RuntimeError(f"{theme}: fixture geometry/state differs from macOS")
            print(f"{theme}: 940x660 at 2x, first row selected, inactive window", flush=True)


if __name__ == "__main__":
    main()
