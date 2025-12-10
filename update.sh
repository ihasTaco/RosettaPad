#!/bin/bash
set -e

# Configuration
# Use SUDO_USER to get the actual user's home directory when running with sudo
if [ -n "$SUDO_USER" ]; then
    USER_HOME=$(getent passwd "$SUDO_USER" | cut -d: -f6)
else
    USER_HOME="$HOME"
fi
SOURCE_DIR="$USER_HOME/OpenPad"
INSTALL_DIR="/opt/ds3-adapter"
SERVICE_NAME="ds3-adapter"

echo "=== OpenPad Updater ==="
echo

# Check for root
if [ "$EUID" -ne 0 ]; then
    echo "Please run as root: sudo ./update.sh"
    exit 1
fi

# Check source directory exists
if [ ! -d "$SOURCE_DIR" ]; then
    echo "Error: Source directory $SOURCE_DIR not found!"
    echo "Create it and place your updated files there."
    exit 1
fi

# Check for source file
if [ ! -f "$SOURCE_DIR/ds3_adapter.c" ]; then
    echo "Error: ds3_adapter.c not found in $SOURCE_DIR"
    exit 1
fi

echo "[1/4] Stopping service..."
systemctl stop $SERVICE_NAME 2>/dev/null || echo "  (service wasn't running)"

echo "[2/4] Copying source files..."
cp "$SOURCE_DIR/ds3_adapter.c" "$INSTALL_DIR/"

# Copy any other files if they exist
[ -f "$SOURCE_DIR/README.md" ] && cp "$SOURCE_DIR/README.md" "$INSTALL_DIR/"

echo "[3/4] Compiling..."
if gcc -O2 -o "$INSTALL_DIR/ds3_adapter" "$INSTALL_DIR/ds3_adapter.c" -lpthread; then
    echo "  Compilation successful!"
else
    echo "  Compilation failed!"
    exit 1
fi

echo "[4/4] Starting service..."
systemctl start $SERVICE_NAME

echo
echo "=== Update Complete ==="
echo
echo "View logs with: sudo journalctl -u $SERVICE_NAME -f"