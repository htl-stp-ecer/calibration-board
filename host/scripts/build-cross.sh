#!/usr/bin/env bash
# Cross-compile für aarch64-linux-gnu (Pi 4/5 mit Debian Trixie).
#
# Strategie: Docker-Image mit Debian-Trixie + Cross-Toolchain.  Das stellt
# sicher dass die produzierte Binary genau zur glibc auf dem Pi passt.
# Nativer Cross-Compile auf einem Ubuntu-Host würde Symbole gegen die
# Ubuntu-glibc linken und auf Debian "GLIBC_X.Y not found" werfen.
#
# Inspiration: raccoon-lib/Dockerfile.cross + ähnliche Pattern.
set -euo pipefail

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
ROOT_DIR="$(cd "${SCRIPT_DIR}/.." && pwd)"

IMAGE_NAME="${IMAGE_NAME:-calib-bridge-cross:trixie}"
BUILD_DIR="${BUILD_DIR:-${ROOT_DIR}/build-aarch64}"
CMAKE_BUILD_TYPE="${CMAKE_BUILD_TYPE:-Release}"
CCACHE_VOL="${CCACHE_VOL:-calib-bridge-ccache-aarch64}"
REBUILD_IMAGE="${REBUILD_IMAGE:-0}"
FORCE_RECONFIGURE="${FORCE_RECONFIGURE:-0}"
JOBS="${JOBS:-$(nproc 2>/dev/null || echo 4)}"

echo "▶ Cross-compiling raccoon-calib-bridge for aarch64 (Debian Trixie)"
echo "  build dir : ${BUILD_DIR}"
echo "  image     : ${IMAGE_NAME}"
echo "  ccache    : ${CCACHE_VOL}"
echo "  jobs      : ${JOBS}"

# Submodule absichern.
if [[ ! -f "${ROOT_DIR}/third_party/raccoon-transport/CMakeLists.txt" ]]; then
    echo "• Initialisiere raccoon-transport submodule…"
    (cd "${ROOT_DIR}" && git submodule update --init --recursive)
fi

# Image bauen wenn nicht vorhanden.
if ! docker image inspect "${IMAGE_NAME}" >/dev/null 2>&1 || [[ "${REBUILD_IMAGE}" == "1" ]]; then
    echo "• Building ${IMAGE_NAME}…"
    docker build -t "${IMAGE_NAME}" -f "${ROOT_DIR}/Dockerfile.cross" "${ROOT_DIR}"
fi

docker volume create "${CCACHE_VOL}" >/dev/null

mkdir -p "${BUILD_DIR}"

# Wenn Reconfigure gewünscht: Build-Dir leeren.
if [[ "${FORCE_RECONFIGURE}" == "1" ]]; then
    echo "• Wiping ${BUILD_DIR} for fresh configure"
    rm -rf "${BUILD_DIR:?}/"*
fi

# Configure (nur wenn CMakeCache.txt fehlt).
docker_run() {
    # ccache wäre nett, aber das Docker-Volume gehört root und wir
     # laufen mit Host-UID → Permission-Denied beim Schreiben.
     # Workaround: ccache in einem User-besessenen Bind-Mount unter
     # ${BUILD_DIR}/.ccache parken — das gehört uns garantiert.
    docker run --rm \
        -v "${ROOT_DIR}":/src \
        -v "${BUILD_DIR}/.ccache":/ccache \
        -w /src \
        -u "$(id -u):$(id -g)" \
        "${IMAGE_NAME}" \
        bash -c "$*"
}

mkdir -p "${BUILD_DIR}/.ccache"

if [[ ! -f "${BUILD_DIR}/CMakeCache.txt" ]]; then
    echo "• Configuring (${CMAKE_BUILD_TYPE})"
    docker_run "cmake -S /src -B /src/$(basename "${BUILD_DIR}") -G Ninja \
        -DCMAKE_TOOLCHAIN_FILE=/src/cmake/toolchain-aarch64-linux-gnu.cmake \
        -DCMAKE_BUILD_TYPE=${CMAKE_BUILD_TYPE} \
        -DCMAKE_C_COMPILER_LAUNCHER=ccache \
        -DCMAKE_CXX_COMPILER_LAUNCHER=ccache"
fi

echo "• Building (ninja -j${JOBS})"
docker_run "cmake --build /src/$(basename "${BUILD_DIR}") -j${JOBS}"

BIN="${BUILD_DIR}/raccoon-calib-bridge"
if [[ ! -x "${BIN}" ]]; then
    echo "✖ Build done but no binary at ${BIN}"
    exit 1
fi

echo
echo "✓ ${BIN}"
file "${BIN}" || true
aarch64-linux-gnu-strip -p --strip-unneeded "${BIN}" 2>/dev/null || true
echo "  size: $(stat -c%s "${BIN}") bytes"
