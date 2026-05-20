# Builder image for the STM32F722 calibration-board firmware.
# Contains only what is needed to produce Firmware.elf — no flash/debug tooling.
# Flashing happens host-side via Firmware/scripts/flash.sh (needs ST-LINK USB access).
#
# Build:  docker build -t raccoon-calibration-firmware:latest Firmware/
# Use:    Firmware/scripts/build-docker.sh [Debug|Release]
FROM debian:trixie-slim

RUN apt-get update && apt-get install -y --no-install-recommends \
        gcc-arm-none-eabi \
        libnewlib-arm-none-eabi \
        libstdc++-arm-none-eabi-newlib \
        cmake \
        ninja-build \
        ca-certificates \
    && rm -rf /var/lib/apt/lists/*

# Source is mounted at /workspace at runtime; nothing copied here so the image
# stays small (~400 MB) and rebuilds aren't triggered by source changes.
WORKDIR /workspace

# Avoid writes to /root when running as host UID via --user.
ENV HOME=/tmp
