#!/usr/bin/env bash
# Stream UART4 output (J703 on PA0/PA1, 115200 8N1) via a USB-UART adapter.
#
#   UART_DEV    Serial device. Default: auto-detected CP2102, else /dev/ttyUSB0.
#   UART_BAUD   Baud rate. Default: 115200 (matches MX_UART4_Init).
#   UART_LOG    Path to log file (tee'd in parallel). Default: $BUILD_DIR/uart.log.
#
# Notes:
#   - Read-only: we never send anything to the MCU. If you need bidirectional,
#     use picocom/tio/minicom instead.
#   - stty puts the port into raw 8N1; cat then streams bytes to stdout + log.
source "$(dirname "$(readlink -f "$0")")/_common.sh"

# Auto-detect CP2102; fall back to /dev/ttyUSB0.
auto_dev=""
for d in /dev/ttyUSB* /dev/ttyACM*; do
    [ -e "$d" ] || continue
    if udevadm info -q property -n "$d" 2>/dev/null | grep -q "CP210"; then
        auto_dev="$d"; break
    fi
done

UART_DEV="${UART_DEV:-${auto_dev:-/dev/ttyUSB0}}"
UART_BAUD="${UART_BAUD:-115200}"
UART_LOG="${UART_LOG:-$BUILD_DIR/uart.log}"

if [ ! -e "$UART_DEV" ]; then
    echo "ERROR: $UART_DEV not present. Plug in the USB-UART adapter." >&2
    exit 1
fi
if [ ! -r "$UART_DEV" ] || [ ! -w "$UART_DEV" ]; then
    echo "ERROR: no R/W access to $UART_DEV. Are you in the 'dialout' group?" >&2
    exit 1
fi

mkdir -p "$(dirname "$UART_LOG")"

echo ">>> UART4 monitor: dev=$UART_DEV baud=$UART_BAUD log=$UART_LOG"
echo ">>> (Ctrl-C to stop)"

# raw 8N1, no echo, no flow control, no line processing.
stty -F "$UART_DEV" "$UART_BAUD" cs8 -cstopb -parenb -crtscts \
    raw -echo -echoe -echok -echoctl -echoke -ixon -ixoff -icrnl -inlcr -igncr

# tee for live view + persistent log. exec replaces the shell so Ctrl-C kills cat directly.
exec cat "$UART_DEV" | tee "$UART_LOG"
