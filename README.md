# RosettaPad

**Universal Controller Adapter for PlayStation Consoles**

RosettaPad lets you use modern controllers (DualSense, and eventually others) on older PlayStation consoles with full feature support, including the PS button, rumble, and motion controls that typically don't work with third-party adapters.

> [!NOTE]
> **Current Status**: Development is currently active for DualSense and PS3. There is a long-term goal to get this working on other Sony consoles and *maybe* Nintendo/Microsoft platforms, but that is not in scope for this project at the moment.

<details>
<summary> View Directory </summary>
  
* [Why RosettaPad?](#why-rosettapad)  
* [Features](#features)  
  * [What Works Now](#what-works-now)  
  * [In Progress](#in-progress)  
  * [Planned](#planned)
* [Hardware Requirements](#hardware-requirements)  
* [Quick Start](#quick-start)  
  * [1. Install](#1-install)  
  * [2. Pair Your Controller](#2-pair-your-controller)  
  * [3. Connect to PS3 & Run](#3-connect-to-ps3--run)  
* [Boot Configuration](#boot-configuration)  
* [Technical Details](#technical-details)  
  * [How DS3 Emulation Works](#how-ds3-emulation-works)  
  * [PS3 Initialization Sequence](#ps3-initialization-sequence)  
  * [Motion Controls & Wake](#motion-controls--wake)  
  * [File Locations](#file-locations)  
* [Troubleshooting](#troubleshooting)  
  * [Controller not detected](#controller-not-detected)  
  * [PS3 not recognizing adapter](#ps3-not-recognizing-adapter)  
  * [Buttons not working](#buttons-not-working)  
  * [High latency / Motion controls not working](#high-latency)  
* [Contributing](#contributing)  
* [Credits / Resources](#credits)  
* [License](#license)  
</details>

---

## Why RosettaPad?

When you connect a DualSense to a PS3, it *technically* works, but the PS3 doesn't know how to talk to it properly. The PS button does nothing, rumble is silent, and motion controls are dead.

RosettaPad solves this by acting as a translator: it presents itself to the PS3 as a genuine DualShock 3 while accepting input from your modern controller. No authentication cracking required, just proper protocol emulation.

---

## Features

### What Works Now

| Feature | Status | Notes |
|---------|:------:|-------|
| DualSense to Pi Bluetooth | ✅ | Works without issues. |
| Pi to PS3 USB | ✅ | Works without issues. |
| All Buttons | ✅ | Face, D-pad, shoulders, triggers, sticks, PS button. |
| Analog Sticks | ✅ | Full 8-bit precision. |
| Analog Triggers | ✅ | Pressure-sensitive L2/R2. |
| Rumble | ✅ | Both motors, tested on PS3 games. |
| Battery Display | ✅ | Uses DualSense's current battery info (Charging, charged, battery level) and displays in PS3 UI |

### In Progress

| Feature | Status | Notes |
|---------|:------:|-------|
| Pi to PS3 Bluetooth | Issues | Pi to PS3 bluetooth works but there is high latency issues. It is not recommended to use Pi to PS3 bluetooth. |
| Motion Controls | Issues | Technically works, there is current calibration issues that causes extreme values. Motion controls also only work over bluetooth, recommended not to use until bluetooth issues are fixed. |
| PS3 Wake from Standby | Issues | Works too well. While pi is connected to ps3 over bluetooth, when you turn the ps3 off the pi will just continuously turn back on. Need to add checks to see if controller is connected and the PS button was pressed before turning on. |
| Touchpad as Right Stick | Needs Work | Meh :/ It works but isn't as precise as I'd like. Needs calibration. |

### Planned

| Feature | Status | Notes |
|---------|:------:|-------|
| Web Configuration Panel | Started | Backend API stubbed, frontend in progress - [Dev Web Panel Branch](https://github.com/ihasTaco/RosettaPad/tree/dev-web-panel) |
| Pico 2W Port | Started | [Dev Pico Port Branch](https://github.com/ihasTaco/RosettaPad/tree/dev-pico-port) |
| Button Remapping | Not Started | Architecture ready, UI needed |
| DualSense Adaptive triggers | Not Started | N/A |
| Lightbar Customization | Not Started | N/A |
| Macros | Not Started | N/A |
| Additional controller support | Not Started | N/A |
| Additional console support | Not Started | N/A |
| TAS recording, editing & playback | Not Started | N/A |

---

## Hardware Requirements

- **Raspberry Pi Zero 2W (or Zero W)**
- **USB data cable** - connects Pi's data port to PS3
- **USB power cable** - connects Pi's power port to a power source
- **DualSense controller** - paired via Bluetooth

> **Important:** The Pi Zero 2W has two micro-USB ports. The one labeled "USB" is for data; the one labeled "PWR" is power-only. You need data connected to the PS3.

---

## Quick Start

### 0. USB Gadget Mode
[Follow these instructions](#boot-configuration)

### 1. Install

```bash
git clone https://github.com/ihasTaco/RosettaPad.git
cd RosettaPad
chmod +x install.sh
./install.sh
```

### 2. Pair Your Controller

Eventually I would like to make a web interface for this, but for now you'll need to do this manually.

```bash
bluetoothctl
scan on
# Put DualSense in pairing mode: hold Create + PS until light flashes rapidly
pair XX:XX:XX:XX:XX:XX
trust XX:XX:XX:XX:XX:XX
connect XX:XX:XX:XX:XX:XX
quit
```

### 3. Connect to PS3 & Run

Plug the Pi into the PS3 via the data port

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

`/boot/firmware/config.txt` - add this line:
```
dtoverlay=dwc2,dr_mode=peripheral
```

`/boot/firmware/cmdline.txt` - append to the end (same line):
```
modules-load=dwc2
```

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

You will need to modify some setting on the pi before it will get recognized by the PS3: (USB Gadget mode](#boot-configuration)

```bash
# Check USB gadget status
ls /sys/kernel/config/usb_gadget/ds3/

# Check for errors
dmesg | tail -30 | grep -i usb
```

### Buttons not working

- The DualSense creates multiple hidraw devices; RosettaPad automatically finds the correct one (VID `054c`, PID `0ce6`)
- Ensure the controller is connected via Bluetooth, not USB

### High latency over bluetooth

This is a known issue, it is recommended to not connect the pi to the PS3 via bluetooth.  
**Some games that require motion controls will not work until latency issues are fixed.**  

**Notes for contributors:**  
I have a few theories on the latency issues:

Currently, the DS3 Emulation thread is handling the conversion logic and creating the reports before sending to the PS3.
I think the controller thread should handle the conversion and report writing for a few reasons: 
 * The DS3 thread would immediately send the latest report as soon as it's ready.  
 * The conversion logic would be kept contained per controllers, so new controllers added wouldn't need to touch DS3 emulation logic. 

The Pi Zero 2W uses a single chip for both wifi and bluetooth. The current setup has both running simultaneously and I think that could cause some latency issues.
Implementing a way to shut off wifi when not in use may help. I am thinking of implementing a system that will turn on wifi during first boot and when a controller is not connected, then shut it off afterwards, and setting up a key bind on the controller will toggle wifi manually.
 * Since the project is only supporting DS5 for now, holding the Mute button on the controller for 5 seconds will toggle wifi.
 * Toggling wifi will be denoted by rumble and light animations.

The code is currently hardcoding a 40ms delay between sending reports, I believe the DS3 sends reports every 10ms. 
I think by dropping the delay to 10ms and the above fixes could allow low latency without dropped inputs. 

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
