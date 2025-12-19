/*
 * RosettaPad - DS3 Emulation Layer
 * Handles all PlayStation 3 / DualShock 3 protocol emulation
 */

#ifndef ROSETTAPAD_DS3_H
#define ROSETTAPAD_DS3_H

#include <stdint.h>
#include <stddef.h>

// =================================================================
// DS3 Battery / Connection Status (USB mode - shifted 9 bytes from BT)
// =================================================================

// Byte 29 - Plugged status
#define DS3_STATUS_PLUGGED      0x02
#define DS3_STATUS_UNPLUGGED    0x03

// Byte 30 - Battery level / charging status
// Maps to sixaxis_battery_capacity[] = { 0, 1, 25, 50, 75, 100 }
#define DS3_BATTERY_SHUTDOWN    0x00  // 0% - shutdown imminent
#define DS3_BATTERY_DYING       0x01  // ~1% - critical
#define DS3_BATTERY_LOW         0x02  // ~25% - low
#define DS3_BATTERY_MEDIUM      0x03  // ~50% - medium
#define DS3_BATTERY_HIGH        0x04  // ~75% - high
#define DS3_BATTERY_FULL        0x05  // ~100% - full (on battery)
#define DS3_BATTERY_CHARGING    0xEE  // Plugged in, charging
#define DS3_BATTERY_CHARGED     0xEF  // Plugged in, fully charged
#define DS3_BATTERY_NOT_CHARGING 0xF1 // Plugged in, not charging (error)

// Byte 31 - Connection mode
#define DS3_CONN_USB_RUMBLE     0x10  // USB connected, rumble on
#define DS3_CONN_USB            0x12  // USB connected, rumble off
#define DS3_CONN_BT_RUMBLE      0x14  // Bluetooth, rumble on
#define DS3_CONN_BT             0x16  // Bluetooth, rumble off

// =================================================================
// DS3 Feature Reports
// =================================================================

// Report sizes
#define DS3_FEATURE_REPORT_SIZE 64
#define DS3_INPUT_REPORT_SIZE   49
#define DS3_BT_INPUT_REPORT_SIZE 50  // 0xA1 header + 49 bytes

// Report IDs
#define DS3_REPORT_CAPABILITIES 0x01
#define DS3_REPORT_BT_MAC       0xF2
#define DS3_REPORT_PAIRING      0xF5
#define DS3_REPORT_CALIBRATION  0xF7
#define DS3_REPORT_STATUS       0xF8
#define DS3_REPORT_EF           0xEF

// =================================================================
// DS3 Button Masks - Byte 2 (ds3_report[2])
// =================================================================
#define DS3_BTN_SELECT    0x01
#define DS3_BTN_L3        0x02
#define DS3_BTN_R3        0x04
#define DS3_BTN_START     0x08
#define DS3_BTN_DPAD_UP   0x10
#define DS3_BTN_DPAD_RIGHT 0x20
#define DS3_BTN_DPAD_DOWN 0x40
#define DS3_BTN_DPAD_LEFT 0x80

// =================================================================
// DS3 Button Masks - Byte 3 (ds3_report[3])
// =================================================================
#define DS3_BTN_L2        0x01
#define DS3_BTN_R2        0x02
#define DS3_BTN_L1        0x04
#define DS3_BTN_R1        0x08
#define DS3_BTN_TRIANGLE  0x10
#define DS3_BTN_CIRCLE    0x20
#define DS3_BTN_CROSS     0x40
#define DS3_BTN_SQUARE    0x80

// =================================================================
// DS3 Button Masks - Byte 4 (ds3_report[4])
// =================================================================
#define DS3_BTN_PS        0x01

// =================================================================
// DS3 Report Byte Offsets
// =================================================================
#define DS3_OFF_REPORT_ID     0
#define DS3_OFF_RESERVED1     1
#define DS3_OFF_BUTTONS1      2   // Select, L3, R3, Start, D-pad
#define DS3_OFF_BUTTONS2      3   // L2, R2, L1, R1, Triangle, Circle, Cross, Square
#define DS3_OFF_PS_BUTTON     4
#define DS3_OFF_RESERVED2     5
#define DS3_OFF_LX            6
#define DS3_OFF_LY            7
#define DS3_OFF_RX            8
#define DS3_OFF_RY            9
#define DS3_OFF_DPAD_UP_P     10  // Pressure
#define DS3_OFF_DPAD_RIGHT_P  11
#define DS3_OFF_DPAD_DOWN_P   12
#define DS3_OFF_DPAD_LEFT_P   13
#define DS3_OFF_L2_PRESSURE   18
#define DS3_OFF_R2_PRESSURE   19
#define DS3_OFF_L1_PRESSURE   20
#define DS3_OFF_R1_PRESSURE   21
#define DS3_OFF_TRIANGLE_P    22
#define DS3_OFF_CIRCLE_P      23
#define DS3_OFF_CROSS_P       24
#define DS3_OFF_SQUARE_P      25
#define DS3_OFF_BATTERY       29  // Plugged status
#define DS3_OFF_CHARGE        30  // Battery level / charging status
#define DS3_OFF_CONNECTION    31  // Connection mode (USB/BT, rumble on/off)
#define DS3_OFF_ACCEL_X       40  // Accelerometer X (little-endian 16-bit)
#define DS3_OFF_ACCEL_Y       42
#define DS3_OFF_ACCEL_Z       44
#define DS3_OFF_GYRO_Z        46  // Gyroscope Z (yaw only)

// =================================================================
// Functions
// =================================================================

/**
 * Initialize DS3 emulation layer
 * Sets up default report values
 */
void ds3_init(void);

/**
 * Get pointer to a feature report by ID
 * @param report_id The report ID (0x01, 0xF2, 0xF5, 0xF7, 0xF8, 0xEF)
 * @param out_name Optional pointer to receive report name string
 * @return Pointer to report data, or NULL if unknown
 */
const uint8_t* ds3_get_feature_report(uint8_t report_id, const char** out_name);

/**
 * Handle SET_REPORT from PS3
 * @param report_id The report ID
 * @param data The report data
 * @param len Length of data
 */
void ds3_handle_set_report(uint8_t report_id, const uint8_t* data, size_t len);

/**
 * Convert D-pad hat value to DS3 button mask
 * @param hat_value 0-7 for directions, 8 for center
 * @return DS3 d-pad button mask
 */
uint8_t ds3_convert_dpad(uint8_t hat_value);

/**
 * Update DS3 report with new input values
 * Thread-safe - acquires report mutex internally
 */
void ds3_update_report(
    uint8_t buttons1,    // DS3_BTN_SELECT, L3, R3, START, D-pad
    uint8_t buttons2,    // DS3_BTN_L2, R2, L1, R1, face buttons
    uint8_t ps_button,   // DS3_BTN_PS
    uint8_t lx, uint8_t ly,
    uint8_t rx, uint8_t ry,
    uint8_t l2, uint8_t r2,
    uint8_t triangle_p, uint8_t circle_p,
    uint8_t cross_p, uint8_t square_p
);

/**
 * Update motion sensor data in DS3 report
 * @param accel_x, accel_y, accel_z Accelerometer values (centered ~512)
 * @param gyro_z Gyroscope Z axis (yaw)
 */
void ds3_update_motion(int16_t accel_x, int16_t accel_y, int16_t accel_z, int16_t gyro_z);

/**
 * Update battery status from DualSense battery level
 * Converts DualSense battery percentage to DS3 format
 * @param ds_battery_level DualSense battery level (0-100)
 * @param ds_charging 1 if charging, 0 if not
 */
void ds3_update_battery_from_dualsense(uint8_t ds_battery_level, int ds_charging);

/**
 * Copy current DS3 report to buffer (thread-safe)
 * @param out_buf Buffer to copy to (must be DS3_INPUT_REPORT_SIZE bytes)
 */
void ds3_copy_report(uint8_t* out_buf);

/**
 * Copy current DS3 report in Bluetooth format (thread-safe)
 * Adjusts connection status byte and swaps motion sensor endianness
 * @param out_buf Buffer to copy to (must be DS3_BT_INPUT_REPORT_SIZE bytes)
 *                Will contain: 0xA1 header + 49 bytes report
 */
void ds3_copy_bt_report(uint8_t* out_buf);

/**
 * Save Bluetooth pairing information
 * Called when PS3 sends SET_REPORT 0xF5 during USB pairing
 * @param ps3_addr PS3's Bluetooth address string
 */
void ds3_save_pairing(const char* ps3_addr);

/**
 * Get stored PS3 Bluetooth address (from pairing)
 * @return Address string or NULL if not paired
 */
const char* ds3_get_ps3_address(void);

/**
 * Get local Bluetooth address
 * @return Address string
 */
const char* ds3_get_local_address(void);

/**
 * Update F2 report with correct local Bluetooth MAC
 * Call after Bluetooth is initialized since /sys/class/bluetooth/hci0
 * may not exist during early init
 * @param mac 6-byte MAC address
 */
void ds3_set_local_bt_mac(const uint8_t* mac);

#endif // ROSETTAPAD_DS3_H