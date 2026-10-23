#!/bin/zsh
# Usage: run.sh <simulator-udid> <out-dir>. Captures system and RIMES keyboards in an Apple
# host (Reminders) and a third-party host (the RIMES app) through xcodebuild + XCUITest.
set -e
export DEVELOPER_DIR=/Applications/Xcode.app/Contents/Developer
U=${1:?udid}; OUT=${2:-/private/tmp/host-style}; HERE=${0:A:h}
mkdir -p $OUT
for tag in system rimes; do
  KB=("org.scholay.rimes.ios.keyboard" "en_US@sw=QWERTY;hw=Automatic")
  xcrun simctl spawn $U defaults write -g AppleKeyboards -array "${KB[@]}"
  # The active keyboard is cached by the running simulator; restart to apply the new list.
  xcrun simctl shutdown $U 2>/dev/null || true; xcrun simctl boot $U; xcrun simctl bootstatus $U -b >/dev/null
  xcrun simctl terminate $U com.apple.reminders 2>/dev/null || true
  xcrun simctl terminate $U org.scholay.rimes.ios 2>/dev/null || true
  TEST_RUNNER_HOST_STYLE_TAG=$tag TEST_RUNNER_HOST_STYLE_OUT=$OUT xcodebuild test -project $HERE/HostStyle.xcodeproj -scheme HostStyleUITests \
    -destination "id=$U" -derivedDataPath /private/tmp/hoststyle-dd -resultBundlePath $OUT/$tag.xcresult 2>&1 | grep -E "error:|Test Case.*(passed|failed)" || true
  rm -rf $OUT/$tag.xcresult.bak
done
