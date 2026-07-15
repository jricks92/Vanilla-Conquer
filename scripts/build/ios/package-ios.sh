#!/bin/bash
# Package an unsigned iOS .ipa for LiveContainer from the CMake ios-preset build.
# Usage: package-ios.sh <td|ra> [--config RelWithDebInfo] [--data <dir>]
#   --data: bundle game data (MIX files + subfolders) into the app next to the
#           binary; the engine's portable-mode detection picks it up there.
set -euo pipefail

GAME="${1:-}"
shift || true
CONFIG="RelWithDebInfo"
DATA_DIR=""
while [ $# -gt 0 ]; do
    case "$1" in
    --config) CONFIG="$2"; shift 2 ;;
    --data) DATA_DIR="$2"; shift 2 ;;
    *) echo "unknown option: $1" >&2; exit 1 ;;
    esac
done

case "$GAME" in
td)
    EXECUTABLE="vanillatd"
    APP_NAME="VanillaTD"
    DISPLAY_NAME="C&C: Tiberian Dawn"
    BUNDLE_ID="com.vanilla-conquer.vanillatd"
    ;;
ra)
    EXECUTABLE="vanillara"
    APP_NAME="VanillaRA"
    DISPLAY_NAME="C&C: Red Alert"
    BUNDLE_ID="com.vanilla-conquer.vanillara"
    ;;
*)
    echo "Usage: $0 <td|ra> [--config <cfg>]" >&2
    exit 1
    ;;
esac

ROOT="$(cd "$(dirname "$0")/../../.." && pwd)"
BIN="$ROOT/build/ios/$CONFIG/$EXECUTABLE.app/$EXECUTABLE"
DIST="$ROOT/dist/ios"
STAGE="$DIST/stage-$EXECUTABLE"
APP="$STAGE/Payload/$APP_NAME.app"
IPA="$DIST/$EXECUTABLE.ipa"

if [ ! -f "$BIN" ]; then
    echo "error: $BIN not found — build with: cmake --build --preset ios" >&2
    exit 1
fi

# Verify the binary really is an iOS arm64 binary before packaging.
if ! otool -l "$BIN" | grep -A3 LC_BUILD_VERSION | grep -q 'platform 2'; then
    echo "error: $BIN is not built for iOS (LC_BUILD_VERSION platform != 2)" >&2
    exit 1
fi

rm -rf "$STAGE" "$IPA"
mkdir -p "$APP"

cp "$BIN" "$APP/$EXECUTABLE"

# Fill the template placeholders, then set the display name via plutil so
# characters like '&' don't need shell/sed escaping.
sed -e "s/__EXECUTABLE__/$EXECUTABLE/g" \
    -e "s/__BUNDLE_ID__/$BUNDLE_ID/g" \
    "$ROOT/ios/Info.plist.in" > "$APP/Info.plist"
plutil -replace CFBundleDisplayName -string "$DISPLAY_NAME" "$APP/Info.plist"
plutil -replace CFBundleName -string "$DISPLAY_NAME" "$APP/Info.plist"
plutil -lint "$APP/Info.plist" > /dev/null

# Generate and copy app icons (no alpha). Regenerated each package so an SVG
# change is always reflected.
"$(dirname "$0")/make-icons.sh" "$GAME" > /dev/null
if [ -d "$ROOT/dist/ios/icons-$EXECUTABLE" ]; then
    cp "$ROOT/dist/ios/icons-$EXECUTABLE"/*.png "$APP/" 2>/dev/null || true
fi

# Bundle game data next to the binary (MIX files + one level of subfolders
# like gdi/). No INI files: an INI beside the binary would flip the engine's
# user-data path to the read-only bundle.
if [ -n "$DATA_DIR" ]; then
    if [ ! -d "$DATA_DIR" ]; then
        echo "error: data dir '$DATA_DIR' not found" >&2
        exit 1
    fi
    rsync -a --include='*/' --include='*.mix' --include='*.MIX' --exclude='*' \
        "$DATA_DIR"/ "$APP"/
    echo "Bundled game data from $DATA_DIR:"
    du -sh "$APP" | cut -f1
fi

(cd "$STAGE" && zip -qry "$IPA" Payload)
rm -rf "$STAGE"

echo "Packaged $IPA"
echo
echo "Install into LiveContainer with:"
echo "  afcclient --container com.kdt.livecontainer.B5L3CPH855 put $IPA /Documents/$EXECUTABLE.ipa"
