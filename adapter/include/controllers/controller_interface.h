/*
 * RosettaPad - Controller Interface Template
 * ============================================
 * 
 * This header defines the interface that ALL controller implementations must follow.
 * To add support for a new controller (Xbox, 8BitDo, Switch Pro, etc.):
 * 
 * 1. Create a new directory: controllers/your_controller/
 * 2. Copy this template and implement all required functions
 * 3. Register your controller in controllers/controller_registry.c
 * 
 * ARCHITECTURE OVERVIEW:
 * ----------------------
 * 
 *   [Physical Controller]
 *          |
 *          v (Bluetooth HID / USB)
 *   [Controller Driver]  <-- You implement this
 *          |
 *          v (Generic controller_state_t)
 *   [Console Emulation Layer]
 *          |
 *          v (Console-specific protocol)
 *   [Target Console]
 * 
 * Your controller driver translates hardware-specific input into the generic
 * controller_state_t format. The console emulation layer then translates that
 * into whatever the target console expects (DS3 reports for PS3, etc.)
 * 
 * REQUIRED IMPLEMENTATIONS:
 * -------------------------
 * 
 * 1. controller_info_t - Static metadata about your controller
 * 2. init() / shutdown() - Lifecycle management
 * 3. find_device() - Locate the controller (hidraw, evdev, etc.)
 * 4. poll_input() - Read and parse input, populate controller_state_t
 * 5. send_output() - Handle rumble, LEDs, etc.
 * 
 * See controllers/dualsense/ for a complete reference implementation.
 */

#ifndef ROSETTAPAD_CONTROLLER_INTERFACE_H
#define ROSETTAPAD_CONTROLLER_INTERFACE_H

#include <stdint.h>
#include <stddef.h>

/* ============================================================================
 * CAPABILITY FLAGS
 * 
 * Indicate what features your controller supports. The console emulation
 * layer uses these to know what data to expect and what features to enable.
 * ============================================================================ */

#define CONTROLLER_CAP_BUTTONS      (1 << 0)   /* Basic buttons (all controllers) */
#define CONTROLLER_CAP_ANALOG_STICKS (1 << 1)  /* Dual analog sticks */
#define CONTROLLER_CAP_TRIGGERS     (1 << 2)   /* Analog triggers (L2/R2) */
#define CONTROLLER_CAP_RUMBLE       (1 << 3)   /* Haptic feedback / rumble */
#define CONTROLLER_CAP_MOTION       (1 << 4)   /* Accelerometer / gyroscope */
#define CONTROLLER_CAP_TOUCHPAD     (1 << 5)   /* Touchpad input */
#define CONTROLLER_CAP_LIGHTBAR     (1 << 6)   /* RGB LED control */
#define CONTROLLER_CAP_PLAYER_LEDS  (1 << 7)   /* Player indicator LEDs */
#define CONTROLLER_CAP_BATTERY      (1 << 8)   /* Battery level reporting */
#define CONTROLLER_CAP_AUDIO        (1 << 9)   /* Built-in speaker/mic */

/* ============================================================================
 * GENERIC BUTTON DEFINITIONS
 * 
 * These are abstract button IDs. Your controller driver maps hardware-specific
 * buttons to these generic IDs. The console layer then maps these to whatever
 * the target console expects.
 * ============================================================================ */

/* Face buttons */
#define BTN_SOUTH       0   /* Cross / A / B (Nintendo) */
#define BTN_EAST        1   /* Circle / B / A (Nintendo) */
#define BTN_WEST        2   /* Square / X / Y (Nintendo) */
#define BTN_NORTH       3   /* Triangle / Y / X (Nintendo) */

/* Shoulder buttons */
#define BTN_L1          4   /* L1 / LB */
#define BTN_R1          5   /* R1 / RB */
#define BTN_L2          6   /* L2 / LT (digital) */
#define BTN_R2          7   /* R2 / RT (digital) */

/* Stick clicks */
#define BTN_L3          8   /* Left stick click */
#define BTN_R3          9   /* Right stick click */

/* Center buttons */
#define BTN_SELECT      10  /* Select / Share / - */
#define BTN_START       11  /* Start / Options / + */
#define BTN_HOME        12  /* PS / Xbox / Home */
#define BTN_TOUCHPAD    13  /* Touchpad click (PlayStation) */
#define BTN_MUTE        14  /* Mute button (DualSense) */

/* D-pad */
#define BTN_DPAD_UP     15
#define BTN_DPAD_DOWN   16
#define BTN_DPAD_LEFT   17
#define BTN_DPAD_RIGHT  18

#define BTN_COUNT       19  /* Total number of buttons */

/* ============================================================================
 * CONTROLLER STATE
 * 
 * This is the generic input state that your controller driver populates.
 * The console emulation layer reads from this to generate console-specific
 * reports.
 * ============================================================================ */

typedef struct {
    /* Button states - bitmask using BTN_* defines above */
    uint32_t buttons;
    
    /* Analog sticks - 0-255 range, 128 = center */
    uint8_t left_stick_x;
    uint8_t left_stick_y;
    uint8_t right_stick_x;
    uint8_t right_stick_y;
    
    /* Analog triggers - 0-255 range */
    uint8_t left_trigger;
    uint8_t right_trigger;
    
    /* Motion sensors (if CONTROLLER_CAP_MOTION) */
    /* Raw sensor values - controller-specific scaling */
    /* Console layer handles conversion to target format */
    int16_t accel_x;
    int16_t accel_y;
    int16_t accel_z;
    int16_t gyro_x;
    int16_t gyro_y;
    int16_t gyro_z;
    
    /* Touchpad (if CONTROLLER_CAP_TOUCHPAD) */
    struct {
        uint8_t active;     /* Is finger touching? */
        uint16_t x;         /* X position */
        uint16_t y;         /* Y position */
    } touch[2];             /* Support up to 2 touch points */
    
    /* Battery (if CONTROLLER_CAP_BATTERY) */
    uint8_t battery_level;  /* 0-100 percentage */
    uint8_t battery_charging; /* 1 if charging, 0 if not */
    uint8_t battery_full;   /* 1 if fully charged, 0 if not */
    
    /* Timestamp for input freshness */
    uint64_t timestamp_ms;
    
} controller_state_t;

/* ============================================================================
 * OUTPUT STATE
 * 
 * Data sent TO the controller (rumble, LEDs, etc.)
 * ============================================================================ */

typedef struct {
    /* Rumble motors - 0-255 intensity */
    uint8_t rumble_left;    /* Strong/low-frequency motor */
    uint8_t rumble_right;   /* Weak/high-frequency motor */
    
    /* Lightbar RGB (if CONTROLLER_CAP_LIGHTBAR) */
    uint8_t led_r;
    uint8_t led_g;
    uint8_t led_b;
    
    /* Player LEDs (if CONTROLLER_CAP_PLAYER_LEDS) */
    uint8_t player_leds;    /* Bitmask of which LEDs are on */
    uint8_t player_brightness; /* 0-255 brightness */
    
} controller_output_t;

/* ============================================================================
 * CONTROLLER INFO
 * 
 * Static metadata about your controller. Used for device detection and
 * capability reporting.
 * ============================================================================ */

typedef struct {
    /* Identification */
    const char* name;           /* Human-readable name, e.g. "DualSense" */
    const char* manufacturer;   /* e.g. "Sony" */
    uint16_t vendor_id;         /* USB VID */
    uint16_t product_id;        /* USB PID */
    
    /* Capabilities bitmask */
    uint32_t capabilities;
    
    /* Connection type info */
    uint8_t supports_bluetooth;
    uint8_t supports_usb;
    
} controller_info_t;

/* ============================================================================
 * CONTROLLER DRIVER INTERFACE
 * 
 * Function pointers that each controller driver must implement.
 * ============================================================================ */

typedef struct controller_driver {
    /* Static info about this controller */
    const controller_info_t* info;
    
    /**
     * Initialize the controller subsystem.
     * Called once at startup. Set up any resources (CRC tables, etc.)
     * 
     * @return 0 on success, -1 on failure
     */
    int (*init)(void);
    
    /**
     * Shutdown the controller subsystem.
     * Clean up resources. Called on program exit.
     */
    void (*shutdown)(void);
    
    /**
     * Find and open the controller device.
     * Scan for the controller (hidraw, evdev, etc.) and open it.
     * 
     * @return File descriptor on success, -1 if not found
     */
    int (*find_device)(void);
    
    /**
     * Check if a given VID/PID matches this controller.
     * Used by the device scanner to identify controllers.
     * 
     * @param vid USB Vendor ID
     * @param pid USB Product ID
     * @return 1 if this controller, 0 if not
     */
    int (*match_device)(uint16_t vid, uint16_t pid);
    
    /**
     * Process input data and populate controller state.
     * Called when data is available on the device fd.
     * 
     * @param buf Raw input report from device
     * @param len Length of input data
     * @param out_state Output: populated with current controller state
     * @return 0 on success, -1 on parse error
     */
    int (*process_input)(const uint8_t* buf, size_t len, controller_state_t* out_state);
    
    /**
     * Send output (rumble, LEDs) to the controller.
     * 
     * @param fd Device file descriptor
     * @param output Output state to apply
     * @return 0 on success, -1 on error
     */
    int (*send_output)(int fd, const controller_output_t* output);
    
    /**
     * Handle controller disconnect.
     * Clean up any state. The fd will be closed by the framework.
     */
    void (*on_disconnect)(void);
    
    /**
     * Optional: Enter low-power mode.
     * Called when entering standby. Turn off LEDs, stop rumble, etc.
     * 
     * @param fd Device file descriptor
     */
    void (*enter_low_power)(int fd);
    
} controller_driver_t;

/* ============================================================================
 * CONTROLLER REGISTRATION
 * 
 * Controllers register themselves with the framework. The main loop scans
 * for devices and matches them against registered drivers.
 * ============================================================================ */

/**
 * Register a controller driver.
 * Called at startup by each controller module.
 * 
 * @param driver Pointer to driver implementation (must be static/persistent)
 * @return 0 on success, -1 if registry full
 */
int controller_register(const controller_driver_t* driver);

/**
 * Find a driver that matches the given VID/PID.
 * 
 * @param vid USB Vendor ID
 * @param pid USB Product ID
 * @return Pointer to matching driver, or NULL if none found
 */
const controller_driver_t* controller_find_driver(uint16_t vid, uint16_t pid);

/**
 * Get the currently active controller driver.
 * 
 * @return Pointer to active driver, or NULL if no controller connected
 */
const controller_driver_t* controller_get_active(void);

/**
 * Get the current controller state.
 * Thread-safe copy of the latest input state.
 * 
 * @param out_state Output buffer for state
 * @return 0 on success, -1 if no controller connected
 */
int controller_get_state(controller_state_t* out_state);

/* ============================================================================
 * UTILITY MACROS
 * ============================================================================ */

/* Button state helpers */
#define CONTROLLER_BTN_PRESSED(state, btn)  ((state)->buttons & (1 << (btn)))
#define CONTROLLER_BTN_SET(state, btn)      ((state)->buttons |= (1 << (btn)))
#define CONTROLLER_BTN_CLEAR(state, btn)    ((state)->buttons &= ~(1 << (btn)))

/* Stick deadzone application */
#define CONTROLLER_APPLY_DEADZONE(val, deadzone) \
    (((val) >= (128 - (deadzone)) && (val) <= (128 + (deadzone))) ? 128 : (val))

#endif /* ROSETTAPAD_CONTROLLER_INTERFACE_H */