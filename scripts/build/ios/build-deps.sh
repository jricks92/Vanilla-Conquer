#!/bin/bash
# Build static SDL2 and openal-soft for arm64 iOS.
# Output: build-deps/ios/{include,lib} — pointed at by the ios CMake preset.
set -euo pipefail

SDL2_VERSION="2.32.10"
OPENAL_VERSION="1.24.3"
IOS_DEPLOYMENT_TARGET="15.0"

ROOT="$(cd "$(dirname "$0")/../../.." && pwd)"
DEPS="$ROOT/build-deps"
PREFIX="$DEPS/ios"
SRC="$DEPS/src"

mkdir -p "$PREFIX" "$SRC"

CMAKE_IOS_FLAGS=(
    -DCMAKE_SYSTEM_NAME=iOS
    -DCMAKE_OSX_ARCHITECTURES=arm64
    -DCMAKE_OSX_SYSROOT=iphoneos
    -DCMAKE_OSX_DEPLOYMENT_TARGET="$IOS_DEPLOYMENT_TARGET"
    -DCMAKE_INSTALL_PREFIX="$PREFIX"
    -DCMAKE_BUILD_TYPE=Release
    -DBUILD_SHARED_LIBS=OFF
    -GNinja
)

fetch() {
    local url="$1" out="$2"
    if [ ! -f "$out" ]; then
        echo "Fetching $url"
        curl -L --fail -o "$out" "$url"
    fi
}

## SDL2 (static)
if [ ! -f "$PREFIX/lib/libSDL2.a" ]; then
    fetch "https://github.com/libsdl-org/SDL/releases/download/release-$SDL2_VERSION/SDL2-$SDL2_VERSION.tar.gz" \
          "$SRC/SDL2-$SDL2_VERSION.tar.gz"
    tar -xzf "$SRC/SDL2-$SDL2_VERSION.tar.gz" -C "$SRC"

    # Apply local SDL patches (reset the iOS text field on keyboard show so the
    # predictive-text bar does not carry stale content between sessions).
    PATCH_DIR="$(dirname "$0")/patches"
    for p in "$PATCH_DIR"/sdl2-*.patch; do
        [ -f "$p" ] || continue
        echo "Applying $(basename "$p")"
        patch -p1 -d "$SRC/SDL2-$SDL2_VERSION" < "$p"
    done

    cmake -S "$SRC/SDL2-$SDL2_VERSION" -B "$SRC/SDL2-$SDL2_VERSION/build-ios" \
        "${CMAKE_IOS_FLAGS[@]}" \
        -DSDL_STATIC=ON \
        -DSDL_SHARED=OFF \
        -DSDL_TEST=OFF
    cmake --build "$SRC/SDL2-$SDL2_VERSION/build-ios"
    cmake --install "$SRC/SDL2-$SDL2_VERSION/build-ios"
fi

## openal-soft (static, CoreAudio backend)
if [ ! -f "$PREFIX/lib/libopenal.a" ]; then
    fetch "https://github.com/kcat/openal-soft/archive/refs/tags/$OPENAL_VERSION.tar.gz" \
          "$SRC/openal-soft-$OPENAL_VERSION.tar.gz"
    tar -xzf "$SRC/openal-soft-$OPENAL_VERSION.tar.gz" -C "$SRC"
    cmake -S "$SRC/openal-soft-$OPENAL_VERSION" -B "$SRC/openal-soft-$OPENAL_VERSION/build-ios" \
        "${CMAKE_IOS_FLAGS[@]}" \
        -DLIBTYPE=STATIC \
        -DALSOFT_UTILS=OFF \
        -DALSOFT_EXAMPLES=OFF \
        -DALSOFT_TESTS=OFF \
        -DALSOFT_BACKEND_COREAUDIO=ON
    cmake --build "$SRC/openal-soft-$OPENAL_VERSION/build-ios"
    cmake --install "$SRC/openal-soft-$OPENAL_VERSION/build-ios"
fi

## Verify artifacts are really iOS arm64 (Generals lesson: check, don't trust exit codes)
echo
echo "=== Artifact verification ==="
for lib in "$PREFIX/lib/libSDL2.a" "$PREFIX/lib/libopenal.a"; do
    printf '%s: ' "$(basename "$lib")"
    lipo -info "$lib" | sed 's/.*are: //'
    platform=$(otool -l "$lib" 2>/dev/null | grep -A1 'LC_BUILD_VERSION' | grep 'platform' | head -1 | awk '{print $2}')
    echo "  LC_BUILD_VERSION platform: ${platform:-<none found>} (2 = iOS)"
done
echo "Installed into $PREFIX"
