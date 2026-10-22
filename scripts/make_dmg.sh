#!/bin/bash
# Usage: scripts/make_dmg.sh [version]   e.g. scripts/make_dmg.sh v0.1.0
set -euo pipefail
cd "$(dirname "$0")/.."
VERSION="${1:-dev}"
NAME="EngineV2-${VERSION}"
BUILD=build-release
STAGE="$BUILD/dmg"

# Reuse already-downloaded deps from build/_deps if present (avoids network)
DEPS=""
for d in glfw imgui glm; do
  [ -d "build/_deps/$d-src" ] && DEPS="$DEPS -DFETCHCONTENT_SOURCE_DIR_$(echo $d | tr a-z A-Z)=$PWD/build/_deps/$d-src"
done
cmake -S . -B "$BUILD" $DEPS -DCMAKE_BUILD_TYPE=Release -DENGINE_VERSION="${VERSION#v}" \
  -DCMAKE_OSX_ARCHITECTURES="$(uname -m)" -DENGINE_BUILD_APP_BUNDLE=ON
cmake --build "$BUILD" -j

rm -rf "$STAGE"; mkdir -p "$STAGE" dist
cp -R "$BUILD/EngineV2.app" "$STAGE/"
codesign --force --deep -s - "$STAGE/EngineV2.app"   # ad-hoc sign
ln -s /Applications "$STAGE/Applications"

rm -f "dist/$NAME.dmg"
hdiutil create -volname "$NAME" -srcfolder "$STAGE" -ov -format UDZO "dist/$NAME.dmg"
echo "Created dist/$NAME.dmg"
