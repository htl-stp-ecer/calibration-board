#!/usr/bin/env bash
# Build firmware. Usage: build.sh [Debug|Release]   (default: Debug)
source "$(dirname "$(readlink -f "$0")")/_common.sh"
[[ $# -ge 1 ]] && BUILD_TYPE="$1" && BUILD_DIR="$FIRMWARE_DIR/build/$BUILD_TYPE"

cd "$FIRMWARE_DIR"
if [[ ! -f "$BUILD_DIR/build.ninja" ]]; then
    cmake --preset "$BUILD_TYPE"
fi
cmake --build --preset "$BUILD_TYPE"
