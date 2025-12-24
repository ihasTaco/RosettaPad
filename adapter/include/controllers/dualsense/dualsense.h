/*
 * RosettaPad - DualSense (PS5) Controller Driver
 * ===============================================
 * 
 * Reference implementation of the controller interface.
 * Use this as a template for adding new controllers.
 * 
 * IMPLEMENTATION NOTES:
 * ---------------------
 * 
 * The DualSense communicates via Bluetooth HID with 78-byte input reports.
 * Output reports (rumble, LEDs) are also 78 bytes with CRC32 validation.
 * 
 * LED control uses a hybrid approach:
 * - Lightbar: Controlled via kernel sysfs (avoids driver conflicts)
 * - Rumble: Sent via hidraw output reports
 * 
 * This split is necessary because the hid-playstation kernel driver
 * manages LEDs, and sending LED commands via hidraw conflicts with it.
 */

#ifndef ROSETTAPAD_DUALSENSE_H
#define ROSETTAPAD_DUALSENSE_H

#include "controllers/controller_interface.h"

/* ============================================================================
 * DEVICE IDENTIFICATION
 * ============================================================================ */

#define DUALSENSE_VID     0x054C   /* Sony */
#define DUALSENSE_PID     0x0CE6   /* DualSense */

/* ============================================================================
 * BLUETOOTH REPORT FORMAT
 * ============================================================================ */

#define DS_BT_REPORT_ID       0x31
#define DS_BT_INPUT_SIZE      78
#define DS_BT_OUTPUT_SIZE     78

/* Input report byte offsets */
#define DS_OFF_REPORT_ID      0
#define DS_OFF_COUNTER        1
#define DS_OFF_LX             2
#define DS_OFF_LY             3
#define DS_OFF_RX             4
#define DS_OFF_RY             5
#define DS_OFF_L2             6
#define DS_OFF_R2             7
#define DS_OFF_STATUS         8
#define DS_OFF_BUTTONS1       9    /* D-pad (low nibble) + face buttons */
#define DS_OFF_BUTTONS2       10   /* Shoulders, sticks, options/create */
#define DS_OFF_BUTTONS3       11   /* PS, touchpad, mute */
#define DS_OFF_GYRO_X         16
#define DS_OFF_GYRO_Y         18
#define DS_OFF_GYRO_Z         20
#define DS_OFF_ACCEL_X        22
#define DS_OFF_ACCEL_Y        24
#define DS_OFF_ACCEL_Z        26
#define DS_OFF_TOUCHPAD       34
#define DS_OFF_BATTERY        54

/* Button masks - Byte 9 (buttons1) */
#define DS_BTN1_SQUARE        0x10
#define DS_BTN1_CROSS         0x20
#define DS_BTN1_CIRCLE        0x40
#define DS_BTN1_TRIANGLE      0x80

/* Button masks - Byte 10 (buttons2) */
#define DS_BTN2_L1            0x01
#define DS_BTN2_R1            0x02
#define DS_BTN2_L2            0x04
#define DS_BTN2_R2            0x08
#define DS_BTN2_CREATE        0x10
#define DS_BTN2_OPTIONS       0x20
#define DS_BTN2_L3            0x40
#define DS_BTN2_R3            0x80

/* Button masks - Byte 11 (buttons3) */
#define DS_BTN3_PS            0x01
#define DS_BTN3_TOUCHPAD      0x02
#define DS_BTN3_MUTE          0x04

/* Touchpad constants */
#define DS_TOUCHPAD_WIDTH     1920
#define DS_TOUCHPAD_HEIGHT    1080
#define DS_TOUCH_INACTIVE     0x80

/* ============================================================================
 * CALIBRATION DATA
 * 
 * DualSense provides calibration data via Feature Report 0x05 (41 bytes).
 * This data defines the sensor ranges and biases for proper motion scaling.
 * ============================================================================ */

#define DS_FEATURE_REPORT_CALIBRATION       0x05
#define DS_FEATURE_REPORT_CALIBRATION_SIZE  41

/* DualSense hardware limits (from kernel hid-playstation.c) */
#define DS_ACC_RES_PER_G       8192   /* Accelerometer resolution per g */
#define DS_ACC_RANGE           (4 * DS_ACC_RES_PER_G)  /* ±4g range */
#define DS_GYRO_RES_PER_DEG_S  1024   /* Gyroscope resolution per degree/s */
#define DS_GYRO_RANGE          (2048 * DS_GYRO_RES_PER_DEG_S)  /* ±2048 deg/s */

/* Per-axis calibration data */
typedef struct {
    int16_t bias;       /* Zero offset */
    int sens_numer;     /* Sensitivity numerator */
    int sens_denom;     /* Sensitivity denominator */
} ds_axis_calib_t;

/* Full calibration structure */
typedef struct {
    ds_axis_calib_t gyro[3];   /* Pitch, Yaw, Roll */
    ds_axis_calib_t accel[3];  /* X, Y, Z */
    int valid;                  /* 1 if calibration loaded successfully */
} ds_calibration_t;

/* ============================================================================
 * DRIVER INTERFACE
 * ============================================================================ */

/**
 * Get the DualSense driver instance.
 * Used for registration with the controller framework.
 */
const controller_driver_t* dualsense_get_driver(void);

/**
 * Register the DualSense driver with the controller framework.
 * Called at startup.
 */
void dualsense_register(void);

/* ============================================================================
 * INTERNAL FUNCTIONS (exposed for testing)
 * ============================================================================ */

/**
 * Calculate CRC32 for DualSense Bluetooth output reports.
 */
uint32_t dualsense_calc_crc32(const uint8_t* data, size_t len);

/**
 * Parse D-pad value from buttons1 byte.
 * @param buttons1 Raw buttons1 byte from input report
 * @param out_state State to update with D-pad buttons
 */
void dualsense_parse_dpad(uint8_t buttons1, controller_state_t* out_state);

#endif /* ROSETTAPAD_DUALSENSE_H */