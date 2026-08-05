#!/usr/bin/env bash
# dist1.sh — assemble the NativeBridge Unity package from the per-platform
# install_* artifacts produced by build.ps1.
#
# Usage (from repo root, after all platform build artifacts are present):
#   bash src/nativebridge/dist1.sh <version>
#
# Produces:
#   NativeBridge/                     (Unity package tree, see below)
#   NativeBridge-CSharpe-<version>.zip
#
# Expected inputs (any missing platform is skipped with a warning):
#   install_win32_x64/nativebridge/bin/NativeBridge.dll
#   install_win32_arm64/nativebridge/bin/NativeBridge.dll
#   install_linux_x64/nativebridge/lib/libNativeBridge.so
#   install_linux_arm64/nativebridge/lib/libNativeBridge.so
#   install_android_*/nativebridge/lib/libNativeBridge.so
#   install_osx_x64|arm64/nativebridge/lib/libNativeBridge.dylib
#   install_ios_arm64/.../libNativeBridge.a  + sim + tvos  (xcframework)
set -u

VERSION="${1:-1.0.0}"
LIB=nativebridge
PKG=NativeBridge
OUT="$PKG"
ZIP="${PKG}-CSharpe-${VERSION}.zip"

echo "NativeBridge dist: version=$VERSION"

rm -rf "$OUT"
mkdir -p "$OUT/Runtime" "$OUT/Plugins"

# --- C# runtime sources -----------------------------------------------------
cp -f src/nativebridge/csharp/Curlw.cs "$OUT/Runtime/" 2>/dev/null || echo "WARN: Curlw.cs missing"
cp -f src/nativebridge/csharp/NativeBridge.asmdef "$OUT/Runtime/" 2>/dev/null || true

# --- helper: copy a file if it exists, else warn ---------------------------
copy_if() { # src dstdir
    if [ -f "$1" ]; then
        mkdir -p "$2"
        cp -f "$1" "$2/"
        echo "  + $1 -> $2"
    else
        echo "  - skip (missing): $1"
    fi
}

# --- Windows ----------------------------------------------------------------
copy_if "install_win32_x64/$LIB/bin/NativeBridge.dll"   "$OUT/Plugins/Windows/x86_64"
copy_if "install_win32_arm64/$LIB/bin/NativeBridge.dll" "$OUT/Plugins/Windows/ARM64"

# --- Linux ------------------------------------------------------------------
copy_if "install_linux_x64/$LIB/lib/libNativeBridge.so"   "$OUT/Plugins/Linux/x86_64"
copy_if "install_linux_arm64/$LIB/lib/libNativeBridge.so" "$OUT/Plugins/Linux/ARM64"

# --- Android ----------------------------------------------------------------
copy_if "install_android_arm64/$LIB/lib/libNativeBridge.so" "$OUT/Plugins/Android/arm64-v8a"
copy_if "install_android_armv7/$LIB/lib/libNativeBridge.so" "$OUT/Plugins/Android/armeabi-v7a"
copy_if "install_android_x64/$LIB/lib/libNativeBridge.so"   "$OUT/Plugins/Android/x86_64"
copy_if "install_android_x86/$LIB/lib/libNativeBridge.so"   "$OUT/Plugins/Android/x86"

# --- macOS (universal .bundle-less dylib) -----------------------------------
MAC_X64="install_osx_x64/$LIB/lib/libNativeBridge.dylib"
MAC_ARM="install_osx_arm64/$LIB/lib/libNativeBridge.dylib"
if [ -f "$MAC_X64" ] && [ -f "$MAC_ARM" ] && command -v lipo >/dev/null 2>&1; then
    mkdir -p "$OUT/Plugins/macOS"
    lipo -create "$MAC_X64" "$MAC_ARM" -output "$OUT/Plugins/macOS/NativeBridge.dylib"
    lipo -info "$OUT/Plugins/macOS/NativeBridge.dylib"
    echo "  + macOS universal dylib"
else
    copy_if "$MAC_ARM" "$OUT/Plugins/macOS"
    copy_if "$MAC_X64" "$OUT/Plugins/macOS"
fi

# --- iOS / tvOS xcframeworks (static libs) ----------------------------------
# Build ONE xcframework per Unity platform folder: Unity only searches
# Plugins/iOS for iOS builds and Plugins/tvOS for tvOS builds, so an xcframework
# containing tvOS slices must not live under Plugins/iOS (Unity tvOS wouldn't
# find it). Device + simulator slices are combined within each platform's
# xcframework; simulator arm64+x64 are lipo'd into one fat archive first.
build_xcframework() {
    if ! command -v xcodebuild >/dev/null 2>&1; then
        echo "  - skip xcframework (xcodebuild unavailable)"
        return
    fi

    local plat="$1"       # ios | tvos
    local out_subdir="$2" # iOS | tvOS
    local dev="install_${plat}_arm64/$LIB/lib/libNativeBridge.a"
    local sim_arm="install_${plat}_arm64_sim/$LIB/lib/libNativeBridge.a"
    local sim_x64="install_${plat}_x64/$LIB/lib/libNativeBridge.a"

    local args=()
    local tmp="fat_tmp_${LIB}_${plat}"
    rm -rf "$tmp"; mkdir -p "$tmp"

    # device slice
    [ -f "$dev" ] && args+=(-library "$dev")

    # simulator slice (arm64 + x64 -> fat)
    if [ -f "$sim_arm" ] || [ -f "$sim_x64" ]; then
        local inputs=(); [ -f "$sim_arm" ] && inputs+=("$sim_arm"); [ -f "$sim_x64" ] && inputs+=("$sim_x64")
        lipo -create "${inputs[@]}" -output "$tmp/${plat}_sim.a"
        args+=(-library "$tmp/${plat}_sim.a")
    fi

    if [ ${#args[@]} -eq 0 ]; then
        echo "  - skip ${plat} xcframework (no static libs found)"
        rm -rf "$tmp"
        return
    fi

    mkdir -p "$OUT/Plugins/${out_subdir}"
    xcodebuild -create-xcframework "${args[@]}" -output "$OUT/Plugins/${out_subdir}/NativeBridge.xcframework"
    echo "  + Plugins/${out_subdir}/NativeBridge.xcframework"
    rm -rf "$tmp"
}
build_xcframework ios  iOS
build_xcframework tvos tvOS

# --- version manifest -------------------------------------------------------
{
    echo "nativebridge: $VERSION"
    echo "curlw_abi: 1"
    for verf in install_*/curl/_1kiss; do
        [ -f "$verf" ] && echo "curl: $(head -1 "$verf" | sed 's/^ver:[[:space:]]*//')" && break
    done
} > "$OUT/_nativebridge.yml"

# --- zip --------------------------------------------------------------------
rm -f "$ZIP"
if command -v zip >/dev/null 2>&1; then
    zip -q -r "$ZIP" "$OUT"
    echo "NativeBridge dist: wrote $ZIP"
else
    echo "WARN: zip not found; package tree left at $OUT/"
fi

# Export for the GitHub release step.
if [ -n "${GITHUB_ENV:-}" ]; then
    echo "NB_DIST_ZIP=$ZIP" >> "$GITHUB_ENV"
    echo "NB_DIST_DIR=$OUT" >> "$GITHUB_ENV"
fi
