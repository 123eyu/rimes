#!/bin/bash
# Build, cloud-sign, verify and upload the iOS app to App Store Connect from
# this Mac with an App Store Connect API key, without the browser or Xcode
# Organizer. Optionally submit the processed build for App Review.
#
#   ASC_KEY_ID=… ASC_ISSUER_ID=… ASC_KEY_PATH=/path/AuthKey_XXXX.p8 \
#     platforms/ios/scripts/upload-app-store.sh --build 68 [--submit]
#
# Options:
#   --build N       Build number (CFBundleVersion). Must be new for Apple.
#   --submit        After Apple processes the build, attach the release notes
#                   and submit it for App Review with automatic release
#                   (Fastlane lane submit_local_upload; needs Ruby >= 3.0).
#   --skip-tests    Skip the hosted simulator tests (build checks still run).
#   --check-only    Run every precondition check, then stop before building.
#
# The key file stays outside the repository; it is only passed by path to
# Apple's tools and never copied, printed or written into a receipt.
set -euo pipefail

ROOT="$(cd "$(dirname "$0")/../../.." && pwd)"
IOS="$ROOT/platforms/ios"
export DEVELOPER_DIR="${DEVELOPER_DIR:-/Applications/Xcode.app/Contents/Developer}"
TEAM=585J2TL9U6
BUNDLE=org.scholay.rimes.ios
APP_APPLE_ID=6814620242

BUILD=""
SUBMIT=0
SKIP_TESTS=0
CHECK_ONLY=0
while [ $# -gt 0 ]; do
  case "$1" in
    --build) BUILD="${2:-}"; shift 2 ;;
    --submit) SUBMIT=1; shift ;;
    --skip-tests) SKIP_TESTS=1; shift ;;
    --check-only) CHECK_ONLY=1; shift ;;
    -h|--help) sed -n '2,19p' "$0" | sed 's/^# \{0,1\}//'; exit 0 ;;
    *) echo "Unknown option: $1" >&2; exit 64 ;;
  esac
done

problems=()
fail() { problems+=("$1"); }
step() { printf '\n==> %s\n' "$1"; }

step "Checking preconditions"
cd "$ROOT"

# Source: a clean, committed state that is already on main, like ios-release.yml.
[ -z "$(git status --porcelain --untracked-files=no)" ] \
  || fail "Tracked files have uncommitted changes; release from a clean checkout"
if git fetch --quiet origin main 2>/dev/null; then
  git merge-base --is-ancestor HEAD origin/main \
    || fail "HEAD $(git rev-parse --short HEAD) is not on origin/main"
else
  fail "Could not fetch origin/main to confirm HEAD is a mainline commit"
fi
COMMIT="$(git rev-parse HEAD)"

# Version and three-language release notes, using the shared release rules.
VERSION="$(ruby -r "$IOS/scripts/release-config.rb" -ryaml -e '
  root = ARGV.fetch(0)
  v = YAML.safe_load(File.read(File.join(root, "project.yml"))).dig("settings", "base", "MARKETING_VERSION")
  RimesRelease.require_ios_target(v, root)
  RimesRelease.notes(root, v)
  puts v' "$IOS" 2>&1)" || { fail "Version or release notes: $VERSION"; VERSION=""; }

if [ -z "$BUILD" ]; then
  fail "Pass --build N (a build number Apple has not seen for this app)"
elif ! [[ "$BUILD" =~ ^(0|[1-9][0-9]{0,3})(\.(0|[1-9][0-9]{0,3})){0,2}$ ]]; then
  fail "Build number '$BUILD' is not in Apple's N[.N[.N]] format"
fi

# Credentials: only presence, shape and file permissions are checked here.
[[ "${ASC_KEY_ID:-}" =~ ^[A-Z0-9]{10}$ ]] || fail "Set ASC_KEY_ID to the 10-character App Store Connect key ID"
[[ "${ASC_ISSUER_ID:-}" =~ ^[0-9a-fA-F-]{36}$ ]] || fail "Set ASC_ISSUER_ID to the App Store Connect issuer ID"
if [ -z "${ASC_KEY_PATH:-}" ] || [ ! -f "${ASC_KEY_PATH:-}" ]; then
  fail "Set ASC_KEY_PATH to the downloaded AuthKey_<id>.p8 file"
else
  KEY_REAL="$(cd "$(dirname "$ASC_KEY_PATH")" && pwd -P)/$(basename "$ASC_KEY_PATH")"
  case "$KEY_REAL" in
    "$(cd "$ROOT" && pwd -P)"/*) fail "Keep the .p8 key outside the repository" ;;
  esac
  mode="$(stat -f %Lp "$KEY_REAL")"
  [ $(( 8#$mode & 077 )) -eq 0 ] || fail "Key file is readable by others (mode $mode); run chmod 600 on it"
fi

# Tools.
[ -d "$DEVELOPER_DIR" ] || fail "Xcode not found at DEVELOPER_DIR=$DEVELOPER_DIR"
xcrun --find altool >/dev/null 2>&1 || fail "altool not found in the selected Xcode"
if [ "$SUBMIT" -eq 1 ]; then
  ruby -e 'exit(Gem::Version.new(RUBY_VERSION) >= Gem::Version.new("3.0") ? 0 : 1)' \
    || fail "--submit needs Ruby >= 3.0 for Fastlane (for example: brew install ruby, then put it first on PATH)"
  command -v bundle >/dev/null 2>&1 || fail "--submit needs Bundler"
  ruby -r "$IOS/scripts/release-config.rb" -e 'RimesRelease.store_descriptions(ARGV.fetch(0)); RimesRelease.review_notes(ARGV.fetch(0))' "$IOS" \
    || fail "--submit needs valid localized descriptions and review notes"
fi

# Asset licensing and release prerequisites recorded for distribution.
python3 "$IOS/scripts/verify.py" --distribution >/dev/null 2>&1 \
  || fail "Distribution audit blocked: run python3 platforms/ios/scripts/verify.py --distribution"

if [ ${#problems[@]} -gt 0 ]; then
  printf 'Not ready to upload:\n' >&2
  printf '  - %s\n' "${problems[@]}" >&2
  exit 1
fi
echo "Ready: XSIME iOS $VERSION ($BUILD) from $(git rev-parse --short HEAD)"
[ "$CHECK_ONLY" -eq 1 ] && exit 0

OUT="$IOS/build/local-release/$VERSION-$BUILD"
if [ -e "$OUT" ]; then
  echo "Output $OUT already exists; choose a new build number or remove it" >&2
  exit 1
fi
mkdir -p "$OUT"
AUTH=(-allowProvisioningUpdates
      -authenticationKeyPath "$KEY_REAL"
      -authenticationKeyID "$ASC_KEY_ID"
      -authenticationKeyIssuerID "$ASC_ISSUER_ID")

step "Building and checking (platforms/ios/scripts/build.sh)"
"$IOS/scripts/build.sh"
if [ "$SKIP_TESTS" -eq 0 ]; then
  step "Running hosted simulator tests"
  "$IOS/scripts/test-simulator.sh"
fi

step "Archiving $VERSION ($BUILD)"
xcodebuild -project "$IOS/RIMES.xcodeproj" -scheme RIMES -configuration Release \
  -destination 'generic/platform=iOS' -archivePath "$OUT/RIMES.xcarchive" \
  "${AUTH[@]}" DEVELOPMENT_TEAM="$TEAM" CODE_SIGN_STYLE=Automatic \
  MARKETING_VERSION="$VERSION" CURRENT_PROJECT_VERSION="$BUILD" \
  clean archive

step "Exporting an App Store Connect IPA with cloud signing"
cat > "$OUT/ExportOptions.plist" <<PLIST
<?xml version="1.0" encoding="UTF-8"?>
<!DOCTYPE plist PUBLIC "-//Apple//DTD PLIST 1.0//EN" "http://www.apple.com/DTDs/PropertyList-1.0.dtd">
<plist version="1.0">
<dict>
  <key>method</key><string>app-store-connect</string>
  <key>destination</key><string>export</string>
  <key>signingStyle</key><string>automatic</string>
  <key>teamID</key><string>$TEAM</string>
  <key>manageAppVersionAndBuildNumber</key><false/>
  <key>uploadSymbols</key><true/>
</dict>
</plist>
PLIST
xcodebuild -exportArchive -archivePath "$OUT/RIMES.xcarchive" \
  -exportOptionsPlist "$OUT/ExportOptions.plist" -exportPath "$OUT/export" "${AUTH[@]}"
IPA="$OUT/export/RIMES.ipa"
[ -f "$IPA" ] || { echo "Export did not produce $IPA" >&2; exit 1; }

step "Verifying the IPA (signatures, profiles, version, shared groups)"
python3 "$IOS/scripts/verify-release-ipa.py" "$IPA" "$VERSION" "$BUILD"
IPA_SHA="$(shasum -a 256 "$IPA" | cut -d' ' -f1)"

ALTOOL_AUTH=(--api-key "$ASC_KEY_ID" --api-issuer "$ASC_ISSUER_ID" --p8-file-path "$KEY_REAL")
step "Validating with App Store Connect"
xcrun altool --validate-app "$IPA" "${ALTOOL_AUTH[@]}"

step "Uploading the verified IPA and waiting for Apple processing"
xcrun altool --upload-package "$IPA" "${ALTOOL_AUTH[@]}" --wait | tee "$OUT/upload.log"

write_receipt() {
  python3 - "$OUT/receipt.json" "$@" <<'PY'
import json, sys
path, *pairs = sys.argv[1:]
data = dict(zip(pairs[::2], pairs[1::2]))
json.dump(data, open(path, "w"), indent=2)
PY
}
write_receipt version "$VERSION" build "$BUILD" commit "$COMMIT" \
  bundleID "$BUNDLE" appAppleID "$APP_APPLE_ID" ipaSHA256 "$IPA_SHA" \
  upload "Succeeded" submission "$([ "$SUBMIT" -eq 1 ] && echo pending || echo "not requested")"

if [ "$SUBMIT" -eq 1 ]; then
  step "Submitting $VERSION ($BUILD) for App Review"
  (cd "$IOS" && bundle install --quiet && \
    FASTLANE_SKIP_UPDATE_CHECK=1 FASTLANE_HIDE_CHANGELOG=1 FASTLANE_OPT_OUT_USAGE=1 \
    ASC_KEY_PATH="$KEY_REAL" RIMES_IOS_VERSION="$VERSION" RIMES_IOS_BUILD="$BUILD" \
    RIMES_IOS_COMMIT="$COMMIT" RIMES_IOS_SUBMISSION_RECEIPT="$OUT/submission.json" \
    bundle exec fastlane ios submit_local_upload)
  write_receipt version "$VERSION" build "$BUILD" commit "$COMMIT" \
    bundleID "$BUNDLE" appAppleID "$APP_APPLE_ID" ipaSHA256 "$IPA_SHA" \
    upload "Succeeded" submission "see submission.json"
fi

step "Done"
echo "Receipt: $OUT/receipt.json"
[ "$SUBMIT" -eq 1 ] && echo "Submission: $OUT/submission.json"
echo "Upload, processing, App Review and store availability are separate states;"
echo "read them back in App Store Connect before recording the release."
