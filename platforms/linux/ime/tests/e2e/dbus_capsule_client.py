#!/usr/bin/env python3
"""Drive Fcitx5 + RIMES Capsule over DBus and the Unix-socket control protocol."""

from __future__ import annotations

import json
import os
import socket
import struct
import sys
import time

import gi

gi.require_version("Gio", "2.0")
from gi.repository import Gio, GLib

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
from dbus_client import Probe

KEYSYMS = {
    "Return": 0xFF0D,
    "V": 0x0056,
}


def socket_path() -> str:
    override = os.environ.get("RIMES_CAPSULE_SOCKET")
    if override:
        return override
    runtime = os.environ.get("XDG_RUNTIME_DIR")
    if runtime:
        return os.path.join(runtime, "rimes-capsule.sock")
    return f"/tmp/rimes-capsule-{os.getuid()}.sock"


def _recv_snapshot(sock: socket.socket) -> dict:
    header = sock.recv(4)
    if len(header) < 4:
        raise RuntimeError("short header")
    (length,) = struct.unpack(">I", header)
    body = b""
    while len(body) < length:
        chunk = sock.recv(length - len(body))
        if not chunk:
            raise RuntimeError("short body")
        body += chunk
    return json.loads(body.decode())


def _matches(op: str, snapshot: dict, baseline_generation: int) -> bool:
    if op in ("hello", "status"):
        return True
    if op == "show":
        return bool(snapshot.get("armed")) and not snapshot.get("loading")
    if op == "close":
        return not snapshot.get("visible")
    if op == "activate":
        return True
    return int(snapshot.get("generation") or 0) > baseline_generation


def ctl(op: str, timeout: float = 3.0) -> dict:
    path = socket_path()
    deadline = time.monotonic() + timeout
    last_error = None
    while time.monotonic() < deadline:
        try:
            sock = socket.socket(socket.AF_UNIX, socket.SOCK_STREAM)
            sock.settimeout(2.0)
            sock.connect(path)
            connect_snapshot = _recv_snapshot(sock)
            payload = json.dumps({"v": 1, "op": op}).encode()
            sock.sendall(struct.pack(">I", len(payload)) + payload)
            baseline = int(connect_snapshot.get("generation") or 0)
            if op in ("hello", "status") or (
                op == "show" and connect_snapshot.get("armed") and not connect_snapshot.get("loading")
            ) or (op == "close" and not connect_snapshot.get("visible")):
                sock.close()
                return connect_snapshot
            last = connect_snapshot
            while time.monotonic() < deadline:
                sock.settimeout(max(0.05, deadline - time.monotonic()))
                last = _recv_snapshot(sock)
                if _matches(op, last, baseline):
                    sock.close()
                    return last
            sock.close()
            return last
        except OSError as error:
            last_error = error
            time.sleep(0.1)
        except RuntimeError as error:
            last_error = error
            time.sleep(0.1)
    raise RuntimeError(f"capsule socket failed: {last_error}")


def main() -> int:
    os.environ.setdefault("RIMES_CAPSULE_HEADLESS", "1")
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
    snapshot = ctl("show")
    if snapshot.get("module") != "notes":
        print(f"FAIL: default Capsule page is {snapshot.get('module')!r}, expected notes", file=sys.stderr)
        return 1
    if not snapshot.get("armed"):
        print("FAIL: show did not arm Capsule", file=sys.stderr)
        print(snapshot, file=sys.stderr)
        return 1
    titles = [card.get("title") for card in snapshot.get("cards", [])]
    if "RIMES 默认词条" not in titles:
        print(f"FAIL: seed note missing, cards={titles!r}", file=sys.stderr)
        print(snapshot, file=sys.stderr)
        return 1
    print("ok: dbus capsule showed the seed note")

    ctl("activate")
    probe.pump(0.6)
    if "RIMES" not in probe.commits:
        print(f"FAIL: activate did not commit, commits={probe.commits!r}", file=sys.stderr)
        return 1
    print("ok: dbus capsule activate committed RIMES")
    return 0


if __name__ == "__main__":
    sys.exit(main())
