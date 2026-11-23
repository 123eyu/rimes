#!/usr/bin/env python3
"""Fetch verified source/tool inputs; never fetch executable input schemes."""
from pathlib import Path
import importlib.util,subprocess,tarfile,zipfile,json
ROOT=Path(__file__).resolve().parents[3]
WORK=ROOT/'Vendor/ios-build'
LOCK=json.loads((ROOT/'platforms/ios/dependencies.lock.json').read_text())
# HTTPS only, retried, and kept only on a SHA-256 match; shared with the Android engine build.
spec=importlib.util.spec_from_file_location('rimes_pinned_download',Path(__file__).with_name('pinned_download.py'))
PINNED=importlib.util.module_from_spec(spec);spec.loader.exec_module(PINNED)
def run(*args):subprocess.run([str(a) for a in args],check=True)
def tracked(path):
    return subprocess.run(['git','-C',str(ROOT),'ls-files','--error-unmatch','--',str(path)],stdout=subprocess.DEVNULL,stderr=subprocess.DEVNULL).returncode==0
def download(item,path,**options):
    # A committed copy is reviewed source, not a download cache: report a mismatch instead of replacing it.
    if path.is_file() and tracked(path):
        if PINNED.sha256(path)!=item['sha256']:raise RuntimeError('Checksum mismatch: '+str(path)+' is committed but differs from the lock')
        return
    PINNED.fetch(item,path,**options)
def main():
    WORK.mkdir(parents=True,exist_ok=True)
    src=WORK/'librime'
    if not src.exists():
        run('git','clone','--depth','1','--branch',LOCK['librime']['tag'],LOCK['librime']['url'],src)
    if subprocess.check_output(['git','-C',str(src),'rev-parse','HEAD'],text=True).strip()!=LOCK['librime']['commit']:raise RuntimeError('Unexpected librime revision')
    run('git','-C',src,'submodule','update','--init','--depth','1','deps/leveldb','deps/marisa-trie','deps/yaml-cpp','deps/opencc')
    for dep,sha in LOCK['submodules'].items():
        if subprocess.check_output(['git','-C',str(src/'deps'/dep),'rev-parse','HEAD'],text=True).strip()!=sha:raise RuntimeError('Unexpected dependency: '+dep)
    boost=WORK/'boost.tar.bz2';download(LOCK['boost'],boost)
    if not (WORK/'boost_1_86_0').exists():
        with tarfile.open(boost) as t:
            # Python 3.12+ data filter rejects path traversal / dangerous links.
            t.extractall(WORK,filter='data')
    archive=WORK/'xcodegen.zip';download(LOCK['xcodegen'],archive)
    if not (WORK/'xcodegen/bin/xcodegen').exists():
        with zipfile.ZipFile(archive) as z:z.extractall(WORK)
        (WORK/'xcodegen/bin/xcodegen').chmod(0o755)
    for item in LOCK['data']:
        path=ROOT/item['path'];path.parent.mkdir(parents=True,exist_ok=True);download(item,path)
    print('Source and tool inputs verified')
if __name__=='__main__':main()
