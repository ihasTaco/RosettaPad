# Contributing to RosettaPad

Thanks for your interest in contributing! This document outlines how to help improve RosettaPad.

## Project Status

The maintainer is currently taking a break from active development. During this time:

- **Bug reports** for serious issues will be addressed as time permits
- **Pull requests** will be reviewed, but response times may be longer than usual
- **Feature requests** are welcome but won't be prioritized until active development resumes

---

## How to Contribute

### Reporting Bugs

Before opening an issue:

1. **Check existing issues** to avoid duplicates
2. **Reproduce the problem** and note the exact steps
3. **Gather diagnostic info:**
   ```bash
   # Controller detection
   ls -la /dev/hidraw*
   cat /sys/class/hidraw/hidraw*/device/uevent
   
   # USB gadget status
   ls /sys/kernel/config/usb_gadget/ds3/
   cat /sys/kernel/config/usb_gadget/ds3/UDC
   
   # Recent logs
   sudo journalctl -u rosettapad -n 100
   dmesg | tail -50
   ```

When opening an issue, include:

- **Hardware:** Pi model, controller type, USB cable setup
- **Symptoms:** What happens vs. what you expected
- **Logs:** Relevant output from the commands above
- **Steps to reproduce:** Exact sequence to trigger the issue

### Submitting Pull Requests

1. **Fork the repository** and create a feature branch
2. **Follow the existing code style:**
   - C: 4-space indentation, `snake_case` for functions/variables
   - Comments above functions explaining purpose
   - Header files with documentation for public APIs
3. **Test your changes** on real hardware if possible
4. **Keep commits focused** - one logical change per commit
5. **Write a clear PR description** explaining what and why

### Code Organization

```
RosettaPad/
├── src/
│   ├── core/              # Shared utilities and state management
│   ├── controllers/       # Controller drivers (DualSense, etc.)
│   │   └── dualsense/     # Each controller gets its own directory
│   └── console/           # Console emulation layers
│       └── ps3/           # PS3-specific: USB gadget, BT, DS3 reports
├── web/                   # Web configuration panel (Python)
└── install.sh             # Installation script
```

---

## Adding a New Controller

RosettaPad is designed to be extensible. To add support for a new controller:

### 1. Create the driver directory

```
src/controllers/your_controller/
├── your_controller.h
└── your_controller.c
```

### 2. Implement the controller interface

Your driver must implement `controller_driver_t` from `controller_interface.h`:

```c
typedef struct controller_driver {
    const controller_info_t* info;      // VID, PID, capabilities
    
    int (*init)(void);                  // One-time setup
    void (*shutdown)(void);             // Cleanup on exit
    int (*find_device)(void);           // Locate and open the controller
    int (*match_device)(uint16_t vid, uint16_t pid);
    int (*process_input)(const uint8_t* buf, size_t len, controller_state_t* out);
    int (*send_output)(int fd, const controller_output_t* output);
    void (*on_disconnect)(void);
    void (*enter_low_power)(int fd);    // Optional
} controller_driver_t;
```

### 3. Register the driver

In `controller_registry.c`:

```c
#include "controllers/your_controller/your_controller.h"

void controller_registry_init(void) {
    dualsense_register();
    your_controller_register();  // Add this line
}
```

### 4. Key considerations

- **Input parsing:** Translate your controller's HID reports to `controller_state_t`
- **Output handling:** Convert `controller_output_t` to your controller's rumble/LED format
- **Device detection:** Scan `/dev/hidraw*` for your VID/PID
- **Calibration:** If your controller provides calibration data, read and apply it

See `src/controllers/dualsense/dualsense.c` as a reference implementation.

---

## Adding a New Console

To add support for a new target console (PS4, PS5, etc.):

### 1. Create the emulation directory

```
src/console/your_console/
├── emulation.h      # Console-specific report definitions
├── emulation.c      # Translation from controller_state_t
├── usb_gadget.c     # USB device emulation (if applicable)
└── bt_hid.c         # Bluetooth communication (if applicable)
```

### 2. Implement the translation layer

- Read from `controller_state_t` (generic input)
- Build console-specific HID reports
- Handle console-specific feature reports

### 3. Add threads to main.c

Each console typically needs:
- USB control thread (ep0 handling)
- USB input thread (sending reports)
- USB output thread (receiving rumble/LED commands)
- Bluetooth threads (if the console requires BT for certain features)

---

## Development Tips

### Debugging

```bash
# Watch Bluetooth traffic
sudo btmon

# USB gadget debugging
dmesg -w | grep -i usb

# Test input parsing without PS3
sudo ./rosettapad 2>&1 | grep -E "\[Input\]|\[USB\]"
```

### Capturing controller data

If you're adding a new controller and need to capture its HID reports:

```bash
# Find the device
ls /dev/hidraw*

# Capture raw input
sudo cat /dev/hidrawX | xxd

# Get report descriptor
sudo usbhid-dump -d XXXX:XXXX -e descriptor
```

### Testing motion controls

Motion data only works over Bluetooth. To verify:

1. Connect RosettaPad to PS3 via USB
2. Wait for Bluetooth connection to establish
3. Check logs for "F4 ENABLE" message
4. Test with a game that uses SIXAXIS (e.g., Flower, Heavy Rain)

---

## Questions?

Open an issue with the "question" label. Response times may vary.

---

## Code of Conduct

Be respectful. We're all here to make controllers work better.