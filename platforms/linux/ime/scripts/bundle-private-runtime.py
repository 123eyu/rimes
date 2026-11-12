#!/usr/bin/env python3
"""Keep distro-versioned engine dependencies private to this package.

Build on the oldest supported Ubuntu. Do not copy libc, libstdc++, GTK or
Fcitx: those remain the host's ABI. ICU and the engine's versioned helper
libraries use their original SONAMEs and an application-only RUNPATH.

glog and gflags are the exception. Each loaded copy holds process-wide state:
InitGoogleLogging may run once, and a flag name may be registered once. The
dynamic loader shares one copy per SONAME, so under the distro names a stock
fcitx5-rime in the same Fcitx5 process would get this package's glog (or this
package the distro's), and the second InitGoogleLogging aborts Fcitx5. They
ship under private SONAMEs, like the Rime library, as a pair: two glog copies
on one gflags would register every logging flag twice, which exits.
"""
import argparse
import re
import shutil
import subprocess
from pathlib import Path

ISOLATED = re.compile(r'lib(glog|gflags)\.so\.(.+)')


def output(*args):
    return subprocess.check_output(args, text=True).strip()


def private_soname(soname):
    match = ISOLATED.fullmatch(soname)
    return f'librimes-{match[1]}.so.{match[2]}' if match else soname


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('root', type=Path)
    parser.add_argument('library_path')
    parser.add_argument('version')
    parser.add_argument('shlibs', type=Path)
    args = parser.parse_args()
    private = args.root / args.library_path.lstrip('/')
    notices = args.root / 'usr/share/doc/fcitx5-rimes/third-party'
    private.mkdir(parents=True, exist_ok=True)
    notices.mkdir(parents=True, exist_ok=True)
    # icui18n contains digits in its name and is used by EPUB segmentation.
    allowed = re.compile(r'lib(?:icu[a-z0-9]+|boost_[a-z_]+|glog|gflags|yaml-cpp|opencc|leveldb|marisa|snappy|unwind)\.so\..+')
    libraries = {}
    elf_files = []
    for file in (args.root / 'usr').rglob('*'):
        if not file.is_file() or file.is_symlink():
            continue
        with file.open('rb') as stream:
            if stream.read(4) != b'\x7fELF':
                continue
        elf_files.append(file)
        # ldd includes the transitive closure; all inputs are our own CI build.
        result = subprocess.run(['ldd', str(file)], text=True, capture_output=True)
        if 'not found' in result.stdout:
            raise RuntimeError('Build dependency cannot be resolved: ' + str(file))
        for line in result.stdout.splitlines():
            match = re.match(r'\s*(\S+) => (/\S+) \(', line)
            if match and allowed.fullmatch(match[1]):
                libraries[match[1]] = Path(match[2]).resolve(strict=True)
    receipt = []
    declarations = []
    renamed = {}
    for soname, source in sorted(libraries.items()):
        owner = output('dpkg-query', '-S', str(source)).splitlines()[0].rsplit(': ', 1)[0]
        version = output('dpkg-query', '-W', '-f=${Version}', owner)
        copyright_file = Path('/usr/share/doc') / owner.split(':')[0] / 'copyright'
        if not copyright_file.is_file():
            raise RuntimeError('Missing redistribution notice for ' + owner)
        shutil.copyfile(copyright_file, notices / (owner.replace(':', '_') + '-copyright'))
        shipped = private_soname(soname)
        target = private / shipped
        if target.exists():
            raise RuntimeError('Private dependency collision: ' + shipped)
        shutil.copy2(source, target)
        if shipped != soname:
            subprocess.run(['patchelf', '--set-soname', shipped, str(target)], check=True)
            renamed[soname] = shipped
        subprocess.run(['patchelf', '--set-rpath', '$ORIGIN', str(target)], check=True)
        elf_files.append(target)
        name, abi = shipped.split('.so.', 1)
        declarations.append(f'{name} {abi} fcitx5-rimes (= {args.version})\n')
        receipt.append(f'{shipped}\t{owner}\t{version}\n')
    # Point every packaged user of an isolated library at its private name.
    for file in elf_files:
        needed = set(output('patchelf', '--print-needed', str(file)).split())
        for soname, shipped in renamed.items():
            if soname in needed:
                subprocess.run(['patchelf', '--replace-needed', soname, shipped, str(file)], check=True)
    with args.shlibs.open('a') as stream:
        stream.writelines(declarations)
    (notices / 'runtime-libraries.tsv').write_text(''.join(receipt))
    print('Private runtime:', ', '.join(sorted(private_soname(soname) for soname in libraries)))


if __name__ == '__main__':
    main()
