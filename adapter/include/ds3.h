/*
 * RosettaPad - DS3 Emulation Layer
 * Handles all PlayStation 3 / DualShock 3 protocol emulation
 */

#ifndef ROSETTAPAD_DS3_H
#define ROSETTAPAD_DS3_H

#include <stdint.h>
#include <stddef.h>

// =================================================================
// DS3 Battery / Connection Status (USB mode)
// =================================================================

// Byte 29 - Plugged status
#define DS3_STATUS_PLUGGED      0x02
#define DS3_STATUS_UNPLUGGED    0x03

// Byte 30 - Battery level / charging status
#define DS3_BATTERY_SHUTDOWN    0x00
#define DS3_BATTERY_DYING       0x01
#define DS3_BATTERY_LOW         0x02
#define DS3_BATTERY_MEDIUM      0x03
#define DS3_BATTERY_HIGH        0x04
#define DS3_BATTERY_FULL        0x05
#define DS3_BATTERY_CHARGING    0xEE
#define DS3_BATTERY_CHARGED     0xEF
#define DS3_BATTERY_NOT_CHARGING 0xF1

// Byte 31 - Connection mode
#define DS3_CONN_USB_RUMBLE     0x10
#define DS3_CONN_USB            0x12
#define DS3_CONN_BT_RUMBLE      0x14
#define DS3_CONN_BT             0x16

// =================================================================
// DS3 Feature Reports
// =================================================================

#define DS3_FEATURE_REPORT_SIZE 64
#define DS3_INPUT_REPORT_SIZE   49

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
#define DS3_OFF_BUTTONS1      2
#define DS3_OFF_BUTTONS2      3
#define DS3_OFF_PS_BUTTON     4
#define DS3_OFF_RESERVED2     5
#define DS3_OFF_LX            6
#define DS3_OFF_LY            7
#define DS3_OFF_RX            8
#define DS3_OFF_RY            9
#define DS3_OFF_DPAD_UP_P     10
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
#define DS3_OFF_BATTERY       29
#define DS3_OFF_CHARGE        30
#define DS3_OFF_CONNECTION    31
#define DS3_OFF_ACCEL_X       40
#define DS3_OFF_ACCEL_Y       42
#define DS3_OFF_ACCEL_Z       44
#define DS3_OFF_GYRO_Z        46

// =================================================================
// Functions
// =================================================================

/**
 * Initialize DS3 emulation layer
 */
void ds3_init(void);

/**
 * Set the host (Pi) Bluetooth MAC address in Report 0xF5
 * This tells the PS3 which Bluetooth address to expect connections from
 * @param mac 6-byte MAC address
 */
void ds3_set_host_mac(const uint8_t* mac);

/**
 * Set the host MAC from a string
 * @param mac_str MAC address string "XX:XX:XX:XX:XX:XX"
 * @return 0 on success, -1 on invalid format
 */
int ds3_set_host_mac_str(const char* mac_str);

/**
 * Get the host MAC that's currently set in Report 0xF5
 * @param out_mac Buffer for 6-byte MAC
 */
void ds3_get_host_mac(uint8_t* out_mac);

/**
 * Get pointer to a feature report by ID
 */
const uint8_t* ds3_get_feature_report(uint8_t report_id, const char** out_name);

/**
 * Handle SET_REPORT from PS3
 */
void ds3_handle_set_report(uint8_t report_id, const uint8_t* data, size_t len);

/**
 * Convert D-pad hat value to DS3 button mask
 */
uint8_t ds3_convert_dpad(uint8_t hat_value);

/**
 * Update DS3 report with new input values
 */
void ds3_update_report(
    uint8_t buttons1, uint8_t buttons2, uint8_t ps_button,
    uint8_t lx, uint8_t ly, uint8_t rx, uint8_t ry,
    uint8_t l2, uint8_t r2,
    uint8_t triangle_p, uint8_t circle_p, uint8_t cross_p, uint8_t square_p
);

/**
 * Update motion sensor data
 */
void ds3_update_motion(int16_t accel_x, int16_t accel_y, int16_t accel_z, int16_t gyro_z);

/**
 * Copy current DS3 report (thread-safe)
 */
void ds3_copy_report(uint8_t* out_buf);

/**
 * Update battery status
 */
void ds3_update_battery(uint8_t plugged, uint8_t battery, uint8_t connection);

/**
 * Update battery from DualSense values
 */
void ds3_update_battery_from_dualsense(uint8_t ds_battery_level, int ds_charging);

/**
 * Get PS3's Bluetooth MAC (captured from SET_REPORT 0xF5)
 * @param out_mac 6-byte buffer for MAC
 * @return 0 if MAC available, -1 if not yet received
 */
int ds3_get_ps3_mac(uint8_t* out_mac);

/**
 * Check if PS3 MAC has been captured
 * @return 1 if available, 0 if not
 */
int ds3_has_ps3_mac(void);

#endif // ROSETTAPAD_DS3_H