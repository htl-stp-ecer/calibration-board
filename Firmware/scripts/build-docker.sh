#!/usr/bin/env bash
# Build the firmware inside a Docker container. No host arm-none-eabi-gcc required.
# Artifacts land in Firmware/build/<BUILD_TYPE>/ on the host (owned by you, not root).
# Usage: build-docker.sh [Debug|Release]
source "$(dirname "$(readlink -f "$0")")/_common.sh"
[[ $# -ge 1 ]] && BUILD_TYPE="$1"

IMAGE="${BUILD_DOCKER_IMAGE:-raccoon-calibration-firmware:latest}"

if ! docker image inspect "$IMAGE" >/dev/null 2>&1; then
    echo ">>> Building Docker image $IMAGE (one-time, ~2 min)"
    docker build -t "$IMAGE" "$FIRMWARE_DIR"
fi

echo ">>> Building firmware ($BUILD_TYPE) in container"
# Mount at the same absolute path inside the container as on the host so the
# paths CMake bakes into CMakeCache.txt are valid for both docker and native
# builds — you can alternate between build-docker.sh and build.sh freely.
docker run --rm \
    --user "$(id -u):$(id -g)" \
    -v "$FIRMWARE_DIR":"$FIRMWARE_DIR" \
    -w "$FIRMWARE_DIR" \
    "$IMAGE" \
    bash -c "cmake --preset $BUILD_TYPE && cmake --build --preset $BUILD_TYPE"
