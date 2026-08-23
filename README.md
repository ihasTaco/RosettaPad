# RosettaPad

**Use a DualSense controller on your PS3 to get working PS (Home) button and rumble!**

RosettaPad runs on a Raspberry Pi Zero 2 W and makes the PS3 see your DualSense as a genuine DualShock 3, so features that generic third-party adapters lose (PS/Home button, motion controls, rumble, battery status) work as expected. Setup takes about 15 minutes.

> [!NOTE]
> **Current Status**: Currently RosettaPad is active for DualSense (PS5/DS5) controller support on the PS3. 
> I would like to expand to more controllers and systems, but for now the scope is one controller, one system, and some tools that will help make adding your favorite controllers easier.

<details>
<summary> View Directory </summary>

* [What's the Point?](#whats-the-point)  
* [Features](#features)  
  * [What Works Right Now?](#what-works-right-now)  
  * [What's In Progress?](#whats-in-progress)  
  * [What's Planned?](#whats-planned)
  * [What Can't Be Emulated](#what-cant-be-emulated)
* [Setup](#setup)
  * [Hardware Requirements](#hardware-requirements)
  * [Quick Start](#quick-start)  
* [Troubleshooting](#troubleshooting)  
  * [Controller not detected](#controller-not-detected)  
  * [Buttons not working](#buttons-not-working)  
  * [High latency / Motion controls not working](#high-latency-over-bluetooth)  
* [Credits & Attributions](#credits--attributions)  
* [Contributing](#contributing)  
* [License](#license)  
</details>

## What's the Point?
You may be asking, **"I can connect my DualSense/DS4/Wildcatz/8BitDo controllers to the PS3 right now."**   
Yep.

So the PS3 is kinda dumb, well at least the gamepad driver is. If it isn't specifically a DualShock 3 controller, it deems the controller "generic" and switches to an all purpose gamepad driver that disables stuff like PS button, rumble, motion controls, etc. So if for some reason you wanted to exit a game and go to the XMB, you just.... can't.  

Okay, so I'm kinda cheap and didn't really want to buy a thing to plug in and have it just *work* when I could probably just make it myself. It probably would've cost me $50, and then I'd have to wait like 2 days 🤮 So instead I spent every waking moment for a month reading as much material as I can find on the dualshock 3 HID and bluetooth handshake, doing my own testing, and finally building RosettaPad.   
Was it worth it? Probably not lol I later found out that there is some other projects doing pretty similar things, but they needed needlessly complicated steps to setup. I also wanted to add some features they didn't have. Mostly what intrigued me was a TAS system (TBD) I don't know if it will be useful, but it will be cool and thats all that matters to me.

## Features

What should you expect if you setup RosettaPad right now?

Setup for RosettaPad takes about 15 minutes from start to finish. Once setup you will have all the features you'd expect on a normal DualShock 3 controller but on a DualSense, including the home button, motion controls and rumble working with a few features thrown in on top like a usable trackpad that works as a precision right analog stick.  

So, can you use a DualSense on a PS3? Yes! It works great with RosettaPad.

### What Works Right Now?

| Feature | Status | Notes |
|---------|:------:|-------|
| DualSense to Raspberry Pi connection via bluetooth | ✅ | Works without issues. |
| Raspberry Pi to PS3 connection via USB | ✅ | Works without issues. |
| Raspberry Pi to PS3 connection via bluetooth | ✅ | Works without issues. |
| Motion controls (Sixaxis) | ✅ | Works. Axis orientation tuned against a real DS3 | 
| Complete button translation | ✅ | Works without issues. This includes all face, d-pad, shoulders, triggers, and PS button. See [What Can't Be Emulated](#what-cant-be-emulated) |
| Complete analog input translation | ✅ | Works without issues. This includes analog sticks and analog triggers. |
| Rumble | ✅ | Both motors work without issues |
| PS3 Wake from Standby | ✅ | Works without issues. |
| Battery display on PS3 | ✅ | I know this is a small feature, but im proud of this one lol. The DualSense sends current battery status (Whether it is plugged in and charging, charged, or discharging) and it gets displayed in the PS3 UI. |


### What's In Progress?

| Feature | Status | Notes |
|---------|:------:|-------|
| Safe Mode | 🚧 | Works but needs a service restart in order to be detected, want to make this automatic |
| Touchpad as precision right stick | 🚧 | It works, just not as well as I hoped | 

### What's Planned?

| Feature | Status | Notes |
|---------|:------:|-------|
| Web Configuration Panel | 👀 | Backend API stubbed, frontend in progress - [Dev Web Panel Branch](https://github.com/ihasTaco/RosettaPad/tree/dev-web-panel) |
| Pico 2W Port | ⏸️ | [Dev Pico Port Branch](https://github.com/ihasTaco/RosettaPad/tree/dev-pico-port) |
| Button Remapping | ✖️ | Architecture ready, UI needed |
| DualSense Adaptive triggers | ✖️ | N/A - Waiting for Web Configuration Panel |
| Lightbar Customization | ✖️ | N/A - Waiting for Web Configuration Panel |
| Macros | ✖️ | N/A - Waiting for Web Configuration Panel |
| TAS recording, editing & playback | ✖️ | N/A - Waiting for Macros |
| Additional controller support | 👀 | N/A |
| Additional console support | ✖️ | N/A |

### What Can't Be Emulated

For the most part, the only thing RosettaPad can't emulate (see below) is the pressure sensitive face buttons. This is mostly due to the fact that the DualSense (and DualShock 4) controller does not have pressure sensitive face buttons. 

In the case where the controller does not have pressure sensitive buttons, RosettaPad will just map the pressure sensitive bits to 00 (for not pressed) or FF (for pressed).

**If you require this functionality**, you will need to get a controller that has pressure sensitive buttons. (Then RosettaPad can map those to the correct DualShock 3 layout)

[Here's a list of games that use pressure sensitive buttons](https://emulation.gametechwiki.com/index.php/List_of_console_games_that_support_pressure-sensitive_buttons#PlayStation_3). 

## Setup

> [!TIP]
> **New to this? Is the quick start guide *too* quick?**  
> You can find more detailed instructions that go step-by step from beginning to end [here](#todo). Just in case you need them.

### Hardware Requirements

- **Raspberry Pi Zero 2W (or Zero W)** - Minor testing has been done on Zero W, but it does work.
- **An SD Card** - I think anything over 8gb is overkill.
- **Micro USB data cable** - Connects Raspberry Pi's "USB" port to PS3. 
- **Micro USB power cable** - Connects Raspberry Pi's "PWR In" port to any power source. (recommended not to plug this into PS3. The PS3 turns off power to USB ports when it's off. This may not be needed in future updates of RosettaPad, if you see this, it's still required.)
- **DualSense controller** - See [How to pair the controller via bluetooth](#todo)
- **A computer** - Needed to flash Raspberry Pi OS and configure RosettaPad.
- **SD Card reader** - Only needed to flash Raspberry Pi OS

### Quick Start

1. Flash Raspberry Pi OS Lite (64-bit) for Zero 2W or OS Lite (32-bit) for Zero W.
2. Update system and install Git:
``` bash
sudo apt update && sudo apt upgrade -y && sudo apt install -y git
```
3. Install RosettaPad:
``` bash
git clone https://github.com/ihasTaco/RosettaPad.git
cd RosettaPad
chmod +x install_debian.sh
sudo ./install_debian.sh
```
4. The install script will update the system, install dependencies, enable usb gadget mode, build the source into an executable and finally create a service for you before rebooting.
5. Once back up, run `bluetoothctl`, then: 
``` bash 
scan on
# Put DualSense in pairing mode: hold Create + PS until light flashes rapidly
# Replace XX:XX:XX:XX:XX:XX with your controllers MAC address.
pair XX:XX:XX:XX:XX:XX
trust XX:XX:XX:XX:XX:XX
connect XX:XX:XX:XX:XX:XX
quit
```
6. Plug the Pi into the PS3 via the data port.
7. Done!

## Troubleshooting

### Controller not detected

``` bash
# Check if DualSense is connected
ls /dev/hidraw*

# Verify it's the right device
cat /sys/class/hidraw/hidraw*/device/uevent | grep -E "HID_NAME|PRODUCT"
# Should show: 054C:0CE6 (Sony DualSense)
```

### Buttons not working

- The DualSense creates multiple hidraw devices; RosettaPad automatically finds the correct one (VID `054c`, PID `0ce6`)
- Ensure the controller is connected via Bluetooth, not USB

## Credits & Attributions

- [Eleccelerator Wiki](https://eleccelerator.com/wiki/index.php?title=DualShock_3) - DualShock 3 protocol documentation
- [Linux hid-sony driver](https://github.com/torvalds/linux/blob/master/drivers/hid/hid-sony.c) - Reference implementation
- [USB Host Shield Library](https://github.com/felis/USB_Host_Shield_2.0/blob/master/PS3USB.cpp) - SIXAXIS enable details

## Contributing

See [CONTRIBUTING.md](CONTRIBUTING.md) for guidelines on submitting issues and pull requests.

## License

See [LICENSE](LICENSE) for details.
If you use protocol documentation or findings from this project, please provide attribution by linking back to this repository.
