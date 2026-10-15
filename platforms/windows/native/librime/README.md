# Pinned librime for Windows

CI and local Broker smokes fetch a **pinned official librime** instead of
hoping a Weasel install is already present.

| Field | Value |
| --- | --- |
| Source | [rime/librime 1.17.0](https://github.com/rime/librime/releases/tag/1.17.0) |
| Commit | `33e78140250125871856cdc5b42ddc6a5fcd3cd4` (`33e7814`) |
| x64 archive | `rime-33e7814-Windows-msvc-x64.7z` |
| x64 SHA-256 | `7478c7caa4ff6b37de86daba1f7ce4a994a4f5ba24872a820fb2b3a9b01fed15` |
| x86 archive | `rime-33e7814-Windows-msvc-x86.7z` |
| x86 SHA-256 | `af235c26c06152ce09ceb8fe9d9ab9fba7ab43ce30aa952b40174d806f5cc3d9` |

The lock file is `librime-windows.lock.json`. Fetch with:

```powershell
../scripts/Fetch-RimesLibrime.ps1 -Architecture x64
```

The MSVC `rime.dll` statically includes **librime-lua** (Lua 5.4.8) and
**OpenCC**. The archive does **not** include OpenCC conversion tables; those
come from this repository's `rime-data/opencc` when a shared-data tree is
assembled. It also does not ship a separate `rime-lua.dll`.

The isolated CI schema under `testdata/e2e` is lua-free so typing tests do not
depend on the product `rime_ice` script tree. A later real install that wants
雾凇 still needs the XSIME `rime-data` lua modules next to the shared schemas.
