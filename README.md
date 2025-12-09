# DualSense to PS3 Adapter

Use a PS5 DualSense controller on PlayStation 3 via a Raspberry Pi Zero 2W, with full PS button support!

## What This Does

This adapter allows a DualSense controller connected via Bluetooth to the Pi to appear as an authentic DualShock 3 controller to the PS3. Unlike generic USB adapters, **the PS button works** because we properly emulate the DS3's USB protocol.

## Features

### Current
- ✅ Full DS3 emulation with PS button support
- ✅ All buttons and analog sticks
- ✅ Analog triggers (L2/R2)
- ✅ Auto-reconnect on controller disconnect
- ✅ Systemd service for auto-start

### Planned
- 🔲 Web configuration interface
- 🔲 Bluetooth pairing via web UI
- 🔲 Custom button remapping
- 🔲 Macro system
- 🔲 Profile system with hotkey switching
- 🔲 DualSense lightbar control
- 🔲 Adaptive trigger configuration
- 🔲 Rumble/haptic feedback forwarding
- 🔲 Gyroscope/motion controls
- 🔲 Touchpad as precision joystick
- 🔲 Controller stats (battery, latency)
- 🔲 Debug tools and logging

## Hardware Required

- Raspberry Pi Zero 2W
- USB data cable (connects Pi's inner/data USB port to PS3)
- USB power cable (connects Pi's outer/power USB port to power source)
- PS5 DualSense controller

## How It Works

### The Problem
The PS3 only accepts PS button input from "authenticated" Sony controllers. Generic USB gamepads can send all other buttons, but the PS3 ignores the home/PS button from non-Sony devices.

### The Solution
We discovered that full DS3 emulation (including PS button) works **without cryptographic authentication** by:

1. Using the correct Sony USB VID/PID (054c:0268)
2. Responding to specific USB HID feature report requests (0x01, 0xF2, 0xF5, 0xF7, 0xF8, 0xEF)
3. Echoing back the 0xEF configuration report exactly as the PS3 sends it
4. Sending properly formatted DS3 input reports on the interrupt IN endpoint
5. Reading output reports (LED/rumble) from the PS3 on the interrupt OUT endpoint

### Key Discovery
The PS3's initialization sequence:
1. SET_IDLE
2. GET_REPORT 0x01 (device capabilities)
3. GET_REPORT 0xF2 (controller Bluetooth MAC)
4. GET_REPORT 0xF5 (host Bluetooth MAC)
5. SET_REPORT 0xEF (configuration) → **Must echo this back on GET_REPORT 0xEF**
6. GET_REPORT 0xF8 (status)
7. GET_REPORT 0xF7 (calibration?)
8. SET_REPORT 0xF4 (LED config)
9. Normal input/output report exchange begins

## Technical Details

### USB Gadget Setup
Uses Linux USB Gadget/ConfigFS with FunctionFS:
- UDC: `3f980000.usb` (Pi Zero 2W's dwc2 controller)
- VID: `0x054c` (Sony)
- PID: `0x0268` (DualShock 3)
- FunctionFS mount: `/dev/ffs-ds3`

### Endpoints
- EP0: Control transfers (feature reports)
- EP1 (0x81): Interrupt IN - sends 49-byte input reports to PS3 at 250Hz
- EP2 (0x02): Interrupt OUT - receives LED/rumble commands from PS3

### DS3 Input Report Format (49 bytes)
```
Byte  0:    0x01 (Report ID)
Byte  1:    Reserved (0x00)
Byte  2:    Select(0x01), L3(0x02), R3(0x04), Start(0x08), DPad
Byte  3:    L2(0x01), R2(0x02), L1(0x04), R1(0x08), △(0x10), ○(0x20), ✕(0x40), □(0x80)
Byte  4:    PS Button (0x01)
Byte  5:    Reserved
Bytes 6-7:  Left stick X, Y (0x00-0xFF, center 0x80)
Bytes 8-9:  Right stick X, Y (0x00-0xFF, center 0x80)
Bytes 10-17: D-pad pressure (up, right, down, left) + reserved
Bytes 18-19: L2, R2 analog pressure (0x00-0xFF)
Bytes 20-21: L1, R1 pressure
Bytes 22-25: Triangle, Circle, Cross, Square pressure
Bytes 26-28: Reserved
Bytes 29-31: Battery/status (0x02, 0xEE, 0x12 for USB powered)
Bytes 32-48: Reserved/accelerometer data
```

### DualSense Bluetooth HID Report Format
```
Byte  0:    0x31 (Report ID)
Byte  1:    Counter
Bytes 2-3:  Left stick X, Y
Bytes 4-5:  Right stick X, Y
Bytes 6-7:  L2, R2 triggers
Byte  8:    Counter/status
Byte  9:    D-pad (low nibble) + face buttons (high nibble)
Byte 10:    Shoulders + stick clicks + start/select
Byte 11:    PS button (0x01), Touchpad (0x02), Mute (0x04)
```

### Button Mapping
| DualSense | DS3 |
|-----------|-----|
| ✕ | ✕ |
| ○ | ○ |
| △ | △ |
| □ | □ |
| L1/R1 | L1/R1 |
| L2/R2 | L2/R2 |
| L3/R3 | L3/R3 |
| Options | Start |
| Create | Select |
| Touchpad | Select (alt) |
| PS | PS |

## File Locations

After installation:
- `/opt/ds3-adapter/ds3_adapter` - Main executable
- `/opt/ds3-adapter/ds3_adapter.c` - Source code
- `/etc/systemd/system/ds3-adapter.service` - Systemd service
- `/usr/local/bin/ds3-adapter` - Symlink to executable

## Usage

### Manual Start
```bash
sudo ds3-adapter
```

### As a Service
```bash
sudo systemctl start ds3-adapter
sudo systemctl stop ds3-adapter
sudo systemctl status ds3-adapter

# Enable at boot:
sudo systemctl enable ds3-adapter
```

### Pairing DualSense
```bash
bluetoothctl
> scan on
# Put DualSense in pairing mode (hold Create + PS until light flashes)
> pair XX:XX:XX:XX:XX:XX
> trust XX:XX:XX:XX:XX:XX
> connect XX:XX:XX:XX:XX:XX
> quit
```

## Boot Configuration

Required in `/boot/firmware/config.txt`:
```
dtoverlay=dwc2,dr_mode=peripheral
```

Required in `/boot/firmware/cmdline.txt` (add to end):
```
modules-load=dwc2
```

## Troubleshooting

### "Waiting for DualSense..."
- Ensure DualSense is paired and connected via Bluetooth
- Check: `ls /dev/hidraw*` - should show a device
- Check: `cat /sys/class/hidraw/hidraw*/device/uevent | grep NAME`

### PS3 not detecting controller
- Verify USB data cable is connected to the inner USB port (not power-only)
- Check dmesg for USB gadget errors: `dmesg | tail -20`
- Verify gadget setup: `ls /sys/kernel/config/usb_gadget/ds3/`

### Buttons not responding
- Ensure the correct hidraw device is found (should be DualSense, not touchpad)
- Multiple hidraw devices exist for DualSense; we look for VID 054c PID 0ce6

## Credits

Developed through reverse engineering the DS3 USB protocol by analyzing real controller behavior and PS3 initialization sequences.

Key insight: The PS3 doesn't require cryptographic authentication for the PS button - it just needs proper USB enumeration and feature report handling!
