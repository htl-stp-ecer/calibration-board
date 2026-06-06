#!/usr/bin/env bash
# Lokaler Native-Build der Calib-Bridge.  Für Cross-Compile (z. B. auf Pi)
# später wie stm32-data-reader/build.sh ein Docker-Wrapper drumherum
# packen — bisher noch nicht nötig, läuft direkt auf dem Dev-Host.
set -euo pipefail

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
ROOT_DIR="$(cd "${SCRIPT_DIR}/.." && pwd)"

BUILD_DIR="${BUILD_DIR:-${ROOT_DIR}/build}"
CMAKE_BUILD_TYPE="${CMAKE_BUILD_TYPE:-Release}"
JOBS="${JOBS:-$(nproc 2>/dev/null || echo 4)}"

echo "▶ Building raccoon-calib-bridge (${CMAKE_BUILD_TYPE}) → ${BUILD_DIR}"

# Submodule prüfen — beim ersten Build vergessen Leute das gerne.
if [[ ! -f "${ROOT_DIR}/third_party/raccoon-transport/CMakeLists.txt" ]]; then
    if [[ -d "${ROOT_DIR}/../../raccoon-transport" ]]; then
        echo "• raccoon-transport submodule fehlt — verwende Schwester-Verzeichnis"
    else
        echo "• Initialisiere raccoon-transport submodule…"
        (cd "${ROOT_DIR}" && git submodule update --init --recursive)
    fi
fi

mkdir -p "${BUILD_DIR}"
cmake -S "${ROOT_DIR}" -B "${BUILD_DIR}" -G Ninja \
    -DCMAKE_BUILD_TYPE="${CMAKE_BUILD_TYPE}" \
    -DCALIB_BRIDGE_WERROR="${CALIB_BRIDGE_WERROR:-OFF}"
cmake --build "${BUILD_DIR}" -j"${JOBS}"

BIN="${BUILD_DIR}/raccoon-calib-bridge"
if [[ ! -x "${BIN}" ]]; then
    echo "✖ Build fertig aber keine Binary: ${BIN}"
    exit 1
fi
echo "✓ ${BIN}"
file "${BIN}" || true
