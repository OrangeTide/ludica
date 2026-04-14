#!/bin/bash
# Capture a screenshot from a ludica game via TCP automation.
# Usage: capture.sh <binary> <screenshot_name> [steps] [extra_args...]
set -e

BIN="$1"
NAME="$2"
STEPS="${3:-120}"
shift 3 2>/dev/null || true
EXTRA_ARGS="$@"

PORT=4000
DIR="$(cd "$(dirname "$0")" && pwd)"

cleanup() {
    exec 3<&- 2>/dev/null || true
    exec 3>&- 2>/dev/null || true
    kill $GAME_PID 2>/dev/null || true
    wait $GAME_PID 2>/dev/null || true
}
trap cleanup EXIT

# Launch game
$BIN --auto-port=$PORT --paused --fixed-dt --capture-dir="$DIR" $EXTRA_ARGS 2>&1 &
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

# Open bidirectional TCP connection via bash
exec 3<>/dev/tcp/127.0.0.1/$PORT

# Send STEP and read response
echo "STEP $STEPS" >&3
read -t 30 resp <&3
echo "  step: $resp"

# Send CAPSCREEN and read response
echo "CAPSCREEN ${NAME}.png" >&3
read -t 10 resp <&3
echo "  capscreen: $resp"

# Send QUIT
echo "QUIT" >&3
read -t 5 resp <&3 || true
echo "  quit: $resp"

# Close connection
exec 3<&-
exec 3>&-

wait $GAME_PID 2>/dev/null || true

if [ -f "$DIR/${NAME}.png" ]; then
    echo "OK: ${NAME}.png"
else
    echo "FAIL: no screenshot produced" >&2
    exit 1
fi
