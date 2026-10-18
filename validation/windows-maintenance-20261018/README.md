# Windows branch consolidation — October 17–18, 2026

The Windows work is consolidated on `codex/windows-optimization-20261016`.
Canonical plugin changes are maintained on
`scholay/rimes-plugins:codex/windows-metrics-20261017`; the host gitlink and
`plugins.lock.json` identify the same reviewed plugin revision.

The change includes Buffer focus and plugin switching, last-plugin restoration,
live typing metrics, timed delivery and close-after-send, model/reasoning choices,
streaming blocks, translation lifecycle, Fluent icons, immediate settings,
home/connector pages, Capsule screenshots and clipboard history, outside-click
cancellation, storage cleanup, Mailbox persistence, and statistics/practice pages.
Feature and acceptance boundaries remain documented in the native README,
CONNECTORS.md, METRICS.md and PERIPHERALS.md. Unimplemented actions remain disabled.

## Validation

`checks.json` retains the full native attempts and final results:

- Release builds succeeded for x64 and x86; all 37 CTest cases per architecture
  passed, including the targeted reruns below.
- Updated the stale Default height assertions to include its typing-metrics row
  and to construct an explicit translation-mode fixture.
- Added `/utf-8` to the MSVC scheme-import test target so Unicode collision
  fixtures use the same encoding as the production library.
- x64 settings focus and x86 process-cancellation/settings-startup timing checks
  failed during the first full runs and passed when rerun serially. Their
  timeouts and production behavior were not relaxed to obtain a pass.
- Plugin-import tests: 5; catalog tests: 2; Windows data tests: 11;
  plugin-package tests: 7. Plugin package tests require Python UTF-8 mode on
  this Windows machine (`python -X utf8`); its default GBK run failed decoding
  existing UTF-8 fixtures before executing the tests.
- Catalog consistency, plugin repository/license checks, practice-article
  parity, staged whitespace checks and credential-pattern review passed.
- The upstream Fluent font is marked binary and its staged bytes match the
  original, avoiding the broad Windows text normalization rule.

Native UI and local installation evidence from the preceding changes remains
in the dated validation records, most recently windows-capsule-lifecycle-20261018.
Generated screenshots, raw logs, superseded patch snapshots and accidental
Windows cache directories remain local; they are not required to build the source.
This maintenance does not reinstall the app, publish a release, or assert new
macOS/real-application acceptance. Remote CI results are separate from these
local checks.
