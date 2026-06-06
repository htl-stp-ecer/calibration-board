#!/usr/bin/env bash
# Installiert das gebaute Binary nach /usr/local/bin und richtet einen
# systemd-Service unter /etc/systemd/system/raccoon-calib-bridge.service
# ein.  Idempotent — kann beliebig oft laufen.
set -euo pipefail

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
ROOT_DIR="$(cd "${SCRIPT_DIR}/.." && pwd)"
BUILD_DIR="${BUILD_DIR:-${ROOT_DIR}/build}"
BIN="${BUILD_DIR}/raccoon-calib-bridge"

if [[ ! -x "${BIN}" ]]; then
    echo "✖ Binary fehlt: ${BIN} — vorher scripts/build.sh laufen lassen"
    exit 1
fi

SERVICE_USER="${SERVICE_USER:-${SUDO_USER:-$USER}}"
SERVICE_FILE="/etc/systemd/system/raccoon-calib-bridge.service"
TARGET_BIN="/usr/local/bin/raccoon-calib-bridge"
ENV_FILE="/etc/default/raccoon-calib-bridge"

require_root() {
    if [[ $EUID -ne 0 ]]; then
        echo "▶ Brauche root für Install — re-exec mit sudo…"
        exec sudo -E env "PATH=$PATH" SERVICE_USER="${SERVICE_USER}" \
            BUILD_DIR="${BUILD_DIR}" bash "$0" "$@"
    fi
}
require_root "$@"

# dialout-Gruppe für User sicherstellen (USB-Serial-Zugriff)
if ! id -nG "${SERVICE_USER}" | grep -qw dialout; then
    echo "• Füge ${SERVICE_USER} der dialout-Gruppe hinzu (USB-CDC)"
    usermod -aG dialout "${SERVICE_USER}"
    echo "  ⚠ ${SERVICE_USER} muss sich neu einloggen damit das wirkt"
fi

# Binary deployen
echo "• Installiere ${BIN} → ${TARGET_BIN}"
install -m 0755 "${BIN}" "${TARGET_BIN}"

# Default-Env-File anlegen falls noch nicht da
if [[ ! -f "${ENV_FILE}" ]]; then
    cat > "${ENV_FILE}" <<EOF
# raccoon-calib-bridge — Override hier eintragen, dann:
#   sudo systemctl restart raccoon-calib-bridge
CALIB_BRIDGE_LOG=info
# CALIB_BRIDGE_PORT=/dev/ttyACM0
# CALIB_BRIDGE_TRANSPORT=udpm://239.255.76.67:7667?ttl=0
EOF
    chmod 0644 "${ENV_FILE}"
    echo "• Env-Datei angelegt: ${ENV_FILE}"
fi

# Service-Unit mit eingesetztem User schreiben
echo "• Installiere ${SERVICE_FILE} (User=${SERVICE_USER})"
sed "s|__CALIB_BRIDGE_USER__|${SERVICE_USER}|g" \
    "${ROOT_DIR}/systemd/raccoon-calib-bridge.service" \
    > "${SERVICE_FILE}"

systemctl daemon-reload
systemctl enable raccoon-calib-bridge.service >/dev/null
systemctl restart raccoon-calib-bridge.service

echo
echo "✓ Installation fertig."
echo "  systemctl status   raccoon-calib-bridge"
echo "  journalctl -u      raccoon-calib-bridge -f"
