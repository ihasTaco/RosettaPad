#!/bin/bash
set -e

SERVICE_NAME="rosettapad"
INSTALL_DIR="/opt/rosettapad"

if [ "$EUID" -ne 0 ]; then
    echo "Please run as root: sudo ./uninstall.sh"
    exit 1
fi

echo "=== RosettaPad Uninstaller ==="
echo

echo "[1/4] Stopping service..."
systemctl stop $SERVICE_NAME 2>/dev/null || true
systemctl disable $SERVICE_NAME 2>/dev/null || true

echo "[2/4] Removing service..."
rm -f /etc/systemd/system/${SERVICE_NAME}.service
systemctl daemon-reload

echo "[3/4] Removing symlink..."
rm -f /usr/local/bin/rosettapad

echo "[4/4] Removing installation directory..."
rm -rf "$INSTALL_DIR"

echo
echo "=== Uninstall Complete ==="
echo
echo "Note: Boot config changes were NOT removed."
echo "To disable USB gadget mode, manually remove from config.txt:"
echo "  - dtoverlay=dwc2,dr_mode=peripheral"
echo "And from cmdline.txt:"
echo "  - modules-load=dwc2"