#!/bin/zsh
# Builds OldHost for the simulator with an SDK version stamped into LC_BUILD_VERSION.
# Usage: build.sh <sdk-version> <out-dir>   (e.g. 18.0)
set -e
export DEVELOPER_DIR=/Applications/Xcode.app/Contents/Developer
SDKV=${1:-18.0}; OUT=${2:-/private/tmp/oldhost}; HERE=${0:A:h}
SDK=$(xcrun --sdk iphonesimulator --show-sdk-path)
APP=$OUT/OldHost-$SDKV.app; rm -rf $APP; mkdir -p $APP
xcrun swiftc -target arm64-apple-ios17.0-simulator -sdk $SDK -o $APP/OldHost $HERE/main.swift \
  -Xlinker -platform_version -Xlinker ios-simulator -Xlinker 17.0 -Xlinker $SDKV 2>&1 | grep -v warning || true
cp $HERE/Info.plist $APP/Info.plist
codesign -s - $APP
xcrun vtool -show-build $APP/OldHost | grep -E "platform|minos|sdk"
