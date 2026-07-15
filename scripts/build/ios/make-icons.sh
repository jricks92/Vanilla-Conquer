#!/bin/bash
# Generate iOS app icon PNGs (no alpha) from an SVG source.
# Usage: make-icons.sh <td|ra>
set -euo pipefail

GAME="${1:-}"
ROOT="$(cd "$(dirname "$0")/../../.." && pwd)"

case "$GAME" in
td)
    SVG="$ROOT/resources/vanillatd_icon.svg"
    EXECUTABLE="vanillatd"
    ;;
ra)
    SVG="$ROOT/resources/vanillara_icon.svg"
    EXECUTABLE="vanillara"
    ;;
*)
    echo "Usage: $0 <td|ra>" >&2
    exit 1
    ;;
esac

OUT="$ROOT/dist/ios/icons-$EXECUTABLE"
rm -rf "$OUT"
mkdir -p "$OUT"

# Master render at 1024, RGBA.
MASTER="$OUT/master.png"
sips -s format png "$SVG" --out "$MASTER" > /dev/null

# Flatten onto black to strip alpha (iOS rejects icons with an alpha channel).
FLAT="$OUT/flat.png"
sips -s format png -s formatOptions best "$MASTER" --out "$FLAT" > /dev/null
# sips removes alpha by compositing onto a matte when the format lacks it; force
# an opaque copy via a JPEG round-trip then back to PNG (guarantees no alpha).
sips -s format jpeg "$MASTER" --out "$OUT/flat.jpg" > /dev/null
sips -s format png "$OUT/flat.jpg" --out "$FLAT" > /dev/null
rm -f "$OUT/flat.jpg"

# The loose-PNG names iOS/LiveContainer looks for in the bundle root.
# 60pt @2x/@3x (iPhone) and 76pt/83.5pt @2x (iPad) cover home-screen display.
declare -a ICONS=(
    "AppIcon60x60@2x.png:120"
    "AppIcon60x60@3x.png:180"
    "AppIcon76x76@2x.png:152"
    "AppIcon76x76@2x~ipad.png:152"
    "AppIcon83.5x83.5@2x.png:167"
    "AppIcon83.5x83.5@2x~ipad.png:167"
    "AppIcon1024x1024.png:1024"
)

for entry in "${ICONS[@]}"; do
    name="${entry%%:*}"
    size="${entry##*:}"
    sips -z "$size" "$size" "$FLAT" --out "$OUT/$name" > /dev/null
done

rm -f "$MASTER" "$FLAT"
echo "Generated icons in $OUT:"
ls "$OUT"
# Sanity: confirm no alpha on the primary icon.
if sips -g hasAlpha "$OUT/AppIcon60x60@2x.png" | grep -q "hasAlpha: yes"; then
    echo "WARNING: icon still has alpha channel" >&2
fi
