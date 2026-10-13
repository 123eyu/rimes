#!/usr/bin/env bash
# Build and test a versioned .deb of the Fcitx5 addon + reviewed rime-data.
set -euo pipefail

SCRIPT_DIR=$(CDPATH= cd -- "$(dirname -- "${BASH_SOURCE[0]}")" && pwd -P)
IME_ROOT=$(CDPATH= cd -- "$SCRIPT_DIR/.." && pwd -P)
REPO_ROOT=$(CDPATH= cd -- "$IME_ROOT/../../.." && pwd -P)
if [[ ! -f "$REPO_ROOT/scripts/platform-preview/preview.py" ]]; then
    echo "error: could not locate the repository root from $IME_ROOT" >&2
    exit 1
fi
PRODUCT_VERSION=$(tr -d '[:space:]' < "$REPO_ROOT/VERSION")
RELEASE_VERSION="${RIMES_RELEASE_VERSION:-$PRODUCT_VERSION}"
SEMVER='(0|[1-9][0-9]*)\.(0|[1-9][0-9]*)\.(0|[1-9][0-9]*)'
if [[ ! "$RELEASE_VERSION" =~ ^${SEMVER}(-preview\.[1-9][0-9]*)?$ ]]; then
    echo "error: expected X.Y.Z or X.Y.Z-preview.N: $RELEASE_VERSION" >&2
    exit 1
fi
if [[ "$RELEASE_VERSION" != *-preview.* && "$RELEASE_VERSION" != "$PRODUCT_VERSION" ]]; then
    echo "error: formal Linux version must match repository VERSION ($PRODUCT_VERSION)" >&2
    exit 1
fi
# Debian's '~' sorts previews before the corresponding final version.
VERSION="${RELEASE_VERSION/-preview./~preview.}"
if [[ "${1:-}" == --print-version ]]; then
    echo "$VERSION"
    exit 0
fi
if [[ -n "${RIMES_IME_RIME_PREFIX:-}" ]]; then
    echo "error: this deb uses distro librime; unset RIMES_IME_RIME_PREFIX before packaging" >&2
    exit 1
fi
ARCH=$(dpkg --print-architecture)
OUT_DIR=$(python3 -c 'import os, sys; print(os.path.abspath(sys.argv[1]))' "${1:-$IME_ROOT/dist}")

WORKDIR=$(mktemp -d /tmp/rimes-deb.XXXXXX)
trap 'rm -rf "$WORKDIR"' EXIT

STAGE="$WORKDIR/data"
python3 "$REPO_ROOT/scripts/platform-preview/preview.py" stage \
    --repo-root "$REPO_ROOT" \
    --output-dir "$STAGE"

BUILD_DIR="$WORKDIR/build"
cmake -S "$IME_ROOT" -B "$BUILD_DIR" \
    -DCMAKE_BUILD_TYPE=Release \
    -DRIMES_PRODUCT_VERSION="${RELEASE_VERSION%%-*}" \
    -DCMAKE_INSTALL_PREFIX=/usr \
    -DFCITX_INSTALL_USE_FCITX_SYS_PATHS=ON
cmake --build "$BUILD_DIR" -j"$(nproc)"
ctest --test-dir "$BUILD_DIR" --output-on-failure
"$BUILD_DIR/rimes-engine-smoke" "$STAGE" "$WORKDIR/smoke-user"

ROOT="$WORKDIR/root"
RIMES_IME_STAGE_DIR="$STAGE" DESTDIR="$ROOT" \
    "$SCRIPT_DIR/install.sh" --skip-build --build-dir "$BUILD_DIR" --prefix /usr
DESTDIR="$ROOT" "$SCRIPT_DIR/uninstall.sh" --prefix /usr --dry-run
# dpkg owns the package payload; do not leave a competing source-install receipt.
rm "$ROOT/usr/share/rimes/ime-install-manifest.json"

mkdir -p "$ROOT/usr/share/doc/fcitx5-rimes"
cp "$IME_ROOT/README.md" "$ROOT/usr/share/doc/fcitx5-rimes/README.md"

# Resolve ABI-versioned dependencies from the actual ELF payload. Keep the Lua
# module explicit because librime loads it dynamically rather than via NEEDED.
mkdir -p "$WORKDIR/debian"
cat > "$WORKDIR/debian/control" <<'EOF'
Source: fcitx5-rimes
Section: utils
Priority: optional
Maintainer: RIMES contributors <https://github.com/scholay/rimes>

Package: fcitx5-rimes
Architecture: any
Description: RIMES Fcitx5 input method
EOF
ELF_FILES=()
while IFS= read -r -d '' file; do
    if readelf -h "$file" >/dev/null 2>&1; then
        ELF_FILES+=("-e$file")
    fi
done < <(find "$ROOT/usr" -type f -print0)
DEPS=$(cd "$WORKDIR" && dpkg-shlibdeps -O "${ELF_FILES[@]}")
[[ "$DEPS" == shlibs:Depends=?* && "$DEPS" != *$'\n'* ]] || {
    echo "error: could not determine runtime dependencies" >&2
    exit 1
}
DEPS=${DEPS#shlibs:Depends=}

mkdir -p "$ROOT/DEBIAN"
SIZE=$(du -sk "$ROOT" | awk '{print $1}')
cat > "$ROOT/DEBIAN/control" <<EOF
Package: fcitx5-rimes
Version: $VERSION
Section: utils
Priority: optional
Architecture: $ARCH
Installed-Size: $SIZE
Depends: fcitx5, librime-plugin-lua, $DEPS
Recommends: libgtk-layer-shell0, wl-clipboard
Maintainer: RIMES contributors <https://github.com/scholay/rimes>
Description: RIMES input method frontend for Fcitx 5
 A librime frontend owned by RIMES. It deploys the reviewed RIMES rime-data
 set, keeps user state in ~/.local/share/rimes, and ships the Linux Buffer
 workbench (rimes-buffer) and Capsule rail (rimes-capsule) next to the
 Fcitx5 addon.
EOF

mkdir -p "$OUT_DIR"
DEB="$OUT_DIR/fcitx5-rimes_${VERSION}_${ARCH}.deb"
dpkg-deb --build --root-owner-group "$ROOT" "$DEB"
(cd "$OUT_DIR" && sha256sum "$(basename "$DEB")" > "$(basename "$DEB").sha256")
echo "Wrote $DEB"
