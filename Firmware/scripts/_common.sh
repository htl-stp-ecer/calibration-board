#!/usr/bin/env bash
# Sourced by the other scripts. Defines paths and common helpers.
set -euo pipefail

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
FIRMWARE_DIR="$(cd "$SCRIPT_DIR/.." && pwd)"

BUILD_TYPE="${BUILD_TYPE:-Debug}"
BUILD_DIR="$FIRMWARE_DIR/build/$BUILD_TYPE"
ELF="$BUILD_DIR/Firmware.elf"

SWD_FREQ_KHZ="${SWD_FREQ_KHZ:-4000}"
GDB_PORT="${GDB_PORT:-61234}"

CUBECLT_ROOT="${CUBECLT_ROOT:-/opt/st/stm32cubeclt_1.21.0}"
CUBECLT_PROG_BIN="$CUBECLT_ROOT/STM32CubeProgrammer/bin"
CUBECLT_GDB_BIN="$CUBECLT_ROOT/STLink-gdb-server/bin"

PROG_CLI="${PROG_CLI:-$CUBECLT_PROG_BIN/STM32_Programmer_CLI}"
GDB_SERVER="${GDB_SERVER:-$CUBECLT_GDB_BIN/ST-LINK_gdbserver}"
GDB="${GDB:-arm-none-eabi-gdb}"

# Strip libusb permission warnings and ANSI escapes from ST tool output.
filter_st_output() {
    grep -vE 'libusb:|^\s*$' | sed -r 's/\x1B\[[0-9;]*[mGKHF]//g'
}
