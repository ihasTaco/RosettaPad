#!/bin/sh
set -eu

echo "=== RosettaPad Updater ==="
echo

INSTALL_DIR="/opt/rosettapad"
SERVICE_NAME="rosettapad"
SCRIPT_DIR="$(cd "$(dirname "$0")" && pwd)"

# Requires root (writes to /opt, controls the service)
if [ "$(id -u)" -ne 0 ]; then
    echo "Please run as root: sudo ./update.sh"
    exit 1
fi

if [ ! -d "$SCRIPT_DIR/adapter/src" ]; then
    echo "Error: adapter/src directory not found!"
    exit 1
fi

if [ ! -d "$INSTALL_DIR" ]; then
    echo "Error: RosettaPad not installed at $INSTALL_DIR"
    echo "Run the installer for your OS first (install_debian.sh or install_alpine.sh)."
    exit 1
fi

# Detect init system (systemd or openrc)
if command -v systemctl >/dev/null 2>&1; then
    INIT_SYSTEM="systemd"
elif command -v rc-service >/dev/null 2>&1; then
    INIT_SYSTEM="openrc"
else
    echo "Error: no supported service manager found (need systemctl or rc-service)."
    echo "Update the files manually, then restart the rosettapad service."
    exit 1
fi

echo "Detected init system: $INIT_SYSTEM"

echo "[1/4] Stopping service..."
if [ "$INIT_SYSTEM" = "systemd" ]; then
    systemctl stop "$SERVICE_NAME" 2>/dev/null || echo "  (service wasn't running)"
else
    rc-service "$SERVICE_NAME" stop 2>/dev/null || echo "  (service wasn't running)"
fi

echo "[2/4] Copying source files..."
rm -rf "$INSTALL_DIR/src" "$INSTALL_DIR/include" || true
cp -r "$SCRIPT_DIR/adapter/src" "$INSTALL_DIR/"
cp -r "$SCRIPT_DIR/adapter/include" "$INSTALL_DIR/" 2>/dev/null || true
cp "$SCRIPT_DIR/adapter/Makefile" "$INSTALL_DIR/" 2>/dev/null || true
[ -f "$SCRIPT_DIR/README.md" ] && cp "$SCRIPT_DIR/README.md" "$INSTALL_DIR/" || true

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
if [ "$INIT_SYSTEM" = "systemd" ]; then
    systemctl start "$SERVICE_NAME" || echo "  systemctl start returned non-zero"
else
    rc-service "$SERVICE_NAME" start || echo "  rc-service start returned non-zero"
fi

echo
echo "=== Update Complete ==="
echo
if [ "$INIT_SYSTEM" = "systemd" ]; then
    echo "View logs with: journalctl -u $SERVICE_NAME -f"
else
    echo "Service control: rc-service $SERVICE_NAME start|stop|status"
    echo "Logs are typically in /var/log/messages (install a syslog daemon like busybox-syslogd or syslog-ng to capture stdout/stderr)."
fi