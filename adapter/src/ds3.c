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
// DS3 Feature Reports - From real DS3 USB capture (PS3-DS3_USB_0001.log)
// =================================================================

// Report 0x01 (Capabilities)
// From log: GET 0x01 -> 00 01 04 00 08 0C 01 02 18 18 18 18 09 0A 10 11 12 13 00 00 00 00 04 00 02 02 02 02 00 00 00 04 04 04 04 00 00 04 00 01 02 07 00 17 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00
static uint8_t report_01[DS3_FEATURE_REPORT_SIZE] = {
    0x00, 0x01, 0x04, 0x00, 0x08, 0x0C, 0x01, 0x02, 0x18, 0x18, 0x18, 0x18, 0x09, 0x0A, 0x10, 0x11,
    0x12, 0x13, 0x00, 0x00, 0x00, 0x00, 0x04, 0x00, 0x02, 0x02, 0x02, 0x02, 0x00, 0x00, 0x00, 0x04,
    0x04, 0x04, 0x04, 0x00, 0x00, 0x04, 0x00, 0x01, 0x02, 0x07, 0x00, 0x17, 0x00, 0x00, 0x00, 0x00,
    0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00
};

// Report 0xF2 (Controller Bluetooth MAC)
// From log: GET 0xF2 -> F2 FF FF 00 34 C7 31 25 AE 60 00 03 50 81 D8 01 8A 13 00 00 00 00 04 00 02 02 02 02 00 00 00 04 04 04 04 00 00 04 00 01 02 07 00 17 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00
// Bytes 4-9 contain the controller's MAC (34:C7:31:25:AE:60 in this capture)
// Will be overwritten with Pi's Bluetooth MAC at runtime
static uint8_t report_f2[DS3_FEATURE_REPORT_SIZE] = {
    0xF2, 0xFF, 0xFF, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x03, 0x50, 0x81, 0xD8, 0x01,
    0x8A, 0x13, 0x00, 0x00, 0x00, 0x00, 0x04, 0x00, 0x02, 0x02, 0x02, 0x02, 0x00, 0x00, 0x00, 0x04,
    0x04, 0x04, 0x04, 0x00, 0x00, 0x04, 0x00, 0x01, 0x02, 0x07, 0x00, 0x17, 0x00, 0x00, 0x00, 0x00,
    0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00
};

// Report 0xF5 (Host/Pairing MAC - This is the PS3's Bluetooth MAC)
// From log: GET 0xF5 -> 01 00 B8 27 EB 19 86 FC AE 60 00 03 50 81 D8 01 8A 13 00 00 00 00 04 00 02 02 02 02 00 00 00 04 04 04 04 00 00 04 00 01 02 07 00 17 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00
// Bytes 2-7 contain the host MAC (B8:27:EB:19:86:FC in this capture - the Pi's MAC)
// Will be overwritten with Pi's Bluetooth MAC at runtime
static uint8_t report_f5[DS3_FEATURE_REPORT_SIZE] = {
    0x01, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0xAE, 0x60, 0x00, 0x03, 0x50, 0x81, 0xD8, 0x01,
    0x8A, 0x13, 0x00, 0x00, 0x00, 0x00, 0x04, 0x00, 0x02, 0x02, 0x02, 0x02, 0x00, 0x00, 0x00, 0x04,
    0x04, 0x04, 0x04, 0x00, 0x00, 0x04, 0x00, 0x01, 0x02, 0x07, 0x00, 0x17, 0x00, 0x00, 0x00, 0x00,
    0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00
};

// Report 0xF7 (Calibration data)
// From log: GET 0xF7 -> 02 01 F8 02 07 02 EF FF 14 33 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 05 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00
static uint8_t report_f7[DS3_FEATURE_REPORT_SIZE] = {
    0x02, 0x01, 0xF8, 0x02, 0x07, 0x02, 0xEF, 0xFF, 0x14, 0x33, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
    0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
    0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
    0x05, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00
};

// Report 0xF8 (Status)
// From log: GET 0xF8 -> 00 02 00 00 08 00 03 01 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 05 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00
static uint8_t report_f8[DS3_FEATURE_REPORT_SIZE] = {
    0x00, 0x02, 0x00, 0x00, 0x08, 0x00, 0x03, 0x01, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
    0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
    0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
    0x05, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00
};

// Report 0xEF (Calibration config) - initialized, updated by SET_REPORT
// From log: After SET 0xEF with A0/B0 config, GET 0xEF returns:
// -> 00 EF 04 00 08 00 03 01 00 00...
// Note: This is used for USB GET_REPORT, bt_hid.c has separate A0/B0 versions for BT
static uint8_t report_ef[DS3_FEATURE_REPORT_SIZE] = {
    0x00, 0xEF, 0x04, 0x00, 0x08, 0x00, 0x03, 0x01, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
    0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
    0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
    0x05, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00
};

// =================================================================
// Public Functions
// =================================================================

void ds3_init(void) {
    printf("[DS3] Emulation layer initialized\n");
}

void ds3_set_host_mac(const uint8_t* mac) {
    // Report 0xF5 bytes 2-7 contain the host MAC
    // Format in report is the same byte order as the MAC
    report_f5[2] = mac[0];
    report_f5[3] = mac[1];
    report_f5[4] = mac[2];
    report_f5[5] = mac[3];
    report_f5[6] = mac[4];
    report_f5[7] = mac[5];
    
    // Also set in Report 0xF2 bytes 4-9 (controller's own MAC)
    // For us, the "controller" is the Pi, so same MAC
    report_f2[4] = mac[0];
    report_f2[5] = mac[1];
    report_f2[6] = mac[2];
    report_f2[7] = mac[3];
    report_f2[8] = mac[4];
    report_f2[9] = mac[5];
    
    printf("[DS3] Host MAC set to %02X:%02X:%02X:%02X:%02X:%02X\n",
           mac[0], mac[1], mac[2], mac[3], mac[4], mac[5]);
}

int ds3_set_host_mac_str(const char* mac_str) {
    uint8_t mac[6];
    int result = sscanf(mac_str, "%hhx:%hhx:%hhx:%hhx:%hhx:%hhx",
                        &mac[0], &mac[1], &mac[2], &mac[3], &mac[4], &mac[5]);
    if (result != 6) {
        return -1;
    }
    ds3_set_host_mac(mac);
    return 0;
}

void ds3_get_host_mac(uint8_t* out_mac) {
    out_mac[0] = report_f5[2];
    out_mac[1] = report_f5[3];
    out_mac[2] = report_f5[4];
    out_mac[3] = report_f5[5];
    out_mac[4] = report_f5[6];
    out_mac[5] = report_f5[7];
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

// Stored PS3 MAC from SET_REPORT 0xF5
static uint8_t g_ps3_mac[6] = {0};
static int g_ps3_mac_valid = 0;

void ds3_handle_set_report(uint8_t report_id, const uint8_t* data, size_t len) {
    printf("[DS3] SET_REPORT 0x%02x received (%zu bytes)\n", report_id, len);
    
    if (report_id == DS3_REPORT_PAIRING && len >= 8) {
        // PS3 sends SET_REPORT 0xF5 with its Bluetooth MAC address
        // Format: [01 00] [6-byte MAC]
        // First 2 bytes are header, MAC starts at byte 2
        g_ps3_mac[0] = data[2];
        g_ps3_mac[1] = data[3];
        g_ps3_mac[2] = data[4];
        g_ps3_mac[3] = data[5];
        g_ps3_mac[4] = data[6];
        g_ps3_mac[5] = data[7];
        g_ps3_mac_valid = 1;
        
        printf("[DS3] *** PS3 Bluetooth MAC: %02X:%02X:%02X:%02X:%02X:%02X ***\n",
               g_ps3_mac[0], g_ps3_mac[1], g_ps3_mac[2],
               g_ps3_mac[3], g_ps3_mac[4], g_ps3_mac[5]);
        
        // Also update report_f5 so GET_REPORT returns the correct paired address
        report_f5[2] = data[2];
        report_f5[3] = data[3];
        report_f5[4] = data[4];
        report_f5[5] = data[5];
        report_f5[6] = data[6];
        report_f5[7] = data[7];
    }
    else if (report_id == DS3_REPORT_EF) {
        report_ef[0] = 0xEF;
        size_t copy_len = (len > DS3_FEATURE_REPORT_SIZE - 1) ? DS3_FEATURE_REPORT_SIZE - 1 : len;
        memcpy(&report_ef[1], data, copy_len);
        printf("[DS3] Config 0xEF stored (%zu bytes)\n", len);
    }
    else if (report_id == 0xF4 && len >= 4) {
        printf("[DS3] LED/Enable config: %02x %02x %02x %02x\n", 
               data[0], data[1], data[2], data[3]);
    }
    else if (report_id == 0x01) {
        printf("[DS3] Output report 0x01 received (rumble/LED init)\n");
    }
}

int ds3_get_ps3_mac(uint8_t* out_mac) {
    if (!g_ps3_mac_valid) {
        return -1;
    }
    memcpy(out_mac, g_ps3_mac, 6);
    return 0;
}

int ds3_has_ps3_mac(void) {
    return g_ps3_mac_valid;
}

uint8_t ds3_convert_dpad(uint8_t hat_value) {
    switch (hat_value & 0x0F) {
        case 0: return DS3_BTN_DPAD_UP;
        case 1: return DS3_BTN_DPAD_UP | DS3_BTN_DPAD_RIGHT;
        case 2: return DS3_BTN_DPAD_RIGHT;
        case 3: return DS3_BTN_DPAD_DOWN | DS3_BTN_DPAD_RIGHT;
        case 4: return DS3_BTN_DPAD_DOWN;
        case 5: return DS3_BTN_DPAD_DOWN | DS3_BTN_DPAD_LEFT;
        case 6: return DS3_BTN_DPAD_LEFT;
        case 7: return DS3_BTN_DPAD_UP | DS3_BTN_DPAD_LEFT;
        default: return 0;
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
    
    // D-pad pressure (0xFF when pressed, 0x00 when not)
    g_ds3_report[DS3_OFF_DPAD_UP_P]    = (buttons1 & DS3_BTN_DPAD_UP)    ? 0xFF : 0x00;
    g_ds3_report[DS3_OFF_DPAD_RIGHT_P] = (buttons1 & DS3_BTN_DPAD_RIGHT) ? 0xFF : 0x00;
    g_ds3_report[DS3_OFF_DPAD_DOWN_P]  = (buttons1 & DS3_BTN_DPAD_DOWN)  ? 0xFF : 0x00;
    g_ds3_report[DS3_OFF_DPAD_LEFT_P]  = (buttons1 & DS3_BTN_DPAD_LEFT)  ? 0xFF : 0x00;
    
    pthread_mutex_unlock(&g_report_mutex);
}

void ds3_update_motion(int16_t accel_x, int16_t accel_y, int16_t accel_z, int16_t gyro_z) {
    pthread_mutex_lock(&g_report_mutex);
    
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
        if (ds_battery_level >= 100) {
            battery_status = DS3_BATTERY_CHARGED;
        } else {
            battery_status = DS3_BATTERY_CHARGING;
        }
    } else {
        if (ds_battery_level <= 5) {
            battery_status = DS3_BATTERY_SHUTDOWN;
        } else if (ds_battery_level <= 15) {
            battery_status = DS3_BATTERY_DYING;
        } else if (ds_battery_level <= 35) {
            battery_status = DS3_BATTERY_LOW;
        } else if (ds_battery_level <= 60) {
            battery_status = DS3_BATTERY_MEDIUM;
        } else if (ds_battery_level <= 85) {
            battery_status = DS3_BATTERY_HIGH;
        } else {
            battery_status = DS3_BATTERY_FULL;
        }
    }
    
    uint8_t connection;
    pthread_mutex_lock(&g_rumble_mutex);
    int rumble_active = (g_rumble_right > 0 || g_rumble_left > 0);
    pthread_mutex_unlock(&g_rumble_mutex);
    
    connection = rumble_active ? DS3_CONN_USB_RUMBLE : DS3_CONN_USB;
    
    ds3_update_battery(DS3_STATUS_PLUGGED, battery_status, connection);
}