#!/usr/bin/env bash
# Publish tested Android/Windows artifacts to an existing approved stable release.
# Never build, sign, create a release, or replace an existing asset.
set -euo pipefail
: "${RUNNER_TEMP:?}" "${VERSION:?}" "${RELEASE_COMMIT:?}" "${RELEASE_TAG:?}" "${GH_REPO:?}"
[[ "$GH_REPO" == scholay/xsime && "$RELEASE_TAG" == "v$VERSION" ]]
[[ "$RELEASE_COMMIT" =~ ^[0-9a-f]{40}$ ]]
[[ "$(gh api "repos/$GH_REPO/commits/$RELEASE_TAG" --jq .sha)" == "$RELEASE_COMMIT" ]]
[[ "$(gh release view "$RELEASE_TAG" --json isDraft --jq .isDraft)" == false ]]
[[ "$(gh release view "$RELEASE_TAG" --json isPrerelease --jq .isPrerelease)" == false ]]
[[ "$(gh api "repos/$GH_REPO/releases/latest" --jq .tag_name)" == "$RELEASE_TAG" ]]

python3 - <<'PY'
import json, os, shutil
from pathlib import Path
root = Path(os.environ['RUNNER_TEMP'])
out = root/'application-release'; out.mkdir()
version, commit = os.environ['VERSION'], os.environ['RELEASE_COMMIT']
windows = root/'windows-release'; android = root/'android-release'
for path in [windows/'BUILD-INFO.json', android/f'RIMES-Android-{version}-BUILD-INFO.json']:
    info = json.loads(path.read_text(encoding='utf-8-sig'))
    assert info['sourceCommit'] == commit and info['version'] == version, path.name
for name in [f'RIMES-Windows-{version}-Setup.exe', f'RIMES-Windows-{version}.zip']:
    shutil.copyfile(windows/name, out/name)
for name in ['BUILD-INFO.json', 'SOURCE-FILES.json', 'SETUP-VERIFICATION.json', 'SHA256SUMS']:
    shutil.copyfile(windows/name, out/f'RIMES-Windows-{version}-{name}')
for path in android.iterdir():
    assert path.is_file() and path.name.startswith(f'RIMES-Android-{version}')
    shutil.copyfile(path, out/path.name)
PY

assets="$RUNNER_TEMP/application-release"
readback="$RUNNER_TEMP/application-readback"
mkdir "$readback"
for file in "$assets"/*; do
  name="$(basename "$file")"
  if ! gh release view "$RELEASE_TAG" --json assets --jq '.assets[].name' | grep -Fxq "$name"; then
    gh release upload "$RELEASE_TAG" "$file"
  fi
  gh release download "$RELEASE_TAG" --pattern "$name" --dir "$readback"
  cmp "$file" "$readback/$name"
done
gh release download "$RELEASE_TAG" --pattern "RIMES-$VERSION.pkg" --pattern SHA256SUMS \
  --pattern "fcitx5-rimes_${VERSION}_amd64.deb" --pattern "fcitx5-rimes_${VERSION}_amd64.deb.sha256" --dir "$readback"
(cd "$readback" && sha256sum -c SHA256SUMS && sha256sum -c "fcitx5-rimes_${VERSION}_amd64.deb.sha256")
code=$(python3 -c 'import json,os; from pathlib import Path; print(json.loads((Path(os.environ["RUNNER_TEMP"])/"android-release"/("RIMES-Android-"+os.environ["VERSION"]+"-BUILD-INFO.json")).read_text())["versionCode"])')
python3 scripts/release/update_manifest.py "$readback" --version "$VERSION" --commit "$RELEASE_COMMIT" --android-build "$code"
# This is the final activation step. No partial platform set is advertised.
[[ "$(gh api "repos/$GH_REPO/releases/latest" --jq .tag_name)" == "$RELEASE_TAG" ]]
if gh release view "$RELEASE_TAG" --json assets --jq '.assets[].name' | grep -Fxq update-manifest.json; then
  mkdir "$RUNNER_TEMP/feed-readback"
  gh release download "$RELEASE_TAG" --pattern update-manifest.json --dir "$RUNNER_TEMP/feed-readback"
  cmp "$readback/update-manifest.json" "$RUNNER_TEMP/feed-readback/update-manifest.json"
else
  gh release upload "$RELEASE_TAG" "$readback/update-manifest.json"
  mkdir "$RUNNER_TEMP/feed-readback"
  gh release download "$RELEASE_TAG" --pattern update-manifest.json --dir "$RUNNER_TEMP/feed-readback"
  cmp "$readback/update-manifest.json" "$RUNNER_TEMP/feed-readback/update-manifest.json"
fi
