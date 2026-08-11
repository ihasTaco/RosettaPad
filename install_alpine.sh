#!/bin/sh
set -eu

echo "=== RosettaPad Alpine Installer ==="
echo "  Universal Controller Adapter"
echo

SCRIPT_DIR="$(cd "$(dirname "$0")" && pwd)"
INSTALL_DIR="/opt/rosettapad"
SERVICE_NAME="rosettapad"

echo "[1/8] Installing dependencies (apk)..."
apk update
apk add --no-cache build-base linux-headers bluez bluez-dev pkgconf dbus dbus-openrc

rfkill unblock bluetooth 2>/dev/null || true

echo "[2/8] Configuring boot parameters..."

CONFIG="/boot/firmware/config.txt"
[ ! -f "$CONFIG" ] && CONFIG="/boot/config.txt"

# backup config if present
if [ -f "$CONFIG" ]; then
    cp "$CONFIG" "${CONFIG}.bak" || true
    sed -i '/dtoverlay=dwc2/d' "$CONFIG"
    if grep -q "^\[all\]" "$CONFIG"; then
        sed -i '/^\[all\]/a dtoverlay=dwc2,dr_mode=peripheral' "$CONFIG"
    else
        printf '\n[all]\ndtoverlay=dwc2,dr_mode=peripheral\n' >> "$CONFIG"
    fi
    echo "  Configured dwc2 overlay (peripheral mode) in $CONFIG"
else
    echo "  Warning: no config.txt found at /boot. Skipping dt overlay configuration."
fi

CMDLINE="/boot/firmware/cmdline.txt"
[ ! -f "$CMDLINE" ] && CMDLINE="/boot/cmdline.txt"

if [ -f "$CMDLINE" ]; then
    if ! grep -q "modules-load=dwc2" "$CMDLINE"; then
        sed -i 's/$/ modules-load=dwc2/' "$CMDLINE"
        echo "  Added dwc2 module to $CMDLINE"
    else
        echo "  dwc2 module already configured in $CMDLINE"
    fi
else
    echo "  Warning: no cmdline.txt found at /boot. Skipping modules-load configuration."
fi

echo "[3/8] Creating installation directory..."
rm -rf "$INSTALL_DIR"
mkdir -p "$INSTALL_DIR"
mkdir -p /tmp/rosettapad
mkdir -p /etc/rosettapad

echo "[4/8] Copying source files..."

if [ ! -d "$SCRIPT_DIR/adapter/src" ]; then
    echo "Error: adapter/src directory not found!"
    echo "Make sure you're running from the RosettaPad directory."
    exit 1
fi

cp -r "$SCRIPT_DIR/adapter/"* "$INSTALL_DIR/"

echo "  Copied source files to $INSTALL_DIR"

echo "[5/8] Compiling..."

cd "$INSTALL_DIR"
if make clean all; then
    echo "  Compilation successful!"
else
    echo "  Compilation failed!"
    exit 1
fi

echo "[6/8] Creating symlink..."
ln -sf "$INSTALL_DIR/rosettapad" /usr/local/bin/rosettapad

echo "[7/8] Installing OpenRC service..."
cat >/etc/init.d/${SERVICE_NAME} <<'INIT'
#!/sbin/openrc-run
description="RosettaPad - Universal Controller Adapter"
name="rosettapad"
command="/opt/rosettapad/rosettapad"
command_user="root"
command_background="yes"
pidfile="/run/rosettapad.pid"

depend() {
    need localmount dbus
    after bluetooth
}

start_pre() {
    ebegin "Loading kernel modules for USB gadget"
    /sbin/modprobe libcomposite >/dev/null 2>&1 || true
    /sbin/modprobe usb_f_fs >/dev/null 2>&1 || true
    /bin/sh -c '/usr/sbin/rfkill unblock bluetooth 2>/dev/null || true' >/dev/null 2>&1 || true
    eend $?
}
INIT

chmod +x /etc/init.d/${SERVICE_NAME}

if command -v rc-update >/dev/null 2>&1; then
    rc-update add ${SERVICE_NAME} default || true
fi

# enable dbus and bluetooth if present
if [ -f /etc/init.d/dbus ]; then
    chmod +x /etc/init.d/dbus
    rc-update add dbus default || true
    rc-service dbus start || true
fi

if [ -f /etc/init.d/bluetooth ]; then
    chmod +x /etc/init.d/bluetooth
    rc-update add bluetooth default || true
    rc-service bluetooth restart || true
fi

if [ ! -f /etc/rosettapad/config ]; then
    cat >/etc/rosettapad/config <<'CONFIG'
# RosettaPad Configuration
enable_bluetooth=1
enable_motion=1
enable_wake=1
debug_level=0
CONFIG
fi

echo "[8/8] Done!"
printf "\nInstallation complete.\n"
printf "Start/stop/status: rc-service %s start|stop|status\n" "${SERVICE_NAME}"
printf "Enable at boot: rc-update add %s default\n" "${SERVICE_NAME}"
printf "Logs: check /var/log/messages (install busybox-syslogd or syslog-ng to capture stdout)\n"

# Prompt for reboot
printf "Reboot now? (required for USB gadget mode) [Y/n] "
read ans
ans=${ans:-Y}
case "$ans" in
    [Nn]* ) echo "Skipping reboot.";;
    * ) echo "Rebooting..."; reboot;;
esac
