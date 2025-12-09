#!/bin/bash
set -e

echo "=== DualSense to PS3 Adapter Installer ==="
echo

if [ "$EUID" -ne 0 ]; then
    echo "Please run as root: sudo ./install.sh"
    exit 1
fi

echo "[1/7] Installing dependencies..."
apt-get update
apt-get install -y build-essential bluez

echo "[2/7] Configuring boot parameters..."

CONFIG="/boot/firmware/config.txt"
[ ! -f "$CONFIG" ] && CONFIG="/boot/config.txt"

if ! grep -q "dtoverlay=dwc2" "$CONFIG"; then
    echo "" >> "$CONFIG"
    echo "# USB Gadget mode for DS3 adapter" >> "$CONFIG"
    echo "dtoverlay=dwc2,dr_mode=peripheral" >> "$CONFIG"
    echo "Added dwc2 overlay to $CONFIG"
fi

CMDLINE="/boot/firmware/cmdline.txt"
[ ! -f "$CMDLINE" ] && CMDLINE="/boot/cmdline.txt"

if ! grep -q "modules-load=dwc2" "$CMDLINE"; then
    sed -i 's/$/ modules-load=dwc2/' "$CMDLINE"
    echo "Added dwc2 module to $CMDLINE"
fi

echo "[3/7] Creating installation directory..."
mkdir -p /opt/ds3-adapter

echo "[4/7] Compiling adapter..."
cp ds3_adapter.c /opt/ds3-adapter/
gcc -O2 -o /opt/ds3-adapter/ds3_adapter /opt/ds3-adapter/ds3_adapter.c -lpthread
chmod +x /opt/ds3-adapter/ds3_adapter

echo "[5/7] Creating symlink..."
ln -sf /opt/ds3-adapter/ds3_adapter /usr/local/bin/ds3-adapter

echo "[6/7] Installing systemd service..."
cat > /etc/systemd/system/ds3-adapter.service << 'SERVICE'
[Unit]
Description=DualSense to PS3 Adapter
After=bluetooth.target
Wants=bluetooth.target

[Service]
Type=simple
ExecStartPre=/sbin/modprobe libcomposite
ExecStartPre=/sbin/modprobe usb_f_fs
ExecStart=/opt/ds3-adapter/ds3_adapter
Restart=always
RestartSec=3
StandardOutput=journal
StandardError=journal

[Install]
WantedBy=multi-user.target
SERVICE

systemctl daemon-reload
systemctl enable ds3-adapter
echo "Service enabled for auto-start on boot"

echo "[7/7] Copying documentation..."
cp README.md /opt/ds3-adapter/

echo
echo "=== Installation Complete ==="
echo
echo "The adapter will start automatically on boot."
echo
echo "Commands:"
echo "  sudo systemctl start ds3-adapter   # Start now"
echo "  sudo systemctl stop ds3-adapter    # Stop"
echo "  sudo systemctl status ds3-adapter  # Check status"
echo "  sudo journalctl -u ds3-adapter -f  # View logs"
echo
echo "REBOOT REQUIRED for USB gadget mode!"
echo "Run: sudo reboot"
