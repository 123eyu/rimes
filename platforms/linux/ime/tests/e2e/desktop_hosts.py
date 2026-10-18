#!/usr/bin/env python3
"""Native GTK/Qt, Firefox and XIM terminal input on a private X11 display/bus.

Run under dbus-run-session with ime/tests/portal_fixture_bus.conf and xvfb-run.
The bus must not auto-activate desktop portals in the temporary runtime folder.
No clipboard/paste injection is used.
Firefox is optional and uses an empty temporary profile. Missing or failed hosts
are recorded explicitly; a selected host failure makes this command fail.
"""
import argparse
import json
import os
from pathlib import Path
import shutil
import subprocess
import tempfile
import time


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("build", type=Path)
    parser.add_argument("shared", type=Path)
    parser.add_argument("output", type=Path)
    parser.add_argument("--firefox", type=Path)
    args = parser.parse_args()
    build, shared, output = [p.resolve() for p in (args.build, args.shared, args.output)]
    output.mkdir(parents=True, exist_ok=True)
    assert os.environ.get("DISPLAY") and os.environ.get("DBUS_SESSION_BUS_ADDRESS"), "use a private display/bus"
    rows, processes, logs = [], [], []
    with tempfile.TemporaryDirectory(prefix="xsime-desktop-hosts-") as directory:
        root = Path(directory)
        env = dict(os.environ, XDG_RUNTIME_DIR=str(root / "runtime"),
                   XDG_CONFIG_HOME=str(root / "config"), XDG_DATA_HOME=str(root / "data"),
                   XDG_CACHE_HOME=str(root / "cache"), RIMES_USER_DIR=str(root / "user"),
                   RIMES_SHARED_DIR=str(shared), RIMES_LOG_DIR=str(root / "rime-logs"),
                   GTK_IM_MODULE="fcitx", QT_IM_MODULE="fcitx", XMODIFIERS="@im=fcitx",
                   RIMES_BUFFER_HEADLESS="1", RIMES_CAPSULE_HEADLESS="1",
                   RIMES_BUFFER_AUTO_CAPTURE="0", NO_AT_BRIDGE="1", LC_ALL="C.UTF-8",
                   PATH=str(build) + os.pathsep + os.environ["PATH"])
        for name in ("XDG_RUNTIME_DIR", "XDG_CONFIG_HOME", "XDG_DATA_HOME", "XDG_CACHE_HOME", "RIMES_LOG_DIR"):
            Path(env[name]).mkdir(mode=0o700, parents=True)
        config = root / "config/fcitx5"
        config.mkdir()
        (config / "profile").write_text("[Groups/0]\nName=Default\nDefault Layout=us\nDefaultIM=rimes\n\n[Groups/0/Items/0]\nName=rimes\n\n[GroupOrder]\n0=Default\n")
        for kind in ("addon", "inputmethod"):
            target = root / "data/fcitx5" / kind
            target.mkdir(parents=True)
            shutil.copyfile(build / "test-data" / kind / "rimes.conf", target / "rimes.conf")

        def run(*command):
            return subprocess.check_output([str(x) for x in command], env=env, text=True, stderr=subprocess.STDOUT).strip()

        arch = run("dpkg-architecture", "-qDEB_HOST_MULTIARCH")
        env["FCITX_ADDON_DIRS"] = f"{build}:/usr/lib/{arch}/fcitx5"

        def start(name, *command):
            log = (output / (name + ".log")).open("w")
            logs.append(log)
            process = subprocess.Popen([str(x) for x in command], env=env, stdout=log, stderr=log)
            processes.append(process)
            return process

        def stop(process):
            process.terminate()
            try:
                process.wait(timeout=5)
            except subprocess.TimeoutExpired:
                process.kill()
                process.wait()
            processes.remove(process)

        def wait(check, detail, seconds=20):
            deadline = time.monotonic() + seconds
            last = None
            while time.monotonic() < deadline:
                try:
                    value = check()
                    if value:
                        return value
                except (FileNotFoundError, json.JSONDecodeError, subprocess.CalledProcessError) as error:
                    last = str(error)
                time.sleep(0.05)
            raise AssertionError(f"{detail}; last={last}")

        def activations():
            return (output / "fcitx.log").read_text().count("] activate token=")

        def focus(title, previous_activations, browser=False):
            window = wait(lambda: run("xdotool", "search", "--onlyvisible", "--name", title), f"window {title}").splitlines()[-1]
            run("xdotool", "windowactivate", "--sync", window)
            if browser:
                # Kiosk mode gives this public page a stable viewport without
                # depending on browser chrome, first-run prompts or URL focus.
                run("xdotool", "mousemove", "--window", window, 100, 100, "click", 1)
            run("fcitx5-remote", "-o")
            wait(lambda: "librime deploy finished" in (output / "fcitx.log").read_text(), "Rime deployment", 150)
            # A mapped Qt window can precede its asynchronous IM module/IC.
            # Wait for this new host's real activation, not just X window focus.
            try:
                wait(lambda: activations() > previous_activations, "native host Fcitx activation")
            except AssertionError:
                if browser:
                    print("browser window:", run("xdotool", "getwindowname", window), flush=True)
                    print("active window:", run("xdotool", "getactivewindow"), flush=True)
                    import gi
                    gi.require_version("Gdk", "3.0")
                    from gi.repository import Gdk
                    screen = Gdk.get_default_root_window()
                    Gdk.pixbuf_get_from_window(screen, 0, 0, screen.get_width(), screen.get_height()).savev(str(output / "browser-failure.png"), "png", [], [])
                raise
            return window

        def type_chinese():
            run("xdotool", "type", "--clearmodifiers", "--delay", 80, "nihaox")
            run("xdotool", "key", "BackSpace", "space")

        def native_host(name, binary, title):
            state = root / (name + ".txt")
            previous_activations = activations()
            process = start(name, binary, state, 240000)
            try:
                focus(title, previous_activations)
                type_chinese()
                wait(lambda: state.read_text().strip() == "commit\t你好", f"{name} native commit")
                if name == "gtk3":
                    assert any(line.startswith("preedit\t") and len(line) > len("preedit\t")
                               for line in state.with_suffix(".txt.events").read_text().splitlines()), "GTK did not expose preedit"
                # A second native commit must pass through Buffer's capture and
                # Return delivery, without leaking the staged text to the host.
                run("xdotool", "key", "ctrl+shift+b")
                time.sleep(0.15)
                type_chinese()
                time.sleep(0.15)
                assert state.read_text().strip() in ("commit\t你好", "preedit"), state.read_text()
                run("xdotool", "key", "Return")
                wait(lambda: state.read_text().strip() == "commit\t你好你好", f"{name} Buffer commit")
                rows.append(dict(host=name, status="passed", checks=["preedit/backspace/native commit", "Buffer capture/Return delivery"], text=state.read_text().strip()))
            finally:
                if state.exists():
                    shutil.copyfile(state, output / (name + "-state.txt"))
                if state.with_suffix(".txt.events").exists():
                    shutil.copyfile(state.with_suffix(".txt.events"), output / (name + "-events.txt"))
                stop(process)

        try:
            run("dbus-update-activation-environment", "DISPLAY", "XAUTHORITY", "XDG_RUNTIME_DIR", "XDG_DATA_HOME", "XDG_CONFIG_HOME")
            start("window-manager", "xfwm4", "--compositor=off")
            wait(lambda: "window id # 0x" in run("xprop", "-root", "_NET_SUPPORTING_WM_CHECK"), "X11 window manager")
            assert "false" in run("gdbus", "call", "--session", "--dest", "org.freedesktop.DBus", "--object-path", "/org/freedesktop/DBus", "--method", "org.freedesktop.DBus.NameHasOwner", "org.fcitx.Fcitx5"), "bus already owns Fcitx"
            start("fcitx", "fcitx5", "--disable=wayland,kimpanel,notificationitem")
            wait(lambda: "true" in run("gdbus", "call", "--session", "--dest", "org.freedesktop.DBus", "--object-path", "/org/freedesktop/DBus", "--method", "org.freedesktop.DBus.NameHasOwner", "org.fcitx.Fcitx5"), "Fcitx readiness")
            native_host("gtk3", build / "rimes-gtk-host", "^RIMES GTK host$")
            native_host("qt5", build / "rimes-qt-host", "^RIMES Qt host$")
            for script in ("dbus_client.py", "dbus_buffer_client.py", "dbus_capsule_client.py"):
                observation = run("python3", Path(__file__).with_name(script))
                (output / (script + ".log")).write_text(observation + "\n")
            rows.append(dict(host="fcitx-dbus", status="passed", checks=["ordinary composition/Escape", "Buffer native delivery", "Capsule seed loading/native delivery"]))

            # The terminal child reads stdin; success comes from the actual XIM
            # commit received by the PTY, not from terminal screen OCR.
            terminal_state = root / "terminal.txt"
            terminal_probe = root / "terminal.py"
            terminal_probe.write_text("import pathlib,sys,time\nvalue=input()\npathlib.Path(sys.argv[1]).write_text(value)\ntime.sleep(30)\n")
            previous_activations = activations()
            terminal = start("xterm", "xterm", "-u8", "-title", "XSIME XIM terminal", "-e", "python3", terminal_probe, terminal_state)
            try:
                focus("^XSIME XIM terminal$", previous_activations)
                type_chinese()
                run("xdotool", "key", "Return")
                wait(lambda: terminal_state.read_text() == "你好", "xterm XIM -> PTY commit")
                rows.append(dict(host="xterm", status="passed", checks=["XIM native commit into PTY"], text=terminal_state.read_text(), version=run("xterm", "-version")))
            finally:
                stop(terminal)

            if args.firefox:
                profile = root / "firefox-profile"
                profile.mkdir()
                (profile / "user.js").write_text('user_pref("browser.shell.checkDefaultBrowser", false);\nuser_pref("browser.startup.homepage_override.mstone", "ignore");\nuser_pref("datareporting.policy.dataSubmissionEnabled", false);\nuser_pref("toolkit.telemetry.enabled", false);\n')
                page = root / "browser.html"
                page.write_text('''<!doctype html><meta charset="utf-8"><title>XSIME Browser Probe</title>
<style>textarea{position:absolute;left:40px;top:40px;width:500px;height:180px}input{position:absolute;left:40px;top:260px}</style>
<textarea id="text" autofocus></textarea><input id="secret" type="password" autocomplete="off">
<script>
let composing=false; const text=document.getElementById('text'), secret=document.getElementById('secret');
function record(){document.title='XSIME Browser Probe|'+JSON.stringify({text:text.value,secret:secret.value,composing,active:document.activeElement.id});}
document.addEventListener('input',record);document.addEventListener('compositionstart',()=>{composing=true;record()});
document.addEventListener('compositionend',()=>{composing=false;record()});document.addEventListener('focusin',record);record();
</script>''')
                previous_activations = activations()
                browser = start("firefox", args.firefox.resolve(), "--no-remote", "--new-instance", "--kiosk", "--profile", profile, page.as_uri())
                try:
                    window = focus("^XSIME Browser Probe", previous_activations, browser=True)

                    def browser_state():
                        title = run("xdotool", "getwindowname", window)
                        return json.JSONDecoder().raw_decode(title.split("|", 1)[1])[0]

                    type_chinese()
                    wait(lambda: browser_state()["text"] == "你好" and not browser_state()["composing"], "Firefox DOM native commit")
                    run("xdotool", "key", "ctrl+shift+b")
                    time.sleep(0.15)
                    type_chinese()
                    time.sleep(0.15)
                    assert browser_state()["text"] == "你好", browser_state()
                    run("xdotool", "key", "Return")
                    wait(lambda: browser_state()["text"] == "你好你好", "Firefox Buffer DOM commit")
                    run("xdotool", "key", "Tab", "ctrl+shift+b")
                    run("xdotool", "type", "--clearmodifiers", "--delay", 80, "public123")
                    wait(lambda: browser_state()["secret"] == "public123", "Firefox disabled-IME password bypass")
                    assert browser_state()["text"] == "你好你好", browser_state()
                    (output / "browser-dom.json").write_text(json.dumps(browser_state(), ensure_ascii=False, indent=2) + "\n")
                    version = next(line for line in run(args.firefox.resolve(), "--version").splitlines() if line.startswith("Mozilla Firefox "))
                    rows.append(dict(host="firefox", status="passed", checks=["DOM native commit", "Buffer capture/Return DOM delivery", "password disabled-IME bypass after Buffer shortcut"], version=version))
                finally:
                    stop(browser)
            else:
                rows.append(dict(host="firefox", status="not_run", reason="supply --firefox /path/to/firefox"))
        except Exception as error:
            if isinstance(error, subprocess.CalledProcessError) and error.output:
                (output / "failed-command.log").write_text(error.output)
                print(error.output, flush=True)
            rows.append(dict(host="suite", status="failed", reason=str(error)))
            raise
        finally:
            for process in reversed(processes.copy()):
                stop(process)
            for log in logs:
                log.close()
            report = dict(schemaVersion=1, environment="private Xvfb/X11 + Xfwm4; not GNOME/KDE or Wayland acceptance", hosts=rows)
            (output / "results.json").write_text(json.dumps(report, ensure_ascii=False, indent=2) + "\n")
            print(json.dumps(report, ensure_ascii=False, indent=2))


if __name__ == "__main__":
    main()
