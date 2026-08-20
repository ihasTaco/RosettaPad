# RosettaPad

**A universal controller adapter for the PlayStation 3**

The goal of RosettaPad is to pair any controller to the PS3 with full feature support, including the PS button, rumble, and motion controls that typically don't work with third part adapters as well as add some other features the DualShock 3 does not have, without needing a jailbreak.

> [!NOTE]
> **Current Status**: Currently RosettaPad is active for DualSense (PS5) controller support on the PS3. 
> I would like to expand to more controllers and systems, but for now the scope is one controller, one system, and some tools that will help make adding your favorite controllers easier.

<details>
<summary> View Directory </summary>

* [What's the Point?](#)  
* [Features](#)  
  * [What Works Now](#)  
  * [In Progress](#)  
  * [Planned](#)
* [Hardware Requirements](#)  
* [Quick Start](#)  
  * [1. Install](#)  
  * [2. Pair Your Controller](#)  
  * [3. Connect to PS3 & Run](#)  
* [Boot Configuration](#)  
* [Troubleshooting](#)  
  * [Controller not detected](#controller-not-detected)  
  * [PS3 not recognizing adapter](#ps3-not-recognizing-adapter)  
  * [Buttons not working](#buttons-not-working)  
  * [High latency / Motion controls not working](#high-latency)  
* [Credits & Attributions](#credits)  
* [Contributing](#)  
* [License](#license)  
</details>

## What's the Point?
You may be asking, **"I can connect my DualSense/DS4/Wildcatz/8BitDo controllers to the PS3 right now."**   
Yep.

So the PS3 is kinda dumb, well at least the gamepad drive is. If it isn't specifically a DualShock 3 controller, it deems the controller "generic" and switches to an all purpose gamepad driver that disables stuff like PS button, rumble, motion controls, etc. So if for some reason you wanted to exit a game and go to the XMB, you just.... can't.  

Okay, so I'm kinda cheap and didn't really want to buy a thing to plug in and have it just *work* when I could probably just make it myself. It probably would've cost me $50, and then I'd have to wait like 2 days 🤮 So instead I spent every waking moment for a month reading as much material as I can find on the dualshock 3 HID and bluetooth handshake, doing my own testing, and finally building RosettaPad.   
Was it worth it? Probably not lol I later found out that there is some other projects doing pretty similar things, but they needed needlessly complicated steps to setup. I also wanted to add some features they didn't have. Mostly what intrigued me was a TAS system (TBD) I don't know if it will be useful, but it will be cool and thats all that matters to me.

## Features

What should you expect if you setup RosettaPad right now?

### What Works Right Now?

| Feature | Status | Notes |
|---------|:------:|-------|
| DualSense to Raspberry Pi connection via bluetooth | ✅ | Works without issues. |
| Raspberry Pi to PS3 connection via USB | ✅ | Works without issues. |
| PS3 to Raspberry Pi bluetooth handshake | ✅ | Works without issues. Please see [In Progress](#) for information on Raspberry Pi to PS3 connection via bluetooth |
| Complete button translation | ✅ | Works without issues. This includes all face, d-pad, shoulders, triggers, and PS button. Some caveats: DS3 has pressure sensitive face buttons, DS5 (DualSense) does not. RosettaPad normalizes the values from 00 to FF (off or on), games that rely on this will not work.|
| Complete analog input translation | ✅ | Works without issues. This includes analog sticks and analog triggers. |
| Rumble | ✅ | Both motors work without issues |
| Battery display on PS3 | ✅ | I know this is a small feature, but im proud of this one lol. The DS5 sends current battery status (Whether it is plugged in and charging, charged, or discharging) and it gets displayed in the PS3 UI. |

### What's In Progress?

| Feature | Status | Notes |
|---------|:------:|-------|
| Raspberry Pi to PS3 connection via bluetooth | Issues | It works... if you count a second of latency as working. It's advised not to use bluetooth at the moment |
| Motion controls | Issues | Technically works, but there is currently an issue that causes extreme values. **Note:** Motion controls only work when Pi to PS3 is active. Bluetooth is top priority to get working right, then this. | 
| PS3 Wake from Standby | Issues | This works a little too well right now. While the Pi is connected to the PS3 over bluetooth, the Pi continuously sends power on packets to the PS3, and keeps turning the PS3 on. This feature is disabled for now while the Pi is connected via USB (See [How to use bluetooth](#) on what not to do to prevent this) |
| Touchpad as precision right stick | Needs work | It works, just not as well as I hoped | 

### What's Planned?

| Feature | Status | Notes |
|---------|:------:|-------|
| Web Configuration Panel | Started | Backend API stubbed, frontend in progress - [Dev Web Panel Branch](https://github.com/ihasTaco/RosettaPad/tree/dev-web-panel) |
| Pico 2W Port | Idle | Want to get Zero 2 W bluetooth working first - [Dev Pico Port Branch](https://github.com/ihasTaco/RosettaPad/tree/dev-pico-port) |
| Button Remapping | Not Started | Architecture ready, UI needed |
| DualSense Adaptive triggers | Not Started | N/A - Waiting for Web Configuration Panel |
| Lightbar Customization | Not Started | N/A - Waiting for Web Configuration Panel |
| Macros | Not Started | N/A - Waiting for Web Configuration Panel |
| TAS recording, editing & playback | Not Started | N/A - Waiting for Macros |
| Additional controller support | Not Started | N/A - Waiting for Pi to PS3 connection via bluetooth |
| Additional console support | Not Started | N/A - The codebase is modular enough this can be started at anytime. Want to get PS3 sorted first. |

## Setup

> [!NOTE]
> **New to this? Is the quick start quide *too* quick?**  
> You can find more detailed instructions that go step-by step from beginning to end [here](#). Just in case you need them.

### Hardware Requirements

- **Raspberry Pi Zero 2W (or Zero W)** - Minor testing has been done on Zero W, but it does work.
- **An SD Card** - I think anything over 8gb is overkill.
- **Micro USB data cable** - Connects Raspberry Pi's "USB" port to PS3. 
- **Micro USB power cable** - Connects Raspberry Pi's "PWR In" port to any power source. (recommended not to plug this into PS3. The PS3 turns off power to USB ports when it's off.) (This may not be needed in future updates of RosettaPad, if you see this, it's still required.)
- **DualSense controller** - See [How to pair the controller via bluetooth](#)
- **A computer** - Needed to flash Raspberry Pi OS and configure RosettaPad.
- **SD Card reader** - Only needed to flash Raspberry Pi OS

### Quick Start

1. Flash Raspberry Pi OS Lite (64-bit) for Zero 2W or OS Lite (32-bit) for Zero W.
2. Install Git:
``` bash
sudo apt install -y git
```
3. Install RosettaPad:
``` bash
git clone https://github.com/ihasTaco/RosettaPad.git
cd RosettaPad
chmod +x install.sh
./install.sh
```
3. The install script will update the system, install dependencies, enable usb gadget mode, build the source into an executable and finally create a service for you before rebooting.
4. Once back up, run `bluetoothctl`, then: 
``` bash 
scan on
# Put DualSense in pairing mode: hold Create + PS until light flashes rapidly
# Replace XX:XX:XX:XX:XX:XX with your controllers MAC address.
pair XX:XX:XX:XX:XX:XX
trust XX:XX:XX:XX:XX:XX
connect XX:XX:XX:XX:XX:XX
quit
```
5. Plug the Pi into the PS3 via the data port and run one of these:
``` bash
# As a service
sudo systemctl start rosettapad
sudo systemctl enable rosettapad  # Start on boot
```
``` bash
# Or Manually
sudo rosettapad
```
6. Done!

## Troubleshooting

### Controller not detected

``` bash
# Check if DualSense is connected
ls /dev/hidraw*

# Verify it's the right device
cat /sys/class/hidraw/hidraw*/device/uevent | grep -E "HID_NAME|PRODUCT"
# Should show: 054C:0CE6 (Sony DualSense)
```

### PS3 not recognizing adapter

You will need to modify some setting on the pi before it will get recognized by the PS3: [USB Gadget mode](#boot-configuration)

``` bash
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

---

## Credits & Atrributions

- [Eleccelerator Wiki](https://eleccelerator.com/wiki/index.php?title=DualShock_3) - DS3 protocol documentation
- [Linux hid-sony driver](https://github.com/torvalds/linux/blob/master/drivers/hid/hid-sony.c) - Reference implementation
- [USB Host Shield Library](https://github.com/felis/USB_Host_Shield_2.0/blob/master/PS3USB.cpp) - SIXAXIS enable details

---

## Contributing

See [CONTRIBUTING.md](CONTRIBUTING.md) for guidelines on submitting issues and pull requests.

---

## License

See [LICENSE](LICENSE) for details.
If you use protocol documentation or findings from this project, please provide attribution by linking back to this repository.
