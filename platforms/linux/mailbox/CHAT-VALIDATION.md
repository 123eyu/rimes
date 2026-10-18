# Linux Mailbox AI conversation validation

Task [#173](https://github.com/scholay/xsime/issues/173) uses the production Codex, Claude and OpenAI-compatible transports. `ChatJobs` belongs to the Mailbox host, independent of the visible module/window and of Buffer. Only a validated final response becomes a durable inbound message. The 50 ms streaming preview remains in memory; cancellation, grant revocation and failure never publish partial output as a response.

New conversations freeze the selected CLI model or API profile/model/revision. Global selection changes cannot reroute an existing conversation. API profile edits, credential rotation or deletion invalidate the saved route. Historical API rows without a route fail closed; historical CLI rows pin the adapter's verified default on their first continuation. Request history preserves user/assistant order, excludes local notes/status events, and is bounded before admission. Read/unread, title and archive changes do not invalidate a reply; source, transcript or generation changes do. An explicit retry reuses the failed last user turn without duplicating it.

Native `AITextRequestPlanner` exports actual initial and continuation prompts for plain, Markdown and JSON output. `verify_chat_contract.py` validates the source hashes before the Linux build. The native workspace accepts an injected model catalog for isolated rendering; its normal production catalog remains the default. No personal account or workspace is used for fixtures.

## Verified

- Ubuntu 24.04 arm64, GTK 3.24.41, Fcitx 5.1.7, pinned Rime/Lua, private D-Bus/Xvfb/xfwm4: **80/80 CTests passed** in the clean Release/deb build (104.81 seconds).
- Real CLI child processes and real loopback libcurl: native prompt bytes, continuation history, final paragraph separation, concurrent-turn rejection, capacity, live owner/crash recovery, cancellation, authorization revocation, retry, legacy CLI pinning and frozen API routes passed. Public deterministic peers validate the actual request protocol; they do not represent live provider account generation.
- Rebuilt Fcitx addon: Claude text/cancellation/revocation and Codex text/image E2E hosts both passed with the current shared channel code.
- Actual GTK widgets: new-chat model selection, send/stream/final, next-message draft, hide/navigation, cancel/retry, protection and disconnected admission passed. Existing search/rename/archive/notes and explicit Buffer transfer regression tests also passed. Primary/secondary process deep links now select the requested thread before marking a previous selection read.
- Native Swift debug build, `ai-text-mailbox-smoke`, `mailbox-workspace-smoke` and native readback of **5 Linux-generated conversations** passed. The readback retains routes, sources, messages, formats and dates; the expected native projection omits Linux's generation-owner extension.
- Source install/uninstall: **13 tests passed**. Actual arm64 deb install, reinstall and removal passed; **22 XSIME files and 2 Claude-owned files** retained their hashes, including dictionaries, learning, Mailbox, Provider identities/models and separate credentials.

Unpublished local package: `fcitx5-rimes_1.1.5_arm64.deb`, SHA-256 `0f519d7ef58a867f65b1ef0c5aeb5dc66937c46d7746aa639d1c87574fdabe1c`. Public release remains the shared project `v*` release process; this work creates no release tag.

## Visual and desktop gates

[16 actual AppKit/GTK comparisons](../../../validation/linux-mailbox-chat-20261018/README.md) cover existing, new, streaming and failed conversations in night/day themes, at 1000×700 and 680×500 logical pixels and scale 2. Both sides use identical public records and preview text, with embedded ICC profiles converted to sRGB for comparison. Source hashes and original images are included.

These comparisons remain `requires_review`. Linux now keeps list previews on one line, renders the new-chat control and upward send icon, and matches the composer hint and streaming identity. Remaining differences include font weight/metrics, icons, search and segment controls, model picker/disabled-button styling, and the explicit Linux stop/retry/delete controls absent from the current native surface. Task [#179](https://github.com/scholay/xsime/issues/179) must finish the shared visual/action acceptance. Private desktop tests do not prove physical GNOME/KDE, Wayland IM composition, multi-monitor/scaling, assistive-technology behavior or live provider account generation.

## Reproduction

```sh
# macOS, repository root, isolated production AppKit fixtures
.build/out/Products/Debug/RimeBuffer mailbox-chat-linux-contract /tmp/native-chat.json
python3 platforms/linux/mailbox/verify_chat_contract.py --import-fixture /tmp/native-chat.json
TZ=Asia/Tokyo .build/out/Products/Debug/RimeBuffer mailbox-linux-fixtures /tmp/native-mailbox
python3 platforms/linux/mailbox/verify_native_contract.py --import-fixture /tmp/native-mailbox/native-mailbox.json

# Linux, configured build and disposable test profiles
ctest --test-dir platforms/linux/ime/build --output-on-failure -R 'mailbox|configuration|ai-codec|codex-channel|claude-channel'
TZ=Asia/Tokyo xvfb-run -a platforms/linux/ime/build/rimes-mailbox \
  --render-fixture /tmp/native-mailbox/native-mailbox.json --render-dir /tmp/linux-mailbox
```

`chat_http_fixture.py` drives the executable and both public CLI peers. Setting `RIMES_CHAT_EXPORT` to a new directory additionally exports public Linux conversations for `RimeBuffer mailbox-linux-verify <directory>`. Local full logs are `.build/linux-parity-20261017/task23-*`; committed logs contain only public fixtures and build/verification output.
