---
name: xsime-project
description: XSIME (犀术IME) five-platform input method development guide for macOS, iOS, Android, Windows and Linux. Use when changing the Rime/librime bridges, key routing, composition, candidates, scheme import and per-scheme preferences, settings, Buffer, Capsule, plugins, permissions, installers or release records; preserve compatibility identifiers, user data and each platform's text-delivery contract. Formerly `rimes-project`.
---

# XSIME Project

## Start Here

Use this skill for code changes in `~/Documents/05-dev/apps/rimes` (repository `scholay/xsime`).

1. Identify the platform and the layer: key routing, composition, candidates, engine bridge, scheme import/preferences, settings, Buffer/Capsule/plugins, permissions, install/release, or docs.
2. Read the matching source, `ARCHITECTURE.md` / `SYSTEM-ARCHITECTURE.md`, and for the current version the plan in `docs/plans/input-first-20261020.md`.
3. For nontrivial macOS IME, Rime, candidate-window or install work, also read `references/fable-architecture.md`. For Buffer surfaces read `references/buffer-ui.md` (macOS; the Linux Buffer spec in `platforms/linux/buffer/SPEC.md` builds on it).

## Naming And Compatibility

Follow `BRANDING.md`. Display names are 犀术IME / 犀術IME / XSIME; RIMES, 灵犀输入法 and Lingxi IME appear only in history and migration notes. Never rename compatibility identifiers to match the brand: the `RIMES.app` bundle and `RIMES` executable, the `RimeBuffer` SwiftPM target, bundle IDs and input-source IDs, Windows CLSIDs and registry keys, Android applicationId, Linux package and addon IDs, `~/Library/RIMES` and every other user-data path, preference keys, scheme IDs such as `lingxi_wubi`, plugin IDs, and published asset names. Rime / librime keep their upstream names.

## Platform Map

| Platform | Entry points | Contract to preserve |
|---|---|---|
| Shared | `Shared/SchemeImport/` (ZIP and TXT/TSV import), `Shared/InputHabits/` (per-scheme switches), `Shared/Sources/RimesCore/` | One portable meaning for scheme options; overrides keyed by `builtin:<schema>` or `<package-id>:<schema>`, missing keys inherit, undo restores only values this product owned. |
| macOS | `Sources/RimeBuffer/` (IMK controller, `CRimeBridge`, `CandidateWindow`, `SettingsWindow.swift`, `ShortcutPreferences.swift`, Buffer/Capsule) | Single process; `Delivery.insert` is the only delivery path; marked text while composing; one Rime session per `IMKInputController`; append-only `CRimeBridge` vtable. |
| iOS | `platforms/ios/App/`, keyboard extension, `Shared/Sources/RimesCore/KeyboardPreferences.swift` | App and extension share preferences through revisions; no private APIs to identify the host app. |
| Android | `platforms/android/app/src/main/java/org/scholay/rimes/android/` (`RimesInputMethodService.java`, settings, JNI) | Engine work on the serial engine queue; preferences applied atomically; one touch produces exactly one output. |
| Windows | `platforms/windows/native/src/tsf/`, `engine/`, `workbench/` | Ordinary TSF input in every host, including LPAC/search hosts; settings never steal focus; x64 and x86 behave the same. |
| Linux | `platforms/linux/ime/src/` (Fcitx5 addon), `platforms/linux/buffer/` | Coexist with other Fcitx5 input methods; deployment can be cancelled on exit; never write other input methods' data. |

## macOS Rules

- Never revive external UI polling, `state.json`, paste injection or Accessibility text injection for normal commits.
- Keep `~/Library/RIMES` isolated from Squirrel's `~/Library/Rime`, and preserve it during development installs.
- Raise permission prompts only from an explicit action, through `CapturePermissionGuide`; never from the typing or opening path.

## Checks And Installs

- Product builds and automated tests are authoritative on remote CI (maintainer requirement, `docs/plans/input-first-20261020.md` §8): `ci.yml`, `release.yml` (macOS), `ios.yml`, `android.yml`, `windows-ime.yml` / `windows-native.yml`, `linux-ime.yml` / `linux-deb.yml`. Push the branch and read the run results instead of claiming a pass.
- When a local macOS check is wanted: plain `swift build` fails on this Mac; use `DEVELOPER_DIR=/Applications/Xcode.app/Contents/Developer xcrun swift build` with the macOS SDK, then run the relevant `.build/debug/RimeBuffer <name>-smoke`.
- Install locally only when the maintainer asks for a live test. `build_install.sh` installs per user and refuses while the release package's `/Library/Input Methods/RIMES.app` exists. To test over the release copy, build a package and sign the app with the Apple Development identity (never Developer ID, which is reserved for releases), so privacy grants survive rebuilds. Verify the running build from a new log field or string, not from `pkgutil`.
- Before handing work back: `git diff --check`, inspect the diff, and confirm `.build/` and `*.app/` stay ignored.

## Issues And Releases

Record each item as: issue → commit → automated check → real-host confirmation → release version (`docs/plans/input-first-tracking-20261022.md`). A merge, a compile or a green CI run alone does not close an issue; a release version is read back from the published assets, not inferred from merge dates.

## Debugging

macOS: `~/rimebuffer.log` for behavior and `~/Library/RIMES/*.log` for librime. For real typing issues capture the host bundle id, schema id, key path, Rime handled flag, commit path, composition mode, candidate-window rect source and whether Buffer is open.

## References

- `references/fable-architecture.md`: macOS IME/Rime architecture intent, failure modes and non-negotiable contracts.
- `references/buffer-ui.md`: macOS Buffer model and surface behavior, display requirements and safe extension points.
