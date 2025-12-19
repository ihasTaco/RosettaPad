#!/bin/bash
set -e

if [ "$EUID" -ne 0 ]; then
    echo "Please run as root: sudo ./uninstall.sh"
    exit 1
fi

echo "=== RosettaPad Uninstaller ==="
echo

echo "Stopping service..."
systemctl stop rosettapad 2>/dev/null || true
systemctl disable rosettapad 2>/dev/null || true

echo "Removing files..."
rm -f /etc/systemd/system/rosettapad.service
rm -f /usr/local/bin/rosettapad
rm -rf /opt/rosettapad

echo "Removing config directory..."
read -p "Remove /etc/rosettapad config directory? (y/N): " -n 1 -r
echo
if [[ $REPLY =~ ^[Yy]$ ]]; then
    rm -rf /etc/rosettapad
    echo "Config removed."
else
    echo "Config preserved at /etc/rosettapad/"
fi

systemctl daemon-reload

echo
echo "=== Uninstall Complete ==="
echo
echo "Note: Boot config changes in /boot/firmware/config.txt were not removed."
echo "      (dtoverlay=dwc2 and modules-load=dwc2)"