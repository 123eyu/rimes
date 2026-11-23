#!/usr/bin/env bash
# Stock fcitx5-rime must load in the same Fcitx5 process as the installed
# XSIME addon. With a shared glog its InitGoogleLogging aborted Fcitx5.
# This test installs and removes a distro package: disposable runners only.
set -euo pipefail

if [[ "${1:-}" == --session ]]; then
    # Runs inside a private D-Bus session and X display.
    WORK=$2
    LOG="$WORK/fcitx5.log"
    export HOME="$WORK/home" XDG_RUNTIME_DIR="$WORK/runtime" XDG_CONFIG_HOME="$WORK/config"
    export XDG_DATA_HOME="$WORK/data" XDG_CACHE_HOME="$WORK/cache"
    mkdir -p "$HOME" "$XDG_RUNTIME_DIR" "$XDG_CONFIG_HOME/fcitx5" "$XDG_DATA_HOME" "$XDG_CACHE_HOME"
    chmod 700 "$XDG_RUNTIME_DIR"
    cat > "$XDG_CONFIG_HOME/fcitx5/profile" <<'EOF'
[Groups/0]
Name=Default
Default Layout=us
DefaultIM=rimes

[Groups/0/Items/0]
Name=rimes

[Groups/0/Items/1]
Name=rime

[GroupOrder]
0=Default
EOF
    fcitx5 --disable=wayland,kimpanel,notificationitem > "$LOG" 2>&1 &
    FCITX_PID=$!
    # Wait for Fcitx5 to exit: the XSIME engine still writes its user data
    # under $XDG_DATA_HOME/rimes while it shuts down, and the caller deletes
    # $WORK as soon as this session returns.
    stop_fcitx() {
        kill "$FCITX_PID" 2>/dev/null || return 0
        local _
        for _ in $(seq 50); do
            kill -0 "$FCITX_PID" 2>/dev/null || break
            sleep 0.1
        done
        kill -KILL "$FCITX_PID" 2>/dev/null || true
        wait "$FCITX_PID" 2>/dev/null || true
    }
    trap stop_fcitx EXIT
    fail() {
        echo "error: $1" >&2
        echo "---- fcitx5.log ----" >&2
        tail -n 60 "$LOG" >&2 || true
        exit 1
    }
    # Wait for a log line while Fcitx5 is alive; a crash ends the wait at once.
    wait_for() {
        local pattern=$1 seconds=$2 tick
        for ((tick = 0; tick < seconds * 4; tick++)); do
            grep -Eq -- "$pattern" "$LOG" && return 0
            kill -0 "$FCITX_PID" 2>/dev/null || return 1
            sleep 0.25
        done
        return 1
    }
    controller() {
        gdbus call --session --dest org.fcitx.Fcitx5 --object-path /controller \
            --method "org.fcitx.Fcitx.Controller1.$1" "${@:2}"
    }
    wait_for 'Loaded addon rimes$' 60 || fail 'the XSIME addon did not load'
    # XSIME sets up its own Rime runtime, and with it glog, once deployment ends.
    wait_for 'librime deploy finished' 240 || fail 'XSIME deployment did not finish'
    sleep 1
    # Stock fcitx5-rime loads on demand. Reading its configuration loads it,
    # and its constructor sets up the distro Rime runtime, glog included.
    controller GetConfig "'fcitx://config/addon/rime'" > /dev/null 2>&1 || true
    controller GetConfig "'fcitx://config/inputmethod/rime'" > /dev/null 2>&1 || true
    if ! wait_for 'Loaded addon rime$' 60; then
        kill -0 "$FCITX_PID" 2>/dev/null \
            && fail 'Fcitx5 never loaded stock fcitx5-rime, so coexistence was not exercised'
        fail 'Fcitx5 died while loading stock fcitx5-rime next to XSIME'
    fi
    sleep 2
    kill -0 "$FCITX_PID" 2>/dev/null || fail 'Fcitx5 exited after loading stock fcitx5-rime'
    if grep -Eq 'InitGoogleLogging\(\) twice|was defined more than once|something wrong with flag' "$LOG"; then
        fail 'the two engines share one logging runtime'
    fi
    controller CurrentInputMethod > /dev/null || fail 'Fcitx5 stopped answering after loading both engines'
    echo "ok: stock fcitx5-rime and XSIME loaded in one Fcitx5 process"
    exit 0
fi

[[ "${RIMES_DEB_TEST_DISPOSABLE:-}" == 1 ]] || {
    echo "error: requires a disposable runner and RIMES_DEB_TEST_DISPOSABLE=1" >&2
    exit 1
}
dpkg-query -W -f='${db:Status-Status}' fcitx5-rimes 2>/dev/null | grep -qx installed || {
    echo "error: install the fcitx5-rimes deb first" >&2
    exit 1
}

# 1. No packaged binary may bind the distro's logging runtime by SONAME: the
#    loader would then share one glog with whatever else is in the process.
checked=0
while IFS= read -r file; do
    [[ -f "$file" && ! -L "$file" ]] || continue
    dynamic=$(readelf -d "$file" 2>/dev/null) || continue
    checked=$((checked + 1))
    if grep -E '\(NEEDED\).*\[lib(glog|gflags)\.so\.' <<< "$dynamic"; then
        echo "error: $file binds the distro logging runtime by SONAME" >&2
        exit 1
    fi
done < <(dpkg-query -L fcitx5-rimes)
(( checked > 0 ))
MULTIARCH=$(dpkg-architecture -qDEB_HOST_MULTIARCH)
shopt -s nullglob
private_glog=(/usr/lib/"$MULTIARCH"/rimes/librimes-glog.so.*)
shopt -u nullglob
[[ ${#private_glog[@]} == 1 ]] || { echo "error: expected one private glog" >&2; exit 1; }
readelf -d "${private_glog[0]}" | grep -Eq '\(SONAME\).*\[librimes-glog\.so\.'
echo "ok: $checked packaged ELF files use only the private logging runtime"

# 2. Load both engines into one real Fcitx5 process.
STOCK_WAS_INSTALLED=0
if dpkg-query -W -f='${db:Status-Status}' fcitx5-rime 2>/dev/null | grep -qx installed; then
    STOCK_WAS_INSTALLED=1
fi
WORK=$(mktemp -d)
cleanup() {
    # Under `set -e` a failed rm in this trap would turn a passing run into
    # exit 1, so retry once and only warn.
    rm -rf "$WORK" 2>/dev/null || { sleep 1; rm -rf "$WORK"; } \
        || echo "warning: could not remove $WORK" >&2
    if (( ! STOCK_WAS_INSTALLED )); then
        sudo apt-get remove -y fcitx5-rime > /dev/null || true
    fi
}
trap cleanup EXIT
sudo apt-get install -y fcitx5-rime
dbus-run-session -- xvfb-run -a "$(realpath "${BASH_SOURCE[0]}")" --session "$WORK"
