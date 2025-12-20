#!/bin/bash
set -e

echo "=== RosettaPad Updater ==="
echo

# Configuration
INSTALL_DIR="/opt/rosettapad"
SERVICE_NAME="rosettapad"

# Get the directory where this script is located
SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"

# Check for root
if [ "$EUID" -ne 0 ]; then
    echo "Please run as root: sudo ./update.sh"
    exit 1
fi

# Check source directory exists
if [ ! -d "$SCRIPT_DIR/adapter/src" ]; then
    echo "Error: adapter/src directory not found!"
    echo "Run this script from the RosettaPad directory."
    exit 1
fi

# Check for at least main.c
if [ ! -f "$SCRIPT_DIR/adapter/src/main.c" ]; then
    echo "Error: main.c not found in adapter/src/"
    exit 1
fi

# Check if installed
if [ ! -d "$INSTALL_DIR" ]; then
    echo "Error: RosettaPad not installed at $INSTALL_DIR"
    echo "Run install.sh first."
    exit 1
fi

echo "[1/4] Stopping service..."
systemctl stop $SERVICE_NAME 2>/dev/null || echo "  (service wasn't running)"

echo "[2/4] Copying source files..."
cp "$SCRIPT_DIR/adapter/src/"*.c "$INSTALL_DIR/src/"
cp "$SCRIPT_DIR/adapter/include/"*.h "$INSTALL_DIR/include/"

# Copy any other files if they exist
[ -f "$SCRIPT_DIR/README.md" ] && cp "$SCRIPT_DIR/README.md" "$INSTALL_DIR/"

echo "[3/4] Compiling..."
SRC_FILES=$(find "$INSTALL_DIR/src" -name "*.c" | tr '\n' ' ')

if gcc -O2 -Wall -Wextra \
    -I"$INSTALL_DIR/include" \
    -o "$INSTALL_DIR/rosettapad" \
    $SRC_FILES \
    -lpthread; then
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