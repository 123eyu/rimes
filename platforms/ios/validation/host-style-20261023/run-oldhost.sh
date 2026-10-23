#!/bin/zsh
# Usage: run-oldhost.sh <udid> <sdk-version> <out-dir>; captures system and RIMES keyboards in OldHost.
set -e
export DEVELOPER_DIR=/Applications/Xcode.app/Contents/Developer
U=${1:?udid}; SDKV=${2:?sdk}; OUT=${3:-/private/tmp/host-style}; HERE=${0:A:h}
mkdir -p $OUT
$HERE/OldHost/build.sh $SDKV /private/tmp/oldhost >/dev/null
xcrun simctl uninstall $U org.scholay.rimes.oldhost 2>/dev/null || true
xcrun simctl install $U /private/tmp/oldhost/OldHost-$SDKV.app
for tag in system rimes; do
  TEST_RUNNER_HOST_STYLE_TAG=$tag TEST_RUNNER_HOST_STYLE_SDK=$SDKV TEST_RUNNER_HOST_STYLE_OUT=$OUT \
    xcodebuild test -project $HERE/HostStyle.xcodeproj -scheme HostStyleUITests -only-testing HostStyleUITests/HostStyleTests/testOldHost \
    -destination "id=$U" -derivedDataPath /private/tmp/hoststyle-dd 2>&1 | grep -E "error:|Test Case.*(passed|failed)" || true
done
