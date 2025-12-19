#!/bin/bash
set -e

echo "=== RosettaPad Updater ==="
echo

# Check for root
if [ "$EUID" -ne 0 ]; then
    echo "Please run as root: sudo ./update.sh"
    exit 1
fi

# Configuration
INSTALL_DIR="/opt/rosettapad"
SERVICE_NAME="rosettapad"

# Get the directory where this script is located
SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"

# Check for required source files
if [ ! -f "$SCRIPT_DIR/adapter/src/main.c" ] || [ ! -f "$SCRIPT_DIR/adapter/Makefile" ]; then
    echo "Error: Source files not found!"
    echo "Expected structure:"
    echo "  $SCRIPT_DIR/adapter/src/*.c"
    echo "  $SCRIPT_DIR/adapter/include/*.h"
    echo "  $SCRIPT_DIR/adapter/Makefile"
    exit 1
fi

echo "[1/6] Checking dependencies..."
if ! dpkg -s libbluetooth-dev >/dev/null 2>&1; then
    echo "  Installing libbluetooth-dev..."
    apt-get update
    apt-get install -y libbluetooth-dev
fi

echo "[2/6] Stopping service..."
systemctl stop $SERVICE_NAME 2>/dev/null || echo "  (service wasn't running)"

echo "[3/6] Copying source files..."
mkdir -p "$INSTALL_DIR"
mkdir -p /etc/rosettapad

cp "$SCRIPT_DIR"/adapter/src/*.c "$INSTALL_DIR/"
cp "$SCRIPT_DIR"/adapter/include/*.h "$INSTALL_DIR/"

# Create Makefile for flat structure in install directory
cat > "$INSTALL_DIR/Makefile" << 'MAKEFILE'
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

# Copy docs if they exist
[ -f "$SCRIPT_DIR/README.md" ] && cp "$SCRIPT_DIR/README.md" "$INSTALL_DIR/"

# Create default config if it doesn't exist
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
    echo "  Created default config at /etc/rosettapad/config"
fi

echo "[4/6] Compiling (full build with USB + Bluetooth)..."
cd "$INSTALL_DIR"
if make clean && make full; then
    echo "  Compilation successful!"
else
    echo "  Compilation failed!"
    echo "  Attempting to restart with old binary..."
    systemctl start $SERVICE_NAME 2>/dev/null || true
    exit 1
fi

echo "[5/6] Setting permissions..."
chmod +x "$INSTALL_DIR/rosettapad"

echo "[6/6] Starting service..."
if systemctl start $SERVICE_NAME 2>/dev/null; then
    echo "  Service started!"
else
    echo "  Service not installed. Run install.sh first, or start manually:"
    echo "    sudo $INSTALL_DIR/rosettapad"
fi

echo
echo "=== Update Complete ==="
echo
echo "View logs with: sudo journalctl -u $SERVICE_NAME -f"
echo
echo "Current config: /etc/rosettapad/config"
echo "Edit to change mode (usb/bluetooth/auto) or debug level"