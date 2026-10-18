# 更新日志

本文件由 `python3 scripts/release/release_tool.py changelog --write` 根据公开 GitHub Release 与
[Conventional Commits](https://www.conventionalcommits.org/zh-hans/v1.0.0/) 生成，请勿手工编辑。
每个版本的安装方式与校验和见 [GitHub Releases](https://github.com/scholay/xsime/releases)。

## [v1.1.6](https://github.com/scholay/xsime/releases/tag/v1.1.6) — 2026-10-19

### 合并的 PR

- #216
- #217
- #186
- #185
- #183
- #182
- #181

### 新功能

- **release:** publish complete cross-platform update feeds for 1.1.6 (a8b5bb22)
- **updates:** add Windows and Linux package upgrade dialogs (ddc1810a)
- **updates:** add settings update flow for Apple and Android apps (ff34be5f)
- **linux:** verify deb upgrades and shared release lifecycle (fff17a35)
- **linux:** add native article practice and private score history (8225246f)
- **linux:** add private daily input statistics and native dashboards (07b8d616)
- **linux:** add ordinary chord input, keymap deployment and learning (f5b4c47b)
- **windows:** align desktop workspaces and input lifecycle with macOS (c6c06dab)
- **linux:** add Mailbox PTY workspace and explicit result collection (38e4841e)
- **linux:** add native Claude CLI text channel (b835eaea)
- **linux:** add Codex text and persistent Mailbox image channel (36b03923)
- **linux:** add local mailbox HTTP and MCP gateway (ef124f11)
- **linux:** receive mailbox content into Capsule and Buffer (e4d6b7fa)
- **linux:** add the Mailbox workspace and native persistence contract (96462bdd)
- **linux:** add Capsule vault and guarded secret interactions (8b7ca2fc)
- **linux:** add explicit screen capture and private media archive (5c6d1b4b)
- **linux:** add Capsule references and bounded resource previews (24e65cc7)
- **linux:** add private multi-format clipboard history (b22d2c7f)
- **linux:** add structured Capsule notes and native rich text (3488a5fb)
- **linux:** add Capsule module host and native manager (4f786e7d)
- **linux:** align passive windows, toolbar controls and monitor behavior (a9a41ac3)
- **linux:** launch workspaces across input methods with global shortcuts (5045e71c)
- **linux:** protect workspaces across session and sensitive input transitions (bdcc87f0)
- **linux:** add cancellable AI API streaming and native result delivery (21803a55)
- **linux:** deliver derived blocks and add Default auto-send (3a869c3d)
- **linux:** connect incremental offline translation to Buffer (f06e3f6a)
- **linux:** add verified local translation resources (0a3ad261)
- **linux:** connect derived Buffer workspace and native visual metrics (50f92d81)
- **linux:** add private settings host and native visual baseline (641abe8e)
- **linux:** validate official packages and revoke live grants (71dbd3f5)
- **linux:** add revocable plugin registry and parity roadmap (1ad6cbdd)

### 修复

- **android:** persist installer outcomes across activity callbacks (de20d8c9)
- **windows:** allow bounded cold-engine replies before disconnecting (6a629c1b)
- **android:** preserve installer cancellation feedback on return (78c38980)
- **ci:** isolate Android installer fixture and report Windows selection failures (932cfc54)
- **android:** preserve installer callbacks on current Android releases (08ea4170)
- **ci:** align installer evidence with current plugin transitions (c16536f0)
- **release:** require platform evidence and preserve upgrade state (afc9a71a)
- **updates:** resolve remote build dependencies and recovery paths (94c168be)
- **linux:** align Capsule form controls with native macOS layout (8d4de78a)
- **linux:** bound Buffer and Capsule focus revocation timers (3852fd75)
- **linux:** validate native desktop hosts and shortcut focus (8033c084)
- **linux:** retain capture actions during metadata refresh (25f6a1a5)
- **linux:** retry busy vault metadata reads off the event loop (95998798)
- **linux:** export mailbox images without hard links (d63c45e7)
- **linux:** consume only committed mailbox drafts (13217938)
- **linux:** keep copied vault secrets pasteable in password fields (bed089f5)
- **linux:** preserve vault popup focus and cancelled migration data (c9246f16)
- **linux:** gate clipboard writes and keep successful MIME transfers (9288ffaa)
- **linux:** select an enabled Capsule module on recovery (7f1617c9)
- **linux:** check eventfd wake results in fortified builds (8947eaeb)
- **plugins:** refresh Capsule distribution manifest hashes (aa034256)
- **plugins:** refresh Capsule distribution manifest hashes (3ce31091)
- **linux:** finish cancelled queued AI requests (606b1683)
- **linux:** finish cancelled queued AI requests (6002c989)
- **linux:** finish cancelled queued AI requests (9302062e)
- **linux:** finish cancelled queued AI requests (a8ddf237)
- **linux:** complete cancelled queued translation requests (e84272f0)
- **linux:** complete cancelled queued translation requests (72db88a8)
- **linux:** complete cancelled queued translation requests (64b5763e)
- **linux:** complete cancelled queued translation requests (733222e7)
- **linux:** complete cancelled queued translation requests (4024bb40)
- **linux:** complete cancelled queued translation requests (7ec982bf)
- **linux:** reuse GTK-owned fallback displays across panels (6fcd9889)
- **linux:** queue AI replacements and clear revoked previews (f5a256a2)
- **linux:** queue AI replacements and clear revoked previews (6db5ff69)
- **linux:** queue AI replacements and clear revoked previews (153c6b2a)
- **linux:** queue AI replacements and clear revoked previews (b1ecd4e8)
- **linux:** queue translation retries and publish revoked results (ca9875be)
- **linux:** queue translation retries and publish revoked results (f2bbea7f)
- **linux:** queue translation retries and publish revoked results (c9add4d0)
- **linux:** queue translation retries and publish revoked results (8cf90c59)
- **linux:** queue translation retries and publish revoked results (3a6e6b27)
- **linux:** queue translation retries and publish revoked results (d2ed32a8)
- **linux:** retire protection gestures and preserve drag requests (7033c604)
- **linux:** preserve ready results and drain translated prefixes (d852d502)
- **linux:** retire protection gestures and preserve drag requests (b290a33d)
- **linux:** preserve ready results and drain translated prefixes (ab00c885)
- **linux:** preserve ready results and drain translated prefixes (d503e8fe)
- **linux:** preserve ready results and drain translated prefixes (18d8995a)
- **linux:** use incremental host activation predicate (34f76ac5)
- **linux:** join workers before teardown and retain translation cancellation (cd673ce9)
- **plugins:** synchronize translation catalog across native hosts (00905ca3)
- **plugins:** accept Linux adapters in shared package manifests (d7b43a0c)
- **linux:** preserve protected chrome and companion staging (c6d17635)
- **linux:** preserve protected chrome and companion staging (e8443eb2)
- **linux:** preserve ready Buffer results when retry preflight fails (d710116b)
- **linux:** pin offscreen settings render canvas (61e5f6da)
- **linux:** keep settings feedback within native window dimensions (3d77a0b0)

### 文档

- **linux:** record verified amd64 upgrade and desktop CI (981fce9e)
- **linux:** record focus revocation and persistent IPC regression results (a3a0cf60)
- **linux:** defer stream input from parity delivery (9316ff54)
- **linux:** record Capsule host integration dependencies (8bf9f7d2)
- **linux:** record Capsule host integration dependencies (6a7cd188)

### 维护

- **android:** isolate offscreen rendering from touch focus events (6f7c3b77)
- **linux:** wait for rendered mailbox review completion (5f345d0e)
- align release channel guards with full main checks (a1d3fbe2)
- exercise Android rendering before release (e18b37d0)
- **android:** verify visible update status after installer callbacks (40f0effa)
- **windows:** isolate remembered plugins between product probe scenarios (f012fe6a)
- **windows:** record first-key transport failure details (c4450f1d)
- **plugins:** isolate catalog release from the current distribution (e745d906)
- **plugins:** pin the 1.1.6 immutable distribution (6c4c1954)
- export all native connector settings contracts for Linux (fdc2261e)
- **plugins:** align bundled distribution versions after convergence (e8f2f8d6)
- **linux:** keep Buffer probe connected while receiving snapshots (b154969b)
- **linux:** isolate input probes and clean portal mounts (70258f37)
- **linux:** make statistics fixtures reliable on clean CI runners (588d2e5e)
- **windows:** synchronize the published plugin integration pin (7292d41b)
- **linux:** isolate headless terminal accessibility environment (1d26815b)
- **linux:** isolate headless terminal accessibility environment (cba6d94e)
- **linux:** retain Codex channel validation evidence (aa8d20a1)
- **linux:** wait for cross-bus window teardown to settle (87e2f19b)
- **linux:** stabilize vault deletion checks and install menu test tools (ffa2f0e0)
- **linux:** clean up the Wayland document portal mount (26d7537d)
- **linux:** await Capsule notes after module recovery (a39386e8)
- **linux:** install xprop for window manager readiness checks (3657ef48)
- **linux:** install xprop for window manager readiness checks (c0d6aeb9)
- **linux:** await the window manager before focus assertions (be782d50)
- **linux:** pin AI host compatibility and clean isolated API profiles (8d0825eb)
- **plugins:** expect Translation 2.3.0 in native distribution smoke (cbd5f9db)

### 其他

- Add Linux Mailbox AI conversations with frozen routes and final-only history (fadafb5d)

## [v1.1.5](https://github.com/scholay/xsime/releases/tag/v1.1.5) — 2026-10-16

### 合并的 PR

- #145
- #137

### 新功能

- unify desktop candidate matrices and stabilize mobile scrolling (2daae29d)
- **windows:** align settings and Buffer with native chord and connector profiles (8f3dd2e3)
- simplify macOS permission cards and guided authorization (2236382e)
- **branding:** unify XSIME across five platforms with upgrade compatibility (7944681e)

### 修复

- **windows:** finish settings field styling and discard erased key drafts (a36dbeba)
- retire stale matrix navigation and test live candidate selection (22c50a85)
- **windows:** negotiate pending chord commands and tighten settings controls (344d8550)
- **windows:** retain validated LPAC token coverage (c85adba2)
- **windows:** support LPAC input and legible profile icons (53c5b3de)
- prevent Caps Lock swallowing in macOS chord routing (41d6063f)
- align IMK connection identity and permission drag source (20e02cfe)
- recover interrupted deployment and verify LPAC boundaries (3b0b588b)
- **windows:** support LPAC input and legible profile icons (1ead4f47)
- **linux:** isolate deployment so Fcitx shutdown can cancel it (f8467ba6)
- **macos:** protect secure input and report dictionary failures (74f5ced9)
- **macos:** make candidate presentation immediate and reuse rendering work (7f647bea)
- **ios:** use provider-neutral wording for build 60 review (6cc35fe9)
- **ci:** retry interrupted Apple ticket validation safely (777f469c)
- **ios:** validate Traditional Chinese branding resources (f2d0169b)
- **windows:** preserve Unicode brand names in PowerShell 5.1 (afee3082)
- **release:** validate localized branding while freezing executable identity (4a03392c)

### 文档

- **branding:** adopt 心有灵犀，落笔见术 slogan (6fd204f7)
- **branding:** record repository cutover and preserve former-name notices (9c253496)
- **branding:** record migration validation and canonical attribution (cc4d3370)
- establish XSIME branding and staged compatibility migration (cab7bddd)
- publish 1.1.4 downloads and validation records (51db2072)

### 维护

- **windows:** verify connector transport and preserve system-theme drafts (c4bcc922)
- **macos:** run input repair regressions in CI (d41a8741)
- **release:** prepare XSIME 1.1.5 desktop and Android packages (f0e24193)
- **ios:** archive submitted XSIME 1.1.5 release packet (f86503ee)

## [v1.1.4](https://github.com/scholay/xsime/releases/tag/v1.1.4) — 2026-10-14

### 合并的 PR

- #136
- #129

### 新功能

- **wubi:** add cross-platform mixed input and unique four-key commit (206fa9ff)

### 修复

- **packaging:** include reviewed Lingxi Wubi payload in previews (8132ae9a)
- **windows:** verify retained host categories during uninstall (0ddc0ab9)
- **windows:** support sandboxed and UI-less input hosts (a78bba86)
- **ios:** stop hidden keyboard rendering and animations (349c9122)

### 文档

- publish 1.1.3 downloads and verification records (fc724ca4)

### 维护

- **android:** mount candidate fixture in a native row (b1a21e59)
- **android:** retain actionable Wubi instrumentation failures (6c6a57b8)
- update scheme count and isolate synthetic Windows host timing (f3a68c2d)
- **windows:** reopen settings after saving scheme selection (64c1db43)

## [v1.1.3](https://github.com/scholay/xsime/releases/tag/v1.1.3) — 2026-10-13

### 合并的 PR

- #126
- #125
- #124
- #120
- #114
- #107
- #101

### 新功能

- **linux:** add safe uninstall and versioned Debian release packages (21e5078e)
- unify source scheme imports across five platforms (e81557d2)
- **windows:** connect Buffer to local Codex CLI (c11876cb)

### 修复

- **android:** support imported layouts and small dictionaries on device (d12b0fd2)
- resolve packaged scheme import verification failures (f0cc8d9a)
- **android:** retry an imported layout after fallback (d9b6e692)
- complete imported workspace deployment and layout fallback (831e590a)
- **windows:** preserve input when optional scheme annotations are malformed (460a8807)
- **android:** pair imported T9 layouts and retain warm dictionaries (c7d4fb88)
- **windows:** identify imported engine text failures without logging input (ce344288)
- **windows:** separate scheme import and Buffer setting commands (d5a99283)
- **windows:** align Buffer delivery, translation and settings (a5f0c552)
- **ios:** clear host composition before deactivating keyboard (5db23ee5)
- **ci:** prepare librime data for candidate pagination smoke (613797aa)
- **ios:** stop height recovery when the keyboard loses focus (9cf1ceb6)
- **release:** recognize previously delivered patches during history reconciliation (7681dd0e)
- **schemes:** preserve verified Wanxiang portable packaging (23d14340)
- **release:** allow independent iOS patch versions (107c60b6)
- **ios:** support imported nine-key layouts and recover host height (2ab62c7d)
- **ios:** answer the host's provisional keyboard height on first load (36017c0a)
- **windows:** align Buffer shortcuts and input lifecycle (bbf67b60)
- **macos:** paginate all compact candidates with configurable count (1a554d44)
- **windows:** correct matrix paging and input test isolation (51890121)
- **windows:** align candidate navigation and tray icon with macOS (991ab1ba)
- **windows:** verify final original-user uninstall cleanup (6466aea6)
- **windows:** separate registrar diagnostics from uninstall results (820dd2e0)
- **windows:** make Buffer disposable during IME maintenance (ab483afa)
- **windows:** preserve uninstall recovery and isolate regression tests (77e3571d)
- **windows:** preserve idle keypad input and stage bug acceptance (b1185021)
- **windows:** guard setup handoff and signed bootstrapper (9ba4f219)
- **macos:** align the Buffer to the input box on its first opening (3232e641)
- **macos:** register the input source system-wide at install (53962798)
- **macos:** stop the installer watchdog from killing RIMES during activation (087bcf44)
- **ios:** show effective layout after scheme import (5c94abc2)
- **android:** simplify Buffer controls and translate automatically (23f1c75b)
- **windows:** integrate preview.5 bug fixes and Buffer geometry (e56851bd)
- **windows:** separate machine installation from original-user setup (31a63cd6)
- **windows:** defer Buffer restoration until Settings closes (9fd1e720)
- **windows:** constrain native Buffer placement beneath Settings (122febda)
- **windows:** keep settings above the retained Buffer (0478e083)
- **windows:** retain TSF while display enumerators escape (be34f80e)
- **windows:** keep settings independent of topmost Buffer (2dafbdac)
- **windows:** isolate TSF from host MSVC runtime (a8054cc6)
- **windows:** retain verified caret when TSF geometry is unavailable (eb10c665)

### 文档

- record iOS build 54 convergence and lifecycle regression (bc5e43d3)
- record final iOS and CI convergence validation (1fbf2cae)
- record branch convergence and accepted Windows fixes (506acdb9)
- **readme:** update star benefits and add QQ group (504b3a21)
- **windows:** align evidence checksums with repository line endings (7c890e89)
- **windows:** record native placement and close validation (98ace074)
- **windows:** record verified Settings overlap follow-up (f33b4832)
- **windows:** credit PR105 and record round-two validation (adf06542)
- **release:** publish verified macOS and Android download records (439e093f)
- preserve exact package identities and remaining release gates (ab248edc)
- record staged macOS and Windows upgrade checks (d986d1ff)
- record Android 1.1.1 public delivery (f17e07ec)

### 维护

- **android:** retain instrumentation status in raw test results (3fbca899)
- **android:** provision import fixtures without root access (ab8386e9)
- verify packaged macOS imports and locate Android platform tools (954c05e7)
- prepare 1.1.3 installers with verified portable scheme imports (2ea94e37)
- **windows:** build complete installers with scheme import verification (1b39d88b)
- **schemes:** cover common input families and native confirmation (a1050248)
- **ios:** publish current cold-start fixes as build 55 (ea733ed1)
- **ios:** wait for streamed answer completion on slow runners (fbf1d4cd)
- pin verified Windows plugin integration and record test handoff (6fa92f8e)
- **plugins:** expect the current Codex package version (e545b791)
- **android:** verify pet controls and isolate upgrade checks (f94e1a95)

### 其他

- **windows:** prepare 1.1.1 community fixes (ca004245)

## [v1.1.1](https://github.com/scholay/xsime/releases/tag/v1.1.1) — 2026-10-07

### 合并的 PR

- #100
- #96
- #95
- #92
- #91
- #89
- #88
- #85
- #86
- #80
- #73
- #78
- #74
- #71
- #60
- #58
- #54

### 新功能

- **mobile:** add bounded explicit text clipboard history (427aa71c)
- **tooling:** preserve pinned input scheme catalog and importer checks (b557bbc9)
- add About tabs and shared rhino app icons (776ff81c)
- add RIMES settings home and refine Glass navigation (79e2a4dc)
- **macos:** add optional Liquid Glass theme (b7b3a862)

### 修复

- **windows:** preserve host Shift releases while Buffer is paused (4941d965)
- **windows:** retire Buffer requests when returning to input (34d01a27)
- **windows:** update Buffer shortcuts without losing registration (1c291f1e)
- **windows:** preserve Shift commits and host key ownership (50f8a71c)
- **windows:** qualify exclusive DLL access failures (b3846575)
- **ios:** hand off clipboard browsing before host import (c441e539)
- **ios:** preserve automatic Buffer policy after clipboard browsing (f5bb1e29)
- **windows:** require sign-out for missing registered input DLLs (d3152d9c)
- **windows:** qualify failed exclusive DLL access in uninstall messaging (773092b6)
- **windows:** retain pending sign-out from previous installations (ebe52280)
- **windows:** clarify recovery sign-out and verify buffered hover rendering (84892fae)
- **windows:** buffer settings hover frames and limit redraws (9194f31e)
- **windows:** reject dangling links during installer recovery (bf44117f)
- **macos:** preserve and select deployed external Rime schemas (c7b41fb2)
- **macos:** verify watchdog process groups after setpgid races (07c59c86)
- **windows:** recover damaged installs and configure candidate pages (89ac3d1b)
- **linux:** retire drag capture on release and isolate Rime writers (c4754077)
- **android:** add explicit target-bound clipboard paste into Buffer (ac0267a2)
- **windows:** stop the candidate window jumping to the screen corner (ac316a88)
- **android:** preserve ordinary two-thumb taps during gesture cancellation (535964a0)
- **windows:** preserve Buffer work when opening settings (58fc4bcf)
- **android:** finish held delete backpressure and swipe punctuation (f6ad4872)
- **mobile:** integrate keyboard punctuation and TestFlight feedback fixes (903974b2)
- **android:** seed reported offline vocabulary without replacing user data (cf6390a9)
- **android:** make Buffer clear and held keyboard actions reliable (c1052737)
- **android:** center keys and show readable nine-key spelling (11038eba)
- **windows:** register discoverable install and uninstall entries (#65, #66) (a45965e4)
- **windows:** expose settings and restore background tray lifecycle (#65, #67) (42f1a629)
- **windows:** preserve shifted printable characters (#59) (10b08e71)
- **macos:** align Buffer opening to input box, not caret left edge (7273f217)
- **macos:** use product version for local plugin compatibility (643dedf5)
- **macos:** stabilize Capsule tab layout and hit targets (1a832865)
- **macos:** uncover clipboard glass behind content pane (e0468ceb)
- **macos:** use glass backdrop in clipboard history (9a767281)
- **ios:** quieten Buffer import prompt and keep paste control in the line (#56) (32765d28)
- **ios:** anchor keyboard to host bottom edge during height changes (f7e87659)
- **ios:** exclude temporary frame tracing from release baseline (43ffe899)
- **ios:** consolidate keyboard and Buffer improvements through build 48 (f26ca83e)
- use a rhino horn for macOS input-source symbols (1c23c190)
- **macos:** remove opaque Buffer plates in Glass appearance (94cd70c7)
- **macos:** align Glass Buffer actions and keep Capsule tabs visible (9e6731d5)
- **macos:** simplify permission setup to one short instruction (dfc86fe6)
- cover settings titlebar and align sidebar navigation (d4bf1a3c)
- follow live system colors throughout Glass surfaces (c07f1b41)
- preserve system separator opacity in Glass controls (375ad3ce)
- complete Glass Buffer surfaces and refresh theme chrome (d79f64ca)

### 文档

- verify public packages across platforms (f002db72)
- align public download links across readme languages (57c21512)
- keep tracked acceptance evidence hashes reproducible (0a85d442)
- **macos:** guide recovery from reported sandbox input-source crashes (50703754)
- **windows:** record final recovery and caret review verification (67ca6a7c)
- **windows:** record hover and uninstall follow-up acceptance (43764cbf)
- **validation:** record isolated Android clipboard device attempt (418c4da5)
- **windows:** record isolated community repair acceptance (57134c0e)
- **linux:** distinguish drag commands from physical release (28a44fbd)
- record Windows acceptance and Android follow-up integration (958c030d)
- change early supporter benefit to lifetime cloud sync (d8b392b2)
- **android:** record final code14 verification and completed issue closures (68552cc8)
- **windows:** record installed preview acceptance and issue disposition (45c128f9)
- record mobile integration checks and pending acceptance (11260546)
- **android:** record community fixes and device acceptance (18b6c17d)
- **windows:** retain synthetic product and TSF probe logs (c4a1cd55)
- **windows:** record community issues and exact-source Young validation (5bda4873)
- distinguish RIMES architecture from Lingxi IME product names (89f62280)
- add community contact and early supporter invitation (6f043ce4)
- **ios:** record integrated height follow-up and acceptance limits (7d3c9cbf)
- record macOS 1.1.0 delivery and release ordering (bb0170a7)
- **release:** record public 1.1.0 delivery (724b408c)
- **ios:** publish RIMES support and privacy pages (45dd7d6f)

### 维护

- prepare current main fixes for public package refresh (6d222733)
- record Windows typing acceptance and package freshness (04d1f954)
- **windows:** refresh installer result fixture and CI coverage (30b5e58e)
- **windows:** prepare 1.1.1 community fixes (30b14003)
- **windows:** restore installer result regression in CI (24ca7fa4)
- **windows:** verify candidate caret isolation across document focus (650762b2)
- **macos:** make Capsule reference search fixture deterministic (808d05d5)
- **windows:** diagnose native hover invalidation regions (524cd3ea)
- **windows:** verify recovery refusals and document candidate settings (9faf708f)
- **linux:** exercise competing engine in initialized smoke fixture (1a41856f)
- **android:** physically focus the editor after host process restarts (e1fd515a)
- **android:** settle host edits before ordinary multitouch streams (a221462d)
- **android:** expose active password keyboard ownership (162eec1d)
- **windows:** verify native pass-through and hidden-desktop UI semantics (f5057d3b)
- **windows:** probe shifted symbols and case through librime (#59) (48756e80)
- align integration checks and release readiness records (b4641168)
- remove retired iOS promotional project (26374a60)

## [v1.1.0](https://github.com/scholay/xsime/releases/tag/v1.1.0) — 2026-10-04

### 新功能

- **mobile:** localize branding and add website and email links (a641efc6)
- **mobile:** refine settings hero and complete iOS typing follow-up (e8dff040)
- **windows:** integrate official plugins and chording lifecycle (06ad59ca)
- **plugins:** migrate official sources and mobile package lifecycle (cf84aa5e)
- **plugins:** pin official sources and verify package execution (e0221353)
- **windows:** native 1.0.0 version identity, Setup EXE and installer (dbcf63da)
- **android:** network AI transport, release build and 1.0.0 identity (80b1875d)
- **ios:** custom layouts, imported schemes, nine-key and stats card (3c65b389)
- **mailbox:** add plugin-hosted messages and terminal workspaces (b878580f)
- **android:** restore grouped app settings and keyboard configuration (d14a0e69)
- **android:** execute offline translation and OpenAI format mock plugins (bba58425)
- **android:** bundle verified offline Chinese English dictionary (d858fab5)
- **android:** bind plugin results to captured Buffer sources (08af7678)
- **windows:** align native UI with macOS themes and geometry (efec80f7)
- **android:** align keyboard chrome and add two-thumb input (bd8cafd7)
- **android:** port iOS touch chords and key geometry (5881b8f3)
- **android:** adopt iOS-style keyboard layouts and palettes (889d4208)
- **android:** add offline nine-key pinyin resources (9b4ef871)
- **windows:** add native Buffer workbench and daily preview pipeline (f884ece3)
- **windows:** stage complete native shared data with integrity checks (f23d4c9e)
- **android:** preserve Chinese Buffer blocks and control local learning (70412f55)
- **android:** add Chinese composition and stable candidate keyboard (eff9ca71)
- **android:** bundle pinned offline Rime engine and mobile dictionaries (11bccdc3)
- **windows:** stage complete native shared data with integrity checks (1a627909)
- **android:** add native keyboard and target-bound Buffer foundation (5a23996f)
- **windows:** add TSF composition, candidate window, and IME e2e (beaf105e)
- **ios:** prepare RIMES 0.1.0 for review (9aea6f39)

### 修复

- **ios:** remove typing-path I/O and prioritize key haptics (5c654eb8)
- **windows:** allow explicit reinstall after plugin receipt corruption (26635fc9)
- **windows:** check bounded lock acquisition result explicitly (6ad0785b)
- resolve packaged resources and bounded Windows input contention (2c1d97af)
- **plugins:** read source notices as UTF-8 on Windows (7c9d461b)
- **windows:** split embedded packages within MSVC literal limits (fae05d78)
- **plugins:** enforce module contributions and isolate optional feature smoke (b0afe86e)
- **ci:** include mailbox pages in settings render checks (5daa62b8)
- **windows:** isolate typing tests from workbench settings (454b237c)
- **ci:** repair platform build prerequisites (2d520b48)
- **android:** use a writing icon for the typing entry (ea8a2818)
- **windows:** republish Buffer target after selection changes (48c435b2)
- **windows:** restore explicit Buffer capture after focus changes (be788f7b)
- **windows:** refine macOS control surfaces and settings interactions (6ec52332)
- **android:** retire native key gestures across input boundaries (58f7395c)
- **android:** restore idle Buffer plugins and unify vector icons (5953b96c)
- **android:** reflect Buffer and host action key states (58527b94)
- **windows:** preserve candidate state across unhandled key releases (348822f8)
- **windows:** run candidate clicks in a granted TSF edit session (4ea3e1c0)
- **windows:** repair native host candidate and composition behavior (b5a66eec)
- **windows:** translate completed sentences and stop empty-result retries (7c33090c)
- **windows:** preserve learned databases during deployment and audit installer recovery (f722071a)
- **windows:** verify registration outside TSF caches and revoke stale broker deliveries (201bc5d4)
- **android:** apply learning changes to the active input session (70e63013)
- **windows:** permit Session 0 identities in broker hello messages (689e84dd)
- **android:** keep long candidate phrases readable on phone screens (236546df)
- **android:** keep keyboard controls clear of system insets (55e2ec7b)
- **windows:** permit Session 0 identities in broker hello messages (1b2a86a0)
- **android:** honor editor actions and clear unbound Buffer targets (e89a5f2f)
- **windows:** keep composition across unhandled IME KeyUp (8653befb)
- **windows:** supply ascii_composer bindings for the IME e2e schema (4b3db547)
- **windows:** compile Fake TSF on MSVC and keep Windows CI off the macOS gate (8ef9becb)

### 性能

- **android:** reuse immutable light and dark palettes (f2311f47)

### 文档

- **release:** record final iOS upload and correct TestFlight availability (eeb28994)
- **release:** update platform downloads and source checkout guidance (69fcf3d3)
- **release:** clarify background input validation boundary (76e0407a)
- **release:** finalize 1.1.0 delivery acceptance (bb73f454)
- **windows:** record 1.0.0 native release (4abd5327)
- **android:** record 1.0.0 release and dev8 validation (941111c4)
- **release:** record 1.0.0 publication and readiness (6fecd950)
- **ios:** record 1.0 validation, privacy and release notes (d12280e4)
- **readme:** refresh READMEs and add ja/ko/es translations (e57e8c7d)
- **license:** relicense to Apache-2.0 and record attribution (133374fc)
- **android:** record dev7 settings delivery and validation (00994612)
- **android:** record dev6 local plugins and verified delivery (f2ecb2f6)
- **windows:** record Buffer binding repair acceptance on Young (ab926662)
- **windows:** record second styling round on Young (4511eff7)
- **android:** record dev5 parity and physical acceptance (f0203444)
- **windows:** record installed macOS style parity acceptance (92de58f6)
- **windows:** record real-host preview.4 acceptance and Cursor handoff (e74a6f11)
- **windows:** record preview artifacts and remaining desktop acceptance gates (f8ba5c34)
- **android:** record physical device validation and dev 2 artifacts (1c2b2eeb)
- define platform priorities and record native foundation validation (38953e4a)

### 维护

- **release:** record physical acceptance and documented distribution basis (d886b025)
- pin official plugins with four-platform native CI (f9ec959d)
- **macos:** distinguish pinyin segmentation and isolate fixture sizing (e02d4b7d)
- **windows:** extract verified engine archives with bundled tar (1b7f5131)
- **ios:** verify shared storage across signed app and keyboard (73c5829f)
- **windows:** wait for context binding before typing assertions (5cdae3d0)
- **release:** unify product versions and platform release tooling (186c2a66)
- ignore Shared and Xcode build artifacts (8077a8b3)
- **android:** measure host latency and exercise chord lifecycles (6f4b9326)
- **windows:** verify unavailable Broker input and reconnect state (995751f2)
- **android:** record dev 4 keyboard layout delivery (27878b16)
- **windows:** allow candidate guard checks in headless Broker (c99a3aad)
- **windows:** use a word present in the TSF fixture (4abf14e7)
- **android:** record dev 3 engine and physical-device acceptance (818b02a5)
- standardize mainline maintenance and independent platform channels (5b72a4d2)
- **promo:** preserve local iOS promotional source and assets (946fbe3e)
- **design:** preserve local interactive workbench updates (61bead51)
- **ios:** preserve local keyboard and AI workspace changes (b013051f)
- **macos:** preserve local Buffer and Capsule development (d460f2a2)
- **core:** preserve local shared models and plugin catalog (ef74b977)

## [v1.0.0](https://github.com/scholay/xsime/releases/tag/v1.0.0) — 2026-10-03

### 合并的 PR

- #42
- #41
- #39
- #38
- #37
- #35
- #33
- #32 fix(release): report the notary submission id and bound the wait
- #31
- #30
- #29
- #28 fix(release): handle localized diagnostics on macOS Bash
- #27 fix(release): stage formal installer before publishing
- #26 feat: manage multiple AI provider profiles
- #25
- #24 docs: README 末尾加入微信赞助二维码
- #23 让隐私声明与架构文档回到实现现状
- #22 修复 Xcode 27 默认构建系统下的入口点

### 新功能

- **linux:** port Capsule note rail to Fcitx5 (fa1f8952)
- **linux:** port Default Buffer workbench to Fcitx5 (ffe320ef)
- **linux:** add a Fcitx5 RIMES input method addon (b6f12271)
- **capture:** add ratio frame workflow and clean selection backgrounds (e4642559)
- **ios:** tune keyboard interactions from physical feedback (d3f58ad2)
- **ios:** deliver keyboard and Default Buffer experience through build 22 (68edcea4)
- **ui:** refine capsule cards and adaptive candidate layout (a879d05c)
- **capsule:** simplify secure card workflows and rounded chrome (ab62b425)
- **capture:** refine screenshot UI, interactions, and permissions (02ee1066)
- **ios:** add native RIMES keyboard development preview (ad97986d)
- **capture:** unify capture permissions and rework the editor chrome (20651da8)
- manage multiple AI provider profiles (e7fa86c4)

### 修复

- **linux:** copy Capsule notes on Wayland via wl-copy (46fa869a)
- **linux:** copy Capsule notes only when copy_seq advances (40787c61)
- **linux:** make Capsule double-click insert without rebuilding cards (d0077fb9)
- **linux:** clear Capsule search on hide and flag password fields (91f11a0f)
- **linux:** end Buffer drag on pointer release, not a settle timer (62e93f84)
- **linux:** treat non-drag same-IC reactivation as a field switch (809853d5)
- **linux:** keep a single Buffer UI and treat same-IC caret moves as field switches (81516e47)
- **linux:** keep Buffer capture across X11 drags and place above caret (7f7c4474)
- **linux:** stop Buffer Enter/ZWSP leaks and respawn the panel (5bd60d18)
- **linux:** make Buffer e2e drive real Fcitx5 capture (e2532499)
- **linux:** cover rebuild without a second librime setup() (9a31346b)
- **linux:** notify Fcitx5 when background deploy finishes (98f9e5cd)
- **linux:** keep Fcitx5 responsive during first-run deploy (1b813cba)
- **candidates:** close the panel as soon as text commits (7fcbb559)
- **ios:** protect proxy writes from host callback reentrancy (e144549b)
- **capture:** disambiguate recorder geometry on CI toolchains (aac216b4)
- **capture:** restore protected windows and preserve edit history (38084b1a)
- **ios:** configure device signing and verify first iPhone install (2c06e078)
- **capture:** repair the result overlay, card geometry and search focus (ba1fcb8f)
- **build:** verify one architecture per lipo call (49323851)
- **release:** report the notary submission id and bound the wait (90238f38)
- **release:** make macOS the sole release gate (950c5689)
- **release:** notarize only the formal installer (aedc5f1f)
- **release:** register and validate the signing keychain (fa71f2ee)
- **release:** delimit variables in localized shell diagnostics (0f2a874c)
- **release:** finish GUI rehearsal when the receipt is committed (7fc516f0)
- **release:** allow scholay to approve both release stages (892895e6)
- **release:** stage formal installer before publishing (90c566c9)
- **build:** drop the dead standalone smoke entry points (42341c36)

### 文档

- refresh multilingual README and language badges (#47) (54701c91)
- thank iRime for guidance and promotion (f27a3fb1)
- **release:** separate tag creation from immutability (75b4ded8)
- **release:** clarify one reviewer can approve both stages (2dea9de2)
- reconcile README and issue baselines (92c7e53b)
- **readme:** add the WeChat sponsor QR code (08b1433c)
- state the real paste, identity, and Capsule behaviour (2a87e784)
- update CHANGELOG.md for v0.5.0-preview.3 (25865bd1)

### 维护

- **linux:** keep Buffer drag tail inside 1s under Capsule load (4105ea1e)
- **linux:** keep Buffer drag e2e timers alive after the suite returns (3a2ce3cc)
- **linux:** drain leftover Buffer chips between e2e scenarios (1a29f8ee)
- **linux:** drop the staged raw-input chip before later Buffer e2e steps (ab64133f)
- **linux:** wait for SIGKILL before asserting UI pid is gone (b6b866ab)
- **ios:** await plugin events instead of CI timing assumptions (95d6f00e)
- **ios:** validate release tooling without credentials (5b6fb7c9)
- **ios:** automate tagged App Store releases (43692ba5)
- **capture:** check in the capture module as delivered (df5a9018)
- **release:** align CI with explicit maintainer approvals (f22c77cf)

## [v0.5.0-preview.3](https://github.com/scholay/xsime/releases/tag/v0.5.0-preview.3) — 2026-09-15

### 合并的 PR

- #17 内置呦呦音形折梅、寒梅原生并击方案
- #21 更新 CHANGELOG（v0.5.0-preview.2）

### 新功能

- **chord:** native chord schemes that settle on key release (0930901f)
- **rime-data:** bundle 呦呦音形 折梅 and 寒梅 chord schemes (e6ace2c2)

### 文档

- update CHANGELOG.md for v0.5.0-preview.2 (a9957f09)

### 维护

- run the chord and 呦呦音形 smokes in CI and on the assembled app (a805ad97)

## [v0.5.0-preview.2](https://github.com/scholay/xsime/releases/tag/v0.5.0-preview.2) — 2026-09-15

### 合并的 PR

- #20 不再跟踪 Python 字节码缓存
- #19 统一发布流程——一条命令、tag 为唯一版本来源
- #18 发布前演练 v0.5.0-preview.1 升级，修复 universal 构建
- #16 打包校验改为 55 个文件的审核闭包
- #15 Buffer 输入框锁定、Capsule、自然码并击与 RIMES 更名

### 新功能

- **release:** one release command that only pushes a tag (560c224d)
- **release:** derive versions, notes, and changelog from tags (12a06d5c)
- **chord:** 自然码 output encoding for chord keymaps (2ba4edf4)
- **capsule:** default the Capsule rail to ⌘⇧V and finish the rename (5e503309)
- **capsule:** one Capsule with the manager drawn as the rail grown upward (c79aa54c)
- **capsule:** save Recent cards into Capsule from the rail (d8fcf364)
- **capsule:** turn the clipboard rail into Capsule with saved-entry tabs (34790cc2)
- **buffer:** lock sending to the exact input box and rework the toolbar (ef632a00)
- **settings:** keep only the main title on every settings page (ff773cdd)
- **buffer:** fold the rails whenever the workbench cannot take a keystroke (1b562fcd)
- **buffer:** fold the rails to the toolbar when the workbench has no focus (6b7cf4fd)
- **buffer:** add the composing field so any input method can type here (7009d49b)
- **buffer:** name the two input modes and fence them at the boundary (a0fcba1d)
- **buffer:** show live typing figures as a second line in Default mode (6b5a089c)
- **doctor:** report the app's own identity, signing and permission state (f972a8a4)
- **migration:** one definition of where data lives, and a proven way to move it (4440ef76)
- **permissions:** clear the record so the system will ask again, and name  the identity it asks about (7a7a92e0)
- **settings:** audit system permissions item by item, and stop asking for  grants the system will not prompt for (143c6e23)
- **clipboard:** make activation a choice, and say when a paste was blocked (48bbc1ae)
- **buffer:** put closing-after-the-last-block in the auto-send menu (4bc3f5a0)
- **codex:** follow a real session instead of reading one field of it (6564b5f1)
- **aggregator:** route a published model catalog by endpoint, not by name (cd374ba8)
- **buffer:** add AudioKit music plugin and fix continuous plugin cycling (a6272261)
- **stream-input:** compose in the raw line and age blocks out (6a1d66b5)
- **statistics:** add graphical dashboards and article typing tests (f5d6151c)
- **chord:** add configurable keymaps and unify split strokes (fc8f8a6d)
- **buffer:** source-row import, split pull-downs, tighter geometry (fb71e173)
- **stream-input:** pause segments, comma types, models are selectable (dc9cc40c)
- add workbench menu, toast, auto-paste, and input-box probe (bd701a20)
- visualize Buffer target association (94a1ee0a)
- harden standalone companion workflows (182c19aa)
- copy Capsule media and harden model fetch (dd438162)
- add secure Capsule iCloud sync (eba5185a)
- keep the Buffer toolbar permanently visible (46846b1e)
- complete standalone workspaces and rich clipboard (ce69283b)
- add local-first Mailbox and Capsule workflows (ba611461)

### 修复

- **release:** build CHANGELOG.md from the tags origin publishes (5119ddd1)
- **release:** build each architecture natively and merge with lipo (0e09fd59)
- **platform-preview:** pin the 55-file reviewed closure (47502353)
- finish moving code paths to ~/Library/RIMES (51a8f115)
- keep tool output and a real home path out of logs and fixtures (fd6ee49d)
- **buffer:** restore the target link after leaving Music (36460a56)
- **buffer:** lock Electron apps and custom-drawn apps to their input box (25f2964e)
- **buffer:** fold the whole panel, hold it steady, and recall the lost target (d07e3979)
- **buffer:** give the standalone rail a click that can succeed, and stop  reparenting during layout (209c18ec)
- **buffer:** stop a focus activation from undoing the capture it was given (be7600d6)
- **buffer:** stop the metrics row from collapsing every rail (6db2e529)
- **release:** finish the rename in packaging, and sign so grants survive (d392690a)
- **buffer:** hand Return back when nothing is staged, and stop hiding options (011eb85f)
- **buffer:** keep a capture click that has nowhere to land yet (80f20597)
- **buffer:** isolate translation units and correct auto-send lifecycle (93072bd5)
- **input:** let a Shift tap switch language, and stop swallowing letters (ef3c2b7d)
- **buffer:** align the caret opening to the first character, not the edge (1a7d5659)
- **menu:** dispatch maintenance commands; surface the Accessibility grant (4ef66fb1)
- **clipboard:** trackpad direction, clicks hold the rail, copy feedback (49c2e444)
- preserve Chinese mode after utility shortcuts (e0fb5f91)

### 重构

- one name — RIMES — for the bundle, executable, identifier and data (d3a56202)
- **translation:** extract the session bridge and give codex a working PATH (be77bd65)
- **stream-input:** guess on the connector alone (a30d7733)
- **capsule:** free-form password secrets; retire Prompt/Memory/URL (43d6de74)
- float Buffer rail actions (2d2c361d)

### 文档

- **release:** one-page process, with reference and history split out (9350038c)
- point data paths at ~/Library/RIMES (260c5faa)
- **chord:** point the keymap directory at ~/Library/RIMES (0f334123)
- **chord:** document the 自然码 output encoding (3996d184)

### 维护

- stop tracking Python bytecode caches (ba8e0f66)
- **release:** publish previews from tags, rehearse packaging on PRs and nightly (cb69fd9e)
- **release:** rehearse the upgrade from v0.5.0-preview.1, and finish the rename in docs (cdcfd347)
- wait for the companion launch agent instead of racing it (e8c4cbb5)
- **capsule:** wait for passcode sheets instead of one 50ms turn (d9ed3030)
- **buffer:** let the Music panel smoke run under a foreign input method (dd76bd57)
- bring the smokes and settings render count up to this branch (61544ffe)
- pin the RIMES input-source identity (a34e7f64)
- **chord:** add the Isaac2025 and Isaac2026 keymaps (582dbbf5)

### 其他

- Keep capture across the session the toolbar click itself destroys (ba32b62f)
- Shrink the panel to a toolbar strip when the rails fold (9670ce25)

## [v0.5.0-preview.1](https://github.com/scholay/xsime/releases/tag/v0.5.0-preview.1) — 2026-08-22

### 新功能

- prepare RIMES 0.5 input and buffer release (96ae5dde)
- **ui:** port React design system to native surfaces (09cfe78d)
- **design:** slim input menu and rethink buffer rail layouts (77065717)
- **design:** add interactive React design system (9053a905)
- **settings:** add quiet theme and configurable shortcuts (db67a001)
- **windows:** add native TSF foundation (4c20c39a)

### 修复

- **settings:** refresh choice card visual states (ae170fcb)
- repair settings choices and clipboard shortcut (a8add728)
- **ui:** repair settings and compact buffer layout (bb400a99)
- **design:** restore compact buffer actions (c419b065)
- **design:** harden buffer and inbox interactions (97794097)
- **design:** align semantic colors with native UI (6893277a)
- show candidates in iShot annotations (089b4f19)
- **windows:** avoid PowerShell automatic variable collision (2209b094)

### 文档

- establish community contribution workflows (3ee4cf03)
- formalize AI contributor attribution (47a68d92)
- credit Claude, Cursor, Codex, and Grok as contributors (04384924)

### 维护

- render default settings surface deterministically (bf2446b1)
- **windows:** pin Visual Studio 2022 runner (cfcd8309)

### 其他

- enable controlled unsigned previews (08917f7e)

## [v0.4.3](https://github.com/scholay/xsime/releases/tag/v0.4.3) — 2026-08-15

### 修复

- harden RIMES installation, updater, and release (ce636924)

## [v0.4.2](https://github.com/scholay/xsime/releases/tag/v0.4.2) — 2026-08-10

### 新功能

- add Windows and Linux input scheme previews (4acf8ca5)
- add marine chrome integration and polish buffer UI (77624176)
- add buffer plugin cycling and improve candidate contrast (e1b5f1ff)
- broaden CLI compatibility and polish workbench UI (a3309f97)
- add My Prompt search plugin (3006c342)
- add local OCR for Remarkable (da81d4ba)
- add configurable plugins and remarkable import (fb87fa8c)
- support multiple enabled buffer plugins (45891910)
- promote prepared actions to the workbench primary control (526311d2)
- refine workbench editing and stream input (e401ccc6)
- ship RIMES workbench and stream input (347f90dd)
- unify AI connectors and support context-only generation (f5991fca)
- **输入法:** 重构设置并扩展缓冲插件平台 (851345f1)
- **缓冲工作台:** 简化交互并加固焦点与主题 (bf15df2f)
- **缓冲工作台:** 独立窗口与焦点安全投递 (9576f3a0)
- **候选窗+MCP:** 尺寸约束+实时预览, MCP 2025-06-18, 通用接入 (87db0b34)
- **M2:** local gateway + MCP + inbound bus + inbox (verified end-to-end) (6e376cc7)
- **ui:** workbench settings IA + three-layer panel preview (56b43ef7)
- **M1:** block provenance (Origin) + echo guard + source badges (91826f78)
- buffer workbench groundwork + M0 safety baseline (41583c44)

### 修复

- verify universal release binary correctly (9dbc258a)
- centralize releases in scholay/rimes (8fa9fce4)
- support macOS save panels and literal v input (3a6d381d)
- stabilize focused workbench and marine chrome capture (76cff6da)
- harden plugin configuration storage and layout (15d5a2bd)
- support stream chords and three-row layouts (addfc66e)
- harden buffer paste and stream alternatives (0aff412c)
- restore Control-Space input switching (1dbdbf97)
- unify workbench capture and semantic blocks (b41cbaed)
- harden workbench delivery and Shift handling (ae251d8e)
- **ime:** restore candidate paging and literal keys (7fe7c82b)
- restore local CLI authentication (dcfc30f7)
- **缓冲工作台:** 收紧单行界面并彻底隔离 Enter 回调 (76706215)
- **候选窗+MCP:** 补齐预览约束并加固本地网关 (001c0156)

### 文档

- add MIT license (68c716a2)
- add bilingual READMEs with product demo links (fdbe0d78)
- note WeChat input-switch crash as a known macOS 26 limitation (e7858011)

### 维护

- normalize Rime data line endings (d61d5345)
- harden preview and prompt smoke timing (e2aef889)
- point release updates at scholay/rimes (baaf2ae4)
- 隔离 AI 缓冲插件夹具 (870b49e0)
- 隔离并标注冒烟测试 (e0d98b29)
- bump version to 0.4.0 (804276bd)
- bump version to 0.3.1 (0e9cb426)
- bump version to 0.3.0 (2f368def)
- run remote-smoke in CI (552bad97)
- bump version to 0.2.0 (d3b55b7b)

### 其他

- Refine input schemes, buffer UI, and compatibility (df5b5091)
- Improve input source registration and installer flow (2f63d6fa)
- Add option-based character selection (111a146c)
- Refine Enter input method UI (154ab5f5)
- Refine candidate window UI (584b6b41)
- Add Marine buffer bridge and harden ETInput metadata (f6884664)
- v0.4.1: restore schema-switch menu, import user Rime config, tidy input source (87404403)
- Bundle 雾凇/串击/并击 Rime schemas for a truly self-contained build (d6d9f9ce)
- v0.3.8: fix input-source enable/icons on macOS 26 + move menu into system input menu (661a6b2e)
- Rebrand to 恩特输入法 + new icon set (app, status-bar, input-source) (419db50f)
- Fix "no candidates": dynamic schema menu + guard stale preference; PDF icon (2719e20b)
- Add input-mode menu icon (fixes blank input source row + enable failure) (448c74e0)
- Fix duplicate/blank input source: give the input mode a distinct id (2321f6d9)
- Show localized name 恩特输入法 in input source list + fix installer dark-mode text (be6c78f5)
- Add guided .pkg installer for first-time install (568a217d)
- Fix repeated Keychain password prompt for remote-typing identity (c90969cb)
- Add 隔空传字 (Mac-to-Mac remote typing) over encrypted LAN P2P (f4738f48)
- Self-contained librime + rebrand to 恩特输入法 (ETInput) + logo (0131ff6b)
- Add GitHub Actions CI and in-app auto-update (7cd74049)
- Improve buffer mode controls and release feedback (548ddb0c)
- Refine candidate buffer UI and controls (b4e9c748)
- Keep buffer anchored with candidate window (141fa918)
- Add keyboard frequency heatmap (1d8bd55f)
- Refine RimeBuffer candidate and buffer UI (3b63bc4f)
- Initial RimeBuffer import (a26da807)
