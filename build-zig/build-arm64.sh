#!/bin/sh
# zig cc cross build for the ARM64 target: compiles every translation
# unit listed in files.txt then links an aarch64 windows executable.
# usage: sh build-zig/build-arm64.sh
# requires: python3 with the ziglang wheel (pip install ziglang).
#
# the corrections review's finding: viv_state.h carries the
# VERSION_ARM64 branch of the machine chain, but no pipeline ever
# compiled it - the target existed as source text and nothing else.
# this build is the compile gate for that target: it proves the tree
# still translates and links for aarch64 on every push. the binary is
# not executed anywhere (no windows-arm64 runner rides the free tier)
# - runtime verification for the target stays on the deferred record
# until a machine answers for it.
set -e
cd "$(dirname "$0")/.."

ZIG="${ZIG:-python3 -m ziglang}"
FLAGS="-target aarch64-windows-gnu -Os -DNDEBUG -DVERSION_ARM64 -DUNICODE -D_UNICODE -Wno-macro-redefined"
OBJDIR="build-zig/obj-arm64"

mkdir -p "$OBJDIR"

# compile in small batches: each zig cc process is a full clang front end.
fail=0
batch=0
while read -r f; do
	[ -n "$f" ] || continue
	# libwebp includes repo relative headers ("src/dec/..."), the app code
	# is self rooted ("viv.h") - give each tree only its own include root so
	# the two string.h headers can never shadow each other.
	case "$f" in
		libwebp/*) inc="-Ilibwebp" ;;
		*) inc="-Ilibwebp" ;;
	esac
	o="$OBJDIR/$(printf '%s' "$f" | tr '/' '_' | sed 's/\.c$/.o/')"
	$ZIG cc $FLAGS $inc -c "$f" -o "$o" 2>> "$OBJDIR/errors.log" || fail=1
	batch=$((batch+1))
	if [ $batch -ge 8 ]; then
		wait 2>/dev/null || true
		batch=0
	fi
done < build-zig/files.txt
wait 2>/dev/null || true

if [ "$fail" -ne 0 ]; then
	echo "COMPILE FAILED - see $OBJDIR/errors.log"
	grep -m 12 "error:" "$OBJDIR/errors.log" || true
	exit 1
fi

$ZIG cc $FLAGS $OBJDIR/*.o -o build-zig/viv-arm64.exe -mwindows \
	-lcomctl32 -lcomdlg32 -lshlwapi -lshell32 -lgdi32 -luser32 -limm32 \
	-ladvapi32 -lole32 -loleaut32 -luuid -lwinmm -ldwmapi -lversion

cp res/voidImageViewer.Manifest build-zig/viv-arm64.exe.manifest
ls -l build-zig/viv-arm64.exe
echo "ARM64 BUILD OK"
