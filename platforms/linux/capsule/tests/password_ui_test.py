"""Run the vault UI with real key-window ownership in an isolated X11 desktop."""
import os
from pathlib import Path
import subprocess
import sys
import tempfile
import time

# _NET_SUPPORTED appears before xfwm4 has finished starting, and a window
# presented in that gap may never become active. Wait until the window manager
# activates a throwaway window before the test opens its own.
PROBE = r'''
import sys, time
import gi
gi.require_version('Gtk', '3.0')
gi.require_version('GdkX11', '3.0')
from gi.repository import Gtk, GdkX11
window = Gtk.Window(title='xsime-wm-probe')
window.set_default_size(64, 64)
window.show()
until, presented = time.monotonic() + 15, 0.0
while time.monotonic() < until:
    while Gtk.events_pending():
        Gtk.main_iteration_do(False)
    if window.is_active():
        sys.exit(0)
    if time.monotonic() - presented > 1:
        window.present_with_time(GdkX11.x11_get_server_time(window.get_window()))
        presented = time.monotonic()
    time.sleep(.02)
sys.exit('private window manager never activated a window')
'''

with tempfile.TemporaryDirectory(prefix='xsime-vault-desktop-') as raw:
    root = Path(raw)
    env = dict(os.environ, XDG_CONFIG_HOME=str(root / 'config'),
               XDG_CACHE_HOME=str(root / 'cache'), GTK_IM_MODULE='simple', NO_AT_BRIDGE='1')
    wm_env = dict(env)
    wm_env.pop('G_DEBUG', None)
    with (root / 'wm.log').open('w+') as log:
        wm = subprocess.Popen(['xfwm4', '--compositor=off'], env=wm_env, stdout=log, stderr=log)
        try:
            until = time.monotonic() + 8
            while time.monotonic() < until:
                assert wm.poll() is None, 'private window manager exited'
                result = subprocess.run(['xprop', '-root', '_NET_SUPPORTING_WM_CHECK', '_NET_SUPPORTED'],
                                        capture_output=True, text=True, timeout=2)
                if '_NET_ACTIVE_WINDOW' in result.stdout and 'window id # 0x' in result.stdout:
                    break
                time.sleep(.05)
            else:
                raise AssertionError('private window manager startup timed out')
            subprocess.run([sys.executable, '-c', PROBE], env=wm_env, check=True, timeout=30)
            assert wm.poll() is None, 'private window manager exited'
            subprocess.run(sys.argv[1:], env=env, check=True, timeout=120)
        finally:
            wm.terminate()
            try:
                wm.wait(timeout=3)
            except subprocess.TimeoutExpired:
                wm.kill()
                wm.wait()
