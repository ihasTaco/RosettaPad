#!/bin/sh
set -eu

echo "=== RosettaPad Updater ==="
echo

INSTALL_DIR="/opt/rosettapad"
SERVICE_NAME="rosettapad"
SCRIPT_DIR="$(cd "$(dirname "$0")" && pwd)"

if [ ! -d "$SCRIPT_DIR/adapter/src" ]; then
    echo "Error: adapter/src directory not found!"
    exit 1
fi

if [ ! -d "$INSTALL_DIR" ]; then
    echo "Error: RosettaPad not installed at $INSTALL_DIR"
    echo "Run install.sh first."
    exit 1
fi

# Detect OS (only support ID=alpine or ID=debian)
OS_ID=""
if [ -f /etc/os-release ]; then
    # shellcheck disable=SC1091
    . /etc/os-release
    OS_ID="${ID:-}"
fi

INIT_SYSTEM=""

if [ "${OS_ID}" = "alpine" ]; then
    INIT_SYSTEM="openrc"
elif [ "${OS_ID}" = "debian" ]; then
    INIT_SYSTEM="systemd"
else
    echo "Unsupported OS: ${OS_ID:-unknown}"
    echo "This updater supports only Alpine (ID=alpine) and Debian (ID=debian)."
    exit 1
fi

echo "Detected OS: ${OS_ID:-unknown}, using init: $INIT_SYSTEM"

echo "[1/4] Stopping service..."
if [ "$INIT_SYSTEM" = "systemd" ]; then
    if command -v systemctl >/dev/null 2>&1; then
        systemctl stop "$SERVICE_NAME" 2>/dev/null || echo "  (service wasn't running or systemctl failed)"
    else
        echo "  systemctl not available; skipping stop"
    fi
else
    # openrc
    if command -v rc-service >/dev/null 2>&1; then
        rc-service "$SERVICE_NAME" stop 2>/dev/null || echo "  (service wasn't running or rc-service failed)"
    else
        echo "  rc-service not available; skipping stop"
    fi
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
    if command -v systemctl >/dev/null 2>&1; then
        systemctl start "$SERVICE_NAME" || echo "  systemctl start returned non-zero"
    else
        echo "  systemctl not available; start the service manually: systemctl start $SERVICE_NAME"
    fi
else
    if command -v rc-service >/dev/null 2>&1; then
        rc-service "$SERVICE_NAME" start 2>/dev/null || echo "  rc-service start returned non-zero"
    else
        echo "  rc-service not available; start the service manually: rc-service $SERVICE_NAME start"
    fi
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
