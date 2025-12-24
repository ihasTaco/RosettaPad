/*
 * RosettaPad - DualSense (PS5) Controller Interface
 * Handles Bluetooth communication with DualSense controllers
 */

#ifndef ROSETTAPAD_DUALSENSE_H
#define ROSETTAPAD_DUALSENSE_H

#include <stdint.h>
#include <stddef.h>

// =================================================================
// DualSense Identifiers
// =================================================================
#define DUALSENSE_VID     0x054C
#define DUALSENSE_PID     0x0CE6

// =================================================================
// DualSense Bluetooth Report Format
// =================================================================
#define DS_BT_REPORT_ID       0x31
#define DS_BT_INPUT_SIZE      78
#define DS_BT_OUTPUT_SIZE     78   // Fixed: was 79, should be 78

// Input report byte offsets (for report ID 0x31)
#define DS_OFF_REPORT_ID      0
#define DS_OFF_COUNTER        1
#define DS_OFF_LX             2
#define DS_OFF_LY             3
#define DS_OFF_RX             4
#define DS_OFF_RY             5
#define DS_OFF_L2             6
#define DS_OFF_R2             7
#define DS_OFF_STATUS         8
#define DS_OFF_BUTTONS1       9   // D-pad (low nibble) + face buttons (high nibble)
#define DS_OFF_BUTTONS2       10  // Shoulders, sticks, options/create
#define DS_OFF_BUTTONS3       11  // PS, touchpad, mute
#define DS_OFF_GYRO_X         16  // Gyroscope (16-bit LE)
#define DS_OFF_GYRO_Y         18
#define DS_OFF_GYRO_Z         20
#define DS_OFF_ACCEL_X        22  // Accelerometer (16-bit LE)
#define DS_OFF_ACCEL_Y        24
#define DS_OFF_ACCEL_Z        26
#define DS_OFF_TOUCHPAD       34  // Touchpad data (BT report: offset 2 + struct offset 32)
#define DS_OFF_BATTERY        54  // Battery level and status

// Touchpad constants
#define DS_TOUCHPAD_WIDTH     1920
#define DS_TOUCHPAD_HEIGHT    1080
#define DS_TOUCH_POINT_SIZE   4    // Each touch point is 4 bytes
#define DS_TOUCH_INACTIVE     0x80 // Bit 7 set = finger not touching

// =================================================================
// DualSense Button Masks - Byte 9 (buttons1)
// =================================================================
// D-pad is in low nibble (0-7 = directions, 8 = center)
#define DS_BTN1_SQUARE    0x10
#define DS_BTN1_CROSS     0x20
#define DS_BTN1_CIRCLE    0x40
#define DS_BTN1_TRIANGLE  0x80

// =================================================================
// DualSense Button Masks - Byte 10 (buttons2)
// =================================================================
#define DS_BTN2_L1        0x01
#define DS_BTN2_R1        0x02
#define DS_BTN2_L2        0x04
#define DS_BTN2_R2        0x08
#define DS_BTN2_CREATE    0x10
#define DS_BTN2_OPTIONS   0x20
#define DS_BTN2_L3        0x40
#define DS_BTN2_R3        0x80

// =================================================================
// DualSense Button Masks - Byte 11 (buttons3)
// =================================================================
#define DS_BTN3_PS        0x01
#define DS_BTN3_TOUCHPAD  0x02
#define DS_BTN3_MUTE      0x04

// =================================================================
// Functions
// =================================================================

/**
 * Initialize DualSense subsystem
 * Sets up CRC32 table for BT output reports
 */
void dualsense_init(void);

/**
 * Find and open a DualSense controller hidraw device
 * @return File descriptor on success, -1 on failure
 */
int dualsense_find_hidraw(void);

/**
 * Parse DualSense input report and update DS3 report
 * @param buf Raw input report data
 * @param len Length of data
 * @return 0 on success, -1 if not a valid report
 */
int dualsense_process_input(const uint8_t* buf, size_t len);

/**
 * Send output report to DualSense
 * @param fd hidraw file descriptor
 * @param right_motor Right rumble motor (0-255)
 * @param left_motor Left rumble motor (0-255)
 * @param led_r, led_g, led_b Lightbar RGB values
 * @param player_leds Player LED bitmask
 */
void dualsense_send_output(int fd, 
    uint8_t right_motor, uint8_t left_motor,
    uint8_t led_r, uint8_t led_g, uint8_t led_b,
    uint8_t player_leds);

/**
 * Send rumble command (uses current lightbar state)
 * @param fd hidraw file descriptor
 * @param right_motor Right rumble motor
 * @param left_motor Left rumble motor
 */
void dualsense_send_rumble(int fd, uint8_t right_motor, uint8_t left_motor);

/**
 * Calculate CRC32 for DualSense BT output reports
 * @param data Data to calculate CRC for
 * @param len Length of data
 * @return CRC32 value
 */
uint32_t dualsense_calc_crc32(const uint8_t* data, size_t len);

/**
 * DualSense input polling thread function
 * @param arg Unused
 * @return NULL
 */
void* dualsense_thread(void* arg);

/**
 * Shutdown the DualSense controller
 * Turns off rumble, LEDs, and disconnects
 * Used when entering standby mode
 */
void dualsense_shutdown(void);

/**
 * Check if DualSense controller is connected
 * @return 1 if connected, 0 if not
 */
int dualsense_is_connected(void);

#endif // ROSETTAPAD_DUALSENSE_H