# Imported Rime Lua on iOS

`build-engine.py` invokes `prepare-lua.py`, verifies the exact upstream revisions in
`versions.json`, applies the local boundary from pristine pinned sources and merges
the Lua plugin into each librime library. The module is named `lua`. Runtime native
plugin loading remains disabled. There is no Lua JIT or runtime native code download.

The Lua archive is fetched over HTTPS from the lock's `url`, then its `mirrors`
(the official PUC-Rio mirror), with a timeout and three attempts per source. Only
bytes matching the locked `sha256` are kept; any other download is discarded.
Network-free tests of this fallback are in
`platforms/linux/ime/tests/test_lua_download.py`, run by the Linux IME checks.

Sources:

- [librime-lua](https://github.com/hchunhui/librime-lua/tree/6f30968058a3ca83c47949308ef0ddc51a11a264)
- [Lua release hashes](https://www.lua.org/ftp/)

## Capabilities

- Lua source `require`, `loadfile`, `dofile`, `load`; binary chunks are rejected.
- Lua strings, tables, math, coroutines, UTF-8 and librime bindings remain available.
- `debug.getinfo` and `debug.traceback` remain for relative source paths and diagnostics.
  Registry, hook, stack-local and upvalue access are removed, including `require('debug')`.
- `os.date`, `os.time`, `os.clock`, `os.difftime` remain. Shell execution, process exit,
  environment access and locale changes are unavailable.
- `io.open` and named `io.lines` may read inside the current engine user or shared
  directories. Writes, remove and rename are confined to the current user directory.
  Relative paths resolve under the user directory. Canonical paths resolve symlinks
  before checking containment. Standard streams, temporary files and subprocesses
  are unavailable.
- A missing file read under the user directory may fall back to the same relative
  path in the shared directory. Existing user files win. Writes never target shared
  files. This supports Lua that hardcodes `get_user_data_dir()` for bundled source
  dictionaries without copying megabytes into the keyboard at first launch.
  `require` probes retain their original search ordering, including user `?/init.lua`.
  This is a read-only overlay; a first append creates a new user file and does not
  copy shared seed records. Imports that ship editable pre-existing state require a
  separate seed/copy-on-write policy (the supplied package's dynamic store is empty).
- Native shared-library loading and C module searchers are removed. No socket, HTTP,
  FFI, LuaFileSystem or other native extension is bundled. Optional `pcall(require,
  'lfs')` can use a scheme's fallback. Desktop app-launch actions cannot work on iOS.
- Rime Config load/save paths use the same read/write roots; Schema and user/reverse
  dictionary names reject traversal and path separators. OpenCC entry paths are
  confined to the engine roots. The importer must additionally validate every JSON
  dictionary reference; OpenCC reads nested dictionary files in native code.

## Budgets and diagnostics

Each outer Lua entry (component init, call or translator/filter resume) has a maximum
of 50,000,000 VM instructions or one second, checked every 1,000 instructions. Nested
calls share the budget. Protected calls and coroutines rethrow an expired budget so
Lua cannot catch an overrun and loop indefinitely. The Lua allocator is capped at
32 MiB per state. Native librime/OpenCC allocations and time spent in native calls
are outside these Lua limits; archive/file sizes and dictionary resources must also
be bounded by the importer. Many separate short component calls can cumulatively
take longer than one second; this is not a whole-keystroke wall-clock deadline.

`rimes_lua_sandbox.h` ships in the XCFramework headers. The bridge serializes engine
operations and uses:

```c
void rimes_lua_clear_error(void);
const char *rimes_lua_last_error(void);
```

Clear immediately before a high-level operation; inspect immediately after it.
The first unhandled source/module/init/runtime error remains until cleared.
An empty string means no Lua error was reported. Expected, caught Lua errors (for
example optional `require('lfs')`) are not failures. Returned storage is thread-local
and remains valid until the next clear/record on that thread. A missing callable Lua
component is also recorded, so selection cannot silently treat a broken component
as a working import.

## Verification

```sh
python3 platforms/ios/scripts/build-engine.py host device simulator
python3 platforms/ios/scripts/test-lua-sandbox.py
```

The host smoke executable uses the same compiled Lua and bindings as the mobile
slices. It verifies successful source loading and user writes, read/write traversal,
symlink and native-binding restrictions, disabled process/native/network escape
surfaces, protected-call/coroutine infinite loops, memory limits and continued use
after rejected code. Device/simulator linkage and real scheme behavior are separate
integration checks.

The instruction limit permits the supplied scheme's one-time 410,710-line
`eng_quick_exclude` source scan. With the original 10M instruction limit the host
stopped this valid scan after ~0.43 seconds; the scan itself completes in ~0.65
seconds without the instruction hook. Real device timing still requires an input
check of `iabc`/`ihello` and the protected `ia`/`ii`/`io`/`iu` codes under the final
budget; the one-second wall deadline remains enforced.
