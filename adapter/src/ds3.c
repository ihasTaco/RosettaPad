/*
 * RosettaPad - DS3 Emulation Layer
 * Handles all PlayStation 3 / DualShock 3 protocol emulation
 */

#include <stdio.h>
#include <string.h>
#include <pthread.h>
#include "common.h"
#include "ds3.h"

// =================================================================
// DS3 Feature Reports - From real DS3 captures
// =================================================================

// Report 0x01 (Capabilities)
static uint8_t report_01[DS3_FEATURE_REPORT_SIZE] = {
    0x00, 0x01, 0x04, 0x00, 0x08, 0x0c, 0x01, 0x02, 0x18, 0x18, 0x18, 0x18, 0x09, 0x0a, 0x10, 0x11,
    0x12, 0x13, 0x00, 0x00, 0x00, 0x00, 0x04, 0x00, 0x02, 0x02, 0x02, 0x02, 0x00, 0x00, 0x00, 0x04,
    0x04, 0x04, 0x04, 0x00, 0x00, 0x04, 0x00, 0x01, 0x02, 0x07, 0x00, 0x17, 0x00, 0x00, 0x00, 0x00,
    0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00
};

// Report 0xF2 (Controller Bluetooth MAC)
static uint8_t report_f2[DS3_FEATURE_REPORT_SIZE] = {
    0xf2, 0xff, 0xff, 0x00, 0x34, 0xc7, 0x31, 0x25, 0xae, 0x60, 0x00, 0x03, 0x50, 0x81, 0xd8, 0x01,
    0x8a, 0x13, 0x00, 0x00, 0x00, 0x00, 0x04, 0x00, 0x02, 0x02, 0x02, 0x02, 0x00, 0x00, 0x00, 0x04,
    0x04, 0x04, 0x04, 0x00, 0x00, 0x04, 0x00, 0x01, 0x02, 0x07, 0x00, 0x17, 0x00, 0x00, 0x00, 0x00,
    0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00
};

// Report 0xF5 (Host Bluetooth MAC / Pairing)
static uint8_t report_f5[DS3_FEATURE_REPORT_SIZE] = {
    0x01, 0x00, 0x38, 0x4f, 0xf0, 0x10, 0x02, 0x41, 0xae, 0x60, 0x00, 0x03, 0x50, 0x81, 0xd8, 0x01,
    0x8a, 0x13, 0x00, 0x00, 0x00, 0x00, 0x04, 0x00, 0x02, 0x02, 0x02, 0x02, 0x00, 0x00, 0x00, 0x04,
    0x04, 0x04, 0x04, 0x00, 0x00, 0x04, 0x00, 0x01, 0x02, 0x07, 0x00, 0x17, 0x00, 0x00, 0x00, 0x00,
    0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00
};

// Report 0xF7 (Calibration data)
static uint8_t report_f7[DS3_FEATURE_REPORT_SIZE] = {
    0x02, 0x01, 0xf7, 0x02, 0xcb, 0x01, 0xef, 0xff, 0x14, 0x33, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
    0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
    0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
    0x05, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00
};

// Report 0xF8 (Status)
static uint8_t report_f8[DS3_FEATURE_REPORT_SIZE] = {
    0x00, 0x01, 0x00, 0x00, 0x08, 0x00, 0x03, 0x01, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
    0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
    0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
    0x05, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00
};

static uint8_t report_ef[DS3_FEATURE_REPORT_SIZE] = {0xEF};  // First byte is report ID

// =================================================================
// Public Functions
// =================================================================

void ds3_init(void) {
    // Report is already initialized via g_ds3_report in common.c
    // This function exists for any future initialization needs
    printf("[DS3] Emulation layer initialized\n");
}

const uint8_t* ds3_get_feature_report(uint8_t report_id, const char** out_name) {
    const char* name = "UNKNOWN";
    const uint8_t* data = NULL;
    
    switch (report_id) {
        case DS3_REPORT_CAPABILITIES:
            data = report_01;
            name = "Capabilities";
            break;
        case DS3_REPORT_BT_MAC:
            data = report_f2;
            name = "BT MAC";
            break;
        case DS3_REPORT_PAIRING:
            data = report_f5;
            name = "Pairing";
            break;
        case DS3_REPORT_CALIBRATION:
            data = report_f7;
            name = "Calibration";
            break;
        case DS3_REPORT_STATUS:
            data = report_f8;
            name = "Status";
            break;
        case DS3_REPORT_EF:
            data = report_ef;
            name = "EF Config";
            break;
    }
    
    if (out_name) *out_name = name;
    return data;
}

void ds3_handle_set_report(uint8_t report_id, const uint8_t* data, size_t len) {
    printf("[DS3] SET_REPORT 0x%02x received (%zu bytes)\n", report_id, len);
    
    if (report_id == DS3_REPORT_EF) {
        // PS3 sends SET_REPORT 0xEF during init
        // Store the data and prepend 0xEF for GET_REPORT response
        report_ef[0] = 0xEF;
        size_t copy_len = (len > DS3_FEATURE_REPORT_SIZE - 1) ? DS3_FEATURE_REPORT_SIZE - 1 : len;
        memcpy(&report_ef[1], data, copy_len);
        printf("[DS3] Config 0xEF stored (%zu bytes)\n", len);
    }
    else if (report_id == 0xF4 && len >= 5) {
        // LED configuration report
        printf("[DS3] LED config: %02x %02x %02x %02x\n", 
               data[1], data[2], data[3], data[4]);
    }
    else if (report_id == 0x01) {
        // Output report (rumble/LED init) - PS3 sends this before 0xF7
        printf("[DS3] Output report 0x01 received (rumble/LED init)\n");
    }
}

uint8_t ds3_convert_dpad(uint8_t hat_value) {
    // Convert hat switch (0-7 clockwise from up, 8=center) to DS3 d-pad mask
    switch (hat_value & 0x0F) {
        case 0: return DS3_BTN_DPAD_UP;                              // Up
        case 1: return DS3_BTN_DPAD_UP | DS3_BTN_DPAD_RIGHT;        // Up-Right
        case 2: return DS3_BTN_DPAD_RIGHT;                          // Right
        case 3: return DS3_BTN_DPAD_DOWN | DS3_BTN_DPAD_RIGHT;      // Down-Right
        case 4: return DS3_BTN_DPAD_DOWN;                           // Down
        case 5: return DS3_BTN_DPAD_DOWN | DS3_BTN_DPAD_LEFT;       // Down-Left
        case 6: return DS3_BTN_DPAD_LEFT;                           // Left
        case 7: return DS3_BTN_DPAD_UP | DS3_BTN_DPAD_LEFT;         // Up-Left
        default: return 0;                                          // Center/released
    }
}

void ds3_update_report(
    uint8_t buttons1, uint8_t buttons2, uint8_t ps_button,
    uint8_t lx, uint8_t ly, uint8_t rx, uint8_t ry,
    uint8_t l2, uint8_t r2,
    uint8_t triangle_p, uint8_t circle_p, uint8_t cross_p, uint8_t square_p)
{
    pthread_mutex_lock(&g_report_mutex);
    
    g_ds3_report[DS3_OFF_BUTTONS1] = buttons1;
    g_ds3_report[DS3_OFF_BUTTONS2] = buttons2;
    g_ds3_report[DS3_OFF_PS_BUTTON] = ps_button;
    
    g_ds3_report[DS3_OFF_LX] = lx;
    g_ds3_report[DS3_OFF_LY] = ly;
    g_ds3_report[DS3_OFF_RX] = rx;
    g_ds3_report[DS3_OFF_RY] = ry;
    
    g_ds3_report[DS3_OFF_L2_PRESSURE] = l2;
    g_ds3_report[DS3_OFF_R2_PRESSURE] = r2;
    
    g_ds3_report[DS3_OFF_TRIANGLE_P] = triangle_p;
    g_ds3_report[DS3_OFF_CIRCLE_P] = circle_p;
    g_ds3_report[DS3_OFF_CROSS_P] = cross_p;
    g_ds3_report[DS3_OFF_SQUARE_P] = square_p;
    
    pthread_mutex_unlock(&g_report_mutex);
}

void ds3_update_motion(int16_t accel_x, int16_t accel_y, int16_t accel_z, int16_t gyro_z) {
    pthread_mutex_lock(&g_report_mutex);
    
    // DS3 motion data is little-endian 16-bit values
    // Accelerometer centered around ~512 (0x200), gyro around ~498
    g_ds3_report[DS3_OFF_ACCEL_X]     = accel_x & 0xFF;
    g_ds3_report[DS3_OFF_ACCEL_X + 1] = (accel_x >> 8) & 0xFF;
    
    g_ds3_report[DS3_OFF_ACCEL_Y]     = accel_y & 0xFF;
    g_ds3_report[DS3_OFF_ACCEL_Y + 1] = (accel_y >> 8) & 0xFF;
    
    g_ds3_report[DS3_OFF_ACCEL_Z]     = accel_z & 0xFF;
    g_ds3_report[DS3_OFF_ACCEL_Z + 1] = (accel_z >> 8) & 0xFF;
    
    g_ds3_report[DS3_OFF_GYRO_Z]     = gyro_z & 0xFF;
    g_ds3_report[DS3_OFF_GYRO_Z + 1] = (gyro_z >> 8) & 0xFF;
    
    pthread_mutex_unlock(&g_report_mutex);
}

void ds3_copy_report(uint8_t* out_buf) {
    pthread_mutex_lock(&g_report_mutex);
    memcpy(out_buf, g_ds3_report, DS3_INPUT_REPORT_SIZE);
    pthread_mutex_unlock(&g_report_mutex);
}

void ds3_update_battery(uint8_t plugged, uint8_t battery, uint8_t connection) {
    pthread_mutex_lock(&g_report_mutex);
    g_ds3_report[DS3_OFF_BATTERY] = plugged;
    g_ds3_report[DS3_OFF_CHARGE] = battery;
    g_ds3_report[DS3_OFF_CONNECTION] = connection;
    pthread_mutex_unlock(&g_report_mutex);
}

void ds3_update_battery_from_dualsense(uint8_t ds_battery_level, int ds_charging) {
    uint8_t battery_status;
    
    if (ds_charging) {
        // When charging via USB, show charging status
        if (ds_battery_level >= 100) {
            battery_status = DS3_BATTERY_CHARGED;   // 0xEF - fully charged
        } else {
            battery_status = DS3_BATTERY_CHARGING;  // 0xEE - charging
        }
    } else {
        // Convert DualSense percentage to DS3 battery level
        // DS3 uses: 0x00=0%, 0x01=1%, 0x02=25%, 0x03=50%, 0x04=75%, 0x05=100%
        if (ds_battery_level <= 5) {
            battery_status = DS3_BATTERY_SHUTDOWN;  // 0x00
        } else if (ds_battery_level <= 15) {
            battery_status = DS3_BATTERY_DYING;     // 0x01
        } else if (ds_battery_level <= 35) {
            battery_status = DS3_BATTERY_LOW;       // 0x02
        } else if (ds_battery_level <= 60) {
            battery_status = DS3_BATTERY_MEDIUM;    // 0x03
        } else if (ds_battery_level <= 85) {
            battery_status = DS3_BATTERY_HIGH;      // 0x04
        } else {
            battery_status = DS3_BATTERY_FULL;      // 0x05
        }
    }
    
    // We're always USB-connected to PS3, check if rumble is active
    uint8_t connection;
    pthread_mutex_lock(&g_rumble_mutex);
    int rumble_active = (g_rumble_right > 0 || g_rumble_left > 0);
    pthread_mutex_unlock(&g_rumble_mutex);
    
    connection = rumble_active ? DS3_CONN_USB_RUMBLE : DS3_CONN_USB;
    
    ds3_update_battery(DS3_STATUS_PLUGGED, battery_status, connection);
}