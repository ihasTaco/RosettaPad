#!/bin/bash
set -e

echo "=== RosettaPad Installer ==="
echo

if [ "$EUID" -ne 0 ]; then
    echo "Please run as root: sudo ./install.sh"
    exit 1
fi

# Get the directory where this script is located
SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"

# Verify source files exist
if [ ! -f "$SCRIPT_DIR/adapter/src/main.c" ] || [ ! -f "$SCRIPT_DIR/adapter/Makefile" ]; then
    echo "Error: Source files not found!"
    echo "Expected structure:"
    echo "  $SCRIPT_DIR/adapter/src/*.c"
    echo "  $SCRIPT_DIR/adapter/include/*.h"
    echo "  $SCRIPT_DIR/adapter/Makefile"
    exit 1
fi

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
    echo "Added dwc2 overlay to $CONFIG"
fi

CMDLINE="/boot/firmware/cmdline.txt"
[ ! -f "$CMDLINE" ] && CMDLINE="/boot/cmdline.txt"

if ! grep -q "modules-load=dwc2" "$CMDLINE"; then
    sed -i 's/$/ modules-load=dwc2/' "$CMDLINE"
    echo "Added dwc2 module to $CMDLINE"
fi

echo "[3/8] Creating installation directory..."
mkdir -p /opt/rosettapad
mkdir -p /etc/rosettapad

echo "[4/8] Copying source files..."
cp "$SCRIPT_DIR"/adapter/src/*.c /opt/rosettapad/
cp "$SCRIPT_DIR"/adapter/include/*.h /opt/rosettapad/

# Create Makefile for flat structure in /opt/rosettapad
cat > /opt/rosettapad/Makefile << 'MAKEFILE'
CC = gcc
CFLAGS = -Wall -Wextra -O2 -pthread
LDFLAGS = -pthread

SRCS = common.c debug.c ds3.c dualsense.c usb_gadget.c bt_hid.c main.c
TARGET = rosettapad

full: CFLAGS += -DENABLE_USB -DENABLE_BLUETOOTH
full: LDFLAGS += -lbluetooth
full: $(TARGET)
	@echo "Built full version (USB + Bluetooth)"

usb: CFLAGS += -DENABLE_USB
usb: $(TARGET)
	@echo "Built USB-only version"

bt: CFLAGS += -DENABLE_BLUETOOTH
bt: LDFLAGS += -lbluetooth
bt: $(TARGET)
	@echo "Built Bluetooth-only version"

$(TARGET): $(SRCS)
	$(CC) $(CFLAGS) -o $@ $^ $(LDFLAGS)

clean:
	rm -f $(TARGET)

.PHONY: full usb bt clean
MAKEFILE

echo "[5/8] Compiling adapter (full build with USB + Bluetooth)..."
cd /opt/rosettapad
make clean
make full
chmod +x /opt/rosettapad/rosettapad

echo "[6/8] Creating symlink..."
ln -sf /opt/rosettapad/rosettapad /usr/local/bin/rosettapad

echo "[7/8] Creating default config..."
if [ ! -f /etc/rosettapad/config ]; then
    cat > /etc/rosettapad/config << 'CONFIG'
# RosettaPad Configuration
# MODE: usb, bluetooth, or auto (recommended)
# auto = USB until paired, then Bluetooth
MODE=auto

# DEBUG: comma-separated categories
# Options: error,warn,info,init,usb,bt,handshake,pairing,motion,input,reports,rumble,all,none
DEBUG=error,warn,info
CONFIG
    echo "Created default config at /etc/rosettapad/config"
fi

echo "[8/8] Installing systemd service..."
cat > /etc/systemd/system/rosettapad.service << 'SERVICE'
[Unit]
Description=RosettaPad - DualSense to PS3 Controller Adapter
After=bluetooth.target
Wants=bluetooth.target

[Service]
Type=simple
ExecStartPre=/sbin/modprobe libcomposite
ExecStartPre=/sbin/modprobe usb_f_fs
ExecStartPre=-/usr/sbin/rfkill unblock bluetooth
ExecStart=/opt/rosettapad/rosettapad
Restart=always
RestartSec=3
StandardOutput=journal
StandardError=journal

# Optional: Set mode/debug via environment instead of config file
#Environment="ROSETTAPAD_MODE=auto"
#Environment="ROSETTAPAD_DEBUG=error,warn,info"

[Install]
WantedBy=multi-user.target
SERVICE

systemctl daemon-reload
systemctl enable rosettapad
echo "Service enabled for auto-start on boot"

echo
echo "=== Installation Complete ==="
echo
echo "The adapter will start automatically on boot."
echo
echo "Configuration:"
echo "  Edit /etc/rosettapad/config to change mode (usb/bluetooth/auto) and debug level"
echo
echo "Commands:"
echo "  sudo systemctl start rosettapad   # Start now"
echo "  sudo systemctl stop rosettapad    # Stop"
echo "  sudo systemctl status rosettapad  # Check status"
echo "  sudo journalctl -u rosettapad -f  # View logs"
echo
echo "CLI options (for manual testing):"
echo "  rosettapad --usb              # USB mode"
echo "  rosettapad --bluetooth        # Bluetooth mode"
echo "  rosettapad --auto             # Auto mode (default)"
echo "  rosettapad --debug all        # Full debug output"
echo "  rosettapad --list-debug       # Show debug categories"
echo
echo "REBOOT REQUIRED for USB gadget mode!"
echo "Run: sudo reboot"