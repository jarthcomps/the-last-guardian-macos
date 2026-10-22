#!/bin/zsh
# Builds "The Last Guardian.app" (launcher + bundled shadPS4) and zips it into dist/.
#
#   ENGINE_DIR=<folder with shadps4, libvulkan.dylib, libvulkan_kosmickrisp.dylib and
#               kosmickrisp_mesa_icd.json> PKG_TOOL=<ps4-pkg-tool binary> launcher/build.sh
#
# Both default to engine-bin/ next to this script (see README, "Building from source").
# OUT (default build/) and DIST (default dist/) pick where the app and the zip go.
set -euo pipefail
HERE=${0:A:h}
ROOT=${HERE:h}
ENGINE_DIR=${ENGINE_DIR:-$HERE/engine-bin}
PKG_TOOL=${PKG_TOOL:-$HERE/engine-bin/ps4-pkg-tool}
OUT=${OUT:-$ROOT/build}
DIST=${DIST:-$ROOT/dist}
APP="$OUT/The Last Guardian.app"

for f in shadps4 libvulkan.dylib libvulkan_kosmickrisp.dylib kosmickrisp_mesa_icd.json; do
  [[ -f $ENGINE_DIR/$f ]] || { echo "missing $ENGINE_DIR/$f" >&2; exit 1; }
done
[[ -x $PKG_TOOL ]] || { echo "missing PKG extractor $PKG_TOOL" >&2; exit 1; }

rm -rf "$APP" && mkdir -p "$APP/Contents/MacOS" "$APP/Contents/Resources"
for arch in arm64 x86_64; do
  xcrun swiftc -O -parse-as-library -target $arch-apple-macos14.0 \
    -o "$OUT/TLGLauncher-$arch" "$HERE"/Sources/*.swift
done
lipo -create "$OUT/TLGLauncher-arm64" "$OUT/TLGLauncher-x86_64" -output "$APP/Contents/MacOS/TLGLauncher"
rm -f "$OUT"/TLGLauncher-*
cp "$HERE/Info.plist" "$APP/Contents/Info.plist"
cp -R "$HERE/Resources/defaults" "$APP/Contents/Resources/defaults"
# Shader recipes (no shader code; the driver rewrites its own generated code and checks a hash).
if [[ -f "$HERE/Resources/shader-recipes.txt" ]]; then
  cp "$HERE/Resources/shader-recipes.txt" "$APP/Contents/Resources/shader-recipes.txt"
fi

# Two-player co-op scripts (the launcher installs them into the game's mods overlay when the
# option is on). COOP_LUA defaults to coop/lua next to the launcher.
COOP_LUA=${COOP_LUA:-$ROOT/coop/lua}
if [[ -d $COOP_LUA ]]; then
  mkdir -p "$APP/Contents/Resources/coop"
  cp "$COOP_LUA"/*.lua "$APP/Contents/Resources/coop/"
else
  echo "note: no co-op scripts at $COOP_LUA; the co-op option will report them missing" >&2
fi

# The engine is a plain folder: shadPS4 loads the Vulkan driver manifest that sits next to its
# executable, which a nested app bundle's signature would not allow.
ENGINE="$APP/Contents/Resources/engine"
mkdir -p "$ENGINE"
cp "$ENGINE_DIR"/{shadps4,libvulkan.dylib,libvulkan_kosmickrisp.dylib} "$ENGINE/"
sed 's#"library_path": *"[^"]*"#"library_path": "./libvulkan_kosmickrisp.dylib"#' \
    "$ENGINE_DIR/kosmickrisp_mesa_icd.json" > "$ENGINE/kosmickrisp_mesa_icd.json"

# PKG extractor (ps4-pkg-tools, GPL-2.0-or-later).
mkdir -p "$APP/Contents/Resources/tools"
cp "$PKG_TOOL" "$APP/Contents/Resources/tools/ps4-pkg-tool"

# No debug map in the download: it lists the object files' absolute build paths.
strip -S "$ENGINE"/{libvulkan.dylib,libvulkan_kosmickrisp.dylib,shadps4} \
  "$APP/Contents/Resources/tools/ps4-pkg-tool"

# Ad-hoc signatures, inside out (no Apple developer account involved).
for f in "$ENGINE"/{libvulkan.dylib,libvulkan_kosmickrisp.dylib,shadps4} \
         "$APP/Contents/Resources/tools/ps4-pkg-tool"; do
  codesign --force --sign - "$f"
done
codesign --force --sign - "$APP"
codesign --verify --strict "$APP"

mkdir -p "$DIST"
rm -f "$DIST/The-Last-Guardian-macOS.zip"
ditto -c -k --keepParent "$APP" "$DIST/The-Last-Guardian-macOS.zip"
echo "built $APP"
ls -lh "$DIST/The-Last-Guardian-macOS.zip"
