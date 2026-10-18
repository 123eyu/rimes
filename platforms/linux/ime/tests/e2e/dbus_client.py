#!/usr/bin/env python3
"""Drive a live Fcitx5 InputContext over DBus and assert IME behaviour."""

from __future__ import annotations

import argparse
import sys
import time
from typing import Any

import gi

gi.require_version("Gio", "2.0")
from gi.repository import Gio, GLib

FCITX_NAME = "org.fcitx.Fcitx5"
IM_PATH = "/org/freedesktop/portal/inputmethod"
IM_IFACE = "org.fcitx.Fcitx.InputMethod1"
IC_IFACE = "org.fcitx.Fcitx.InputContext1"
CONTROLLER_PATH = "/controller"
CONTROLLER_IFACE = "org.fcitx.Fcitx.Controller1"

# Enough for client preedit + client-side UI if the frontend supports it.
CAPABILITY_PREEDIT = 1 << 1
CAPABILITY_CLIENT_UNFOCUS_COMMIT = 1 << 4
CAPABILITY_CLIENT_SIDE_UI = 1 << 8

# X11 keysyms used by Fcitx5 ProcessKeyEvent.
KEYSYMS = {
    "n": 0x006E,
    "i": 0x0069,
    "h": 0x0068,
    "a": 0x0061,
    "o": 0x006F,
    "1": 0x0031,
    "2": 0x0032,
    "3": 0x0033,
    "space": 0x0020,
    "Escape": 0xFF1B,
    "Page_Down": 0xFF56,
    "Page_Up": 0xFF55,
}


class Probe:
    def __init__(self) -> None:
        self.commits: list[str] = []
        self.preedits: list[str] = []
        self.connection = Gio.bus_get_sync(Gio.BusType.SESSION, None)

    def controller(self) -> Gio.DBusProxy:
        return Gio.DBusProxy.new_sync(
            self.connection,
            Gio.DBusProxyFlags.DO_NOT_AUTO_START,
            None,
            FCITX_NAME,
            CONTROLLER_PATH,
            CONTROLLER_IFACE,
            None,
        )

    def wait_for_name(self, timeout_s: float = 30.0) -> None:
        deadline = time.monotonic() + timeout_s
        while time.monotonic() < deadline:
            try:
                self.controller().call_sync(
                    "CurrentInputMethod",
                    None,
                    Gio.DBusCallFlags.NO_AUTO_START,
                    2000,
                    None,
                )
                return
            except GLib.Error:
                time.sleep(0.2)
        raise RuntimeError("fcitx5 DBus name did not appear")

    def create_ic(self) -> tuple[str, Gio.DBusProxy]:
        im = Gio.DBusProxy.new_sync(
            self.connection,
            Gio.DBusProxyFlags.DO_NOT_AUTO_START,
            None,
            FCITX_NAME,
            IM_PATH,
            IM_IFACE,
            None,
        )
        result = im.call_sync(
            "CreateInputContext",
            GLib.Variant("(a(ss))", ([("program", "rimes-dbus-e2e"), ("display", "x11:")],)),
            Gio.DBusCallFlags.NO_AUTO_START,
            5000,
            None,
        )
        path, _uuid = result.unpack()
        ic = Gio.DBusProxy.new_sync(
            self.connection,
            Gio.DBusProxyFlags.DO_NOT_AUTO_START,
            None,
            FCITX_NAME,
            path,
            IC_IFACE,
            None,
        )
        ic.connect("g-signal", self._on_signal)
        ic.call_sync("FocusIn", None, Gio.DBusCallFlags.NO_AUTO_START, 2000, None)
        caps = CAPABILITY_PREEDIT | CAPABILITY_CLIENT_UNFOCUS_COMMIT | CAPABILITY_CLIENT_SIDE_UI
        ic.call_sync(
            "SetCapability",
            GLib.Variant("(t)", (caps,)),
            Gio.DBusCallFlags.NO_AUTO_START,
            2000,
            None,
        )
        return path, ic

    def _on_signal(
        self,
        _proxy: Gio.DBusProxy,
        _sender: str,
        signal: str,
        parameters: GLib.Variant,
    ) -> None:
        values = parameters.unpack()
        if signal == "CommitString":
            self.commits.append(values[0])
        elif signal == "UpdateFormattedPreedit":
            chunks, _cursor = values
            text = "".join(piece[0] for piece in chunks)
            self.preedits.append(text)

    def process(self, ic: Gio.DBusProxy, name: str, release: bool = False) -> bool:
        keysym = KEYSYMS[name]
        result = ic.call_sync(
            "ProcessKeyEvent",
            GLib.Variant("(uuubu)", (keysym, 0, 0, release, 0)),
            Gio.DBusCallFlags.NO_AUTO_START,
            2000,
            None,
        )
        GLib.MainContext.default().iteration(False)
        return bool(result.unpack()[0])

    def type_text(self, ic: Gio.DBusProxy, text: str) -> None:
        for char in text:
            self.process(ic, char, False)
            self.process(ic, char, True)
            time.sleep(0.03)

    def pump(self, seconds: float = 0.4) -> None:
        deadline = time.monotonic() + seconds
        while time.monotonic() < deadline:
            GLib.MainContext.default().iteration(False)
            time.sleep(0.02)


def main() -> int:
    parser = argparse.ArgumentParser()
    parser.add_argument("--expect-commit", default="你好")
    args = parser.parse_args()

    probe = Probe()
    probe.wait_for_name()
    try:
        probe.controller().call_sync(
            "SetCurrentIM",
            GLib.Variant("(s)", ("rimes",)),
            Gio.DBusCallFlags.NO_AUTO_START,
            2000,
            None,
        )
    except GLib.Error as error:
        print(f"warning: SetCurrentIM failed: {error}", file=sys.stderr)

    _path, ic = probe.create_ic()
    probe.type_text(ic, "nihao")
    probe.pump(0.6)
    if not any(probe.preedits):
        print("warning: no preedit observed over DBus", file=sys.stderr)
    probe.process(ic, "space", False)
    probe.process(ic, "space", True)
    probe.pump(0.8)
    if args.expect_commit not in probe.commits:
        print(f"FAIL: expected commit {args.expect_commit!r}, got {probe.commits!r}", file=sys.stderr)
        return 1
    print(f"ok: dbus commit {args.expect_commit}")

    probe.commits.clear()
    probe.preedits.clear()
    probe.type_text(ic, "nihao")
    probe.process(ic, "Escape", False)
    probe.process(ic, "Escape", True)
    probe.pump(0.4)
    if args.expect_commit in probe.commits:
        print("FAIL: Escape committed text", file=sys.stderr)
        return 1
    print("ok: dbus Escape did not commit")
    return 0


if __name__ == "__main__":
    sys.exit(main())
