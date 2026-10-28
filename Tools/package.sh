#!/usr/bin/env bash
# Build the watch app and pack the community-app-store upload:
#   Release/SkiDay_<version>_store.zip = .uapp + app-manifest.json + icon.png
# Usage: UNA_SDK=/path/to/una-sdk Tools/package.sh
set -euo pipefail
ROOT=$(cd "$(dirname "$0")/.." && pwd)
: "${UNA_SDK:?Set UNA_SDK to the una-sdk checkout}"
CMAKE_DIR="$ROOT/Software/Apps/SkiDay-CMake"
BUILD="$ROOT/build/watch"

cmake -G "Unix Makefiles" -S "$CMAKE_DIR" -B "$BUILD"
cmake --build "$BUILD" -j"$(nproc 2>/dev/null || echo 4)"

UAPP=$(ls -t "$BUILD"/SkiDay_*.uapp | head -1)
NAME=$(basename "$UAPP")
VERSION=${NAME#SkiDay_}; VERSION=${VERSION%.uapp}; VERSION=${VERSION%%-*}
APP_ID=$(sed -n 's/^set(APP_ID "\([0-9A-F]*\)").*/\1/p' "$CMAKE_DIR/CMakeLists.txt")

STAGE=$(mktemp -d)
cp "$UAPP" "$STAGE/"
cp "$ROOT/Resources/icon.png" "$STAGE/icon.png"

# The flags below describe what the FIT file holds: a lap per run and per
# lift ride, distance, the GPS track, elevation and speed (no heart rate or
# steps).
cat > "$STAGE/app-manifest.json" <<JSON
{
  "manifest_version": 1,
  "type": ["activity"],
  "name": "Ski Day",
  "icon": "icon.png",
  "binary": "$NAME",
  "appVersion": "$VERSION",
  "minKernelVersion": "1.4.0",
  "requiredHardware": ["GPS"],
  "stravaExport": true,
  "id": "$APP_ID",
  "supportsLaps": true,
  "supportsDistance": true,
  "supportsTrack": true,
  "supportsHeartbeat": false,
  "supportsElevation": true,
  "supportsStep": false,
  "supportsSpeed": "speed",
  "customMeasures": []
}
JSON
python3 "$UNA_SDK/Utilities/Scripts/app_packer/min_kernel_version.py" --stamp "$STAGE/app-manifest.json"
# The store rejects any manifest key it does not know ("must NOT have
# additional properties"). Keep to the keys of the SDK's own sample manifests;
# the description and screenshots are entered on the app's page in the portal.
python3 - "$STAGE/app-manifest.json" <<'PY'
import json, sys
allowed = {"manifest_version", "type", "name", "icon", "binary", "appVersion", "minKernelVersion",
           "requiredHardware", "stravaExport", "id", "supportsLaps", "supportsDistance",
           "supportsTrack", "supportsHeartbeat", "supportsElevation", "supportsStep",
           "supportsSpeed", "customMeasures", "configFields", "configFile"}
extra = sorted(set(json.load(open(sys.argv[1]))) - allowed)
if extra:
    sys.exit(f"app-manifest.json has keys the store rejects: {extra}")
PY

mkdir -p "$ROOT/Release"
ZIP="$ROOT/Release/SkiDay_${VERSION}_store.zip"
rm -f "$ZIP"
(cd "$STAGE" && zip -qr "$ZIP" .)
cp "$UAPP" "$ROOT/Release/"
rm -rf "$STAGE"
echo "Store package: $ZIP"
unzip -l "$ZIP"
