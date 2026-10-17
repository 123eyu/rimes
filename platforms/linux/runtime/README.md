# Linux plugin runtime

`rimes-plugins --list` returns the pinned official catalog with `available`,
`installed`, `enabled`, `bundled`, `grant` and a user-facing error for each entry.
Only an explicit Linux platform declaration **and** a compiled adapter make an
entry available. The current pin has no Linux declarations; existing legacy
Capsule notes remain controlled by Fcitx configuration until their module port.

`rimes-plugins --request` reads one JSON object from standard input and returns
one JSON result. This companion keeps downloads outside the input-method event loop. Requests contain `op`, `id`, and the `grant`
from the latest list. Operations are `install`, `restore-bundled`, `enable`,
`disable`, and `uninstall`. No shell command or executable can be installed by
a plugin package. `restore-bundled` works offline for a preinstalled package.

The build verifies the official source lock and embeds the catalog, bundled
manifests and checksums. Downloads use HTTPS from the official release with
bounded size/time and a restricted redirect list. Installation validates the
digest, identity, version, host SDK, minimum host, platform, capabilities and
contribution before publishing a new receipt. A failed download or validation
leaves the existing version intact. Install/update/reinstall defaults to disabled;
enablement is explicit. Bundled defaults apply only once, never after an uninstall.

Receipts and downloaded manifests live in `$XDG_DATA_HOME/rimes/plugins`
(`~/.local/share/rimes/plugins` by default, or `$RIMES_USER_DIR/plugins` for an
isolated profile). Directories are private `0700`, files `0600`. Access is
descriptor-relative, refuses symlinks/hardlinks and rechecks private permissions.
A lock and atomic receipt replacement prevent a stale settings window from
overwriting a later uninstall. **Do not delete the Rime user directory to
uninstall a plugin or the application.** Dictionaries, learning records,
Capsule content, credentials and sessions live outside package ownership and
are preserved. Application removal still follows the [installation guide](../ime/README.md).

Fcitx watches receipt changes through inotify. Changes stop the old adapter and
revoke its grants before applying the new state; a selected Buffer plugin falls
back to Default. Worker results also recheck the persisted receipt immediately
before acceptance, including when the filesystem notification has not yet run.
Losing the watched directory closes authorization until a new host lifetime.

Portable core validation on Linux:

```bash
cmake -S platforms/linux/runtime -B /tmp/xsime-runtime -DBUILD_TESTING=ON
cmake --build /tmp/xsime-runtime
ctest --test-dir /tmp/xsime-runtime --output-on-failure
```

These checks cover receipt races, invalid packages, offline failures, unsafe
paths, lifecycle revocation and data retention. Desktop interaction and release
package acceptance remain separately tracked in [the parity plan](../PARITY.md).

## Native settings and private configuration

Open **XSIME 设置** from the application menu or Fcitx's XSIME configuration,
or run `rimes-settings`. The settings host provides official plugin management,
declarative plugin forms, API provider configuration, theme previews and Buffer
preferences. Unported module pages remain explicitly unavailable while their
tasks are in progress; a catalog card does not imply Linux runtime support.

Configuration lives under `$XDG_DATA_HOME/rimes/configuration`: `preferences.json`
holds ordinary fields and opaque credential references, while `credentials/`
holds separately owned `0600` credential files inside `0700` directories. Keys
never enter command arguments, status text or ordinary preference fields. A
blank key field retains the saved value; **清除密钥…** explicitly removes it.
Concurrent saves reject stale revisions. Invalid or unsafe files are preserved
for recovery instead of silently replaced with defaults. Reload settings after
restoring a private backup. If credential cleanup fails after a successful save,
the UI reports that distinction.

Each request receives an immutable configuration snapshot. Saving preferences
invalidates outstanding plugin grants. The running Fcitx addon observes atomic
updates; Buffer width, close-after-last and app-switch behavior take effect
without restart. App-switch clearing defaults to **off**, matching macOS; when
enabled it clears local typing only and preserves staged external content.

`generate_visual_tokens.py` imports native macOS palettes, Settings chrome,
dimensions, navigation, catalog labels and existing brand images at build time.
The current GTK implementation is still undergoing the
[pixel comparison acceptance](../VISUAL-PARITY.md); screenshots do not establish
finished visual parity. Linux currently uses Inter/Noto Sans CJK and drawn
semantic icons; Liquid Glass uses a solid fallback pending compositor work.

Render only into an isolated test profile and compare with the actual native
macOS `settings-render` output:

```bash
RIMES_USER_DIR=/tmp/xsime-visual-fixture xvfb-run -a rimes-settings --render /tmp/linux-render
python3 platforms/linux/runtime/compare_visuals.py --macos /tmp/macos-render \
  --linux /tmp/linux-render --output /tmp/visual-comparison
```

The comparison tool requires Pillow, converts embedded monitor ICC profiles to
sRGB, writes pairs/difference maps and records unresolved review status. It never
turns a similarity percentage into an automatic acceptance. `rimes-settings
--smoke` creates its own disposable profile and exercises real controls, rejected
saves, credential redaction, restart and layout dimensions under Xvfb.
