# iOS input latency follow-up — 1.1.3 (57)

## Feedback and scope

- [Issue #112](https://github.com/scholay/rimes/issues/112) reports intermittent iOS 1.1.1 / Rime Ice Flypy lag with Buffer off. The maintainer additionally received reports that quitting the containing app restores responsiveness.
- On 2026-10-13, App Store Connect had three screenshot submissions (all build 48), no crash-feedback submissions, and no hang signatures for builds 49 and 55. Those submissions do not identify a cause for the reported lag. Older local crash reports are not evidence of this regression.
- This change fixes two independently reproduced resource/rendering defects. It does not establish that every report has the same cause. Keep issue #112 open pending real-device feedback.

## Reproduction and fix

1. With Buffer closed, ordinary keyboard layout still laid out its zero-width source/result rows. Insetting that empty viewport created `CGRect.null`; rounding the clipping path sent non-finite coordinates to CoreGraphics. A 1,500-key hosted test produced 9,432 numeric warnings. The diagnostic backtrace points to `SingleLineTextView.layoutSubviews`.
2. Status lights retained layer animations while hidden or detached; hidden state changes could restart them. Three baseline assertions failed with four live animation keys each. Mascot scheduling and decoded image frames also lacked a common visibility cleanup path.

Closed Buffer rows now skip layout, empty/too-small viewports receive an empty clipping mask, and unchanged caret positions do not invalidate layout. Closing Buffer, losing focus, or detaching the indicator stops its animations and releases decoded mascot frames. Reopening restores the indicator's current state. Waiting dots stop when Buffer closes.

## Validation

- iPhone 17 Pro / iOS 26.5 simulator, Debug hosted suite: **268 tests, 3 conditional skips, 0 failures**. Skips are opt-in delivered Wanxiang/Flypy fixtures and promotional capture; bundled upstream-package import tests did run.
- Repeated presentation test: 12 keyboard controllers, 1,500 synthetic letter presses with Chinese commits, no retained controllers after teardown. Final run: P50 4.24 ms, P95 13.45 ms, max 16.10 ms. These are simulator handler/layout timings, not physical touch-to-display latency or a guaranteed performance target.
- No CoreGraphics numeric warnings in the final complete suite. Visibility tests cover hide/show, state changes while hidden, detach/reattach, Buffer close/reopen, and host focus loss/resume. Geometry tests cover zero/tiny/accessory-only widths and recovery without losing text/caret state. Existing visible status-skin assertions pass in an attached window.
- Distribution resource/privacy verifier passed; release configuration: 4 tests / 15 assertions passed. `git diff --check` passed.
- A separate host bridge probe against a deployed Rime Ice dictionary completed 2,280 keys (P95 0.253 ms; max 17.45 ms), without progressive engine slowdown. This was a macOS bridge probe, not a physical iPhone test or a test of every imported package.

No physical iPhone was available in this pass. Device acceptance, Apple upload processing, TestFlight approval, App Review, and public availability remain separate from the automated results. Raw feedback and diagnostic artifacts stay outside Git because they can contain private user information.
