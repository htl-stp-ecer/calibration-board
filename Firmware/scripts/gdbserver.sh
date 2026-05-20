#!/usr/bin/env bash
# Start ST-LINK GDB server (foreground). Attach with: arm-none-eabi-gdb -ex "target remote :61234"
# Useful for CLion / VSCode debug configs that connect to an external GDB server.
source "$(dirname "$(readlink -f "$0")")/_common.sh"

: "${ATTACH:=0}"        # 1 → attach to running target (no reset)
: "${PERSISTENT:=1}"    # 1 → keep server alive across client reconnects

EXTRA=()
[[ "$PERSISTENT" == "1" ]] && EXTRA+=("-e")
[[ "$ATTACH"     == "1" ]] && EXTRA+=("-g")

echo ">>> ST-LINK GDB server on tcp:localhost:$GDB_PORT (SWD ${SWD_FREQ_KHZ} kHz, attach=$ATTACH, persistent=$PERSISTENT)"
exec "$GDB_SERVER" \
    -p "$GDB_PORT" \
    -l 1 \
    -d -s \
    "${EXTRA[@]}" \
    -cp "$CUBECLT_PROG_BIN" \
    -m 0 \
    --frequency "$SWD_FREQ_KHZ"
