# Host keyboard style comparison (2026-10-23)

XCUITest driver that captures the system and XSIME keyboards in different hosts on an iOS 26.5 simulator.

- `run.sh <udid> <out>`: Reminders (Apple host) and the XSIME app playground (current-SDK third-party host).
- `run-oldhost.sh <udid> <sdk> <out>`: `OldHost/`, a minimal app stamped with an older SDK in `LC_BUILD_VERSION`.
- Env reaches the test runner only as `TEST_RUNNER_HOST_STYLE_{TAG,SDK,OUT}`.

Findings: a host linked against SDK 18.0 gets the legacy system keyboard (opaque square container, grey 123/return/delete/#+=, white letter keys with a 1 pt lower shadow, wide evenly spaced punctuation row). Current-SDK hosts, Apple or not, get flat white keys in a rounded translucent container. The extension's own view tree, traits and frames are identical in all hosts, so no in-process signal tells them apart. See `reference/`.
