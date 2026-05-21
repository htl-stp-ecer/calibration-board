#!/usr/bin/env bash
# Stream SWO/ITM trace from PB3 via ST-LINK V2 to stdout (and a log file).
#
#   SYSCLK_MHZ  CPU clock in MHz (must match HCLK used by swo_init() in firmware).
#               Default: 192 — matches the HSI+PLL config in Firmware.ioc.
#   SWV_PORT    ITM stimulus port to print (0–31 or "all"). Default: 0.
#   SWV_LOG     Path to log file. Default: $FIRMWARE_DIR/build/swv.log.
#
# Notes:
#   - The MCU side (TPIU/ITM, baud) is configured by swo_init() at boot, so the
#     CLI here just attaches and reads. No flashing happens.
#   - STM32_Programmer_CLI -startswv keeps the run going; Ctrl-C to stop. The
#     CLI also tails the log file with its own buffering, so we additionally
#     `tail -f` it in the background for a real-time view.
source "$(dirname "$(readlink -f "$0")")/_common.sh"

SYSCLK_MHZ="${SYSCLK_MHZ:-192}"
SWV_PORT="${SWV_PORT:-0}"
SWV_LOG="${SWV_LOG:-$BUILD_DIR/swv.log}"

mkdir -p "$(dirname "$SWV_LOG")"
: > "$SWV_LOG"

echo ">>> SWO trace: sysclk=${SYSCLK_MHZ} MHz, port=${SWV_PORT}, log=$SWV_LOG"
echo ">>> (Ctrl-C to stop)"

tail -n +1 -f "$SWV_LOG" &
TAIL_PID=$!
trap 'kill $TAIL_PID 2>/dev/null || true' EXIT

"$PROG_CLI" -c port=SWD freq="$SWD_FREQ_KHZ" mode=HOTPLUG \
    -startswv freq="$SYSCLK_MHZ" portnumber="$SWV_PORT" "$SWV_LOG" \
    2>&1 | filter_st_output
