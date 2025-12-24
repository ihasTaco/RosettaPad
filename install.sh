#!/bin/bash
set -e

echo "=== RosettaPad Installer ==="
echo "  DualSense to PS3 Controller Adapter"
echo

if [ "$EUID" -ne 0 ]; then
    echo "Please run as root: sudo ./install.sh"
    exit 1
fi

# Get the directory where this script is located
SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
INSTALL_DIR="/opt/rosettapad"
SERVICE_NAME="rosettapad"

echo "[1/8] Installing dependencies..."
apt-get update
apt-get install -y build-essential bluez libbluetooth-dev

# Ensure Bluetooth isn't blocked
rfkill unblock bluetooth 2>/dev/null || true

echo "[2/8] Configuring boot parameters..."

CONFIG="/boot/firmware/config.txt"
[ ! -f "$CONFIG" ] && CONFIG="/boot/config.txt"

if ! grep -q "dtoverlay=dwc2" "$CONFIG"; then
    # Insert under [all] section to ensure it applies to all Pi models
    if grep -q "^\[all\]" "$CONFIG"; then
        sed -i '/^\[all\]/a dtoverlay=dwc2,dr_mode=peripheral' "$CONFIG"
    else
        # If no [all] section exists, add it
        echo "" >> "$CONFIG"
        echo "[all]" >> "$CONFIG"
        echo "dtoverlay=dwc2,dr_mode=peripheral" >> "$CONFIG"
    fi
    echo "  Added dwc2 overlay to $CONFIG"
else
    echo "  dwc2 overlay already configured"
fi

CMDLINE="/boot/firmware/cmdline.txt"
[ ! -f "$CMDLINE" ] && CMDLINE="/boot/cmdline.txt"

if ! grep -q "modules-load=dwc2" "$CMDLINE"; then
    sed -i 's/$/ modules-load=dwc2/' "$CMDLINE"
    echo "  Added dwc2 module to $CMDLINE"
else
    echo "  dwc2 module already configured"
fi

echo "[3/8] Creating installation directory..."
mkdir -p "$INSTALL_DIR"
mkdir -p "$INSTALL_DIR/src"
mkdir -p "$INSTALL_DIR/include"
mkdir -p /tmp/rosettapad
mkdir -p /etc/rosettapad

echo "[4/8] Copying source files..."

# Check for source files
if [ ! -d "$SCRIPT_DIR/adapter/src" ]; then
    echo "Error: adapter/src directory not found!"
    echo "Expected structure:"
    echo "  adapter/"
    echo "    src/*.c"
    echo "    include/*.h"
    exit 1
fi

# Copy source files
cp "$SCRIPT_DIR/adapter/src/"*.c "$INSTALL_DIR/src/"
cp "$SCRIPT_DIR/adapter/include/"*.h "$INSTALL_DIR/include/"

echo "  Copied source files to $INSTALL_DIR"

echo "[5/8] Compiling adapter..."

# Compile all .c files together with Bluetooth support
SRC_FILES=$(find "$INSTALL_DIR/src" -name "*.c" | tr '\n' ' ')

gcc -O2 -Wall -Wextra \
    -I"$INSTALL_DIR/include" \
    -o "$INSTALL_DIR/rosettapad" \
    $SRC_FILES \
    -lpthread -lbluetooth

chmod +x "$INSTALL_DIR/rosettapad"
echo "  Compilation successful!"

echo "[6/8] Creating symlink..."
ln -sf "$INSTALL_DIR/rosettapad" /usr/local/bin/rosettapad

echo "[7/8] Installing systemd service..."
cat > /etc/systemd/system/${SERVICE_NAME}.service << 'SERVICE'
[Unit]
Description=RosettaPad - DualSense to PS3 Controller Adapter
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

# Create default config if it doesn't exist
if [ ! -f /etc/rosettapad/config ]; then
    cat > /etc/rosettapad/config << 'CONFIG'
# RosettaPad Configuration
enable_bluetooth=1
enable_motion=1
enable_wake=1
debug_level=0
# ps3_mac will be auto-detected from USB connection
CONFIG
fi

systemctl daemon-reload
systemctl enable ${SERVICE_NAME}
echo "  Service enabled for auto-start on boot"

echo "[8/8] Copying documentation..."
[ -f "$SCRIPT_DIR/README.md" ] && cp "$SCRIPT_DIR/README.md" "$INSTALL_DIR/"

echo
echo "╔══════════════════════════════════════════════════════════════╗"
echo "║              Installation Complete!                          ║"
echo "╠══════════════════════════════════════════════════════════════╣"
echo "║  The adapter will start automatically on boot.               ║"
echo "║                                                              ║"
echo "║  Features:                                                   ║"
echo "║    • USB:       Buttons, sticks, triggers (low latency)      ║"
echo "║    • Bluetooth: Motion controls (SIXAXIS), PS wake           ║"
echo "║                                                              ║"
echo "║  Commands:                                                   ║"
echo "║    sudo systemctl start rosettapad   # Start now             ║"
echo "║    sudo systemctl stop rosettapad    # Stop                  ║"
echo "║    sudo systemctl status rosettapad  # Check status          ║"
echo "║    sudo journalctl -u rosettapad -f  # View logs             ║"
echo "║                                                              ║"
echo "║  REBOOT REQUIRED for USB gadget mode!                        ║"
echo "║  Run: sudo reboot                                            ║"
echo "╚══════════════════════════════════════════════════════════════╝"