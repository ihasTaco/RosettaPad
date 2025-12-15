# RosettaPad
RosettaPad is a system that allows you to pair any (eventually) generic controller to your PS3 and access all of the features you'd expect from a first-party Sony Dualshock 3 controller, including a usable PS button, rumble, and accelerometer and gyroscope, while also giving you many more features like customizable macros, button remapping and many more to come!

> Currently this is setup for the DualSense (PS5) controllers specifically, it's what I like to use. If you want me to add new controllers, open an issue and when I have time we can work out getting the correct reports for the controller.

## Features

### Progress
- [ ] Full DS3 emulation
  <details>
  <summary>Feature breakdown</summary>

  | Feature | Status | Notes |
  |---------|--------|-------|
  | Face Buttons | ✅ | |
  | D-Pad Buttons | ✅ | |
  | Triggers (and pressure) | ✅ | |
  | Shoulders | ✅ | |
  | Start and Select | ✅ | |
  | PS Button | ✅ | |
  | Analog Sticks | ✅ | |
  | Rumble | ➖ | Tested on PS3 games, need to verify PS2 mode compatibility |
  | Acceleration & Gyro | ➖ | Gyro works. Acceleration implemented but PS3 not reading it. After a lot of research, I think the F4 set report is sent to the DS3 to enable the accelerometer, may be missing a f4 get request that makes the ps3 start listening for motion data, more testing required |
  | Power Display | ➖ | Byte 30 controls battery status; need to wire DS5 battery to this |
  | Adaptive Triggers | ⬜ | This will not allow the dynamic adaptive triggers, just would be cool to have |
  | Touchpad as precision joystick | ⬜ | |
  | PS button to power on PS3 | ⬜ | Discovered the PS3 USB ports aren't fully disabled in standby, they monitor for wake signals. While debugging an issue, when the Pi rebooted and sent a report, the PS3 turned on. May be possible to implement wireless power-on via PS button. 🤔 |

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
- [ ] Test setup on ESP32-S3 microcontrollers
- [ ] Test setup on Raspberry Pi Pico 2w
- [ ] Add DS3 and DS4 support
- [ ] Add Xbox One, Series controller support(? This would be unholy...)

### Future Endeavors
- TAS system
  - Set up a TAS system that will let you import or record and playback controller inputs on real hardware.
- Bluetooth Connectivity
  - Allow the Pi to connect to console via bluetooth
- Multiconsole/one controller support
  - This will have a physical issue, the raspberry pi only has a single data usb, may require bluetooth to consoles, will add latency to setup
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
Bytes 36-39:  Calibration? The numbers, Sony! What do they mean? (I *think* these bytes are some kind of flag for accelerometer and gyro, need more testing.)
Byte 40 - 41: Accelerometer X Axis, LE 10bit unsigned
Byte 42 - 43: Accelerometer Y Axis, LE 10bit unsigned
Byte 44 - 45: Accelerometer Z Axis, LE 10bit unsigned
Byte 46 - 47: Gyroscope, LE 10bit unsigned
Byte 48:      ???? (This is always 0x02 on the 2 controllers I have to test)
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
| Touchpad | Select (alt) |
| PS | PS |

## File Locations

After installation:
- `/opt/ds3-adapter/ds3_adapter` - Main executable
- `/opt/ds3-adapter/ds3_adapter.c` - Source code
- `/etc/systemd/system/ds3-adapter.service` - Systemd service
- `/usr/local/bin/ds3-adapter` - Symlink to executable

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
