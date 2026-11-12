import importlib.util
from pathlib import Path
import sys
import tempfile
import unittest
from unittest import mock


REPO = Path(__file__).resolve().parents[4]
SPEC = importlib.util.spec_from_file_location(
    'linux_private_runtime', REPO / 'platforms/linux/ime/scripts/bundle-private-runtime.py')
BUNDLE = importlib.util.module_from_spec(SPEC)
SPEC.loader.exec_module(BUNDLE)


class FakeTools:
    """ldd, patchelf and dpkg-query over an in-memory dependency table."""

    def __init__(self, needed, system):
        self.needed = {str(path): list(names) for path, names in needed.items()}
        self.system = system
        self.sonames = {}
        self.rpaths = {}

    def closure(self, path):
        seen, queue = [], list(self.needed.get(path, []))
        while queue:
            name = queue.pop(0)
            if name not in seen:
                seen.append(name)
                queue += self.needed.get(str(self.system[name]), [])
        return seen

    def run(self, command, **_):
        tool, *args = command
        if tool == 'ldd':
            lines = [f'\t{name} => {self.system[name]} (0x0)' for name in self.closure(args[0])]
            return mock.Mock(stdout='\n'.join(lines), returncode=0)
        assert tool == 'patchelf', command
        if args[0] == '--set-soname':
            self.sonames[args[2]] = args[1]
        elif args[0] == '--set-rpath':
            self.rpaths[args[2]] = args[1]
        elif args[0] == '--replace-needed':
            old, new, path = args[1:]
            self.needed[path] = [new if name == old else name for name in self.needed[path]]
        else:
            raise AssertionError(command)
        return mock.Mock(returncode=0)

    def check_output(self, command, **_):
        tool, *args = command
        if tool == 'patchelf' and args[0] == '--print-needed':
            return '\n'.join(self.needed.get(args[1], [])) + '\n'
        assert tool == 'dpkg-query', command
        return 'fake-package:amd64: ' + args[1] if args[0] == '-S' else '1.0-1'


class PrivateRuntime(unittest.TestCase):
    def test_only_logging_libraries_get_private_sonames(self):
        self.assertEqual(BUNDLE.private_soname('libglog.so.1'), 'librimes-glog.so.1')
        self.assertEqual(BUNDLE.private_soname('libgflags.so.2.2'), 'librimes-gflags.so.2.2')
        for soname in ('libyaml-cpp.so.0.8', 'libicuuc.so.74', 'libboost_regex.so.1.83.0',
                       'libglogger.so.1', 'libunwind.so.8'):
            self.assertEqual(BUNDLE.private_soname(soname), soname)

    def test_bundle_isolates_glog_and_gflags_from_the_host_process(self):
        with tempfile.TemporaryDirectory() as temporary:
            base = Path(temporary).resolve()
            root, distro = base / 'root', base / 'distro'
            private = root / 'usr/lib/x86_64-linux-gnu/rimes'
            addon = root / 'usr/lib/x86_64-linux-gnu/fcitx5/librimes.so'
            rime = private / 'librimes-rime.so.1'
            tool = root / 'usr/bin/rimes-schemes'
            text = root / 'usr/share/rimes/data/default.yaml'
            for path in (addon, rime, tool):
                path.parent.mkdir(parents=True, exist_ok=True)
                path.write_bytes(b'\x7fELF fixture')
            text.parent.mkdir(parents=True)
            text.write_text('not an ELF file')
            distro.mkdir()
            system = {}
            for soname in ('libglog.so.1', 'libgflags.so.2.2', 'libunwind.so.8', 'libyaml-cpp.so.0.8'):
                system[soname] = distro / soname
                system[soname].write_bytes(b'\x7fELF ' + soname.encode())
            # The engine is the only direct glog user; glog itself needs gflags.
            tools = FakeTools({
                addon: ['librimes-rime.so.1'],
                tool: ['librimes-rime.so.1'],
                rime: ['libglog.so.1', 'libyaml-cpp.so.0.8'],
                system['libglog.so.1']: ['libgflags.so.2.2', 'libunwind.so.8'],
            }, system)
            # The engine was copied into the private directory before this step.
            tools.system['librimes-rime.so.1'] = rime
            shlibs = base / 'shlibs.local'
            shlibs.write_text('librimes-rime 1 fcitx5-rimes (= 9.9.9)\n')
            real_is_file, real_copyfile = Path.is_file, BUNDLE.shutil.copyfile

            def copyfile(source, target, **options):
                # Package copyright notices live outside the fixture.
                if not str(source).startswith('/usr/share/doc/'):
                    return real_copyfile(source, target, **options)
                return target

            def copy2(source, target):
                # A copied library keeps the dependencies recorded in its ELF.
                real_copyfile(source, target)
                tools.needed[str(target)] = list(tools.needed.get(str(source), []))

            argv = ['bundle', str(root), '/usr/lib/x86_64-linux-gnu/rimes', '9.9.9', str(shlibs)]
            with mock.patch.object(BUNDLE.subprocess, 'run', tools.run), \
                    mock.patch.object(BUNDLE.subprocess, 'check_output', tools.check_output), \
                    mock.patch.object(BUNDLE.shutil, 'copyfile', copyfile), \
                    mock.patch.object(BUNDLE.shutil, 'copy2', copy2), \
                    mock.patch.object(Path, 'is_file', lambda path: str(path).startswith('/usr/share/doc/')
                                      or real_is_file(path)), \
                    mock.patch.object(sys, 'argv', argv):
                BUNDLE.main()

            shipped = sorted(path.name for path in private.iterdir())
            self.assertEqual(shipped, ['librimes-gflags.so.2.2', 'librimes-glog.so.1', 'librimes-rime.so.1',
                                       'libunwind.so.8', 'libyaml-cpp.so.0.8'])
            # Renamed copies carry the private SONAME; the others keep theirs.
            self.assertEqual(tools.sonames, {
                str(private / 'librimes-glog.so.1'): 'librimes-glog.so.1',
                str(private / 'librimes-gflags.so.2.2'): 'librimes-gflags.so.2.2',
            })
            # Every packaged user follows the rename, including the private glog itself.
            self.assertEqual(tools.needed[str(rime)], ['librimes-glog.so.1', 'libyaml-cpp.so.0.8'])
            self.assertEqual(tools.needed[str(private / 'librimes-glog.so.1')],
                             ['librimes-gflags.so.2.2', 'libunwind.so.8'])
            packaged = [path for path in tools.needed if path.startswith(str(root))]
            self.assertTrue(packaged)
            for path in packaged:
                for name in tools.needed[path]:
                    self.assertNotRegex(name, r'^lib(glog|gflags)\.so\.', path)
            # The distro's own copies are never rewritten.
            self.assertEqual(tools.needed[str(system['libglog.so.1'])], ['libgflags.so.2.2', 'libunwind.so.8'])
            for name in shipped:
                if name != 'librimes-rime.so.1':
                    self.assertEqual(tools.rpaths[str(private / name)], '$ORIGIN')
            self.assertEqual(shlibs.read_text().splitlines(), [
                'librimes-rime 1 fcitx5-rimes (= 9.9.9)',
                'librimes-gflags 2.2 fcitx5-rimes (= 9.9.9)',
                'librimes-glog 1 fcitx5-rimes (= 9.9.9)',
                'libunwind 8 fcitx5-rimes (= 9.9.9)',
                'libyaml-cpp 0.8 fcitx5-rimes (= 9.9.9)',
            ])
            receipt = (root / 'usr/share/doc/fcitx5-rimes/third-party/runtime-libraries.tsv').read_text()
            self.assertIn('librimes-glog.so.1\tfake-package:amd64\t1.0-1\n', receipt)
            self.assertNotIn('\nlibglog.so.1\t', '\n' + receipt)


if __name__ == '__main__':
    unittest.main()
