#!/bin/bash
set -e

echo "=== RosettaPad Installer ==="
echo "  Universal Controller Adapter"
echo

if [ "$EUID" -ne 0 ]; then
    echo "Please run as root: sudo ./install.sh"
    exit 1
fi

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
INSTALL_DIR="/opt/rosettapad"
SERVICE_NAME="rosettapad"

echo "[1/8] Installing dependencies..."
apt-get update
apt-get install -y build-essential bluez libbluetooth-dev pkg-config

rfkill unblock bluetooth 2>/dev/null || true

echo "[2/8] Configuring boot parameters..."

CONFIG="/boot/firmware/config.txt"
[ ! -f "$CONFIG" ] && CONFIG="/boot/config.txt"

# Remove any existing dwc2 overlay (might be wrong mode)
sed -i '/dtoverlay=dwc2/d' "$CONFIG"

# Add dwc2 in peripheral mode
if grep -q "^\[all\]" "$CONFIG"; then
    sed -i '/^\[all\]/a dtoverlay=dwc2,dr_mode=peripheral' "$CONFIG"
else
    echo "" >> "$CONFIG"
    echo "[all]" >> "$CONFIG"
    echo "dtoverlay=dwc2,dr_mode=peripheral" >> "$CONFIG"
fi
echo "  Configured dwc2 overlay (peripheral mode)"

CMDLINE="/boot/firmware/cmdline.txt"
[ ! -f "$CMDLINE" ] && CMDLINE="/boot/cmdline.txt"

if ! grep -q "modules-load=dwc2" "$CMDLINE"; then
    sed -i 's/$/ modules-load=dwc2/' "$CMDLINE"
    echo "  Added dwc2 module"
else
    echo "  dwc2 module already configured"
fi

echo "[3/8] Creating installation directory..."
rm -rf "$INSTALL_DIR"
mkdir -p "$INSTALL_DIR"
mkdir -p /tmp/rosettapad
mkdir -p /etc/rosettapad

echo "[4/8] Copying source files..."

if [ ! -d "$SCRIPT_DIR/adapter/src" ]; then
    echo "Error: adapter/src directory not found!"
    echo "Make sure you're running from the RosettaPad directory."
    exit 1
fi

# Copy entire adapter directory (preserves structure)
cp -r "$SCRIPT_DIR/adapter/"* "$INSTALL_DIR/"

echo "  Copied source files to $INSTALL_DIR"

echo "[5/8] Compiling..."

cd "$INSTALL_DIR"
if make clean all; then
    echo "  Compilation successful!"
else
    echo "  Compilation failed!"
    exit 1
fi

echo "[6/8] Creating symlink..."
ln -sf "$INSTALL_DIR/rosettapad" /usr/local/bin/rosettapad

echo "[7/8] Installing systemd service..."
cat > /etc/systemd/system/${SERVICE_NAME}.service << 'SERVICE'
[Unit]
Description=RosettaPad - Universal Controller Adapter
After=bluetooth.target
Wants=bluetooth.target

[Service]
Type=simple
ExecStartPre=/sbin/modprobe libcomposite
ExecStartPre=/sbin/modprobe usb_f_fs
ExecStartPre=/bin/sh -c '/usr/sbin/rfkill unblock bluetooth 2>/dev/null || true'
ExecStart=/opt/rosettapad/rosettapad
Restart=always
RestartSec=3
StandardOutput=journal
StandardError=journal

[Install]
WantedBy=multi-user.target
SERVICE

if [ ! -f /etc/rosettapad/config ]; then
    cat > /etc/rosettapad/config << 'CONFIG'
# RosettaPad Configuration
enable_bluetooth=1
enable_motion=1
enable_wake=1
debug_level=0
CONFIG
fi

systemctl daemon-reload
systemctl enable ${SERVICE_NAME}

echo "[8/8] Done!"

echo
echo "╔════════════════════════════════════════════════════════════════╗"
echo "║              Installation Complete!                            ║"
echo "╠════════════════════════════════════════════════════════════════╣"
echo "║  The adapter will start automatically on boot.                 ║"
echo "║                                                                ║"
echo "║  Supported Controllers:                                        ║"
echo "║    • DualSense (PS5)                                           ║"
echo "║                                                                ║"
echo "║  Commands:                                                     ║"
echo "║    sudo systemctl start rosettapad                             ║"
echo "║    sudo systemctl stop rosettapad                              ║"
echo "║    sudo systemctl status rosettapad                            ║"
echo "║    sudo journalctl -u rosettapad -f                            ║"
echo "╚════════════════════════════════════════════════════════════════╝"
echo

# Prompt for reboot
read -p "Reboot now? (required for USB gadget mode) [Y/n] " -n 1 -r
echo
if [[ ! $REPLY =~ ^[Nn]$ ]]; then
    echo "Rebooting..."
    reboot
fi