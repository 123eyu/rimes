# Windows Capsule invocation, recording and cleanup — 2026-10-18

Installed local development version: `1.1.5-win-capsule-lifecycle.20261018`.
Source base: `823c910b600c14f674e41ac906435eea0757972f` plus the preserved working tree.
Source snapshot: `ee2756ce6e6b5f9899c9df9aaba857a09c60fedb3d6ff0be08748d855a4e2bbe`.

## Behavior

- The quick rail immediately accepts search/navigation. A valid TSF target keeps
  the rail nonactivating; detached invocation focuses its native search control.
- An outside mouse down cancels the rail, releases its input lease, and passes
  the original click through. Internal children and owned popups are excluded.
  Stale queued dismissal cannot close a subsequent invocation. Loss of a borrowed
  input lease also dismisses the rail. Manager/editor windows retain normal
  window behavior and unsaved-edit protection.
- Automatic clipboard recording is default-on and controlled only from
  Settings → Capsule. Changes persist immediately. Resuming establishes a new
  clipboard baseline instead of collecting content copied while paused.
- Recent history retains the existing 200-item cap and adds a 1 GiB budget,
  removing least-recently-used records and unreferenced managed attachments.
- New automatic captures follow macOS CaptureStore's 30-day/10-GiB pruning
  policy. The card menu can mark a capture for permanent retention. Saved/imported
  and legacy unmarked captures remain permanent. Active cards, capture editors,
  stack entries, shared references and currently copied attachments are protected.
- Cleanup scans every module, including disabled modules, before deleting files.
  Unreferenced managed files have a one-hour grace period unless retired by the
  current cleanup. Corrupt metadata aborts before deletion and prevents further
  automatic collection. External source files and resource directories are not
  traversed or removed.

macOS references: `ClipboardHistoryModel.swift`, `ClipboardHistoryStore.swift`,
`ClipboardHistoryWindowController.swift`, and `Capture/CaptureStore.swift`.
Windows deliberately keeps its smaller recent-history budget instead of adopting
macOS's 100,000-item/64-GiB defaults. This is source-based parity, not a macOS
device comparison or a claim that every Capsule feature is implemented.

## Validation

- x64/x86 RimesPeripherals and RimesRuntime passed.
- x64/x86 RimesSettingsUi passed, including recording persistence and existing
  plugin shortcut controls. The x64 combined log includes the intermediate
  failure; `settings-ui-x64.log` is the final successful rerun. The control-ID
  collision was fixed; the fixture was corrected to show its window before
  asserting visibility.
- x64 native fixture passed with 60 images, including immediate search,
  navigation, internal/popup/outside click routing, cancellation generations,
  input lease loss, paused collection baseline and previous card/hover regressions.
- Production visual export completed with 165 images. Inspected light/dark
  Capsule settings and the light quick rail; controls and cleanup copy fit.
- Scoped whitespace review passed against the previous installed source.

Tests use isolated profiles, owned fixture HWNDs and injected clipboard/input
boundaries. Outside click routing is exercised through the shared observer;
no real user-app clicks, typing or paste are synthesized. Actual third-party
application behavior and global-hook timing remain manual acceptance boundaries.

## Local installation

`verification.json` records the verified installation, sole running new Broker
(PID 120432 at verification), matching binary hashes and 157 source files,
unchanged settings and all 57 existing peripheral files, and the backup location.
User dictionaries and user data were retained. The new Broker is running; the
installer still reports sign-out required for existing hosts to fully reload the
registered TSF DLL. No old loaded DLL was overwritten.
