#!/usr/bin/env python3
"""Actual GTK IM module -> Fcitx -> practice editor, under an isolated display/bus."""
import argparse, json, os, shutil, subprocess, tempfile, time
from pathlib import Path
p = argparse.ArgumentParser(description=__doc__)
p.add_argument('build', type=Path)
p.add_argument('shared', type=Path)
a = p.parse_args()
build = a.build.resolve()
shared = a.shared.resolve()
assert os.environ.get('DISPLAY') and os.environ.get('DBUS_SESSION_BUS_ADDRESS'), 'use dbus-run-session and xvfb-run'
with tempfile.TemporaryDirectory(prefix='rimes-practice-desktop-') as directory:
    root = Path(directory)
    processes = []
    env = dict(os.environ, HOME=str(root / 'home'), XDG_RUNTIME_DIR=str(root / 'runtime'), XDG_CONFIG_HOME=str(root / 'config'), XDG_DATA_HOME=str(root / 'data'), XDG_CACHE_HOME=str(root / 'cache'), RIMES_USER_DIR=str(root / 'user'), RIMES_SHARED_DIR=str(shared), RIMES_BUFFER_HEADLESS='1', RIMES_CAPSULE_HEADLESS='1', RIMES_BUFFER_AUTO_CAPTURE='0', GTK_IM_MODULE='fcitx', XMODIFIERS='@im=fcitx', NO_AT_BRIDGE='1')
    for name in ['HOME', 'XDG_RUNTIME_DIR', 'XDG_CONFIG_HOME', 'XDG_DATA_HOME', 'XDG_CACHE_HOME']:
        Path(env[name]).mkdir(mode=448, parents=True, exist_ok=True)
    config = root / 'config/fcitx5'
    config.mkdir()
    (config / 'profile').write_text('[Groups/0]\nName=Default\nDefault Layout=us\nDefaultIM=rimes\n\n[Groups/0/Items/0]\nName=rimes\n\n[GroupOrder]\n0=Default\n')
    for kind in ['addon', 'inputmethod']:
        out = root / 'data/fcitx5' / kind
        out.mkdir(parents=True)
        shutil.copyfile(build / 'test-data' / kind / 'rimes.conf', out / 'rimes.conf')
    arch = subprocess.check_output(['dpkg-architecture', '-qDEB_HOST_MULTIARCH'], text=True).strip()
    env['FCITX_ADDON_DIRS'] = str(build) + ':/usr/lib/' + arch + '/fcitx5'

    def run(*args):
        return subprocess.check_output(args, env=env, text=True, stderr=subprocess.STDOUT).strip()

    def wait(ready, seconds=90):
        until = time.monotonic() + seconds
        while time.monotonic() < until:
            if any((p.poll() is not None for p in processes)):
                raise RuntimeError('test process exited')
            try:
                result = ready()
                if result:
                    return result
            except (FileNotFoundError, json.JSONDecodeError, subprocess.CalledProcessError):
                pass
            time.sleep(0.05)
        raise RuntimeError('desktop probe timed out')

    def state():
        return json.loads((root / 'state.json').read_text())

    def command(value):
        (root / 'command').write_text(value)
    try:
        run('dbus-update-activation-environment', 'DISPLAY', 'XAUTHORITY', 'XDG_RUNTIME_DIR', 'XDG_DATA_HOME', 'XDG_CONFIG_HOME', 'HOME')
        with (root / 'fcitx.log').open('w') as log:
            processes.append(subprocess.Popen(['fcitx5', '--disable=wayland,kimpanel,notificationitem'], env=env, stdout=log, stderr=log))
        wait(lambda: 'true' in run('gdbus', 'call', '--session', '--dest', 'org.freedesktop.DBus', '--object-path', '/org/freedesktop/DBus', '--method', 'org.freedesktop.DBus.NameHasOwner', 'org.fcitx.Fcitx5'))
        with (root / 'gtk.log').open('w') as log:
            processes.append(subprocess.Popen([str(build / 'runtime/practice-desktop/rimes-settings'), str(root)], env=env, stdout=log, stderr=log))
        wait(lambda: (root / 'state.json').exists())
        window = wait(lambda: run('xdotool', 'search', '--name', '^XSIME Practice Desktop Probe$'))
        run('xdotool', 'windowfocus', '--sync', window)
        run('fcitx5-remote', '-o')
        wait(lambda: 'librime deploy finished' in (root / 'fcitx.log').read_text())
        command('start')
        wait(lambda: state()['armed'])
        current = state()
        assert current['context']['inputSourceID'] == 'fcitx5.rimes', current
        assert current['context']['schemaID'] == 'rime_ice', current
        run('xdotool', 'type', '--clearmodifiers', '--delay', '90', 'nihaox')
        wait(lambda: state()['snapshot'].get('isStarted'))
        assert state()['text'] == '', state()
        assert state()['snapshot']['metrics']['committedCharacterCount'] == 0, state()
        run('xdotool', 'key', 'BackSpace')
        run('xdotool', 'key', 'space')
        wait(lambda: state()['text'] == '你好')
        command('finish')
        wait(lambda: bool(state()['result']))
        result = state()['result']
        assert result['context']['inputSourceID'] == 'fcitx5.rimes', result
        assert result['metrics']['committedCharacterCount'] == 2, result
        assert result['metrics']['physicalKeyCount'] == 8, result
        assert result['metrics']['backspaceCount'] == 1 and result['metrics']['compositionBackspaceCount'] == 1, result
        assert 'focusLost' not in result['practiceReasons'] and 'interrupted' not in result['practiceReasons'], result
        command('quit')
        processes[-1].wait(timeout=5)
        processes.pop()
        history = json.loads((root / 'user/stats/typing_tests.json').read_text())
        assert len(history['results']) == 1 and history['results'][0]['id'].lower() == result['id'].lower(), history
        print('PASS: actual GTK Fcitx focus, frozen schema, preedit exclusion, physical keys, composition backspace, native commit and persisted final score')
    except Exception:
        for name in ['state.json', 'gtk.log', 'fcitx.log']:
            if (root / name).exists():
                print(name, (root / name).read_text()[-10000:])
        raise
    finally:
        for process in reversed(processes):
            process.terminate()
            try:
                process.wait(timeout=5)
            except subprocess.TimeoutExpired:
                process.kill()
                process.wait()
