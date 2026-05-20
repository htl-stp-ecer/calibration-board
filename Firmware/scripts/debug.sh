#!/usr/bin/env bash
# Build, start gdbserver in background, attach arm-none-eabi-gdb, load, break main, run.
# Interactive — use this when you want a CLI debug session.
source "$(dirname "$(readlink -f "$0")")/_common.sh"

"$SCRIPT_DIR/build.sh" "$BUILD_TYPE"

echo ">>> Starting ST-LINK GDB server on :$GDB_PORT"
"$GDB_SERVER" -p "$GDB_PORT" -l 1 -d -s -cp "$CUBECLT_PROG_BIN" -m 0 --frequency "$SWD_FREQ_KHZ" \
    > /tmp/stlink-gdbserver.log 2>&1 &
SERVER_PID=$!
trap 'kill $SERVER_PID 2>/dev/null || true' EXIT

# Wait for server to accept connections (port open).
for _ in $(seq 1 50); do
    (echo > /dev/tcp/127.0.0.1/"$GDB_PORT") 2>/dev/null && break
    sleep 0.1
done

echo ">>> Launching GDB"
"$GDB" -q \
    -ex "target extended-remote :$GDB_PORT" \
    -ex "monitor reset" \
    -ex "load" \
    -ex "tbreak main" \
    -ex "continue" \
    "$ELF"
