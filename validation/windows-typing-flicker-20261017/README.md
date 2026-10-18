# Windows typing-page repaint repair — 2026-10-17

The practice page layout and scoring behavior are unchanged. This update fixes repeated repainting in the native extension settings view.

Root cause: both RichEdit controls send `WM_COMMAND / EN_UPDATE` while painting. The page's generic command handler called `RedrawWindow(..., RDW_ALLCHILDREN)` even for these paint notifications, creating a feedback loop. A native-window probe observed 35–42 page paints in a 650 ms idle observation plus explicit timer probes; removing parent composition only made the loop faster. The final fix keeps the existing parent composition style.

Changes in canonical `OfficialPlugins/native/windows/metrics_ui.cpp`:

- Ignore RichEdit paint notifications; only an actual editor `EN_CHANGE` publishes changed text. Accept selector-change and button-click notifications explicitly.
- Skip unchanged practice snapshots on the 200 ms timer. Changed metrics invalidate only the status, metrics, progress, curve and detail regions, leaving the picker/article/editor untouched.
- Compose the background, cards and text in a memory DC, then present the completed frame together.

Validation:

- x64 and x86 native metrics/gallery UI suites pass. Each idle probe handles six explicit timer events and twelve RichEdit paint notifications with **zero command-induced paints**; the following 650 ms observe only 0–1 incidental native parent presentations, without a recurring loop. The forced-paint control confirms the probe observes real paints.
- Existing scoring, identical-text correction, practice exclusion from daily totals, assisted-input handling, article changes, stored results, plugin enablement, themes and scrollbar checks continue to pass. Live metric invalidation checks exclude the article and editor.
- UI test message pumping is bounded even when a regression continuously queues paints, so the test can report the loop rather than hang.
- Both architecture settings suites are recorded beside this file. Production screenshots come from owned native fixture windows; no user's text or external app is exercised.
- Plugin source and generated host projection match. Local uncommitted source is captured in the installation manifest; no published release or Git commit is implied.

Installed package identity and data-preservation checks are recorded in `installed-result.json` and `post-install-check.json`. This update changes the Broker UI only; the TSF DLLs match the prior version.

The broader macOS settings parity review and configurable plugin-switch shortcuts were deferred when the user prioritized this flicker repair. Initial source inspection found hardcoded Alt+Shift+Up/Down cycling already present, but its settings controls still need work; no shortcut implementation was changed in this repair.
