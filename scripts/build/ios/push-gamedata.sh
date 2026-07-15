#!/bin/bash
# Push game data (MIX files) into a LiveContainer app's data container over USB.
# The app must already be installed inside LiveContainer (so its data UUID exists).
# Usage: push-gamedata.sh <td|ra> <path-to-dir-with-MIX-files>
set -euo pipefail

LC_CONTAINER="com.kdt.livecontainer.B5L3CPH855"

GAME="${1:-}"
SRC="${2:-}"

case "$GAME" in
td) BUNDLE_ID="com.vanilla-conquer.vanillatd" ;;
ra) BUNDLE_ID="com.vanilla-conquer.vanillara" ;;
*)
    echo "Usage: $0 <td|ra> <path-to-dir-with-MIX-files>" >&2
    exit 1
    ;;
esac

if [ ! -d "$SRC" ]; then
    echo "error: source directory '$SRC' not found" >&2
    exit 1
fi

# Resolve the app's data container UUID from LiveContainer's app metadata.
TMP_PLIST=$(mktemp /tmp/lcappinfo.XXXXXX)
trap 'rm -f "$TMP_PLIST"' EXIT
# afcclient get exits 0 even on a missing remote file, so verify we got data.
afcclient --container "$LC_CONTAINER" get \
    "/Documents/Applications/$BUNDLE_ID.app/LCAppInfo.plist" "$TMP_PLIST" > /dev/null 2>&1 || true
if [ ! -s "$TMP_PLIST" ]; then
    echo "error: $BUNDLE_ID is not installed in LiveContainer yet." >&2
    echo "Install the ipa from LiveContainer's Documents first, then re-run." >&2
    exit 1
fi
UUID=$(plutil -extract LCDataUUID raw "$TMP_PLIST")
DEST="/Documents/Data/Application/$UUID/Documents"
echo "Data container for $BUNDLE_ID: $UUID"

# Push all MIX files, preserving one level of subfolders (gdi/, nod/, covertops/).
shopt -s nullglob nocaseglob
FILES=("$SRC"/*.mix "$SRC"/*/*.mix)
shopt -u nocaseglob
if [ ${#FILES[@]} -eq 0 ]; then
    echo "error: no .MIX files in $SRC" >&2
    exit 1
fi

for f in "${FILES[@]}"; do
    rel=${f#"$SRC"/}
    dir=$(dirname "$rel")
    if [ "$dir" != "." ]; then
        afcclient --container "$LC_CONTAINER" mkdir "$DEST/$dir" > /dev/null 2>&1 || true
    fi
    size=$(du -h "$f" | cut -f1)
    echo "  pushing $rel ($size)..."
    afcclient --container "$LC_CONTAINER" put "$f" "$DEST/$rel" > /dev/null
done

echo "Done. Pushed ${#FILES[@]} files to $DEST"
afcclient --container "$LC_CONTAINER" ls "$DEST"
