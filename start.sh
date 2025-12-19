#!/bin/bash
# Quick start without full install - for testing

set -e

# Get the directory where this script is located
SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
ADAPTER_DIR="$SCRIPT_DIR/adapter"

# Verify structure
if [ ! -d "$ADAPTER_DIR/src" ] || [ ! -d "$ADAPTER_DIR/include" ]; then
    echo "Error: Expected adapter/src and adapter/include directories"
    exit 1
fi

cd "$ADAPTER_DIR"

# Load required modules
sudo modprobe libcomposite 2>/dev/null || true
sudo modprobe usb_f_fs 2>/dev/null || true
sudo rfkill unblock bluetooth 2>/dev/null || true

# Build if needed or if source is newer than binary
BUILD_NEEDED=0
if [ ! -f ./rosettapad ]; then
    BUILD_NEEDED=1
else
    # Check if any source file is newer than the binary
    for src in src/*.c include/*.h; do
        if [ "$src" -nt ./rosettapad ]; then
            BUILD_NEEDED=1
            break
        fi
    done
fi

if [ "$BUILD_NEEDED" -eq 1 ]; then
    echo "Building RosettaPad..."
    make clean 2>/dev/null || true
    make full
fi

echo
echo "Starting RosettaPad..."
echo "Press Ctrl+C to stop"
echo

# Run with optional arguments passed through
# Examples:
#   ./start.sh                     # Auto mode (default)
#   ./start.sh -u                  # USB mode
#   ./start.sh -b                  # Bluetooth mode
#   ./start.sh -d all              # Auto mode with full debug
#   ./start.sh -b -d bt,pairing    # Bluetooth mode with BT debug
sudo ./rosettapad "$@"