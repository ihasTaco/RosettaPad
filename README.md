# RosettaPad
RosettaPad is a system that allows you to pair any (eventually) generic controller to your PS3 and access all of the features you'd expect from a first-party Sony Dualshock 3 controller, including a usable PS button, rumble, and accelerometer and gyroscope, while also giving you many more features like customizable macros, button remapping and many more to come!

> Currently this is setup for the DualSense (PS5) controllers specifically, it's what I like to use. If you want me to add new controllers, open an issue and when I have time we can work out getting the correct reports for the controller.

## Features

### Progress
- [x] Full DS3 emulation
  <details>
  <summary>Feature breakdown</summary>

  | Feature | Status | Notes |
  |---------|--------|-------|
  | All buttons and analog sticks | ✅ | |
  | USB Mode to PS3 | ✅ | |
  | Bluetooth Mode to PS3 | ✅ | This mode introduces ~300ms of latency, but is required if you need accelerometer and gyro, usb mode is recommended. I believe porting this to Pico 2w will fix the buffer issue causing the latency, also there is a weird overshoot issue that happens randomly. |
  | Rumble | ✅ | Tested on PS3 games, need to verify PS2 mode compatibility |
  | Acceleration & Gyro | ✅ | Need to fix calibration |
  | Power Display | ✅ | |
  | Standby Wake | ✅ | |
  | Adaptive Triggers | ⬜ | This will not allow the dynamic adaptive triggers, just would be cool to have |
  | Touchpad as precision joystick | ✅ | |

  </details>

- [ ] Web Panel
  - [ ] Backend API
     
    <details>
    <summary>Feature Breakdown</summary>

    | Feature | Status | Notes |
    |---------|--------|-------|
    | Bluetooth Devices | ➖ | Stubbed Frontend and API, need this to connect with the pi |
    | Profiles | ➖ | Stubbed Frontend and API, need this to connect with the pi |
    | Macros | ➖ | Stubbed Frontend and API, need this to connect with the pi |
    | Button Remapping | ➖ | Stubbed Frontend and API, need this to connect with the pi |
    | Lightbar Customizations | ➖ | Stubbed Frontend and API, need this to connect with the pi |
    | Controller Stats | ⬜ | |
    | Debugging Tools | ⬜ | |

    </details>

  - [ ] Frontend
     
    <details>
    <summary>Feature Breakdown</summary>

    | Feature | Status | Notes |
    |---------|--------|-------|
    | Bluetooth Devices | ➖ | Stubbed Frontend and API, need this to connect with the pi |
    | Profiles | ➖ | Stubbed Frontend and API, need this to connect with the pi |
    | Macros | ➖ | Stubbed Frontend and API, need this to connect with the pi |
    | Button Remapping | ➖ | Stubbed Frontend and API, need this to connect with the pi |
    | Lightbar Customizations | ➖ | Stubbed Frontend and API, need this to connect with the pi |
    | Controller Stats | ⬜ | |
    | Debugging Tools | ⬜ | |

    </details>

### To-Do
- [ ] Test rumble on PS2 mode games
  - Heard that PS2 mode can break rumble on other generic controllers, the PS3 may be sending Dualshock 2 protocols.
- [ ] Modularize HID reports (Enable easier integration of other generic controllers.)
- [ ] Port setup to Raspberry Pi Pico 2w
- [ ] Add other controller support

### Future Endeavors
- TAS system
  - Set up a TAS system that will let you import or record and playback controller inputs on real hardware.
- PS4 / PS5 support
  - This would be cool to have just for the macro and button remapping, will need to look into a MITM setup for authorization from console to controller but should work. Will need a Sony PS4/5 controller 

## Hardware Required

- Raspberry Pi Zero 2W
- USB data cable (connects Pi's data USB port to PS3)
- USB power cable (connects Pi's power USB port to power source)
- PS5 DualSense controller

## Usage

### Installation
```bash
chmod +x ./install.sh
./install.sh
```
That should be it for setup, but just in case see [Boot Configurations](#boot-configuration) to make sure the pi is setup to run as a peripheral.

### Manual Start
```bash
sudo rosettapad
```

### As a Service
```bash
sudo systemctl start rosettapad
sudo systemctl stop rosettapad
sudo systemctl status rosettapad

# Enable at boot:
sudo systemctl enable rosettapad
```

### Manually Pairing DualSense
Once the web panel is up, you should only have to do this if something breaks.
```bash
bluetoothctl
> scan on
# Put DualSense in pairing mode (hold Create + PS until light flashes) should show up as 'DualSense Wireless Controller'
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

## The Technical Stuff

### The Problem
When connecting a DualSense controller (and other generic controllers) to the PS3, the controller may have certain features available, but the PS3 doesn't know how to handle them.

For instance, the DS5 controller has rumble, acceleration, and a PS button but the PS3 just will not send or read the information from the controller, making it almost useless (in case of the PS button). 

This tool will connect to the PS3, authenticate as a DS3 controller and translate and relay all of the necessary information to make these essential features work!

### The Solution
I was half-expecting to have to crack sony's authentication to emulate the DS3, but I was happily surprised. All that's needed is the following:
1. Using the correct Sony USB VID/PID (054c:0268) (I need to test if this is necessary as I only tested before implimenting the below functions)
2. Responding to specific USB HID feature report requests (0x01, 0xF2, 0xF5, 0xF7, 0xF8, 0xEF)
3. Echoing back the 0xEF configuration report exactly as the PS3 sends it
4. Sending properly formatted DS3 input reports on the interrupt IN endpoint
5. Reading output reports (LED/rumble) from the PS3 on the interrupt OUT endpoint

### Key Discoveries
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

### DS3 Input Report Format
```
Byte 0:       0x01 (Report ID)
Byte 1:       Reserved (0x00)
Byte 2:       Released (0x00), Select (0x01), L3 (0x02), R3 (0x04), Start (0x08), D Up (0x10), D Right (0x20), D Down (0x40), D Left (0x80)
Byte 3:       Released (0x00), L2 (0x01), R2 (0x02), L1 (0x04), R1 (0x08), Triangle (0x10), Circle (0x20), Cross (0x40), Square (0x80)
Byte 4:       Released (0x00), PS (0x01)
Byte 5:       Reserved
Byte 6:       Left analog stick X axis (0x00 - 0xFF)
Byte 7:       Left analog stick Y axis (0x00 - 0xFF)
Byte 8:       Right analog stick X axis (0x00 - 0xFF)
Byte 9:       Right analog stick Y axis (0x00 - 0xFF)
Bytes 10-12:  Reserved
Bytes 13-16:  D-pad pressure (up, right, down, left) (0x00 - 0xFF)
Bytes 17-18:  L2, R2 analog pressure (0x00-0xFF)
Bytes 19-20:  L1, R1 pressure (0x00-0xFF)
Bytes 21-24:  Triangle, Circle, Cross, Square pressure (0x00-0xFF)
Bytes 25-29:  Reserved
Byte 30:      Charged (0xEF), Charging (0xEE), No Connection? (0xF0), Dead (0x00, 0x01, 0x02), 1 Bar (0x03), 2 Bar (0x04), 3 Bar (0x05)
Bytes 31-35:  Reserved?
Bytes 36-39:  Calibration? The numbers, Sony! What do they mean?
Byte 40 - 41: Accelerometer X Axis, LE 10bit unsigned
Byte 42 - 43: Accelerometer Y Axis, LE 10bit unsigned
Byte 44 - 45: Accelerometer Z Axis, LE 10bit unsigned
Byte 46 - 47: Gyroscope, LE 10bit unsigned
Byte 48:      ???? (This is almost always 0x02 on the 2 controllers I have to test)
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
| Cross | Cross |
| Circle | Circle |
| Triangle | Triangle |
| Square | Square |
| L1/R1 | L1/R1 |
| L2/R2 | L2/R2 |
| L3/R3 | L3/R3 |
| Options | Start |
| Create | Select |
| Touchpad | R3 anolog stick |
| PS | PS |

## File Locations

After installation:
- `/opt/rosettapad/rosettapad` - Main executable
- `/etc/systemd/system/rosettapad.service` - Systemd service
- `/usr/local/bin/rosettapad` - Symlink to executable

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

## Credits & Attribution
- [Eleccelerator](https://eleccelerator.com/wiki/index.php?title=DualShock_3)
- [Torvalds](https://github.com/torvalds/linux/blob/master/drivers/hid/hid-sony.c)
- [Felis](https://github.com/felis/USB_Host_Shield_2.0/blob/master/PS3USB.cpp)

If you use any of the protocol documentation or findings from this project, please provide attribution by linking back to this repository.
