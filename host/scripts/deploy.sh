#!/usr/bin/env bash
# Deploy auf einen Raspberry-Pi (oder beliebigen aarch64-Host).
#
# Schritte:
#   1. Cross-Build wenn noch nicht da (oder REBUILD=1)
#   2. scp Binary + systemd-Unit + Default-Env
#   3. ssh: install nach /usr/local/bin, Service enablen, restarten
#
# Voraussetzungen am Pi: passwordless sudo für den User (oder du gibst
# das Passwort interaktiv ein), dialout-Gruppe vorhanden.
set -euo pipefail

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
ROOT_DIR="$(cd "${SCRIPT_DIR}/.." && pwd)"

# ── Konfiguration ──────────────────────────────────────────────────────
PI_HOST="${PI_HOST:-${1:-pi@192.168.100.112}}"
BUILD_DIR="${BUILD_DIR:-${ROOT_DIR}/build-aarch64}"
BIN="${BUILD_DIR}/raccoon-calib-bridge"
SERVICE_NAME="raccoon-calib-bridge"
REMOTE_TMP="/tmp/${SERVICE_NAME}-deploy.$$"
REBUILD="${REBUILD:-0}"

# ── Build sicherstellen ────────────────────────────────────────────────
if [[ "${REBUILD}" == "1" || ! -x "${BIN}" ]]; then
    echo "▶ Building (${BIN} missing or REBUILD=1)"
    "${SCRIPT_DIR}/build-cross.sh"
fi

if [[ ! -x "${BIN}" ]]; then
    echo "✖ Binary fehlt nach Build: ${BIN}"
    exit 1
fi

# ── Sanity-Check: ist Binary wirklich aarch64? ─────────────────────────
if ! file "${BIN}" | grep -q "aarch64"; then
    echo "✖ ${BIN} ist nicht aarch64 — Build-Toolchain falsch?"
    file "${BIN}"
    exit 1
fi

# ── User auf dem Pi ableiten ───────────────────────────────────────────
PI_USER="${PI_HOST%@*}"
[[ "${PI_USER}" == "${PI_HOST}" ]] && PI_USER="$(whoami)"

echo "▶ Deploying to ${PI_HOST} (service user: ${PI_USER})"

# ── Verbindung testen ──────────────────────────────────────────────────
if ! ssh -o ConnectTimeout=5 -o BatchMode=no "${PI_HOST}" "uname -m" >/dev/null 2>&1; then
    echo "✖ Kann ${PI_HOST} nicht erreichen.  SSH-Key eingerichtet?"
    exit 1
fi

# ── Service-Unit mit User-Substitution rendern ─────────────────────────
RENDERED_SERVICE="$(mktemp)"
trap 'rm -f "${RENDERED_SERVICE}"' EXIT
sed "s|__CALIB_BRIDGE_USER__|${PI_USER}|g" \
    "${ROOT_DIR}/systemd/${SERVICE_NAME}.service" > "${RENDERED_SERVICE}"

# ── Upload ─────────────────────────────────────────────────────────────
echo "• Uploading…"
ssh "${PI_HOST}" "mkdir -p ${REMOTE_TMP}"
scp -q \
    "${BIN}" \
    "${RENDERED_SERVICE}" \
    "${PI_HOST}:${REMOTE_TMP}/"

ssh "${PI_HOST}" "mv ${REMOTE_TMP}/$(basename "${RENDERED_SERVICE}") ${REMOTE_TMP}/${SERVICE_NAME}.service"

# ── Remote install ────────────────────────────────────────────────────
echo "• Installing on ${PI_HOST}…"
ssh "${PI_HOST}" "bash -s" <<EOF
set -euo pipefail

# dialout-Membership für USB-Serial
if ! id -nG "${PI_USER}" | grep -qw dialout; then
    echo "  ⚠ Adding ${PI_USER} to dialout group — re-login required for change"
    sudo usermod -aG dialout "${PI_USER}"
fi

sudo install -m 0755 ${REMOTE_TMP}/raccoon-calib-bridge /usr/local/bin/raccoon-calib-bridge
sudo install -m 0644 ${REMOTE_TMP}/${SERVICE_NAME}.service /etc/systemd/system/${SERVICE_NAME}.service

# Default-Env nur anlegen wenn noch nicht da (User-Edits bewahren).
if [[ ! -f /etc/default/${SERVICE_NAME} ]]; then
    sudo tee /etc/default/${SERVICE_NAME} >/dev/null <<DEFAULT
CALIB_BRIDGE_LOG=info
# CALIB_BRIDGE_PORT=/dev/ttyACM0
# CALIB_BRIDGE_TRANSPORT=udpm://239.255.76.67:7667?ttl=0
DEFAULT
fi

sudo systemctl daemon-reload
sudo systemctl enable ${SERVICE_NAME}.service
sudo systemctl restart ${SERVICE_NAME}.service
sleep 1
sudo systemctl --no-pager status ${SERVICE_NAME}.service | head -15

rm -rf ${REMOTE_TMP}
EOF

echo
echo "✓ Deployed."
echo "  ssh ${PI_HOST} 'journalctl -u ${SERVICE_NAME} -f'"
echo "  ssh ${PI_HOST} 'sudo systemctl status ${SERVICE_NAME}'"
