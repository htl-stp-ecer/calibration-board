#!/usr/bin/env bash
# Build (if needed) then flash + verify + start on STM32F722 via ST-LINK SWD.
# Usage: flash.sh [Debug|Release]   (default: Debug)
source "$(dirname "$(readlink -f "$0")")/_common.sh"
[[ $# -ge 1 ]] && BUILD_TYPE="$1" && BUILD_DIR="$FIRMWARE_DIR/build/$BUILD_TYPE" && ELF="$BUILD_DIR/Firmware.elf"

"$SCRIPT_DIR/build.sh" "$BUILD_TYPE"

echo ">>> Flashing $ELF"
"$PROG_CLI" -c port=SWD freq="$SWD_FREQ_KHZ" -w "$ELF" -v --start 2>&1 | filter_st_output
