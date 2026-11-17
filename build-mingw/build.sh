#!/bin/sh
# Complete cross-build with resources and an embedded manifest using LLVM-MinGW.
# MINGW_BIN=/path/to/llvm-mingw/bin sh build-mingw/build.sh x86|x64|arm64
set -eu
cd "$(dirname "$0")/.."
VIV_BUILD_ROOT=$(pwd)
VIV_ARCH=${1:-x64}
case "$VIV_ARCH" in
 x64) VIV_TRIPLE=x86_64-w64-mingw32; VIV_DEFINE=-DVERSION_X64 ;;
 x86) VIV_TRIPLE=i686-w64-mingw32; VIV_DEFINE=-DVERSION_X86 ;;
 arm64) VIV_TRIPLE=aarch64-w64-mingw32; VIV_DEFINE=-DVERSION_ARM64 ;;
 *) echo "usage: $0 x86|x64|arm64" >&2; exit 2 ;;
esac
if [ -n "${MINGW_BIN:-}" ]; then
 VIV_CC="$MINGW_BIN/$VIV_TRIPLE-gcc"; VIV_RC="$MINGW_BIN/$VIV_TRIPLE-windres"
else
 VIV_CC="$VIV_TRIPLE-gcc"; VIV_RC="$VIV_TRIPLE-windres"
fi
VIV_OUT="$VIV_BUILD_ROOT/build-mingw/$VIV_ARCH"
rm -rf "$VIV_OUT"
mkdir -p "$VIV_OUT/tmp"
TMPDIR="$VIV_OUT/tmp"
export TMPDIR
"$VIV_CC" --version > "$VIV_OUT/compiler.txt"
while IFS= read -r VIV_SOURCE; do
 [ -n "$VIV_SOURCE" ] || continue
 VIV_OBJECT=$(printf '%s' "$VIV_SOURCE" | tr '/' '_' | sed 's/\.c$/.o/')
 "$VIV_CC" -Os -DNDEBUG $VIV_DEFINE -DUNICODE -D_UNICODE -mguard=cf \
  -Wno-macro-redefined -Werror=incompatible-pointer-types -Werror=visibility \
  -Ilibwebp -c "$VIV_SOURCE" -o "$VIV_OUT/$VIV_OBJECT" 2>> "$VIV_OUT/compile.log"
done < build-zig/files.txt
cat > "$VIV_OUT/resources.rc" <<EOF
#include "$VIV_BUILD_ROOT/res/voidImageViewer.rc"
1 24 "$VIV_BUILD_ROOT/res/voidImageViewer.Manifest"
EOF
"$VIV_RC" -Ires -DUNICODE -D_UNICODE "$VIV_OUT/resources.rc" -o "$VIV_OUT/resources.o"
"$VIV_CC" "$VIV_OUT"/*.o -o "$VIV_OUT/voidImageViewer.exe" -mwindows \
 -Wl,--large-address-aware,--dynamicbase,--nxcompat,--guard-cf \
 -lcomctl32 -lcomdlg32 -lshlwapi -lshell32 -lgdi32 -luser32 -limm32 \
 -ladvapi32 -lole32 -loleaut32 -luuid -lwinmm -ldwmapi -lversion
python3 tools/pe_header_check.py "$VIV_OUT/voidImageViewer.exe" --machine "$VIV_ARCH"
echo "$VIV_ARCH BUILD OK (cross-compiled; Windows runtime validation required)"
