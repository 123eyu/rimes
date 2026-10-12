#!/usr/bin/env bash
set -euo pipefail

SCRIPT_DIR=$(CDPATH= cd -- "$(dirname -- "${BASH_SOURCE[0]}")" && pwd -P)
IME_ROOT=$(CDPATH= cd -- "$SCRIPT_DIR/.." && pwd -P)

BUILD_DIR="${RIMES_IME_BUILD_DIR:-$IME_ROOT/build}"
PREFIX="${RIMES_IME_PREFIX:-$HOME/.local}"
BUILD_TYPE="${RIMES_IME_BUILD_TYPE:-RelWithDebInfo}"

# An explicit prefix uses the same locked Lua API as the mobile engines.
if [[ -n "${RIMES_IME_RIME_PREFIX:-}" ]]; then
    export PKG_CONFIG_PATH="$RIMES_IME_RIME_PREFIX/lib/pkgconfig:${PKG_CONFIG_PATH:-}"
    export CMAKE_PREFIX_PATH="$RIMES_IME_RIME_PREFIX:${CMAKE_PREFIX_PATH:-}"
fi
mkdir -p "$BUILD_DIR"
cmake -S "$IME_ROOT" -B "$BUILD_DIR" \
    -DCMAKE_BUILD_TYPE="$BUILD_TYPE" \
    -DCMAKE_INSTALL_PREFIX="$PREFIX" \
    -DRIMES_BUILD_FCITX5=ON \
    -DRIMES_BUILD_TESTS=ON \
    -DCMAKE_INSTALL_RPATH_USE_LINK_PATH=ON
cmake --build "$BUILD_DIR" -j"$(nproc)"
echo "Built in $BUILD_DIR (prefix $PREFIX)"
