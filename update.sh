#!/bin/bash
set -e

echo "=== RosettaPad Updater ==="
echo

INSTALL_DIR="/opt/rosettapad"
SERVICE_NAME="rosettapad"
SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"

if [ "$EUID" -ne 0 ]; then
    echo "Please run as root: sudo ./update.sh"
    exit 1
fi

if [ ! -d "$SCRIPT_DIR/adapter/src" ]; then
    echo "Error: adapter/src directory not found!"
    exit 1
fi

if [ ! -d "$INSTALL_DIR" ]; then
    echo "Error: RosettaPad not installed at $INSTALL_DIR"
    echo "Run install.sh first."
    exit 1
fi

echo "[1/4] Stopping service..."
systemctl stop $SERVICE_NAME 2>/dev/null || echo "  (service wasn't running)"

echo "[2/4] Copying source files..."
# Clean old files and copy new
rm -rf "$INSTALL_DIR/src" "$INSTALL_DIR/include"
cp -r "$SCRIPT_DIR/adapter/src" "$INSTALL_DIR/"
cp -r "$SCRIPT_DIR/adapter/include" "$INSTALL_DIR/"
cp "$SCRIPT_DIR/adapter/Makefile" "$INSTALL_DIR/" 2>/dev/null || true

[ -f "$SCRIPT_DIR/README.md" ] && cp "$SCRIPT_DIR/README.md" "$INSTALL_DIR/"

echo "[3/4] Compiling..."
cd "$INSTALL_DIR"

if make clean all; then
    echo "  Compilation successful!"
else
    echo "  Compilation failed!"
    echo "  Service not restarted."
    exit 1
fi

echo "[4/4] Starting service..."
systemctl start $SERVICE_NAME

echo
echo "=== Update Complete ==="
echo
echo "View logs with: sudo journalctl -u $SERVICE_NAME -f"