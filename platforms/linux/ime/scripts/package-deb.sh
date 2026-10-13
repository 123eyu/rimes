#!/usr/bin/env bash
# Build a versioned .deb with the pinned private Rime/Lua runtime.
set -euo pipefail

SCRIPT_DIR=$(CDPATH= cd -- "$(dirname -- "${BASH_SOURCE[0]}")" && pwd -P)
IME_ROOT=$(CDPATH= cd -- "$SCRIPT_DIR/.." && pwd -P)
REPO_ROOT=$(CDPATH= cd -- "$IME_ROOT/../../.." && pwd -P)
if [[ ! -f "$REPO_ROOT/scripts/platform-preview/preview.py" ]]; then
    echo "error: could not locate the repository root from $IME_ROOT" >&2
    exit 1
fi
VERSION=$(tr -d '[:space:]' < "$IME_ROOT/VERSION")
[[ "$VERSION" == "$(tr -d '[:space:]' < "$REPO_ROOT/VERSION")" ]] || {
    echo 'error: Linux release version must match repository VERSION' >&2; exit 1;
}
RIME_PREFIX="${RIMES_IME_RIME_PREFIX:-$IME_ROOT/.engine/install}"
[[ -f "$RIME_PREFIX/lib/librime.so" ]] || {
    echo 'error: run scripts/build-engine.py to build pinned Rime/Lua first' >&2; exit 1;
}
command -v patchelf >/dev/null
export PKG_CONFIG_PATH="$RIME_PREFIX/lib/pkgconfig:${PKG_CONFIG_PATH:-}"
export CMAKE_PREFIX_PATH="$RIME_PREFIX:${CMAKE_PREFIX_PATH:-}"
ARCH=$(dpkg --print-architecture)
MULTIARCH=$(dpkg-architecture -qDEB_HOST_MULTIARCH)
OUT_DIR=$(python3 -c 'import os,sys; print(os.path.abspath(sys.argv[1]))' "${1:-$IME_ROOT/dist}")

WORKDIR=$(mktemp -d /tmp/rimes-deb.XXXXXX)
trap 'rm -rf "$WORKDIR"' EXIT

STAGE="$WORKDIR/data"
python3 "$REPO_ROOT/scripts/platform-preview/preview.py" stage \
    --repo-root "$REPO_ROOT" \
    --output-dir "$STAGE"

BUILD_DIR="$WORKDIR/build"
cmake -S "$IME_ROOT" -B "$BUILD_DIR" \
    -DCMAKE_BUILD_TYPE=Release \
    -DCMAKE_INSTALL_PREFIX=/usr \
    -DFCITX_INSTALL_USE_FCITX_SYS_PATHS=ON
cmake --build "$BUILD_DIR" -j"$(nproc)"
ctest --test-dir "$BUILD_DIR" --output-on-failure

ROOT="$WORKDIR/root"
DESTDIR="$ROOT" cmake --install "$BUILD_DIR"
mkdir -p "$ROOT/usr/share/rimes/data"
cp -a "$STAGE"/. "$ROOT/usr/share/rimes/data/"

# A distinct SONAME prevents an already-loaded fcitx5-rime from substituting
# the distro's older Lua bindings. No system Rime library is replaced.
PRIVATE_LIB="/usr/lib/$MULTIARCH/rimes"
PRIVATE_SONAME=librimes-rime.so.1
mkdir -p "$ROOT$PRIVATE_LIB"
cp -L "$RIME_PREFIX/lib/librime.so" "$ROOT$PRIVATE_LIB/$PRIVATE_SONAME"
OLD_SONAME=$(patchelf --print-soname "$ROOT$PRIVATE_LIB/$PRIVATE_SONAME")
[[ "$OLD_SONAME" == librime.so.* ]] || { echo 'Unexpected Rime SONAME' >&2; exit 1; }
patchelf --set-soname "$PRIVATE_SONAME" "$ROOT$PRIVATE_LIB/$PRIVATE_SONAME"
patchelf --set-rpath '$ORIGIN' "$ROOT$PRIVATE_LIB/$PRIVATE_SONAME"
ELF_FILES=()
while IFS= read -r -d '' file; do
    if readelf -h "$file" >/dev/null 2>&1; then
        if patchelf --print-needed "$file" | grep -Fxq "$OLD_SONAME"; then
            patchelf --replace-needed "$OLD_SONAME" "$PRIVATE_SONAME" "$file"
            patchelf --set-rpath "$PRIVATE_LIB" "$file"
        fi
        ELF_FILES+=("-e$file")
    fi
done < <(find "$ROOT/usr" -type f -print0)

mkdir -p "$ROOT/usr/share/doc/fcitx5-rimes" "$WORKDIR/debian"
cp "$IME_ROOT/README.md" "$ROOT/usr/share/doc/fcitx5-rimes/README.md"
for notice in LICENSE NOTICE LICENSING.md ATTRIBUTION.md THIRD_PARTY_NOTICES.md; do
    cp "$REPO_ROOT/$notice" "$ROOT/usr/share/doc/fcitx5-rimes/$notice"
done
cp "$REPO_ROOT/platforms/ios/dependencies.lock.json" "$ROOT/usr/share/doc/fcitx5-rimes/engine-dependencies.json"
cat > "$WORKDIR/debian/control" <<'EOF'
Source: fcitx5-rimes
Section: utils
Priority: optional
Maintainer: RIMES contributors <https://github.com/scholay/rimes>

Package: fcitx5-rimes
Architecture: any
Description: RIMES Fcitx5 input method
EOF
printf 'librimes-rime 1 fcitx5-rimes (= %s)\n' "$VERSION" > "$WORKDIR/debian/shlibs.local"
DEPS=$(cd "$WORKDIR" && dpkg-shlibdeps -O -xfcitx5-rimes -l"$ROOT$PRIVATE_LIB" "${ELF_FILES[@]}")
DEPS=${DEPS#shlibs:Depends=}
[[ -n "$DEPS" && "$DEPS" != *$'\n'* && "$DEPS" != *shlibs:* ]] || {
    echo 'error: could not determine runtime dependencies' >&2; exit 1;
}

mkdir -p "$ROOT/DEBIAN"
SIZE=$(du -sk "$ROOT" | awk '{print $1}')
cat > "$ROOT/DEBIAN/control" <<EOF
Package: fcitx5-rimes
Version: $VERSION
Section: utils
Priority: optional
Architecture: $ARCH
Installed-Size: $SIZE
Depends: fcitx5, $DEPS
Recommends: libgtk-layer-shell0, wl-clipboard
Maintainer: RIMES contributors <https://github.com/scholay/rimes>
Description: RIMES input method frontend for Fcitx 5
 A librime frontend owned by RIMES. It deploys the reviewed RIMES rime-data
 set, keeps user state in ~/.local/share/rimes, and ships the Linux Buffer
 workbench, Capsule rail and scheme importer next to the Fcitx5 addon.
 A private pinned Rime/Lua runtime supports portable third-party schemes.
EOF

mkdir -p "$OUT_DIR"
DEB="$OUT_DIR/fcitx5-rimes_${VERSION}_${ARCH}.deb"
dpkg-deb --build --root-owner-group "$ROOT" "$DEB"
(cd "$OUT_DIR" && sha256sum "$(basename "$DEB")" > SHA256SUMS)
echo "Wrote $DEB"
