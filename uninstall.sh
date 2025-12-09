#!/bin/bash
set -e

if [ "$EUID" -ne 0 ]; then
    echo "Please run as root: sudo ./uninstall.sh"
    exit 1
fi

echo "Stopping service..."
systemctl stop ds3-adapter 2>/dev/null || true
systemctl disable ds3-adapter 2>/dev/null || true

echo "Removing files..."
rm -f /etc/systemd/system/ds3-adapter.service
rm -f /usr/local/bin/ds3-adapter
rm -rf /opt/ds3-adapter

systemctl daemon-reload

echo "Uninstalled. Boot config changes in /boot/firmware/config.txt were not removed."
