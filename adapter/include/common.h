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