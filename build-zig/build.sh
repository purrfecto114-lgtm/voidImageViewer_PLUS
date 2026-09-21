#!/bin/sh
# zig cc cross build: compiles every translation unit listed in files.txt
# then links a 64 bit windows executable. usage: sh build-zig/build.sh
# requires: python3 with the ziglang wheel (pip install ziglang).
set -e
cd "$(dirname "$0")/.."

ZIG="${ZIG:-python3 -m ziglang}"
FLAGS="-target x86_64-windows-gnu -Os -DNDEBUG -DVERSION_X64 -DUNICODE -D_UNICODE -Wno-macro-redefined"
OBJDIR="build-zig/obj"

# a clean slate per build: the old shape only mkdir'd, so a deleted or
# renamed source left its .o behind and the link stage's *.o glob linked
# the ghost into the binary (stale symbols, duplicate definitions,
# "deleted from the source but alive in the exe" - and the appended
# errors.log carried the previous failure into the next run's diagnosis).
# the directory is gitignored scratch: wiping it costs one rebuild and
# closes the whole class.
rm -rf "$OBJDIR"
mkdir -p "$OBJDIR"

# each zig cc process is a full clang front end; the build runs them
# serially (the old batch/wait pair was a no-op - nothing ever ran in
# the background).
fail=0
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
done < build-zig/files.txt

if [ "$fail" -ne 0 ]; then
	echo "COMPILE FAILED - see $OBJDIR/errors.log"
	grep -m 12 "error:" "$OBJDIR/errors.log" || true
	exit 1
fi

$ZIG cc $FLAGS $OBJDIR/*.o -o build-zig/viv.exe -mwindows \
	-lcomctl32 -lcomdlg32 -lshlwapi -lshell32 -lgdi32 -luser32 -limm32 \
	-ladvapi32 -lole32 -loleaut32 -luuid -lwinmm -ldwmapi -lversion

cp res/voidImageViewer.Manifest build-zig/viv.exe.manifest
ls -l build-zig/viv.exe
echo "BUILD OK"
