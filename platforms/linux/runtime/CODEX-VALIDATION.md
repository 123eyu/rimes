# ChatGPT / Codex CLI validation — 2026-10-18

Task [#171](https://github.com/scholay/xsime/issues/171) uses actual macOS
request builders, parser replays and settings widgets as its contract. Linux
hosts the official app-server in a private workspace with fixed argv, empty MCP
configuration and a separate ChatGPT credential home. It never runs a shell to
construct a model request. The CLI is installed by the user, independently of
the deb; the explicit imagegen Skill is included with XSIME.

## Verified environment and behavior

Ubuntu 24.04 arm64 container on the macOS development host; GTK 3.24.41,
Fcitx 5.1.7, private D-Bus, Xvfb/xfwm4 and the pinned Rime engine.

- Native request/limit exports and six actual Swift parser replays pass their
  source-hash checks. JSON-line tests split UTF-8 across writes and reject tools,
  model changes, malformed/oversized output, wrong turns, missing completion and
  authentication failures. Commentary and reasoning never become output prose.
- Tests cover fixed argv/environment, quoted workspace paths, dedicated 0700
  directories, login URL validation, explicit cancel/retry, executable loss,
  output limits, timeouts, child-process termination and temporary-file cleanup.
  The real GTK login controls also cover page changes and window destruction.
- The **official Linux Codex CLI 0.163.0** completed an isolated, prompt-free
  initialize → empty MCP → account/read probe and reported login required. No
  personal Codex home, account or external generation was used for that check.
- The production addon in real Fcitx `testfrontend` verifies explicit generation,
  semantic streaming previews, model/effort/Skill changes, invalid-setting
  rejection, native `commitString`, final source consumption, close cancellation
  and late-result rejection. It also verifies ordinary Chinese typing while an
  image job continues after Buffer closes.
- Mailbox image tests verify a durable prompt, one independent active job,
  recovery that respects a live owner lock, revoked grants, stale completions,
  cancellation, provider-path confinement and an independently stored PNG.
- A real primary/secondary GTK process test verifies the passive 292×48 receipt,
  no automatic window opening or mark-read, exact-thread click routing and
  completion deduplication. Session tests cover hiding the receipt on lock and
  keeping it hidden after wake. The receipt contains no prompt or response body.

These are protocol-peer and isolated-desktop tests. The actual CLI probe does
not establish authenticated text/image generation, a physical X11/Wayland
desktop result, or a public Release.

The final clean arm64 deb passed **74/74 CTests (85.20 seconds)**. The 17 source-install/version tests and real deb install/reinstall/remove check passed; all 20 user-data sentinels survived. The package SHA-256 is `3e9503e52d2de799526df20470bfde884984e7283062e8ea8edab390d29fbf38`. It has not been publicly released.

## Visual comparison

[Actual native/GTK captures and provenance](../../../validation/linux-codex-channel-20261018/README.md)
contain five connector states in night/day at 980×680 logical pixels, scale 2,
plus five Linux Buffer image-result themes. Settings cards, row dimensions,
spacing, radio controls, state labels and action buttons follow the native page.
Model/effort/Skill controls use native widths and 10-point regular labels.

The settings comparison still records font-metric, weight, icon and border
differences; every report remains `requires_review`. The native Buffer render
was concealed by the host's protected-input state. That placeholder is excluded
from the visual fixtures, and no protection bypass was added. A valid native
Buffer comparison, notification renders and physical desktop checks remain in
[#179](https://github.com/scholay/xsime/issues/179); #171 stays open for these gates
and an explicitly authenticated external-channel check.

## Repeat the checks

```sh
python3 platforms/linux/runtime/verify_codex_contract.py
ctest --test-dir platforms/linux/ime/build --output-on-failure
RIMES_SHARED_DIR=/path/to/staged/data \
  platforms/linux/ime/build/rimes-codex-fcitx-e2e \
  platforms/linux/ime/build . test-data \
  "$PWD/platforms/linux/runtime/tests/codex_fixture.py"
platforms/linux/ime/build/runtime/rimes-codex-tests --probe-official /absolute/path/to/codex
```

The official probe uses a new private test home and requires no account. The
ordinary Codex tests take `codex_fixture.py` as their executable; never substitute
a personal authenticated CLI into the fake-peer suite.

For native exports use the isolated debug executable's `codex-linux-fixtures`
and `codex-settings-render` commands. Import with `verify_codex_contract.py
--import-fixture FILE --import-settings FILE`. The verifier fails on changed
native source rather than silently generating a new Linux expectation.

Source and deb lifecycle coverage includes Codex `auth.json`, generated images,
generation-owner metadata, Mailbox records, dictionaries and learning data.
Installed imagegen Skill files belong to the installation receipt and are removed
with the program; user-owned data stays intact. Linux remains on the same project
`v*` Release, and this implementation does not create a tag or publish a package.
