# Flatpak-compatible install path

The XSIME Fcitx5 addon is a **host Fcitx5 module**, not a standalone Flatpak
app. Flatpak applications talk to the host or Flatpak Fcitx5 over the portal.

## User-local prefix (recommended for testers)

```bash
platforms/linux/ime/scripts/install.sh --prefix "$HOME/.local"
```

Host Fcitx5 loads:

- `$prefix/lib/<multiarch>/fcitx5/rimes.so`
- `$prefix/share/fcitx5/addon/rimes.conf`
- `$prefix/share/fcitx5/inputmethod/rimes.conf`
- `$prefix/share/rimes/data` (reviewed SharedSupport)

This works with a native Fcitx5. It is the path CI and `package-deb.sh` also
exercise (`/usr` instead of `~/.local`).

## Flatpak Fcitx5 (`org.fcitx.Fcitx5`)

A sandboxed Fcitx5 cannot load a random host `.so`. Options:

1. Install the addon on the host and use host Fcitx5 (apps talk via portal).
2. Copy the addon into the Flatpak data tree **only if** the runtime ABI
   matches (`~/.var/app/org.fcitx.Fcitx5/…`). This is unsupported and often
   fails across runtime upgrades.
3. A future Flatpak extension of `org.fcitx.Fcitx5` could ship `rimes.so`
   built against that runtime. That is not automated here.

Do not put the addon `.so` inside the data-preview tarball.
