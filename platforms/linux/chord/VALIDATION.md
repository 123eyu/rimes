# Linux ordinary chord validation

Task [#176](https://github.com/scholay/xsime/issues/176) connects the shared chord rules to the production Fcitx addon and five GTK settings pages. The shared implementation is maintained in [official plugin PR #16](https://github.com/scholay/rimes-plugins/pull/16). Stream input and its chord integration are deferred by the maintainer.

## Behavior and persistence

FlyYao batches settle after the configured physical-key inactivity interval. Cross-batch pairing requires the exact raw input, raw caret, displayed caret and selection from the prior insertion; two singletons remain literal. Focus, protection, schema, configuration and plugin-grant changes retire pending work. Candidate selection and nonchord boundaries settle synchronously. Native Yoyo schemes receive physical presses/releases directly and settle only after the last release. Provisional native composition stays out of client preedit because GTK/Qt can commit client preedit before focus-out cancellation. All real text uses the existing Fcitx commit path.

Applying freezes a profile, retires input sessions, preserves ordinary schemas and other YAML patch fields, then deploys and verifies real Rime outputs in a private session. Only a successful verification publishes the active snapshot. Failure restores exact source bytes/modes and verifies the old profile; concurrent external edits are not overwritten. Plugin lifecycle startup waits until the registry has published its live grant. Timers specify a 1 ms accuracy instead of accepting the event backend's coarser default.

Draft and active files are separate. Private files refuse unsafe paths and stale revisions, bound profiles/entries/bytes and preserve corrupted records. Learning uses the active key zones even if the draft differs. Native course IDs, opaque mapping IDs, source ordering, seeded sampling and mastery rules are shared. Progress stores only aggregate counts and opaque identities; concurrent writers merge under a lock.

The Linux full data stage adds 12 hash-locked native Yoyo files and their source notice to the original 57-file data closure. The independent Data Preview remains unchanged. Runtime activation checks published text/candidate traces as well as schema selection, so a missing dictionary cannot appear successfully activated.

## Verification

- Actual macOS exports: four byte-identical schema compiler outputs, 48 boundary/routing examples, 15 profile validation cases, full-pinyin/Ziranma encoding and three course/sample fixtures. Source digests make stale references fail the build.
- Pinned Linux Rime/Lua: 870 actual compiled mapping outputs and 16 shared Yoyo text/candidate traces passed.
- Actual Fcitx test frontend: timed batches, left/right pairing, focus cancellation, immediate Space settlement, protected fields, draft/application isolation, disable/reenable and ordinary fallback passed. A valid profile with a deliberately failing deployed mapping restored the previous applied snapshot and exact deployment files. Both native presets passed all shared traces, duplicate press suppression, held-key behavior, last-release settlement and focus-out cancellation.
- GTK with fatal warnings: editing and saving a draft, updating a mapping, local preview, case/whitespace search, cancelled row changes, discard-before-copy, practice scoring, feedback-period suppression, loss of focus, and active-versus-draft key zones passed. Capture remains local to the focused widget.
- Source installation/data staging: 20 Python tests passed, including native data inventory/hash validation and preservation of chord drafts, active snapshot, input selection and learning records.
- macOS debug build and nine smoke commands passed: startup, plugin distribution, native routing, keymaps, activation, Ziranma, unified input configuration, learning, and both real Yoyo engine schemas.

Clean Release/deb verification and installed-package lifecycle evidence are recorded in the [provenance manifest](../../../validation/linux-chord-20261018/provenance.json).

## Visual and desktop acceptance

[20 actual AppKit/GTK pairs](../../../validation/linux-chord-20261018/README.md) cover five pages and four themes at 698 logical pixels wide and scale 2. Page and card geometry match. Font metrics, icons, control chrome, table spacing and full interactive states remain `requires_review`; this is not pixel-complete acceptance. [#179](https://github.com/scholay/xsime/issues/179) retains GNOME/KDE, X11/Wayland host input, scaling, accessibility, hardware layouts, unusual modifier/key-release behavior and crash/restart acceptance. The private Fcitx frontend is not a physical desktop.

## Reproduction

```sh
# macOS, public and isolated native fixtures
.build/out/Products/Debug/RimeBuffer chord-linux-fixtures /tmp/native-chord.json
python3 platforms/linux/chord/verify_contract.py --import-fixture /tmp/native-chord.json
.build/out/Products/Debug/RimeBuffer chord-linux-ui-smoke /tmp/native-chord-ui

# Linux, configured build and disposable profiles
python3 platforms/linux/ime/scripts/stage-data.py --output-dir /tmp/chord-shared
ctest --test-dir platforms/linux/ime/build --output-on-failure -R chord
platforms/linux/ime/build/rimes-chord-engine-tests /tmp/chord-shared
RIMES_SHARED_DIR=/tmp/chord-shared dbus-run-session -- \
  platforms/linux/ime/build/rimes-chord-fcitx-e2e platforms/linux/ime/build . test-data
NO_AT_BRIDGE=1 G_DEBUG=fatal-warnings GDK_SCALE=2 xvfb-run -a \
  -s '-screen 0 2560x2200x24' platforms/linux/ime/build/runtime/rimes-chord-ui-tests /tmp/linux-chord-ui
```

Logs under `validation/linux-chord-20261018/logs` contain public fixtures and result summaries. No real account or personal input data is used. No release tag or asset publication is part of this task.
