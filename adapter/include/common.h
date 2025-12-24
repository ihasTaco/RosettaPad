/*
 * RosettaPad - Common definitions and shared state
 */

#ifndef ROSETTAPAD_COMMON_H
#define ROSETTAPAD_COMMON_H

#include <stdint.h>
#include <pthread.h>

// =================================================================
// Global State
// =================================================================
extern volatile int g_running;
extern volatile int g_usb_enabled;

// =================================================================
// Standby Mode (PS3 power-off detection)
// =================================================================
typedef enum {
    SYSTEM_STATE_ACTIVE = 0,      // Normal operation
    SYSTEM_STATE_STANDBY,         // PS3 off, waiting for controller wake
    SYSTEM_STATE_WAKING           // Controller connected, waking PS3
} system_state_t;

extern volatile system_state_t g_system_state;
extern pthread_mutex_t g_system_state_mutex;

// Controller wake request - set when user presses PS button in standby
extern volatile int g_controller_wake_requested;

/**
 * Enter standby mode (PS3 powered off)
 * - Disconnects from PS3 (USB/BT)
 * - Puts DualSense into low-power mode (dim LEDs, stop rumble)
 * - Stops all transmission
 */
void system_enter_standby(void);

/**
 * Exit standby mode (user pressed PS button)
 * - Attempts to wake PS3 via Bluetooth
 * - Resumes normal operation
 */
void system_exit_standby(void);

/**
 * Check if in standby mode
 */
int system_is_standby(void);

/**
 * Set system state (thread-safe)
 */
void system_set_state(system_state_t state);

/**
 * Get system state (thread-safe)
 */
system_state_t system_get_state(void);

// File descriptors
extern int g_ep0_fd;
extern int g_ep1_fd;
extern int g_ep2_fd;
extern int g_hidraw_fd;

// =================================================================
// DS3 Report (shared between modules)
// =================================================================
#define DS3_REPORT_SIZE 49

extern uint8_t g_ds3_report[DS3_REPORT_SIZE];
extern pthread_mutex_t g_report_mutex;

// =================================================================
// Rumble State
// =================================================================
extern uint8_t g_rumble_right;
extern uint8_t g_rumble_left;
extern pthread_mutex_t g_rumble_mutex;

// =================================================================
// Lightbar State
// =================================================================
typedef struct {
    uint8_t r;
    uint8_t g;
    uint8_t b;
    uint8_t player_leds;
    uint8_t player_brightness;
} lightbar_state_t;

extern lightbar_state_t g_lightbar_state;
extern pthread_mutex_t g_lightbar_mutex;

// =================================================================
// Controller Output Callback
// =================================================================
// Function pointer type for controller-specific output handling
// Each controller module registers its own handler
typedef void (*controller_output_fn)(
    uint8_t rumble_right,
    uint8_t rumble_left,
    uint8_t led_r,
    uint8_t led_g,
    uint8_t led_b,
    uint8_t player_leds
);

/**
 * Register a controller output handler
 * Called by controller modules (dualsense.c, etc.) at init
 */
void controller_register_output(controller_output_fn handler);

/**
 * Unregister the controller output handler
 * Called when controller disconnects
 */
void controller_unregister_output(void);

/**
 * Send output to the currently registered controller
 * Called by the generic output thread
 */
void controller_send_output(
    uint8_t rumble_right,
    uint8_t rumble_left,
    uint8_t led_r,
    uint8_t led_g,
    uint8_t led_b,
    uint8_t player_leds
);

/**
 * Check if a controller output handler is registered
 */
int controller_has_output_handler(void);

/**
 * Generic controller output thread
 * Reads from g_rumble_* and g_lightbar_state, calls registered handler
 */
void* controller_output_thread(void* arg);

// =================================================================
// Bluetooth Status (for BT HID reports)
// =================================================================
// Controller modules populate this; bt_hid.c reads it
typedef struct {
    uint8_t plugged_status;    // DS3_STATUS_PLUGGED (0x02) or DS3_STATUS_UNPLUGGED (0x03)
    uint8_t battery_status;    // DS3_BATTERY_* values (0x00-0x05, 0xEE, 0xEF, 0xF1)
    uint8_t connection_type;   // DS3_CONN_BT (0x16) or DS3_CONN_BT_RUMBLE (0x14)
} bt_status_t;

extern bt_status_t g_bt_status;
extern pthread_mutex_t g_bt_status_mutex;

/**
 * Update Bluetooth status from controller module
 * Called by dualsense.c (or future controller modules)
 */
void bt_status_update(uint8_t plugged, uint8_t battery, uint8_t connection);

/**
 * Get current Bluetooth status (thread-safe copy)
 */
void bt_status_get(bt_status_t* out_status);

// =================================================================
// Touchpad-as-R3 Configuration
// =================================================================
extern volatile int g_touchpad_as_r3;  // 0=disabled, 1=enabled

// Touchpad state for tracking touch position
typedef struct {
    int active;          // Is finger touching?
    int initial_x;       // X position when touch started
    int initial_y;       // Y position when touch started
    int current_x;       // Current X position
    int current_y;       // Current Y position
} touchpad_state_t;

extern touchpad_state_t g_touchpad_state;
extern pthread_mutex_t g_touchpad_mutex;

// =================================================================
// Debug Utilities
// =================================================================
void print_hex(const char* label, const uint8_t* data, size_t len);

#endif // ROSETTAPAD_COMMON_H