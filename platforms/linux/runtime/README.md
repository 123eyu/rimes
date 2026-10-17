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
