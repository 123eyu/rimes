# Linux Mailbox terminal validation

Task [#174](https://github.com/scholay/xsime/issues/174) embeds GTK VTE in the production Mailbox host. Shell, Codex and Claude Code start in independent real PTYs and chosen working directories. Window/module visibility is independent of the process lifetime. An explicit stop terminates all same-user children in the owned PTY session, including separate job-control process groups, with a bounded TERM/KILL sequence and pidfd identity checks. Host shutdown also reaps owned children; a new application run starts without invented live sessions. A child that explicitly detaches into a new session is outside this ownership boundary.

PTY input is direct: normal terminal keys, Escape, Ctrl+W and Ctrl+C reach the session. Ctrl+Shift+C/V use VTE's selection/clipboard actions. Protection disables input and hides the host while preserving processes. Interactive CLI sessions use the user's ordinary CLI home; background Mailbox Codex generation retains its separate managed home. A clean environment keeps required Linux desktop sockets and SSH agent access without inheriting unrelated IME secrets. No terminal output is automatically interpreted as an AI result.

The file index is nonrecursive, skips hidden files and links, scans at most 10,000 entries and retains 60 naturally sorted regular files. An explicit chooser can select another file. A separate helper reads stable, bounded bytes without following path symlinks, enforces CPU/address-space/time bounds, and only sends allowlisted raster formats to GdkPixbuf. Limits match the native contract: 24 MiB input, 1 MiB UTF-8 text, 16,000 pixels per image side and 32 million total pixels. Previews are tied to the selected session UUID. Manual collection persists the exact previewed bytes, attachment and UUID in one receipt transaction. Cancellation before publication removes staged images. Navigation or selection changes reject stale results; duplicate clicks cannot duplicate a receipt.

The native reference revealed two existing defects, also fixed here: ImageIO can return a source object for ordinary UTF-8 text before recognizing an image type, and SwiftTerm on Darwin forwards packed waitpid status. Native text collection now falls through correctly and terminal exit 7 displays as 7. Actual AppKit/SwiftTerm fixtures exercise both fixes. Source hashes prevent silently using a stale native contract in the Linux build.

## Verified

- Ubuntu 24.04 arm64, GTK 3.24.41, VTE 0.76.0, Fcitx 5.1.7 and pinned Rime/Lua: **81/81 CTests passed** in the clean Release/deb build (106.52 seconds).

- Real PTY processes under private D-Bus/Xvfb/xfwm4: UTF-8 input, independent cwd/session IDs, interactive key events including explicit copy/paste, SIGWINCH, wide-to-compact resize, navigation/protection, cancel, exit 7, separate process-group shutdown, pending-spawn destruction and empty restart passed with `G_DEBUG=fatal-warnings`.
- File/receipt tests cover exact UTF-8 and normalized PNG, empty/invalid/NUL/oversized text, oversized files, symlinks, FIFOs, natural sorting and limits, cancellation, stale-session preview, edits after preview, duplicate collection, atomic attachment cleanup and persisted UUIDs.
- Native Swift debug build and `mailbox-workspace-smoke` passed. Actual AppKit fixtures produced 20 captures and verified preview, collection, UUID association and exit status. The native store read back two Linux-generated receipts with complete messages, sources, dates and images.
- Source install/uninstall: 13 lifecycle tests passed, including the new helper's ownership/removal and preservation of dictionaries, learning, Mailbox and CLI-owned files.

- The actual installed arm64 deb executed all 20 terminal fixtures, including real PTY input/exit and the installed preview helper, then passed primary/secondary Mailbox process regression. Install, reinstall and removal passed; **22 XSIME files and 2 Claude-owned files** retained their hashes.

Unpublished local package: `fcitx5-rimes_1.1.5_arm64.deb`, SHA-256 `a69a4f0c01878a9b0e7f464f9466f7d0fe0ed87ecbb588f2148089b5a02f28b0`. Public release remains the shared project `v*` release process; this work creates no release tag.

## Visual and desktop gates

[20 actual AppKit/SwiftTerm and GTK/VTE comparisons](../../../validation/linux-mailbox-terminal-20261018/README.md) cover empty, running, file preview, collected receipt and exited states in night/day themes, at 1000×700 and 680×500 logical pixels and scale 2. Both run the same public command and collect the same public file. All pairs have matching dimensions; ICC profiles are converted to sRGB for comparison.

The captures remain `requires_review`: fonts, icons, picker/segment controls, scrollbar and cursor rendering differ. Task [#179](https://github.com/scholay/xsime/issues/179) retains visual and desktop acceptance. Private X11 tests do not prove physical GNOME/KDE, Wayland IM composition, multi-monitor/scaling, accessibility or real Codex/Claude account sessions. Terminal keys and process ownership are tested with actual public PTY peers; no personal provider account is used.

## Reproduction

```sh
# macOS, repository root, isolated production AppKit/SwiftTerm fixtures
TZ=Asia/Tokyo .build/out/Products/Debug/RimeBuffer mailbox-terminal-linux-fixtures /tmp/native-terminal
python3 platforms/linux/mailbox/verify_terminal_contract.py --import-fixture /tmp/native-terminal/native-terminal.json

# Linux, configured build and disposable test profiles
ctest --test-dir platforms/linux/ime/build --output-on-failure -R 'mailbox-terminal|mailbox-ui|mailbox-chat-ui'
G_DEBUG=fatal-warnings TZ=Asia/Tokyo xvfb-run -a platforms/linux/ime/build/rimes-mailbox --render-terminal /tmp/linux-terminal
```

Both fixture entry points reserve `/tmp/xsime-terminal-public-workspace` exclusively and remove only the directory they created. Linux executes the compiled, validated public command, never a command from an arbitrary input JSON. `RIMES_TERMINAL_EXPORT` on the terminal test exports public receipts for `RimeBuffer mailbox-linux-verify <directory>`. Local full logs are `.build/linux-parity-20261017/task24-*`; committed logs contain only public fixtures and verification output.
