#!/usr/bin/env bash
# This test installs/removes a system package. Run only on a disposable Linux runner.
set -euo pipefail
[[ "${RIMES_DEB_TEST_DISPOSABLE:-}" == 1 ]] || {
    echo "error: requires a disposable runner and RIMES_DEB_TEST_DISPOSABLE=1" >&2
    exit 1
}
[[ $# -ge 1 && $# -le 2 && -f "$1" ]] || {
    echo "Usage: deb_lifecycle.sh /path/to/package.deb [/path/to/older.deb]" >&2; exit 2;
}
DEB=$(realpath "$1")
if dpkg-query -W -f='${db:Status-Status}' fcitx5-rimes 2>/dev/null | grep -qx installed; then
    echo "error: refusing to replace an existing fcitx5-rimes installation" >&2
    exit 1
fi
[[ "$(dpkg-deb -f "$DEB" Package)" == fcitx5-rimes ]]
VERSION=$(dpkg-deb -f "$DEB" Version)
ARCH=$(dpkg-deb -f "$DEB" Architecture)
(cd "$(dirname "$DEB")" && sha256sum -c "$(basename "$DEB").sha256")
WORK=$(mktemp -d)
trap 'rm -rf "$WORK"' EXIT
FIXTURE_UPGRADE=0
RETIRED_FILE=/usr/share/rimes/lifecycle-retired-fixture
if [[ $# == 2 ]]; then
    OLD_DEB=$(realpath "$2")
    (cd "$(dirname "$OLD_DEB")" && sha256sum -c "$(basename "$OLD_DEB").sha256")
else
    # The first release has no historical deb. Exercise a real dpkg upgrade
    # using a clearly identified older-version payload, not a reinstall.
    # This covers package replacement, not historical application migrations.
    FIXTURE_UPGRADE=1
    OLD_DEB="$WORK/fcitx5-rimes-upgrade-fixture.deb"
    dpkg-deb -R "$DEB" "$WORK/old-root"
    [[ ! -e "$WORK/old-root$RETIRED_FILE" && ! -e "$RETIRED_FILE" ]]
    python3 - "$WORK/old-root/DEBIAN/control" "$VERSION~lifecycle.1" <<'PY'
import re, sys
from pathlib import Path
path = Path(sys.argv[1])
text, count = re.subn(r'^Version: .*$', 'Version: ' + sys.argv[2], path.read_text(), flags=re.M)
assert count == 1
path.write_text(text)
PY
    printf '%s\n' 'obsolete managed payload; must disappear on upgrade' > "$WORK/old-root$RETIRED_FILE"
    dpkg-deb --root-owner-group -b "$WORK/old-root" "$OLD_DEB"
    echo "Upgrade coverage: controlled old-version payload fixture; no historical application migration claim."
fi
[[ "$(dpkg-deb -f "$OLD_DEB" Package)" == fcitx5-rimes ]]
[[ "$(dpkg-deb -f "$OLD_DEB" Architecture)" == "$ARCH" ]]
OLD_VERSION=$(dpkg-deb -f "$OLD_DEB" Version)
dpkg --compare-versions "$OLD_VERSION" lt "$VERSION"
echo "Upgrade path: $OLD_VERSION -> $VERSION ($ARCH)"
sha256sum "$DEB" "$OLD_DEB"
DATA="$WORK/user-data/rimes"
CLI_HOME="$WORK/home"
mkdir -p "$CLI_HOME/.claude"
printf '%s\n' 'keep CLI-owned Claude authentication' > "$CLI_HOME/.claude/.credentials.json"
printf '%s\n' 'keep CLI-owned Claude preferences' > "$CLI_HOME/.claude.json"
mkdir -p "$DATA/rime_ice.userdb" "$DATA/schemes/learning/custom.userdb" "$DATA/capsule/clipboard" "$DATA/capsule/passwords" "$DATA/capsule/assets" "$DATA/capsule/captures/fixture" "$DATA/translation/cache" "$DATA/mailbox/images" "$DATA/gateway" "$DATA/ai/codex-home/generated_images" "$DATA/mailbox/generations/fixture" "$DATA/configuration/credentials"
printf '%s\n' 'keep learned dictionary' > "$DATA/rime_ice.userdb/CURRENT"
printf '%s\n' 'keep imported learning' > "$DATA/schemes/learning/custom.userdb/CURRENT"
printf '%s\n' 'keep notes' > "$DATA/capsule/note.md"
printf '%s\n' 'keep original MIME history and favorites' > "$DATA/capsule/clipboard/history-v1.sqlite3"
printf '%s\n' 'keep downloaded model' > "$DATA/translation/cache/model.argosmodel"
printf '%s\n' 'keep imported reference attachment' > "$DATA/capsule/assets/reference.asset"
printf '%s\n' 'apa7' > "$DATA/capsule/reference-style-v1"
printf '%s\n' 'keep capture metadata' > "$DATA/capsule/captures/fixture/record.json"
printf '%s\n' 'keep original screen recording' > "$DATA/capsule/captures/fixture/original.webm"
printf '%s\n' 'keep existing encryption master key' > "$DATA/capsule/master-key"
printf '%s\n' 'keep salted reveal credential' > "$DATA/capsule/reveal-credential-v1"
printf '%s\n' 'keep encrypted password record' > "$DATA/capsule/passwords/fixture.md"
printf '%s\n' 'keep Provider identities and models' > "$DATA/configuration/preferences.json"
printf '%s\n' 'keep separate Provider credential' > "$DATA/configuration/credentials/provider.fixture"
printf '%s\n' 'keep Mailbox threads, terminal session UUID receipts and messages' > "$DATA/mailbox/mailbox.json"
printf '%s\n' 'keep Mailbox original attachment' > "$DATA/mailbox/images/fixture.png"
printf '%s\n' 'keep independent Capsule receipt image' > "$DATA/capsule/assets/mailbox-copy.png"
printf '%s\n' 'keep existing private gateway token' > "$DATA/gateway/token"
printf '%s\n' 'keep Mailbox window preferences' > "$DATA/mailbox/window.json"
printf '%s\n' 'keep private ChatGPT credential' > "$DATA/ai/codex-home/auth.json"
printf '%s\n' 'keep original generated image' > "$DATA/ai/codex-home/generated_images/fixture.png"
printf '%s\n' 'keep generation ownership metadata' > "$DATA/mailbox/generations/fixture/.lock"
mkdir -p "$DATA/chord-keymaps" "$DATA/learning"
printf '%s\n' 'keep saved keymap draft' > "$DATA/chord-keymaps/00000000-0000-4000-8000-000000000001.json"
printf '%s\n' 'keep separately applied keymap' > "$DATA/chord-keymaps/active-profile.json"
printf '%s\n' 'keep ordinary input scheme selection' > "$DATA/chord-keymaps/state.json"
printf '%s\n' 'keep chord learning counts and mastery' > "$DATA/learning/my_combo_progress.json"
mkdir -p "$DATA/stats"
printf '%s\n' 'keep independent physical key history' > "$DATA/stats/key_frequency.json"
printf '%s\n' 'keep daily character totals, active seconds and sessions' > "$DATA/stats/typing_speed.json"
printf '%s\n' 'keep article practice aggregate scores and comparison identities' > "$DATA/stats/typing_tests.json"
printf '%s\n' 'keep explicit corrupt statistics backup' > "$DATA/stats/corrupt-statistics-backup.json"
find "$DATA" "$CLI_HOME" -type d -exec chmod 700 {} +
find "$DATA" "$CLI_HOME" -type f -exec chmod 600 {} +
# A user-chosen non-secret permission must also remain unchanged.
chmod 640 "$DATA/capsule/note.md"
if [[ $(id -u) == 0 ]]; then
    # Root-only disposable containers should still prove that package scripts
    # preserve data owned by an unprivileged account.
    chown -R "$(id -u nobody):$(id -g nobody)" "$DATA" "$CLI_HOME"
fi
snapshot_user_data() {
    python3 - "$DATA" "$CLI_HOME" <<'PY'
import hashlib, json, stat, sys
from pathlib import Path
rows = []
for index, name in enumerate(sys.argv[1:]):
    root = Path(name)
    for path in [root, *sorted(root.rglob('*'))]:
        info = path.lstat()
        assert stat.S_ISDIR(info.st_mode) or stat.S_ISREG(info.st_mode), path
        rows.append([index, str(path.relative_to(root)), stat.S_IFMT(info.st_mode),
                     stat.S_IMODE(info.st_mode), info.st_uid, info.st_gid,
                     hashlib.sha256(path.read_bytes()).hexdigest() if path.is_file() else None])
print(json.dumps(rows, ensure_ascii=False, indent=2))
PY
}
snapshot_user_data > "$WORK/user-before.json"
USER_FILE_COUNT=$(python3 -c 'import json,sys; print(sum(row[-1] is not None for row in json.load(open(sys.argv[1]))))' "$WORK/user-before.json")
check_user_data() {
    snapshot_user_data > "$WORK/user-after.json"
    diff -u "$WORK/user-before.json" "$WORK/user-after.json"
    echo "ok: $1 preserved $USER_FILE_COUNT user/CLI files, directory layout, ownership and permissions"
}
install_deb() {
    sudo env HOME="$CLI_HOME" XDG_DATA_HOME="$WORK/user-data" apt-get install -y "$@"
}
remove_deb() {
    sudo env HOME="$CLI_HOME" XDG_DATA_HOME="$WORK/user-data" apt-get remove -y fcitx5-rimes
    for file in "${FILES[@]}"; do
        [[ ! -f "$file" && ! -L "$file" ]] || { echo "Package file survived removal: $file" >&2; exit 1; }
    done
}

verify_installed() {
local WORK="$WORK/runtime-$1"
mkdir -p "$WORK"
[[ "$(dpkg-query -W -f='${Version}' fcitx5-rimes)" == "$VERSION" ]]
[[ -x /usr/bin/rimes-schemes && -x /usr/bin/rimes-buffer && -x /usr/bin/rimes-capsule ]]
[[ -s /usr/share/licenses/rimes/plugins/Swift-Apache-2.0.txt ]]
[[ -x /usr/bin/rimes-settings && -x /usr/bin/rimes-plugins && -x /usr/bin/rimes-mailbox ]]
[[ -x /usr/libexec/rimes/rimes-mailbox-file ]]
printf '%s\n' 'public terminal receipt' > "$WORK/terminal.txt"
/usr/libexec/rimes/rimes-mailbox-file "$WORK/terminal.txt" | python3 -c 'import json,sys; v=json.load(sys.stdin); assert v["body"] == "public terminal receipt\n" and v["png"] == ""'
[[ -x /usr/bin/rimes-gateway && -f /usr/share/rimes/skills/imagegen/SKILL.md ]]
RIMES_USER_DIR="$WORK/gateway-status" /usr/bin/rimes-gateway --status | python3 -c 'import json,sys; assert json.load(sys.stdin)["state"] == "not-started"'
[[ -f /usr/share/applications/org.scholay.rimes.mailbox.desktop ]]
/usr/bin/rimes-mailbox --version
[[ -x /usr/bin/rimes-translation && -f /usr/libexec/rimes/translation/resources.lock.json ]]
RIMES_USER_DIR="$WORK/resource-status" /usr/bin/rimes-translation --status | python3 -c 'import json,sys; assert json.load(sys.stdin)["state"] == "missing"'
[[ -f /usr/share/applications/org.scholay.rimes.settings.desktop ]]
/usr/bin/rimes-settings --help
RIMES_USER_DIR="$WORK/chord-settings" NO_AT_BRIDGE=1 G_DEBUG=fatal-warnings xvfb-run -a \
    /usr/bin/rimes-settings --render "$WORK/settings-render"
python3 - "$WORK/settings-render/manifest.json" <<'PY'
import json, sys, hashlib
from pathlib import Path
pages = json.load(open(sys.argv[1]))
assert {row['subpageID'] for row in pages if row['routeID'] == 'extension.fly-chord-learning'} == {'settings', 'keymap', 'lessons', 'practice', 'progress'}
assert {row['subpageID'] for row in pages if row['routeID'] == 'extension.typing-speed'} == {'overview', 'history'}
assert any(row['routeID'] == 'extension.statistics' and row['subpageID'] == 'test-results' for row in pages)
assert {row['subpageID'] for row in pages if row['routeID'] == 'extension.statistics'} == {'daily', 'history', 'test-results'}
data = Path('/usr/share/rimes/data')
lock = json.loads((data / 'linux-native-chord-manifest.json').read_text())
assert lock['schemas'] == ['yoyo-yx', 'yoyo-yx-hm'] and len(lock['files']) == 12
for name, digest in lock['files'].items():
    assert hashlib.sha256((data / name).read_bytes()).hexdigest() == digest, name
PY
[[ -f /usr/share/rimes/data/default.yaml ]]
for program in /usr/bin/rimes-gateway /usr/bin/rimes-mailbox /usr/bin/rimes-schemes /usr/bin/rimes-buffer /usr/bin/rimes-capsule /usr/bin/rimes-settings /usr/bin/rimes-plugins /usr/bin/rimes-translation /usr/libexec/rimes/rimes-capsule-preview /usr/libexec/rimes/rimes-capsule-capture /usr/libexec/rimes/rimes-mailbox-file; do
    ldd "$program" > "$WORK/ldd.txt"
    if grep -q 'not found' "$WORK/ldd.txt"; then
        cat "$WORK/ldd.txt" >&2
        exit 1
    fi
done
# Exercise the installed ELF and its resolved shared libraries, not only a build-tree binary.
SCRIPT_DIR=$(CDPATH= cd -- "$(dirname -- "${BASH_SOURCE[0]}")" && pwd -P)
# The headless renderer has no accessibility bus; real desktop accessibility is
# covered separately. Retain fatal GTK/VTE warnings for the installed-binary check.
NO_AT_BRIDGE=1 G_DEBUG=fatal-warnings TZ=Asia/Tokyo xvfb-run -a /usr/bin/rimes-mailbox \
    --render-terminal "$WORK/terminal-render"
python3 - "$WORK/terminal-render/renders.json" <<'PY'
import json, sys
rows = json.load(open(sys.argv[1]))
assert len(rows) == 20
assert all(row['logicalWidth'] == (680 if '-compact' in row['image'] else 1000) for row in rows)
assert all(row['logicalHeight'] == (500 if '-compact' in row['image'] else 700) for row in rows)
PY
dbus-run-session -- xvfb-run -a python3 "$SCRIPT_DIR/../../capsule/tests/password_ui_test.py" \
    python3 "$SCRIPT_DIR/../../mailbox/tests/mailbox_process_test.py" /usr/bin/rimes-mailbox \
    "$SCRIPT_DIR/../../mailbox/tests/fixtures/native-mailbox.json"
}

install_deb "$DEB"
mapfile -t FILES < <(dpkg-query -L fcitx5-rimes)
check_user_data "fresh install"
verify_installed fresh
remove_deb
check_user_data "fresh removal"

install_deb "$OLD_DEB"
[[ "$(dpkg-query -W -f='${Version}' fcitx5-rimes)" == "$OLD_VERSION" ]]
if (( FIXTURE_UPGRADE )); then [[ -f "$RETIRED_FILE" ]]; fi
check_user_data "older-version install"
install_deb "$DEB"
[[ "$(dpkg-query -W -f='${Version}' fcitx5-rimes)" == "$VERSION" ]]
if (( FIXTURE_UPGRADE )); then [[ ! -e "$RETIRED_FILE" ]]; fi
check_user_data "version upgrade"
verify_installed upgraded
install_deb --reinstall "$DEB"
check_user_data "reinstall"
remove_deb
check_user_data "final removal"
echo "deb fresh install/upgrade/reinstall/remove passed; user dictionaries, learning, records and permissions preserved."
