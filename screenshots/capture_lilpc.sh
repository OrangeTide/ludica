#!/bin/bash
# Capture a screenshot of lilpc after ~4 seconds of boot.
set -e
cd "$(dirname "$0")/.."

BIN=_out/x86_64-linux-gnu/bin/lilpc
BIOS=samples/lilpc/bios/pcxtbios.bin
PORT=4000
DIR=screenshots
NAME=lilpc

cleanup() {
    exec 3<&- 2>/dev/null || true
    exec 3>&- 2>/dev/null || true
    kill $GAME_PID 2>/dev/null || true
    wait $GAME_PID 2>/dev/null || true
}
trap cleanup EXIT

# Launch lilpc with automation
$BIN --bios="$BIOS" --auto-port=$PORT --paused --fixed-dt --capture-dir="$DIR" 2>&1 &
GAME_PID=$!

# Wait for port to be ready
for i in $(seq 1 50); do
    ss -tln 2>/dev/null | grep -q ":$PORT " && break
    sleep 0.1
done

if ! ss -tln 2>/dev/null | grep -q ":$PORT "; then
    echo "ERROR: port $PORT not listening after 5s" >&2
    exit 1
fi

# Open bidirectional TCP connection
exec 3<>/dev/tcp/127.0.0.1/$PORT

# Step 240 frames (~4 seconds at 60fps)
echo "STEP 240" >&3
read -t 30 resp <&3
echo "  step: $resp"

# Capture screenshot
echo "CAPSCREEN ${NAME}.png" >&3
read -t 10 resp <&3
echo "  capscreen: $resp"

# Quit
echo "QUIT" >&3
read -t 5 resp <&3 || true
echo "  quit: $resp"

# Close connection
exec 3<&-
exec 3>&-

wait $GAME_PID 2>/dev/null || true

if [ -f "$DIR/${NAME}.png" ]; then
    convert "$DIR/${NAME}.png" "$DIR/${NAME}.jpg"
    rm "$DIR/${NAME}.png"
else
    echo "FAIL: no screenshot produced" >&2
    exit 1
fi
