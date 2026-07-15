#!/bin/sh
# run.sh — build the Windows clipboard test with mingw and run it under Wine.
#
# Links the real win32/clipboard.c and the shared input.c against a one-symbol
# stub (win_stub.c), so no EGL/GLES or window code is pulled in. Runs entirely
# on a Linux box; needs x86_64-w64-mingw32-gcc and wine on PATH.
#
# Made by a machine. PUBLIC DOMAIN (CC0-1.0)
set -e

CC=${CC:-x86_64-w64-mingw32-gcc}
WINE=${WINE:-wine}
here=$(dirname "$0")
root=$(cd "$here/../.." && pwd)
out=$(mktemp -d)
trap 'rm -rf "$out"' EXIT

if ! command -v "$CC" >/dev/null 2>&1; then
	echo "SKIP: $CC not found (install mingw-w64 to run this test)" >&2
	exit 0
fi
if ! command -v "$WINE" >/dev/null 2>&1; then
	echo "SKIP: $WINE not found (install wine to run this test)" >&2
	exit 0
fi

"$CC" -o "$out/clipboard_test.exe" \
	"$root/src/ludica/win32/clipboard.c" \
	"$root/src/ludica/input.c" \
	"$here/win_stub.c" \
	"$here/clipboard_test.c" \
	-I "$root/src/ludica/include" -I "$root/src/ludica" \
	-Wall -Wextra -O2 -lshell32 -lole32

WINEDEBUG=${WINEDEBUG:--all} "$WINE" "$out/clipboard_test.exe"
