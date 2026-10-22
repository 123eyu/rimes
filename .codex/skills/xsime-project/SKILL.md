---
name: rimes-project
description: RIMES macOS IME architecture, development, testing, and live-install guidance. Use when changing the Swift/InputMethodKit input method, librime bridge, composition, candidate window, status menu, settings, Buffer, plugins, or installer; preserve IMK insertion, Rime sessions, user data, and nonactivating panel behavior.
---

# RIMES Project

## Start Here

Use this skill for code changes in `~/Documents/05-dev/apps/rimes`.

Before editing, read the relevant local source and `ARCHITECTURE.md`. For nontrivial IME, Rime, candidate-window, or install changes, also read `references/fable-architecture.md`. For buffer mode, staging strip, flush behavior, or block display changes, read `references/buffer-ui.md`.

## Project Rules

- Keep RIMES single-process: IMK controller, librime bridge, candidate window, Buffer, status menu, and settings live inside the IME app.
- Never revive external UI polling, `state.json`, paste injection, or Accessibility text injection for normal commits.
- Keep `Delivery.insert` as the only text delivery path into the focused client.
- Keep marked text active while composing; this prevents raw-letter leaks and makes caret rect lookup reliable.
- Keep one Rime session per `IMKInputController`; do not reintroduce a shared session.
- Treat `CRimeBridge.cpp` vtable order as load-bearing; only append wrappers unless the ABI is revalidated.
- Keep `~/Library/RIMES` isolated from Squirrel's `~/Library/Rime` until a deliberate sync/direct-use migration is implemented.
- Preserve `~/Library/RIMES` during development installs. Do not delete it or reseed it from Squirrel as part of a routine update.

## Workflow

1. Identify the layer being changed: key routing, composition, candidate window, bridge, buffer, menu/settings, install, or docs.
2. Read the matching files before editing. Prefer local patterns over new abstractions.
3. Make scoped edits. Avoid unrelated cleanup in IME code because small timing changes can alter input behavior.
4. Run the basic checks for the changed layer: `swift build -c debug`, a relevant `.build/debug/RimeBuffer *-smoke` (and `smoke` for Rime startup, bridge, schema, or key processing), plus design-system checks when its source changes. Fix failures before installing.
5. Check `git diff --check` and inspect the diff. Confirm `.build/` and `*.app/` remain ignored.
6. Once those checks pass, build and install the current source with `RB_KEEP_USERDB=1 ./build_install.sh`. The script safely switches the active source, replaces the previous per-user RIMES bundle, removes legacy ETInput/RimeBuffer bundles, and restarts the input method. Announce the brief restart before running it; user authorization to develop this app includes this routine local update unless the user explicitly asks for source-only work.
7. Verify the installed bundle and its signature, the selected RIMES input source, and the process running from `~/Library/Input Methods/RIMES.app`. If activation is pending or the old version remains, report the exact state rather than calling the install complete.

## Debugging

Use `~/rimebuffer.log` for high-level behavior and `~/Library/RIMES/*.log` for librime logs. For real typing issues, capture: active app bundle id, schema id, key path, Rime handled flag, commit path, composition mode, candidate window rect source, and whether buffer mode is enabled.

## References

- `references/fable-architecture.md`: Fable-era architecture intent, failure modes, and non-negotiable IME/Rime contracts.
- `references/buffer-ui.md`: Buffer model/surface behavior, display requirements, and safe extension points.
