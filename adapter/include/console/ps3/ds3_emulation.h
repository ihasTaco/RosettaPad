/*
 * RosettaPad - PS3 / DualShock 3 Emulation Layer
 * ===============================================
 * 
 * This module handles all PS3-specific protocol emulation:
 * - Translates generic controller_state_t to DS3 input reports
 * - Manages DS3 feature reports (0xF2, 0xF5, 0xF7, etc.)
 * - Handles SET_REPORT commands from PS3
 * 
 * The emulation layer is CONSOLE-SPECIFIC. The controller layer is
 * CONTROLLER-SPECIFIC. This separation allows any controller to work
 * with any supported console.
 */

#ifndef ROSETTAPAD_PS3_DS3_EMULATION_H
#define ROSETTAPAD_PS3_DS3_EMULATION_H

#include <stdint.h>
#include <stddef.h>

#include "controllers/controller_interface.h"

/* ============================================================================
 * DS3 REPORT CONSTANTS
 * ============================================================================ */

#define DS3_INPUT_REPORT_SIZE   49
#define DS3_FEATURE_REPORT_SIZE 64

/* Report IDs */
#define DS3_REPORT_CAPABILITIES 0x01
#define DS3_REPORT_BT_MAC       0xF2
#define DS3_REPORT_PAIRING      0xF5
#define DS3_REPORT_CALIBRATION  0xF7
#define DS3_REPORT_STATUS       0xF8
#define DS3_REPORT_EF           0xEF

/* Battery status values */
#define DS3_BATTERY_SHUTDOWN    0x00
#define DS3_BATTERY_DYING       0x01
#define DS3_BATTERY_LOW         0x02
#define DS3_BATTERY_MEDIUM      0x03
#define DS3_BATTERY_HIGH        0x04
#define DS3_BATTERY_FULL        0x05
#define DS3_BATTERY_CHARGING    0xEE
#define DS3_BATTERY_CHARGED     0xEF

/* Connection status values */
#define DS3_STATUS_PLUGGED      0x02
#define DS3_STATUS_UNPLUGGED    0x03
#define DS3_CONN_USB            0x12
#define DS3_CONN_USB_RUMBLE     0x10
#define DS3_CONN_BT             0x16
#define DS3_CONN_BT_RUMBLE      0x14

/* ============================================================================
 * DS3 BUTTON MASKS
 * ============================================================================ */

/* Byte 2 */
#define DS3_BTN_SELECT      0x01
#define DS3_BTN_L3          0x02
#define DS3_BTN_R3          0x04
#define DS3_BTN_START       0x08
#define DS3_BTN_DPAD_UP     0x10
#define DS3_BTN_DPAD_RIGHT  0x20
#define DS3_BTN_DPAD_DOWN   0x40
#define DS3_BTN_DPAD_LEFT   0x80

/* Byte 3 */
#define DS3_BTN_L2          0x01
#define DS3_BTN_R2          0x02
#define DS3_BTN_L1          0x04
#define DS3_BTN_R1          0x08
#define DS3_BTN_TRIANGLE    0x10
#define DS3_BTN_CIRCLE      0x20
#define DS3_BTN_CROSS       0x40
#define DS3_BTN_SQUARE      0x80

/* Byte 4 */
#define DS3_BTN_PS          0x01

/* ============================================================================
 * DS3 REPORT OFFSETS
 * ============================================================================ */

#define DS3_OFF_REPORT_ID     0
#define DS3_OFF_BUTTONS1      2
#define DS3_OFF_BUTTONS2      3
#define DS3_OFF_PS_BUTTON     4
#define DS3_OFF_LX            6
#define DS3_OFF_LY            7
#define DS3_OFF_RX            8
#define DS3_OFF_RY            9
#define DS3_OFF_L2_PRESSURE   18
#define DS3_OFF_R2_PRESSURE   19
#define DS3_OFF_BATTERY       29
#define DS3_OFF_CHARGE        30
#define DS3_OFF_CONNECTION    31
#define DS3_OFF_ACCEL_X       40
#define DS3_OFF_ACCEL_Y       42
#define DS3_OFF_ACCEL_Z       44
#define DS3_OFF_GYRO_Z        46

/* ============================================================================
 * FUNCTIONS
 * ============================================================================ */

/**
 * Initialize DS3 emulation layer.
 */
void ds3_init(void);

/**
 * Set the host (Pi) Bluetooth MAC address in Report 0xF5.
 * This tells the PS3 which address to expect BT connections from.
 * @param mac 6-byte MAC address
 */
void ds3_set_host_mac(const uint8_t* mac);

/**
 * Get a feature report by ID.
 * @param report_id Report ID (0x01, 0xF2, 0xF5, etc.)
 * @param out_name Optional output for report name (for logging)
 * @return Pointer to report data, or NULL if unknown
 */
const uint8_t* ds3_get_feature_report(uint8_t report_id, const char** out_name);

/**
 * Handle SET_REPORT from PS3.
 * @param report_id Report ID
 * @param data Report data
 * @param len Data length
 */
void ds3_handle_set_report(uint8_t report_id, const uint8_t* data, size_t len);

/**
 * Build DS3 input report from generic controller state.
 * This is the main translation function.
 * 
 * @param state Generic controller state from any controller
 * @param out_report Output buffer (49 bytes)
 */
void ds3_build_input_report(const controller_state_t* state, uint8_t* out_report);

/**
 * Copy current DS3 report (thread-safe).
 * @param out_buf 49-byte output buffer
 */
void ds3_copy_report(uint8_t* out_buf);

/**
 * Get PS3's Bluetooth MAC (captured from SET_REPORT 0xF5).
 * @param out_mac 6-byte buffer
 * @return 0 if available, -1 if not yet received
 */
int ds3_get_ps3_mac(uint8_t* out_mac);

/**
 * Check if PS3 MAC has been captured.
 */
int ds3_has_ps3_mac(void);

/**
 * Parse DS3 output report (rumble/LED commands from PS3).
 * Updates the global controller output state.
 * 
 * @param data Output report data
 * @param len Data length
 */
void ds3_parse_output_report(const uint8_t* data, size_t len);

#endif /* ROSETTAPAD_PS3_DS3_EMULATION_H */