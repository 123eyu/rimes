# Portable scheme import integration

The source change in PR #126 builds on the Windows Buffer work merged by PR #124 and the iOS build-55 source record merged by PR #125. It does not publish or replace release installers.

## Automated evidence

- macOS: debug build, native engine startup, text-table import/deploy/activate/type/restore/remove, and untouched Wanxiang full-Pinyin and T9 candidate/commit probes passed.
- iOS: 65 hosted import/library/layout/runtime tests validated across two runs, with one optional legacy-fixture skip. The importer subset was rerun after renaming a fixture that used the Windows-reserved basename `aux`. Original Wanxiang full-Pinyin/T9 and text-table runtime probes passed in the simulator.
- Android: 95 core tests passed with the original upstream ZIP enabled, including five native T9 layout-pairing regressions. The Java classifier also recognized both directions of the actual compiled Wanxiang pair. JNI, application and instrumentation APK builds passed. Service field-boundary/private-directory regressions and original T9 typing are included in instrumentation, which has not run on a phone.
- Linux: Ubuntu 24.04 container build and 15 CTest cases passed; original Wanxiang full-Pinyin/T9 passed with the pinned Rime/Lua runtime. An older distro runtime lacking required Lua functionality rejected the import before activation.
- Common-family regression: [common-families.json](common-families.json) records exact archive hashes for ten scheme imports, eight with actual candidate selection and commit. These are historical catalog-generated packages, not untouched upstream archives. Zhengma and Tiger have import/load coverage only.
- Windows: MSVC x64/x86 build, CTest, and native `rime.dll` import/typing checks run in PR #126 CI. The initial run exposed a duplicate settings control ID between scheme management and Buffer close-after-send. The fix gives them separate IDs and tests independent dispatch. Use the latest commit's CI results for the final outcome.

## Host and distribution boundaries

Android physical testing is deferred at the maintainer's request. Young SSH was unavailable, so the new Windows scheme-manager GUI has not been accepted on that desktop. macOS development installation stopped at the duplicate-install guard because an existing system-wide bundle is present; it was not replaced. Simulator, container and CI results do not establish physical-host acceptance, and no public package was updated.

The portable contract, user entry points, runtime limits and original upstream fixture are documented in [portable-scheme-import.md](../../docs/portable-scheme-import.md).
