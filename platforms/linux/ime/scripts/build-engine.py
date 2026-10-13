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

ROOT = Path(__file__).resolve().parents[4]
LOCK = json.loads((ROOT / 'platforms/ios/dependencies.lock.json').read_text())


def run(*args, **kwargs):
    subprocess.run([str(arg) for arg in args], check=True, **kwargs)


def repair_table_image_relocation(source):
    """Keep table pointers valid when a small dictionary grows its mmap.

    Rime 1.17's final string-table allocation can exceed its initial estimate.
    On Linux the enlarged mapping may move, invalidating the cached pointers
    before the format header is written. Relative on-disk pointers remain valid.
    """
    path = source / 'src/rime/dict/table.cc'
    original = '''  size_t image_size = string_table_builder_->BinarySize();
  char* image = Allocate<char>(image_size);
  if (!image) {
    LOG(ERROR) << "Error creating string table image.";
    return false;
  }
  string_table_builder_->Dump(image, image_size);'''
    repaired = '''  size_t image_size = string_table_builder_->BinarySize();
  // RIMES: the final allocation can relocate the mmap for small dictionaries.
  const auto metadata_offset = reinterpret_cast<char*>(metadata_) - address();
  const auto syllabary_offset = reinterpret_cast<char*>(syllabary_) - address();
  const auto index_offset = reinterpret_cast<char*>(index_) - address();
  char* image = Allocate<char>(image_size);
  if (!image) {
    LOG(ERROR) << "Error creating string table image.";
    return false;
  }
  metadata_ = Find<table::Metadata>(metadata_offset);
  syllabary_ = Find<table::Syllabary>(syllabary_offset);
  index_ = Find<table::Index>(index_offset);
  string_table_builder_->Dump(image, image_size);'''
    text = path.read_text()
    if repaired in text:
        return
    if text.count(original) != 1:
        raise RuntimeError('Pinned Rime table implementation changed; review the relocation repair')
    path.write_text(text.replace(original, repaired))


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
    repair_table_image_relocation(source)
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
