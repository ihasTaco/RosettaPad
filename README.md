# RosettaPad

**Universal Controller Adapter for PlayStation Consoles**

RosettaPad lets you use modern controllers (DualSense, and eventually others) on older PlayStation consoles with full feature support-including the PS button, rumble, and motion controls that typically don't work with third-party adapters.

> **Current Status:** Actively developed for DualSense → PS3. The project is in a functional state but the maintainer is taking a break. Bug reports welcome; new features will be addressed as time permits.

---

## Why RosettaPad?

When you connect a DualSense to a PS3, it *technically* works-but the PS3 doesn't know how to talk to it properly. The PS button does nothing, rumble is silent, and motion controls are dead.

RosettaPad solves this by acting as a translator: it presents itself to the PS3 as a genuine DualShock 3 while accepting input from your modern controller. No authentication cracking required-just proper protocol emulation.

---

## Features

### What Works Now

| Feature | Status | Notes |
|---------|:------:|-------|
| All Buttons | ✅ | Face, D-pad, shoulders, triggers, sticks, PS button |
| Analog Sticks | ✅ | Full 8-bit precision |
| Analog Triggers | ✅ | Pressure-sensitive L2/R2 |
| Rumble | ✅ | Both motors, tested on PS3 games |
| Battery Display | ✅ | Shows DualSense battery on PS3 UI |
| Touchpad as Right Stick | ✅ | Swipe to control camera |
| Motion Controls | ✅ | Accelerometer + gyroscope via Bluetooth |
| PS3 Wake from Standby | ✅ | Press PS button to wake console |

### In Progress

| Feature | Status | Notes |
|---------|:------:|-------|
| Web Configuration Panel | 🚧 | Backend API stubbed, frontend in progress |
| Button Remapping | 🚧 | Architecture ready, UI needed |
| Macros | 🚧 | Planned |
| Lightbar Customization | 🚧 | IPC mechanism in place |

### Planned

- Additional controller support (Xbox, 8BitDo, Switch Pro)
- PS4/PS5 console support (for macros/remapping, requires auth research)
- TAS recording and playback
- Hardware migration to Pico 2W

---

## Hardware Requirements

- **Raspberry Pi Zero 2W**
- **USB data cable** - connects Pi's data port to PS3
- **USB power cable** - connects Pi's power port to a power source
- **DualSense controller** - paired via Bluetooth

> **Important:** The Pi Zero 2W has two micro-USB ports. The one labeled "USB" is for data; the one labeled "PWR" is power-only. You need data connected to the PS3.

---

## Quick Start

### 1. Install

```bash
git clone https://github.com/ihasTaco/RosettaPad.git
cd RosettaPad
chmod +x install.sh
./install.sh
```

### 2. Pair Your Controller

```bash
bluetoothctl
scan on
# Put DualSense in pairing mode: hold Create + PS until light flashes rapidly
pair XX:XX:XX:XX:XX:XX
trust XX:XX:XX:XX:XX:XX
connect XX:XX:XX:XX:XX:XX
quit
```

### 3. Connect to PS3

Plug the Pi's data USB port into your PS3. The console should recognize it as a controller.

### 4. Run

```bash
# Manual
sudo rosettapad

# Or as a service
sudo systemctl start rosettapad
sudo systemctl enable rosettapad  # Start on boot
```

---

## Boot Configuration

These settings are required for USB gadget mode:

**`/boot/firmware/config.txt`** - add this line:
```
dtoverlay=dwc2,dr_mode=peripheral
```

**`/boot/firmware/cmdline.txt`** - append to the end (same line):
```
modules-load=dwc2
```

---

## Architecture

```
┌─────────────────┐     Bluetooth      ┌─────────────────┐
│   DualSense     │◄──────────────────►│  Raspberry Pi   │
│   Controller    │                    │    Zero 2W      │
└─────────────────┘                    └────────┬────────┘
                                                │ USB Gadget
                                                ▼
                                       ┌─────────────────┐
                                       │   PlayStation   │
                                       │       3         │
                                       └─────────────────┘
```

RosettaPad runs on the Pi and:
1. Receives input from DualSense via Bluetooth HID
2. Translates it to DualShock 3 format
3. Presents itself to PS3 as a genuine DS3 via USB gadget
4. Maintains a Bluetooth connection to PS3 for motion data and wake

---

## Technical Details

### How DS3 Emulation Works

The PS3 doesn't require cryptographic authentication for DS3 controllers. It just needs:

1. Correct USB VID/PID (`054c:0268`)
2. Proper responses to feature report requests (0x01, 0xF2, 0xF5, 0xF7, 0xF8, 0xEF)
3. Echo back the 0xEF configuration report exactly as received
4. Send 49-byte input reports on the interrupt IN endpoint
5. Receive output reports (LED/rumble) on the interrupt OUT endpoint

### PS3 Initialization Sequence

```
1. SET_IDLE
2. GET_REPORT 0x01  → Device capabilities
3. GET_REPORT 0xF2  → Controller Bluetooth MAC
4. GET_REPORT 0xF5  → Host Bluetooth MAC
5. SET_REPORT 0xEF  → Configuration (must echo back)
6. GET_REPORT 0xF8  → Status
7. GET_REPORT 0xF7  → Calibration data
8. SET_REPORT 0xF4  → LED configuration
9. Normal input/output exchange begins
```

### Motion Controls & Wake

The PS3 only sends the SIXAXIS enable command (0xF4) over Bluetooth, not USB. RosettaPad maintains a parallel Bluetooth L2CAP connection to the PS3 for:
- Receiving the motion enable command
- Sending accelerometer/gyroscope data
- Waking the PS3 from standby

### File Locations

| Path | Description |
|------|-------------|
| `/opt/rosettapad/` | Installation directory |
| `/usr/local/bin/rosettapad` | Symlink to executable |
| `/etc/systemd/system/rosettapad.service` | Systemd service |
| `/tmp/rosettapad/` | Runtime state (IPC, cached MAC) |

---

## Troubleshooting

### Controller not detected

```bash
# Check if DualSense is connected
ls /dev/hidraw*

# Verify it's the right device
cat /sys/class/hidraw/hidraw*/device/uevent | grep -E "HID_NAME|PRODUCT"
# Should show: 054C:0CE6 (Sony DualSense)
```

### PS3 not recognizing adapter

```bash
# Check USB gadget status
ls /sys/kernel/config/usb_gadget/ds3/

# Check for errors
dmesg | tail -30 | grep -i usb
```

### Buttons not working

- The DualSense creates multiple hidraw devices; RosettaPad automatically finds the correct one (VID `054c`, PID `0ce6`)
- Ensure the controller is connected via Bluetooth, not USB

### High latency

- Bluetooth to PS3 has inherent latency due to PS3's SNIFF mode (~40ms polling)
- USB input runs at 250Hz with minimal latency
- Motion data is rate-limited to prevent buffer buildup

---

## Contributing

See [CONTRIBUTING.md](CONTRIBUTING.md) for guidelines on submitting issues and pull requests.

---

## Credits

- [Eleccelerator Wiki](https://eleccelerator.com/wiki/index.php?title=DualShock_3) - DS3 protocol documentation
- [Linux hid-sony driver](https://github.com/torvalds/linux/blob/master/drivers/hid/hid-sony.c) - Reference implementation
- [USB Host Shield Library](https://github.com/felis/USB_Host_Shield_2.0/blob/master/PS3USB.cpp) - SIXAXIS enable details

---

## License

This project is open source. See [LICENSE](LICENSE) for details.

If you use protocol documentation or findings from this project, please provide attribution by linking back to this repository.