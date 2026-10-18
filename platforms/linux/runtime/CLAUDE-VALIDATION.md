# Claude Code CLI validation — 2026-10-18

Task [#172](https://github.com/scholay/xsime/issues/172) follows the actual macOS
request builder, login arguments, streaming parser and settings widgets. The
optional official plugin declares Linux support only with its compiled adapter.

## Verified behavior

Ubuntu 24.04 arm64 container, GTK 3.24.41, Fcitx 5.1.7 and pinned Rime on the
macOS development host; private D-Bus/Xvfb for settings tests.

- Six actual Swift parser replays are fed byte by byte into the Linux parser.
  The contract checks source hashes, exact request/login flags and limits.
  JSON validation rejects duplicate keys, changed sessions/models, tool/MCP
  activity, unknown content deltas, incomplete results and oversized output.
  Reasoning and account metadata never become Buffer text.
- Print-mode stdin closes after the complete request. Streaming previews cannot
  be delivered until the final semantic result validates, stdout reaches EOF
  and the CLI exits successfully. A complete-looking reply followed by exit
  failure remains unusable. Cancellation kills the owned process group and
  cleans its private request directory.
- Settings tests click the actual GTK controls: prompt-free probe, explicit
  login, cancel, retry, reauthorization, page switching and window destruction.
  Browser codes/links and CLI account output are drained without being retained
  or shown by XSIME. The CLI owns its existing credentials.
- Discovery skips an older incompatible installation for a compatible candidate
  later in PATH. A configured `RIMES_CLAUDE_PATH` is a pin and does not fall back.
  Executable identity is checked before each subprocess. API keys, OAuth tokens,
  alternative configuration roots and runtime injection variables are removed
  from the subprocess environment.
- Actual Fcitx verifies the full source request, model/effort controls, partial
  previews, public activity labels, native `commitString`, source consumption,
  source edits, closing, focus changes, grant revocation and ordinary Chinese
  typing. A regression test also exercises Codex text and persistent images.
  The test control connection stays open like the real Buffer UI: a one-shot
  client that closed immediately could race the initial server snapshot.
- The **official Linux Claude Code 2.1.302** passed declared-capability and
  isolated `auth status --json` checks in a fresh HOME without a prompt or
  personal account. Its downloaded arm64 binary matched the official manifest
  SHA-256 `2bd4f4e5ed5cb09955b490098f745321cb5491e7ea8c752ba1e3bb010cdadb10`.
  This does not establish authenticated model generation or browser login.

Source install/uninstall and real deb install/reinstall/remove preserve user
dictionaries, learning, Capsule/Mailbox data, Codex credentials/images and
Claude-owned `~/.claude/.credentials.json` / `~/.claude.json` test sentinels.
XSIME neither bundles nor removes the separately installed Claude CLI.

The clean arm64 deb passed **77/77 CTests (95.50 seconds)**; native Swift build/AI smoke, 13 source lifecycle tests and real deb lifecycle checks also passed. All 20 XSIME and 2 Claude-owned file sentinels survived. Package SHA-256: `c78c42e6fb0edb34994b711a9d5a6e36cab2dca14cda78a6439e1c17418f3a00`.

## Visual comparison and remaining gates

[Native AppKit and GTK renders](../../../validation/linux-claude-channel-20261018/README.md)
cover five login states in night/day, 980×680 logical pixels at scale 2. Card,
row, icon tile, radio and button geometry follow actual native widgets; expected
button labels come from the exported native states.

Font metrics/weight, substitute symbols, waiting spinner and small borders still
differ. Reports remain `requires_review`, with shared component calibration in
[#179](https://github.com/scholay/xsime/issues/179). Live authenticated generation,
physical X11/Wayland focus/browser behavior and final pixel acceptance remain
open. No release tag or public package has been created by this task.

## Repeat

```sh
python3 platforms/linux/runtime/verify_claude_contract.py
ctest --test-dir platforms/linux/ime/build --output-on-failure
RIMES_SHARED_DIR=/path/to/staged/data \
  platforms/linux/ime/build/rimes-claude-fcitx-e2e \
  platforms/linux/ime/build . test-data \
  "$PWD/platforms/linux/runtime/tests/claude_fixture.py"
platforms/linux/ime/build/runtime/rimes-claude-tests --probe-official /absolute/path/to/claude
```

The optional official probe uses a new private HOME and sends no prompt. Ordinary
protocol tests use `claude_fixture.py`; never replace that argument with a
personal authenticated CLI. Native exports are produced by the isolated debug
executable's `claude-linux-fixtures` and `claude-settings-render` commands, then
imported with `verify_claude_contract.py --import-fixture FILE --import-settings FILE`.

Official CLI documentation: [reference](https://code.claude.com/docs/en/cli-reference)
and [authentication](https://code.claude.com/docs/en/authentication). Actual
`--help` declarations are checked at runtime, rather than assuming that a
version number supplies all required flags.
