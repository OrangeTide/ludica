#!/bin/bash
# Capture screenshots of all ludica samples
set -e
cd "$(dirname "$0")/.."
SCRIPT=screenshots/capture.sh
BIN=_out/x86_64-linux-gnu/bin

capture() {
    local bin="$1" name="$2" steps="$3"
    shift 3
    echo "=== Capturing $name ($steps frames) ==="
    bash "$SCRIPT" "$BIN/$bin" "$name" "$steps" "$@" 2>&1
    echo ""
}

# demo01: palette framebuffer + CRT - animated, 120 frames is fine
# (already captured)

# demo02: parallax scrolling - auto-scrolls, needs more frames to see motion
capture demo02_multiscroll demo02_multiscroll 180

# demo03: text & dialogs
capture demo03_text_dialogs demo03_text_dialogs 60

# demo04: sprite rendering
capture demo04_sprites demo04_sprites 120

# demo05: audio mixer UI
capture demo05_audio demo05_audio 60

# hero: 3D portal engine
capture hero hero 120

# tridrop: triangle drop
capture tridrop tridrop 180

# ansiview: ANSI art viewer
capture ansiview ansiview 60

echo "=== All done ==="
ls -la screenshots/*.png
