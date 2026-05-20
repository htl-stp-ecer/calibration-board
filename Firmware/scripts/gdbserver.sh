#!/usr/bin/env bash
# Start ST-LINK GDB server (foreground). Attach with: arm-none-eabi-gdb -ex "target remote :61234"
# Useful for CLion / VSCode debug configs that connect to an external GDB server.
source "$(dirname "$(readlink -f "$0")")/_common.sh"

echo ">>> ST-LINK GDB server on tcp:localhost:$GDB_PORT (SWD ${SWD_FREQ_KHZ} kHz)"
exec "$GDB_SERVER" \
    -p "$GDB_PORT" \
    -l 1 \
    -d -s \
    -cp "$CUBECLT_PROG_BIN" \
    -m 0 \
    --frequency "$SWD_FREQ_KHZ"
