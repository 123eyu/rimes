# Windows Capsule card actions — 2026-10-18

Local development version: `1.1.5-win-capsule-actions.20261018`.
Source base: `823c910b600c14f674e41ac906435eea0757972f` plus the preserved working tree.
Source snapshot: `361d10f45b78b01ae38770ba4e6f3c2fada7fd61e48e501ee42b56b61e9b4a13`.

Implemented against the current macOS history controller, auto-paste helper,
copy toast and store:

- Recent image double-click/Enter prepares PNG/DIB, closes the rail and dispatches
  one delayed native paste to the unchanged exact target. Text retains TSF delivery.
- Copy copies the selected card, closes Capsule and shows a short themed,
  nonactivating, click-through hint. Failed writes keep the window open.
- Recent uses persistent content deduplication and last-promotion ordering;
  repeats and explicit use promote the original identity without changing capture
  time. Saved modules keep update-time ordering. Own writes do not recapture.

Validation: x64/x86 `RimesPeripherals` and `RimesRuntime` passed, plus the x64 native
window fixture. Logs and rendered fixtures are in this directory. Coverage includes
same-process focus changes and return, foreground changes, clipboard replacement,
composition, lock/reopen/timeout cancellation, held keys, partial/blocked native
input dispatch, copy errors, toast lifecycle, legacy records, restart deduplication,
stable sorting, and the previous hover/module-persistence regressions.

Tests use isolated profiles and injected clipboard/input boundaries. They do not
paste into user applications. Actual image acceptance by a third-party application
has not been exercised here; unsupported fields may ignore image clipboard formats.
The success hint reports dispatch, not confirmed insertion. Missing/stale targets
fall back to copying with a manual-paste hint.

`verification.json` records the completed installation, running Broker, source and
binary checksums, preserved settings/history, backup location and pending sign-out.
