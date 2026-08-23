/*
 * RosettaPad - PS3 / DualShock 3 Emulation Layer
 * ===============================================
 * 
 * Translates generic controller state to DS3 protocol.
 */

#include <stdio.h>
#include <string.h>
#include <pthread.h>
#include <time.h>

#include "core/common.h"
#include "console/ps3/ds3_emulation.h"

/* ============================================================================
 * DS3 INPUT REPORT STATE
 * ============================================================================ */

static uint8_t g_ds3_report[DS3_INPUT_REPORT_SIZE] = {
    /* Default neutral state */
    0x01,       /* [0]  Report ID */
    0x00,       /* [1]  Reserved */
    0x00,       /* [2]  Buttons1 */
    0x00,       /* [3]  Buttons2 */
    0x00,       /* [4]  PS button */
    0x00,       /* [5]  Reserved */
    0x80,       /* [6]  Left stick X */
    0x80,       /* [7]  Left stick Y */
    0x80,       /* [8]  Right stick X */
    0x80,       /* [9]  Right stick Y */
    0x00, 0x00, 0x00, 0x00,  /* [10-13] Reserved */
    0x00, 0x00, 0x00, 0x00,  /* [14-17] D-pad pressure (Up, Right, Down, Left) */
    0x00,       /* [18] L2 pressure */
    0x00,       /* [19] R2 pressure */
    0x00,       /* [20] L1 pressure */
    0x00,       /* [21] R1 pressure */
    0x00,       /* [22] Triangle pressure */
    0x00,       /* [23] Circle pressure */
    0x00,       /* [24] Cross pressure */
    0x00,       /* [25] Square pressure */
    0x00, 0x00, 0x00,  /* [26-28] Reserved */
    0x02,       /* [29] Plugged status */
    0xEE,       /* [30] Battery: charging */
    0x12,       /* [31] Connection: USB */
    0x00, 0x00, 0x00, 0x00,  /* [32-35] Reserved */
    0x33, 0x04, /* [36-37] Unknown */
    0x77, 0x01, /* [38-39] Unknown */
    0xDE,       /* [40]    Unknown */
    0x02, 0x35, /* [41-42] Accel X = 565 (big-endian) */
    0x02, 0x08, /* [43-44] Accel Y = 520 */
    0x01, 0x94, /* [45-46] Accel Z = 404 (~512 - 1g) */
    0x00, 0x02  /* [47-48] Gyro Z  = 2   */
};
static pthread_mutex_t g_ds3_report_mutex = PTHREAD_MUTEX_INITIALIZER;

/* ============================================================================
 * DS3 FEATURE REPORTS
 * ============================================================================ */

/* Report 0x01 - Capabilities */
static uint8_t report_01[DS3_FEATURE_REPORT_SIZE] = {
    0x00, 0x01, 0x04, 0x00, 0x08, 0x0C, 0x01, 0x02,
    0x18, 0x18, 0x18, 0x18, 0x09, 0x0A, 0x10, 0x11,
    0x12, 0x13, 0x00, 0x00, 0x00, 0x00, 0x04, 0x00,
    0x02, 0x02, 0x02, 0x02, 0x00, 0x00, 0x00, 0x04,
    0x04, 0x04, 0x04, 0x00, 0x00, 0x04, 0x00, 0x01,
    0x02, 0x07, 0x00, 0x17, 0x00, 0x00, 0x00, 0x00,
    0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
    0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00
};

/* Report 0xF2 - Controller Bluetooth MAC */
static uint8_t report_f2[DS3_FEATURE_REPORT_SIZE] = {
    0xF2, 0xFF, 0xFF, 0x00, 0x00, 0x00, 0x00, 0x00,
    0x00, 0x00, 0x00, 0x03, 0x50, 0x81, 0xD8, 0x01,
    0x8A, 0x13, 0x00, 0x00, 0x00, 0x00, 0x04, 0x00,
    0x02, 0x02, 0x02, 0x02, 0x00, 0x00, 0x00, 0x04,
    0x04, 0x04, 0x04, 0x00, 0x00, 0x04, 0x00, 0x01,
    0x02, 0x07, 0x00, 0x17, 0x00, 0x00, 0x00, 0x00,
    0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
    0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00
};

/* Report 0xF5 - Host/Pairing MAC (Pi's BT MAC) */
static uint8_t report_f5[DS3_FEATURE_REPORT_SIZE] = {
    0x01, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
    0xAE, 0x60, 0x00, 0x03, 0x50, 0x81, 0xD8, 0x01,
    0x8A, 0x13, 0x00, 0x00, 0x00, 0x00, 0x04, 0x00,
    0x02, 0x02, 0x02, 0x02, 0x00, 0x00, 0x00, 0x04,
    0x04, 0x04, 0x04, 0x00, 0x00, 0x04, 0x00, 0x01,
    0x02, 0x07, 0x00, 0x17, 0x00, 0x00, 0x00, 0x00,
    0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
    0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00
};

/* Report 0xF7 - Calibration */
static uint8_t report_f7[DS3_FEATURE_REPORT_SIZE] = {
    0x02, 0x01, 0xF8, 0x02, 0x07, 0x02, 0xEF, 0xFF,
    0x14, 0x33, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
    0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
    0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
    0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
    0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
    0x05, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
    0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00
};

/* Report 0xF8 - Status */
static uint8_t report_f8[DS3_FEATURE_REPORT_SIZE] = {
    0x00, 0x02, 0x00, 0x00, 0x08, 0x00, 0x03, 0x01,
    0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
    0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
    0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
    0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
    0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
    0x05, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
    0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00
};

/* Report 0xEF - Config */
static uint8_t report_ef[DS3_FEATURE_REPORT_SIZE] = {
    0x00, 0xEF, 0x04, 0x00, 0x08, 0x00, 0x03, 0x01,
    0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
    0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
    0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
    0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
    0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
    0x05, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
    0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00
};

/* PS3's Bluetooth MAC (from SET_REPORT 0xF5) */
static uint8_t g_ps3_mac[6] = {0};
static int g_ps3_mac_valid = 0;

/* ============================================================================
 * INITIALIZATION
 * ============================================================================ */

void ds3_init(void) {
    printf("[DS3] Emulation layer initialized\n");
}

/* ============================================================================
 * MAC ADDRESS MANAGEMENT
 * ============================================================================ */

void ds3_set_host_mac(const uint8_t* mac) {
    /* Report 0xF5 bytes 2-7: Host (Pi) MAC */
    memcpy(&report_f5[2], mac, 6);
    
    /* Report 0xF2 bytes 4-9: Controller MAC (same as Pi) */
    memcpy(&report_f2[4], mac, 6);
    
    printf("[DS3] Host MAC: %02X:%02X:%02X:%02X:%02X:%02X\n",
           mac[0], mac[1], mac[2], mac[3], mac[4], mac[5]);
}

int ds3_get_ps3_mac(uint8_t* out_mac) {
    if (!g_ps3_mac_valid) return -1;
    memcpy(out_mac, g_ps3_mac, 6);
    return 0;
}

int ds3_has_ps3_mac(void) {
    return g_ps3_mac_valid;
}

/* ============================================================================
 * FEATURE REPORTS
 * ============================================================================ */

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
    printf("[DS3] SET_REPORT 0x%02X (%zu bytes)\n", report_id, len);
    
    if (report_id == DS3_REPORT_PAIRING && len >= 8) {
        /* PS3 sends its Bluetooth MAC */
        memcpy(g_ps3_mac, &data[2], 6);
        g_ps3_mac_valid = 1;
        
        printf("[DS3] PS3 MAC: %02X:%02X:%02X:%02X:%02X:%02X\n",
               g_ps3_mac[0], g_ps3_mac[1], g_ps3_mac[2],
               g_ps3_mac[3], g_ps3_mac[4], g_ps3_mac[5]);
        
        /* Update report_f5 so GET_REPORT returns correct paired address */
        memcpy(&report_f5[2], &data[2], 6);
    }
    else if (report_id == DS3_REPORT_EF && len > 0) {
        report_ef[0] = 0xEF;
        size_t copy_len = (len > DS3_FEATURE_REPORT_SIZE - 1) ? 
                          DS3_FEATURE_REPORT_SIZE - 1 : len;
        memcpy(&report_ef[1], data, copy_len);
    }
    else if (report_id == 0xF4 && len >= 4) {
        printf("[DS3] LED/Enable config: %02X %02X %02X %02X\n",
               data[0], data[1], data[2], data[3]);
    }
}

/* ============================================================================
 * INPUT REPORT TRANSLATION
 * 
 * This is the core translation function - converts generic controller state
 * to DS3-specific input report format.
 * ============================================================================ */

void ds3_build_input_report(const controller_state_t* state, uint8_t* out_report) {
    /* Start with template */
    memset(out_report, 0, DS3_INPUT_REPORT_SIZE);
    out_report[0] = 0x01;  /* Report ID */
    
    /* --- Buttons1 (byte 2) --- */
    uint8_t btn1 = 0;
    if (CONTROLLER_BTN_PRESSED(state, BTN_SELECT))     btn1 |= DS3_BTN_SELECT;
    if (CONTROLLER_BTN_PRESSED(state, BTN_L3))         btn1 |= DS3_BTN_L3;
    if (CONTROLLER_BTN_PRESSED(state, BTN_R3))         btn1 |= DS3_BTN_R3;
    if (CONTROLLER_BTN_PRESSED(state, BTN_START))      btn1 |= DS3_BTN_START;
    if (CONTROLLER_BTN_PRESSED(state, BTN_DPAD_UP))    btn1 |= DS3_BTN_DPAD_UP;
    if (CONTROLLER_BTN_PRESSED(state, BTN_DPAD_RIGHT)) btn1 |= DS3_BTN_DPAD_RIGHT;
    if (CONTROLLER_BTN_PRESSED(state, BTN_DPAD_DOWN))  btn1 |= DS3_BTN_DPAD_DOWN;
    if (CONTROLLER_BTN_PRESSED(state, BTN_DPAD_LEFT))  btn1 |= DS3_BTN_DPAD_LEFT;
    out_report[DS3_OFF_BUTTONS1] = btn1;
    
    /* --- Buttons2 (byte 3) --- */
    uint8_t btn2 = 0;
    if (CONTROLLER_BTN_PRESSED(state, BTN_L2))    btn2 |= DS3_BTN_L2;
    if (CONTROLLER_BTN_PRESSED(state, BTN_R2))    btn2 |= DS3_BTN_R2;
    if (CONTROLLER_BTN_PRESSED(state, BTN_L1))    btn2 |= DS3_BTN_L1;
    if (CONTROLLER_BTN_PRESSED(state, BTN_R1))    btn2 |= DS3_BTN_R1;
    if (CONTROLLER_BTN_PRESSED(state, BTN_NORTH)) btn2 |= DS3_BTN_TRIANGLE;
    if (CONTROLLER_BTN_PRESSED(state, BTN_EAST))  btn2 |= DS3_BTN_CIRCLE;
    if (CONTROLLER_BTN_PRESSED(state, BTN_SOUTH)) btn2 |= DS3_BTN_CROSS;
    if (CONTROLLER_BTN_PRESSED(state, BTN_WEST))  btn2 |= DS3_BTN_SQUARE;
    out_report[DS3_OFF_BUTTONS2] = btn2;
    
    /* --- PS Button (byte 4) --- */
    out_report[DS3_OFF_PS_BUTTON] = CONTROLLER_BTN_PRESSED(state, BTN_HOME) ? DS3_BTN_PS : 0;
    
    /* --- Analog Sticks (bytes 6-9) --- */
    out_report[DS3_OFF_LX] = state->left_stick_x;
    out_report[DS3_OFF_LY] = state->left_stick_y;
    out_report[DS3_OFF_RX] = state->right_stick_x;
    out_report[DS3_OFF_RY] = state->right_stick_y;
    
    /* --- D-pad Pressure (bytes 14-17) --- */
    /*
     * The DS3 has pressure-sensitive D-pad buttons. Since the DualSense
     * D-pad is digital-only, we simulate full pressure (0xFF) when pressed
     * and zero when released. Bytes 10-13 are reserved/unused on DS3.
     *
     * Reference: PS3_ANALOG_BUTTONS in https://github.com/felis/USB_Host_Shield_2.0/blob/master/PS3Enums.h
     * readBuf offsets (shifted -9 for raw report): Up=14, Right=15, Down=16, Left=17
     */
    out_report[10] = 0x00;  /* Reserved */
    out_report[11] = 0x00;  /* Reserved */
    out_report[12] = 0x00;  /* Reserved */
    out_report[13] = 0x00;  /* Reserved */
    out_report[DS3_OFF_DPAD_UP_PRESSURE]    = CONTROLLER_BTN_PRESSED(state, BTN_DPAD_UP)    ? 0xFF : 0x00;
    out_report[DS3_OFF_DPAD_RIGHT_PRESSURE] = CONTROLLER_BTN_PRESSED(state, BTN_DPAD_RIGHT) ? 0xFF : 0x00;
    out_report[DS3_OFF_DPAD_DOWN_PRESSURE]  = CONTROLLER_BTN_PRESSED(state, BTN_DPAD_DOWN)  ? 0xFF : 0x00;
    out_report[DS3_OFF_DPAD_LEFT_PRESSURE]  = CONTROLLER_BTN_PRESSED(state, BTN_DPAD_LEFT)  ? 0xFF : 0x00;
    
    /* --- Trigger Pressure (bytes 18-19) --- */
    out_report[DS3_OFF_L2_PRESSURE] = state->left_trigger;
    out_report[DS3_OFF_R2_PRESSURE] = state->right_trigger;
    
    /* --- Shoulder Pressure (bytes 20-21) --- */
    out_report[20] = CONTROLLER_BTN_PRESSED(state, BTN_L1) ? 0xFF : 0x00;
    out_report[21] = CONTROLLER_BTN_PRESSED(state, BTN_R1) ? 0xFF : 0x00;
    
    /* --- Face Button Pressure (bytes 22-25) --- */
    out_report[22] = CONTROLLER_BTN_PRESSED(state, BTN_NORTH) ? 0xFF : 0x00;  /* Triangle */
    out_report[23] = CONTROLLER_BTN_PRESSED(state, BTN_EAST)  ? 0xFF : 0x00;  /* Circle */
    out_report[24] = CONTROLLER_BTN_PRESSED(state, BTN_SOUTH) ? 0xFF : 0x00;  /* Cross */
    out_report[25] = CONTROLLER_BTN_PRESSED(state, BTN_WEST)  ? 0xFF : 0x00;  /* Square */
    
    /* --- Battery Status (bytes 29-31) --- */
    /* Convert generic battery level to DS3 format */
    uint8_t ds3_battery;
    if (state->battery_full) {
        ds3_battery = DS3_BATTERY_CHARGED;  /* 0xEF = fully charged */
    } else if (state->battery_charging) {
        ds3_battery = DS3_BATTERY_CHARGING;  /* 0xEE = charging */
    } else if (state->battery_level <= 5) {
        ds3_battery = DS3_BATTERY_SHUTDOWN;
    } else if (state->battery_level <= 15) {
        ds3_battery = DS3_BATTERY_DYING;
    } else if (state->battery_level <= 35) {
        ds3_battery = DS3_BATTERY_LOW;
    } else if (state->battery_level <= 60) {
        ds3_battery = DS3_BATTERY_MEDIUM;
    } else if (state->battery_level <= 85) {
        ds3_battery = DS3_BATTERY_HIGH;
    } else {
        ds3_battery = DS3_BATTERY_FULL;
    }
    
    out_report[DS3_OFF_BATTERY] = DS3_STATUS_PLUGGED;
    out_report[DS3_OFF_CHARGE] = ds3_battery;
    out_report[DS3_OFF_CONNECTION] = DS3_CONN_USB;
    
    /* --- Unknown bytes (from real DS3 captures) --- */
    out_report[36] = 0x33;
    out_report[37] = 0x04;
    out_report[38] = 0x77;
    out_report[39] = 0x01;
    
    /* --- Motion Data (bytes 41-48) --- */
    /*
     * Input is calibrated in hid-playstation units (accel 8192/g, gyro 1024/deg/s).
     * DS3 motion is 10-bit unsigned, offset around a rest value, stored BIG-endian.
     *
     * Mapping (DualSense -> DS3):
     *   DS3 accel X (left/right tilt) <- DS5 accel_x / 72
     *   DS3 accel Y (fwd/back tilt)   <- DS5 accel_y / 72
     *   DS3 accel Z (vertical)        <- DS5 accel_z / 72
     *   DS3 gyro  Z (yaw)             <- DS5 gyro_y  / 128
     *
     * Rest offsets and signs below were matched against a real DS3 measured
     * in the same orientations. Reference captures (X, Y, Z, G):
     *   flat on table            509, 400, 489, 512
     *   45 deg triggers up       510, 430, 588, 512
     *   45 deg triggers down     510, 454, 412, 512
     *   45 deg tilt right        580, 433, 471, 512
     *   45 deg tilt left         450, 423, 471, 512
     *   45 deg yaw right (wheel) 590, 455, 566, 700+
     *   45 deg yaw left  (wheel) 436, 465, 580, 200
     */
    #define DS3_MOTION_SIGN_X   (+1)
    #define DS3_MOTION_SIGN_Y   (-1)   /* DS5 Y points the opposite way to DS3 Y */
    #define DS3_MOTION_SIGN_Z   (-1)   /* DS5 reads -1g on Z when flat; DS3 Z sits above rest */
    #define DS3_MOTION_SIGN_YAW (+1)   /* clockwise yaw raises the DS3 gyro value */

    int32_t ds3_accel_x = 509 + DS3_MOTION_SIGN_X   * (state->accel_x / 72);
    int32_t ds3_accel_y = 512 + DS3_MOTION_SIGN_Y   * (state->accel_y / 72);
    int32_t ds3_accel_z = 489 + DS3_MOTION_SIGN_Z   * (state->accel_z / 72);
    int32_t ds3_gyro_z  = 512 + DS3_MOTION_SIGN_YAW * (state->gyro_y  / 128);

    /* Clamp to 10-bit range (int32 math above, so no wrap before this point) */
    #define CLAMP10(v) ((v) < 0 ? 0 : ((v) > 1023 ? 1023 : (v)))
    ds3_accel_x = CLAMP10(ds3_accel_x);
    ds3_accel_y = CLAMP10(ds3_accel_y);
    ds3_accel_z = CLAMP10(ds3_accel_z);
    ds3_gyro_z  = CLAMP10(ds3_gyro_z);

    /* Big-endian 16-bit, as a real sixaxis sends them */
    out_report[DS3_OFF_ACCEL_X]     = (ds3_accel_x >> 8) & 0x03;
    out_report[DS3_OFF_ACCEL_X + 1] = ds3_accel_x & 0xFF;
    out_report[DS3_OFF_ACCEL_Y]     = (ds3_accel_y >> 8) & 0x03;
    out_report[DS3_OFF_ACCEL_Y + 1] = ds3_accel_y & 0xFF;
    out_report[DS3_OFF_ACCEL_Z]     = (ds3_accel_z >> 8) & 0x03;
    out_report[DS3_OFF_ACCEL_Z + 1] = ds3_accel_z & 0xFF;
    out_report[DS3_OFF_GYRO_Z]      = (ds3_gyro_z >> 8) & 0x03;
    out_report[DS3_OFF_GYRO_Z + 1]  = ds3_gyro_z & 0xFF;

    /* Once-a-second motion dump under -v so axis/sign tuning can be done
     * from the log: flat on a table should read roughly 509 / 400 / 489 / 512. */
    if (g_verbose) {
        static time_t last_motion_log = 0;
        time_t now = time(NULL);
        if (now != last_motion_log) {
            last_motion_log = now;
            printf("[MOTION] ds3 ax=%d ay=%d az=%d gz=%d | ds5 a=(%d,%d,%d) g=(%d,%d,%d)\n",
                   (int)ds3_accel_x, (int)ds3_accel_y, (int)ds3_accel_z, (int)ds3_gyro_z,
                   (int)state->accel_x, (int)state->accel_y, (int)state->accel_z,
                   (int)state->gyro_x, (int)state->gyro_y, (int)state->gyro_z);
        }
    }

    /* Byte 40: unknown, constant in captures */
    out_report[40] = 0xDE;

    /* Update cached report */
    pthread_mutex_lock(&g_ds3_report_mutex);
    memcpy(g_ds3_report, out_report, DS3_INPUT_REPORT_SIZE);
    pthread_mutex_unlock(&g_ds3_report_mutex);
}

void ds3_copy_report(uint8_t* out_buf) {
    pthread_mutex_lock(&g_ds3_report_mutex);
    memcpy(out_buf, g_ds3_report, DS3_INPUT_REPORT_SIZE);
    pthread_mutex_unlock(&g_ds3_report_mutex);
}

/* ============================================================================
 * OUTPUT REPORT PARSING
 * 
 * Parse rumble/LED commands from PS3 and update global output state.
 * ============================================================================ */

void ds3_parse_output_report(const uint8_t* data, size_t len) {
    if (len < 6) return;
    
    /*
     * DS3 output report format:
     * [0] 0x01 - Report ID
     * [1] 0x00 - Padding
     * [2] Weak motor duration (0-255, 0x96 = 150 = indefinite)
     * [3] Weak motor power (0 or 1)
     * [4] Strong motor duration
     * [5] Strong motor power (0-255)
     * [6-9] Unknown/padding
     * [10] LED bitmask:
     *      Bit 1 (0x02) = LED4 / Player 1
     *      Bit 2 (0x04) = LED3 / Player 2
     *      Bit 3 (0x08) = LED2 / Player 3
     *      Bit 4 (0x10) = LED1 / Player 4
     * [11+] LED PWM parameters
     */
    
    uint8_t weak_power = data[3];     /* Binary: 0 or 1 */
    uint8_t strong_power = data[5];   /* Variable: 0-255 */
    
    /* Convert to generic output format */
    /* Weak motor = right (high frequency), Strong motor = left (low frequency) */
    controller_output_t output;
    controller_output_copy(&output);
    
    output.rumble_right = weak_power ? 0xFF : 0x00;
    output.rumble_left = strong_power;
    
    /* Parse player LED assignment from byte 10 if present */
    if (len >= 11) {
        uint8_t ds3_leds = data[10];
        
        /*
         * Map DS3 LED bitmask to DualSense 5-LED array
         * DualSense LEDs: [1][2][3][4][5] in a row
         * 
         * DS3 Player 1 (0x02) -> DualSense LED 3 (center only) = 0x04
         * DS3 Player 2 (0x04) -> DualSense LEDs 2,4 (inner pair) = 0x0A
         * DS3 Player 3 (0x08) -> DualSense LEDs 1,3,5 (edges + center) = 0x15
         * DS3 Player 4 (0x10) -> DualSense LEDs 1,2,4,5 (all but center) = 0x1B
         */
        uint8_t ds_player_leds = 0;
        
        if (ds3_leds & 0x02) {
            /* Player 1: center LED only */
            ds_player_leds = 0x04;
        } else if (ds3_leds & 0x04) {
            /* Player 2: two inner LEDs */
            ds_player_leds = 0x0A;
        } else if (ds3_leds & 0x08) {
            /* Player 3: three LEDs (center + edges) */
            ds_player_leds = 0x15;
        } else if (ds3_leds & 0x10) {
            /* Player 4: four LEDs (all but center) */
            ds_player_leds = 0x1B;
        }
        
        if (ds_player_leds != 0 && ds_player_leds != output.player_leds) {
            static int led_log_count = 0;
            if (++led_log_count <= 5) {
                printf("[DS3] Player LED: DS3=0x%02X -> DualSense=0x%02X\n", 
                       ds3_leds, ds_player_leds);
            }
            output.player_leds = ds_player_leds;
        }
    }
    
    controller_output_update(&output);
}