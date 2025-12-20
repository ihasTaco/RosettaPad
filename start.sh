#!/bin/bash
# Quick start without full install - for development/testing
set -e

# Get the directory where this script is located
SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
BUILD_DIR="$SCRIPT_DIR/build"

echo "=== RosettaPad Quick Start ==="
echo

# Check for source files
if [ ! -d "$SCRIPT_DIR/adapter/src" ]; then
    echo "Error: adapter/src directory not found!"
    exit 1
fi

# Create build directory
mkdir -p "$BUILD_DIR"

echo "[1/3] Loading kernel modules..."
sudo modprobe libcomposite
sudo modprobe usb_f_fs

echo "[2/3] Compiling..."
SRC_FILES=$(find "$SCRIPT_DIR/adapter/src" -name "*.c" | tr '\n' ' ')

gcc -O2 -Wall -Wextra -g \
    -I"$SCRIPT_DIR/adapter/include" \
    -o "$BUILD_DIR/rosettapad" \
    $SRC_FILES \
    -lpthread

echo "  Compiled to $BUILD_DIR/rosettapad"

echo "[3/3] Starting adapter..."
echo
sudo "$BUILD_DIR/rosettapad"