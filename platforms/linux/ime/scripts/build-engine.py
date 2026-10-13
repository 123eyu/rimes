#!/usr/bin/env python3
"""Build the shared pinned Rime/Lua runtime without replacing distro libraries.

Needs CMake, a C++17 compiler, Boost.Regex, glog, yaml-cpp, LevelDB, Marisa
and OpenCC development packages. The cache and prefix are entirely local.
"""
import argparse
import importlib.util
import json
import os
from pathlib import Path
import subprocess
import sys

ROOT = Path(__file__).resolve().parents[4]
LOCK = json.loads((ROOT / 'platforms/ios/dependencies.lock.json').read_text())


def run(*args, **kwargs):
    subprocess.run([str(arg) for arg in args], check=True, **kwargs)


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--work', type=Path, default=ROOT / 'platforms/linux/ime/.engine')
    parser.add_argument('--jobs', type=int, default=min(6, os.cpu_count() or 2))
    args = parser.parse_args()
    work = args.work.resolve()
    source, prefix = work / 'librime', work / 'install'
    work.mkdir(parents=True, exist_ok=True)
    if not source.exists():
        run('git', 'clone', '--branch', LOCK['librime']['tag'], '--depth', '1', LOCK['librime']['url'], source)
    revision = subprocess.check_output(['git', '-C', str(source), 'rev-parse', 'HEAD'], text=True).strip()
    if revision != LOCK['librime']['commit']:
        raise RuntimeError('Cached librime revision differs from the lock; choose an empty --work directory')
    run(sys.executable, ROOT / 'scripts/input-schemes/repair-rime-table.py', source)
    spec = importlib.util.spec_from_file_location('rimes_lua_policy', ROOT / 'platforms/ios/scripts/prepare-lua.py')
    policy = importlib.util.module_from_spec(spec)
    spec.loader.exec_module(policy)
    policy.prepare(work)
    env = dict(os.environ, RIME_PLUGINS='lua')
    run('cmake', '-S', source, '-B', work / 'build', '-DCMAKE_BUILD_TYPE=Release',
        '-DCMAKE_INSTALL_PREFIX=' + str(prefix), '-DBUILD_TEST=OFF', '-DENABLE_EXTERNAL_PLUGINS=OFF',
        '-DBUILD_MERGED_PLUGINS=ON', '-DENABLE_LOGGING=ON', '-DENABLE_TIMESTAMP=OFF', env=env)
    run('cmake', '--build', work / 'build', '--target', 'install', '--parallel', max(1, args.jobs), env=env)
    print('Pinned Rime/Lua runtime:', prefix)
    print('Build the frontend with RIMES_IME_RIME_PREFIX set to this prefix.')


if __name__ == '__main__':
    main()
