/*
 * RosettaPad - DualSense (PS5) Controller Driver
 * ===============================================
 * 
 * REFERENCE IMPLEMENTATION - Use as template for new controllers!
 * 
 * This file demonstrates how to implement a controller driver:
 * 
 * 1. Define controller info (VID, PID, capabilities)
 * 2. Implement find_device() to locate the controller
 * 3. Implement process_input() to parse hardware-specific reports
 * 4. Implement send_output() for rumble/LED control
 * 5. Register the driver at startup
 * 
 * Key patterns to follow:
 * - Parse hardware-specific format into generic controller_state_t
 * - Handle both Bluetooth and USB connections if applicable
 * - Use sysfs for LED control if kernel driver manages them
 * - Calculate CRC for Bluetooth output reports if required
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <fcntl.h>
#include <unistd.h>
#include <errno.h>
#include <dirent.h>
#include <linux/hidraw.h>
#include <sys/ioctl.h>

#include "core/common.h"
#include "controllers/dualsense/dualsense.h"

/* ============================================================================
 * CRC32 FOR BLUETOOTH OUTPUT
 * 
 * DualSense BT output reports require CRC32 validation.
 * Other controllers may not need this.
 * ============================================================================ */

static uint32_t crc32_table[256];
static int crc32_initialized = 0;

static void init_crc32_table(void) {
    if (crc32_initialized) return;
    
    for (uint32_t i = 0; i < 256; i++) {
        uint32_t crc = i;
        for (int j = 0; j < 8; j++) {
            crc = (crc >> 1) ^ ((crc & 1) ? 0xEDB88320 : 0);
        }
        crc32_table[i] = crc;
    }
    crc32_initialized = 1;
}

uint32_t dualsense_calc_crc32(const uint8_t* data, size_t len) {
    uint32_t crc = 0xFFFFFFFF;
    for (size_t i = 0; i < len; i++) {
        crc = (crc >> 8) ^ crc32_table[(crc ^ data[i]) & 0xFF];
    }
    return ~crc;
}

/* ============================================================================
 * CALIBRATION DATA
 * 
 * DualSense provides per-controller calibration via Feature Report 0x05.
 * This data is used to normalize motion sensor readings.
 * ============================================================================ */

static ds_calibration_t g_ds_calibration = { .valid = 0 };

/* Read calibration data from controller */
static int dualsense_read_calibration(int fd) {
    uint8_t buf[DS_FEATURE_REPORT_CALIBRATION_SIZE + 1] = {0};
    buf[0] = DS_FEATURE_REPORT_CALIBRATION;
    
    int ret = ioctl(fd, HIDIOCGFEATURE(sizeof(buf)), buf);
    if (ret < 0) {
        printf("[DualSense] Failed to read calibration: %s\n", strerror(errno));
        /* Use default calibration */
        g_ds_calibration.valid = 0;
        return -1;
    }
    
    printf("[DualSense] Calibration report (%d bytes):", ret);
    for (int i = 0; i < ret && i < 20; i++) {
        printf(" %02X", buf[i]);
    }
    printf(" ...\n");
    
    /* Parse gyroscope calibration (Bluetooth format) */
    int16_t gyro_pitch_bias  = (int16_t)(buf[1] | (buf[2] << 8));
    int16_t gyro_yaw_bias    = (int16_t)(buf[3] | (buf[4] << 8));
    int16_t gyro_roll_bias   = (int16_t)(buf[5] | (buf[6] << 8));
    int16_t gyro_pitch_plus  = (int16_t)(buf[7] | (buf[8] << 8));
    int16_t gyro_yaw_plus    = (int16_t)(buf[9] | (buf[10] << 8));
    int16_t gyro_roll_plus   = (int16_t)(buf[11] | (buf[12] << 8));
    int16_t gyro_pitch_minus = (int16_t)(buf[13] | (buf[14] << 8));
    int16_t gyro_yaw_minus   = (int16_t)(buf[15] | (buf[16] << 8));
    int16_t gyro_roll_minus  = (int16_t)(buf[17] | (buf[18] << 8));
    int16_t gyro_speed_plus  = (int16_t)(buf[19] | (buf[20] << 8));
    int16_t gyro_speed_minus = (int16_t)(buf[21] | (buf[22] << 8));
    
    /* Parse accelerometer calibration */
    int16_t acc_x_plus  = (int16_t)(buf[23] | (buf[24] << 8));
    int16_t acc_x_minus = (int16_t)(buf[25] | (buf[26] << 8));
    int16_t acc_y_plus  = (int16_t)(buf[27] | (buf[28] << 8));
    int16_t acc_y_minus = (int16_t)(buf[29] | (buf[30] << 8));
    int16_t acc_z_plus  = (int16_t)(buf[31] | (buf[32] << 8));
    int16_t acc_z_minus = (int16_t)(buf[33] | (buf[34] << 8));
    
    printf("[DualSense] Gyro: pitch_bias=%d yaw_bias=%d roll_bias=%d\n",
           gyro_pitch_bias, gyro_yaw_bias, gyro_roll_bias);
    printf("[DualSense] Gyro: pitch +/- = %d/%d, yaw +/- = %d/%d, roll +/- = %d/%d\n",
           gyro_pitch_plus, gyro_pitch_minus, gyro_yaw_plus, gyro_yaw_minus,
           gyro_roll_plus, gyro_roll_minus);
    printf("[DualSense] Gyro speed: +/- = %d/%d\n", gyro_speed_plus, gyro_speed_minus);
    printf("[DualSense] Accel X: +/- = %d/%d, Y: +/- = %d/%d, Z: +/- = %d/%d\n",
           acc_x_plus, acc_x_minus, acc_y_plus, acc_y_minus, acc_z_plus, acc_z_minus);
    
    /* Calculate gyro calibration (same formula as kernel driver) */
    int speed_2x = gyro_speed_plus + gyro_speed_minus;
    
    g_ds_calibration.gyro[0].bias = gyro_pitch_bias;
    g_ds_calibration.gyro[0].sens_numer = speed_2x * DS_GYRO_RES_PER_DEG_S;
    g_ds_calibration.gyro[0].sens_denom = gyro_pitch_plus - gyro_pitch_minus;
    
    g_ds_calibration.gyro[1].bias = gyro_yaw_bias;
    g_ds_calibration.gyro[1].sens_numer = speed_2x * DS_GYRO_RES_PER_DEG_S;
    g_ds_calibration.gyro[1].sens_denom = gyro_yaw_plus - gyro_yaw_minus;
    
    g_ds_calibration.gyro[2].bias = gyro_roll_bias;
    g_ds_calibration.gyro[2].sens_numer = speed_2x * DS_GYRO_RES_PER_DEG_S;
    g_ds_calibration.gyro[2].sens_denom = gyro_roll_plus - gyro_roll_minus;
    
    /* Calculate accel calibration */
    int range_2g;
    
    range_2g = acc_x_plus - acc_x_minus;
    g_ds_calibration.accel[0].bias = acc_x_plus - range_2g / 2;
    g_ds_calibration.accel[0].sens_numer = 2 * DS_ACC_RES_PER_G;
    g_ds_calibration.accel[0].sens_denom = range_2g;
    
    range_2g = acc_y_plus - acc_y_minus;
    g_ds_calibration.accel[1].bias = acc_y_plus - range_2g / 2;
    g_ds_calibration.accel[1].sens_numer = 2 * DS_ACC_RES_PER_G;
    g_ds_calibration.accel[1].sens_denom = range_2g;
    
    range_2g = acc_z_plus - acc_z_minus;
    g_ds_calibration.accel[2].bias = acc_z_plus - range_2g / 2;
    g_ds_calibration.accel[2].sens_numer = 2 * DS_ACC_RES_PER_G;
    g_ds_calibration.accel[2].sens_denom = range_2g;
    
    /* Sanity check - avoid division by zero */
    for (int i = 0; i < 3; i++) {
        if (g_ds_calibration.gyro[i].sens_denom == 0) {
            printf("[DualSense] WARNING: Invalid gyro calibration for axis %d\n", i);
            g_ds_calibration.gyro[i].bias = 0;
            g_ds_calibration.gyro[i].sens_numer = DS_GYRO_RANGE;
            g_ds_calibration.gyro[i].sens_denom = 32767;
        }
        if (g_ds_calibration.accel[i].sens_denom == 0) {
            printf("[DualSense] WARNING: Invalid accel calibration for axis %d\n", i);
            g_ds_calibration.accel[i].bias = 0;
            g_ds_calibration.accel[i].sens_numer = DS_ACC_RANGE;
            g_ds_calibration.accel[i].sens_denom = 32767;
        }
    }
    
    g_ds_calibration.valid = 1;
    printf("[DualSense] Calibration loaded successfully\n");
    return 0;
}

/* Apply calibration to raw sensor value */
static inline int32_t apply_calibration(int16_t raw, const ds_axis_calib_t* calib) {
    /* calibrated = (raw - bias) * sens_numer / sens_denom */
    return ((int32_t)(raw - calib->bias) * calib->sens_numer) / calib->sens_denom;
}

/* ============================================================================
 * LED SYSFS CONTROL
 * 
 * The kernel hid-playstation driver exposes LEDs via sysfs.
 * We use sysfs for LED control to avoid conflicts with the driver.
 * ============================================================================ */

static char g_lightbar_path[512] = "";
static char g_player_led_paths[5][512] = {"", "", "", "", ""};

static void find_led_sysfs_paths(void) {
    g_lightbar_path[0] = '\0';
    for (int i = 0; i < 5; i++) {
        g_player_led_paths[i][0] = '\0';
    }
    
    DIR* led_dir = opendir("/sys/class/leds");
    if (!led_dir) return;
    
    struct dirent* entry;
    while ((entry = readdir(led_dir)) != NULL) {
        if (entry->d_name[0] == '.') continue;
        
        char led_path[512];
        char led_link[512];
        snprintf(led_path, sizeof(led_path), "/sys/class/leds/%s", entry->d_name);
        
        /* Check if this LED belongs to a DualSense */
        ssize_t len = readlink(led_path, led_link, sizeof(led_link) - 1);
        if (len <= 0) continue;
        led_link[len] = '\0';
        
        if (!strstr(led_link, "054C") || !strstr(led_link, "0CE6")) continue;
        
        /* Lightbar */
        if (strstr(entry->d_name, "rgb:indicator")) {
            snprintf(g_lightbar_path, sizeof(g_lightbar_path), 
                     "/sys/class/leds/%s", entry->d_name);
            printf("[DualSense] Found lightbar: %s\n", g_lightbar_path);
        }
        /* Player LEDs */
        else if (strstr(entry->d_name, ":white:player-")) {
            int player_num = 0;
            const char* p = strstr(entry->d_name, "player-");
            if (p && sscanf(p, "player-%d", &player_num) == 1) {
                if (player_num >= 1 && player_num <= 5) {
                    snprintf(g_player_led_paths[player_num - 1], 
                             sizeof(g_player_led_paths[0]),
                             "/sys/class/leds/%s", entry->d_name);
                    printf("[DualSense] Found player LED %d: %s\n", 
                           player_num, g_player_led_paths[player_num - 1]);
                }
            }
        }
    }
    
    closedir(led_dir);
}

static void set_lightbar_sysfs(uint8_t r, uint8_t g, uint8_t b) {
    if (g_lightbar_path[0] == '\0') {
        find_led_sysfs_paths();
        if (g_lightbar_path[0] == '\0') return;
    }
    
    char path[576];
    FILE* f;
    
    /* Set color intensities */
    snprintf(path, sizeof(path), "%s/multi_intensity", g_lightbar_path);
    f = fopen(path, "w");
    if (f) {
        fprintf(f, "%d %d %d", r, g, b);
        fclose(f);
    } else {
        g_lightbar_path[0] = '\0';  /* Path stale, search again */
        return;
    }
    
    /* Set brightness to trigger update */
    snprintf(path, sizeof(path), "%s/brightness", g_lightbar_path);
    f = fopen(path, "w");
    if (f) {
        fprintf(f, "255");
        fclose(f);
    }
}

static void set_player_leds_sysfs(uint8_t player_mask) {
    static int pled_log_count = 0;
    if (++pled_log_count <= 10) {
        printf("[DualSense] Setting player LEDs: 0x%02X\n", player_mask);
    }
    
    int leds_set = 0;
    for (int i = 0; i < 5; i++) {
        if (g_player_led_paths[i][0] == '\0') continue;
        
        char path[576];
        snprintf(path, sizeof(path), "%s/brightness", g_player_led_paths[i]);
        
        FILE* f = fopen(path, "w");
        if (f) {
            int on = (player_mask & (1 << i)) ? 255 : 0;
            fprintf(f, "%d", on);
            fclose(f);
            leds_set++;
            if (pled_log_count <= 5) {
                printf("[DualSense] LED %d = %d (path: %s)\n", i+1, on, path);
            }
        }
    }
    
    if (pled_log_count <= 5 && leds_set == 0) {
        printf("[DualSense] WARNING: No player LED paths found!\n");
    }
}

/* ============================================================================
 * CONTROLLER INFO
 * 
 * Static metadata about the DualSense controller.
 * ============================================================================ */

static const controller_info_t dualsense_info = {
    .name = "DualSense",
    .manufacturer = "Sony",
    .vendor_id = DUALSENSE_VID,
    .product_id = DUALSENSE_PID,
    .capabilities = (
        CONTROLLER_CAP_BUTTONS |
        CONTROLLER_CAP_ANALOG_STICKS |
        CONTROLLER_CAP_TRIGGERS |
        CONTROLLER_CAP_RUMBLE |
        CONTROLLER_CAP_MOTION |
        CONTROLLER_CAP_TOUCHPAD |
        CONTROLLER_CAP_LIGHTBAR |
        CONTROLLER_CAP_PLAYER_LEDS |
        CONTROLLER_CAP_BATTERY
    ),
    .supports_bluetooth = 1,
    .supports_usb = 1
};

/* ============================================================================
 * DRIVER IMPLEMENTATION
 * ============================================================================ */

static int dualsense_init(void) {
    init_crc32_table();
    printf("[DualSense] Driver initialized\n");
    return 0;
}

static void dualsense_shutdown(void) {
    printf("[DualSense] Driver shutdown\n");
}

static int dualsense_find_device(void) {
    DIR* dir = opendir("/dev");
    if (!dir) return -1;
    
    struct dirent* entry;
    while ((entry = readdir(dir)) != NULL) {
        if (strncmp(entry->d_name, "hidraw", 6) != 0) continue;
        
        char path[272];
        snprintf(path, sizeof(path), "/dev/%s", entry->d_name);
        
        int fd = open(path, O_RDWR);
        if (fd < 0) continue;
        
        struct hidraw_devinfo info;
        if (ioctl(fd, HIDIOCGRAWINFO, &info) < 0) {
            close(fd);
            continue;
        }
        
        if (info.vendor == DUALSENSE_VID && info.product == DUALSENSE_PID) {
            char name[256] = "";
            ioctl(fd, HIDIOCGRAWNAME(sizeof(name)), name);
            printf("[DualSense] Found: %s (%s) bus=%d\n", name, path, info.bustype);
            
            /* Read calibration data from controller */
            dualsense_read_calibration(fd);
            
            /* Find LED sysfs paths */
            find_led_sysfs_paths();
            
            /* Set initial lightbar color */
            set_lightbar_sysfs(255, 0, 0);  /* Red */
            
            closedir(dir);
            return fd;
        }
        close(fd);
    }
    
    closedir(dir);
    return -1;
}

static int dualsense_match_device(uint16_t vid, uint16_t pid) {
    return (vid == DUALSENSE_VID && pid == DUALSENSE_PID);
}

/* D-pad parsing helper */
void dualsense_parse_dpad(uint8_t buttons1, controller_state_t* out_state) {
    uint8_t dpad = buttons1 & 0x0F;
    
    switch (dpad) {
        case 0: /* N */
            CONTROLLER_BTN_SET(out_state, BTN_DPAD_UP);
            break;
        case 1: /* NE */
            CONTROLLER_BTN_SET(out_state, BTN_DPAD_UP);
            CONTROLLER_BTN_SET(out_state, BTN_DPAD_RIGHT);
            break;
        case 2: /* E */
            CONTROLLER_BTN_SET(out_state, BTN_DPAD_RIGHT);
            break;
        case 3: /* SE */
            CONTROLLER_BTN_SET(out_state, BTN_DPAD_DOWN);
            CONTROLLER_BTN_SET(out_state, BTN_DPAD_RIGHT);
            break;
        case 4: /* S */
            CONTROLLER_BTN_SET(out_state, BTN_DPAD_DOWN);
            break;
        case 5: /* SW */
            CONTROLLER_BTN_SET(out_state, BTN_DPAD_DOWN);
            CONTROLLER_BTN_SET(out_state, BTN_DPAD_LEFT);
            break;
        case 6: /* W */
            CONTROLLER_BTN_SET(out_state, BTN_DPAD_LEFT);
            break;
        case 7: /* NW */
            CONTROLLER_BTN_SET(out_state, BTN_DPAD_UP);
            CONTROLLER_BTN_SET(out_state, BTN_DPAD_LEFT);
            break;
        /* 8+ = centered, no buttons */
    }
}

static int dualsense_process_input(const uint8_t* buf, size_t len, 
                                   controller_state_t* out_state) {
    if (len < 12 || buf[DS_OFF_REPORT_ID] != DS_BT_REPORT_ID) {
        return -1;
    }
    
    /* Clear state */
    memset(out_state, 0, sizeof(*out_state));
    out_state->left_stick_x = 128;
    out_state->left_stick_y = 128;
    out_state->right_stick_x = 128;
    out_state->right_stick_y = 128;
    
    /* Analog sticks */
    out_state->left_stick_x = buf[DS_OFF_LX];
    out_state->left_stick_y = buf[DS_OFF_LY];
    out_state->right_stick_x = buf[DS_OFF_RX];
    out_state->right_stick_y = buf[DS_OFF_RY];
    
    /* Apply deadzone */
    #define DEADZONE 6
    out_state->left_stick_x = CONTROLLER_APPLY_DEADZONE(out_state->left_stick_x, DEADZONE);
    out_state->left_stick_y = CONTROLLER_APPLY_DEADZONE(out_state->left_stick_y, DEADZONE);
    out_state->right_stick_x = CONTROLLER_APPLY_DEADZONE(out_state->right_stick_x, DEADZONE);
    out_state->right_stick_y = CONTROLLER_APPLY_DEADZONE(out_state->right_stick_y, DEADZONE);
    
    /* Triggers */
    out_state->left_trigger = buf[DS_OFF_L2];
    out_state->right_trigger = buf[DS_OFF_R2];
    
    /* Buttons */
    uint8_t buttons1 = buf[DS_OFF_BUTTONS1];
    uint8_t buttons2 = buf[DS_OFF_BUTTONS2];
    uint8_t buttons3 = buf[DS_OFF_BUTTONS3];
    
    /* D-pad */
    dualsense_parse_dpad(buttons1, out_state);
    
    /* Face buttons */
    if (buttons1 & DS_BTN1_CROSS)    CONTROLLER_BTN_SET(out_state, BTN_SOUTH);
    if (buttons1 & DS_BTN1_CIRCLE)   CONTROLLER_BTN_SET(out_state, BTN_EAST);
    if (buttons1 & DS_BTN1_SQUARE)   CONTROLLER_BTN_SET(out_state, BTN_WEST);
    if (buttons1 & DS_BTN1_TRIANGLE) CONTROLLER_BTN_SET(out_state, BTN_NORTH);
    
    /* Shoulder buttons */
    if (buttons2 & DS_BTN2_L1) CONTROLLER_BTN_SET(out_state, BTN_L1);
    if (buttons2 & DS_BTN2_R1) CONTROLLER_BTN_SET(out_state, BTN_R1);
    if (buttons2 & DS_BTN2_L2) CONTROLLER_BTN_SET(out_state, BTN_L2);
    if (buttons2 & DS_BTN2_R2) CONTROLLER_BTN_SET(out_state, BTN_R2);
    
    /* Stick clicks */
    if (buttons2 & DS_BTN2_L3) CONTROLLER_BTN_SET(out_state, BTN_L3);
    if (buttons2 & DS_BTN2_R3) CONTROLLER_BTN_SET(out_state, BTN_R3);
    
    /* Center buttons */
    if (buttons2 & DS_BTN2_CREATE)  CONTROLLER_BTN_SET(out_state, BTN_SELECT);
    if (buttons2 & DS_BTN2_OPTIONS) CONTROLLER_BTN_SET(out_state, BTN_START);
    if (buttons3 & DS_BTN3_PS)      CONTROLLER_BTN_SET(out_state, BTN_HOME);
    if (buttons3 & DS_BTN3_TOUCHPAD) CONTROLLER_BTN_SET(out_state, BTN_TOUCHPAD);
    if (buttons3 & DS_BTN3_MUTE)    CONTROLLER_BTN_SET(out_state, BTN_MUTE);
    
    /* Motion sensors */
    if (len >= 28) {
        int16_t raw_gyro_x  = (int16_t)(buf[DS_OFF_GYRO_X] | (buf[DS_OFF_GYRO_X + 1] << 8));
        int16_t raw_gyro_y  = (int16_t)(buf[DS_OFF_GYRO_Y] | (buf[DS_OFF_GYRO_Y + 1] << 8));
        int16_t raw_gyro_z  = (int16_t)(buf[DS_OFF_GYRO_Z] | (buf[DS_OFF_GYRO_Z + 1] << 8));
        int16_t raw_accel_x = (int16_t)(buf[DS_OFF_ACCEL_X] | (buf[DS_OFF_ACCEL_X + 1] << 8));
        int16_t raw_accel_y = (int16_t)(buf[DS_OFF_ACCEL_Y] | (buf[DS_OFF_ACCEL_Y + 1] << 8));
        int16_t raw_accel_z = (int16_t)(buf[DS_OFF_ACCEL_Z] | (buf[DS_OFF_ACCEL_Z + 1] << 8));
        
        if (g_ds_calibration.valid) {
            /* Apply calibration: output is in DS_GYRO_RES_PER_DEG_S (1024) units per deg/s
             * and DS_ACC_RES_PER_G (8192) units per g */
            out_state->gyro_x  = (int16_t)apply_calibration(raw_gyro_x, &g_ds_calibration.gyro[0]);
            out_state->gyro_y  = (int16_t)apply_calibration(raw_gyro_y, &g_ds_calibration.gyro[1]);
            out_state->gyro_z  = (int16_t)apply_calibration(raw_gyro_z, &g_ds_calibration.gyro[2]);
            out_state->accel_x = (int16_t)apply_calibration(raw_accel_x, &g_ds_calibration.accel[0]);
            out_state->accel_y = (int16_t)apply_calibration(raw_accel_y, &g_ds_calibration.accel[1]);
            out_state->accel_z = (int16_t)apply_calibration(raw_accel_z, &g_ds_calibration.accel[2]);
        } else {
            /* No calibration - use raw values */
            out_state->gyro_x  = raw_gyro_x;
            out_state->gyro_y  = raw_gyro_y;
            out_state->gyro_z  = raw_gyro_z;
            out_state->accel_x = raw_accel_x;
            out_state->accel_y = raw_accel_y;
            out_state->accel_z = raw_accel_z;
        }
    }
    
    /* Touchpad */
    if (len >= DS_OFF_TOUCHPAD + 4) {
        const uint8_t* touch = &buf[DS_OFF_TOUCHPAD];
        
        for (int i = 0; i < 2; i++) {
            uint8_t contact = touch[i * 4];
            out_state->touch[i].active = !(contact & DS_TOUCH_INACTIVE);
            
            if (out_state->touch[i].active) {
                out_state->touch[i].x = touch[i * 4 + 1] | ((touch[i * 4 + 2] & 0x0F) << 8);
                out_state->touch[i].y = (touch[i * 4 + 2] >> 4) | (touch[i * 4 + 3] << 4);
            }
        }
        
        /* --- Touchpad-as-R3 Feature --- */
        /* Swipe on touchpad controls right stick (for controllers without R3 drift issues) */
        static int touch_initial_x = 0;
        static int touch_initial_y = 0;
        static int touch_was_active = 0;
        
        if (out_state->touch[0].active) {
            int touch_x = out_state->touch[0].x;
            int touch_y = out_state->touch[0].y;
            
            if (!touch_was_active) {
                /* Touch just started - record initial position */
                touch_initial_x = touch_x;
                touch_initial_y = touch_y;
                touch_was_active = 1;
            }
            
            /* Calculate delta from initial touch position */
            int delta_x = touch_x - touch_initial_x;
            int delta_y = touch_y - touch_initial_y;
            
            /* Convert to stick value (sensitivity: 400 pixels = full deflection) */
            int sensitivity = 400;
            int stick_x = 128 + (delta_x * 127) / sensitivity;
            int stick_y = 128 + (delta_y * 127) / sensitivity;
            
            /* Clamp to 0-255 */
            if (stick_x < 0) stick_x = 0;
            if (stick_x > 255) stick_x = 255;
            if (stick_y < 0) stick_y = 0;
            if (stick_y > 255) stick_y = 255;
            
            /* Override right stick with touchpad values */
            out_state->right_stick_x = (uint8_t)stick_x;
            out_state->right_stick_y = (uint8_t)stick_y;
        } else {
            touch_was_active = 0;
        }
    }
    
    /* Battery */
    if (len >= 55) {
        uint8_t battery_byte = buf[DS_OFF_BATTERY];
        
        /* Low 4 bits = battery level (0-10, where 10 = 100%) */
        uint8_t battery_data = battery_byte & 0x0F;
        out_state->battery_level = (battery_data > 10) ? 100 : battery_data * 10;
        
        /* High 4 bits = charging status */
        /* 0x0 = discharging, 0x1 = charging, 0x2 = full */
        uint8_t charging_status = (battery_byte >> 4) & 0x0F;
        out_state->battery_charging = (charging_status == 0x1 || charging_status == 0x2) ? 1 : 0;
        
        /* Store full status for more accurate reporting */
        out_state->battery_full = (charging_status == 0x2) ? 1 : 0;
    }
    
    out_state->timestamp_ms = time_get_ms();
    
    return 0;
}

/* Output report sequence counter */
static uint8_t output_seq = 0;

/* Cached LED state for change detection */
static uint8_t last_led_r = 255, last_led_g = 255, last_led_b = 255;
static uint8_t last_player_leds = 0xFF;

static int dualsense_send_output(int fd, const controller_output_t* output) {
    /* 
     * LED control via sysfs.
     * We refresh every call to fight against the kernel driver's defaults.
     * The kernel hid-playstation driver sets blue + player 1, so we override.
     */
    static int led_refresh_counter = 0;
    led_refresh_counter++;
    
    /* Force refresh every 10 calls (~100ms at 100Hz) to fight kernel driver */
    int force_refresh = (led_refresh_counter >= 10);
    if (force_refresh) {
        led_refresh_counter = 0;
    }
    
    if (force_refresh || 
        output->led_r != last_led_r || 
        output->led_g != last_led_g || 
        output->led_b != last_led_b) {
        set_lightbar_sysfs(output->led_r, output->led_g, output->led_b);
        last_led_r = output->led_r;
        last_led_g = output->led_g;
        last_led_b = output->led_b;
    }
    
    if (force_refresh || output->player_leds != last_player_leds) {
        set_player_leds_sysfs(output->player_leds);
        last_player_leds = output->player_leds;
    }
    
    /* Rumble via hidraw */
    if (fd < 0) return -1;
    
    uint8_t report[DS_BT_OUTPUT_SIZE] = {0};
    
    report[0] = 0x31;  /* Report ID */
    report[1] = (output_seq << 4) & 0xF0;
    output_seq = (output_seq + 1) & 0x0F;
    report[2] = 0x10;  /* Tag */
    report[3] = 0x03;  /* Valid flags: rumble + haptics */
    report[4] = 0;     /* No LED flags (using sysfs) */
    report[5] = output->rumble_right;
    report[6] = output->rumble_left;
    
    /* Calculate CRC32 */
    uint8_t crc_buf[75];
    crc_buf[0] = 0xA2;  /* BT output report header */
    memcpy(&crc_buf[1], report, 74);
    uint32_t crc = dualsense_calc_crc32(crc_buf, 75);
    
    report[74] = crc & 0xFF;
    report[75] = (crc >> 8) & 0xFF;
    report[76] = (crc >> 16) & 0xFF;
    report[77] = (crc >> 24) & 0xFF;
    
    ssize_t written = write(fd, report, sizeof(report));
    return (written > 0) ? 0 : -1;
}

static void dualsense_on_disconnect(void) {
    printf("[DualSense] Disconnected\n");
    
    /* Reset LED cache */
    last_led_r = 255;
    last_led_g = 255;
    last_led_b = 255;
    last_player_leds = 0xFF;
    
    /* Clear sysfs paths (device might get new input number on reconnect) */
    g_lightbar_path[0] = '\0';
}

static void dualsense_enter_low_power(int fd) {
    printf("[DualSense] Entering low power mode\n");
    
    /* Turn off LEDs */
    set_lightbar_sysfs(0, 0, 0);
    set_player_leds_sysfs(0);
    
    /* Stop rumble */
    controller_output_t off = {0};
    dualsense_send_output(fd, &off);
}

/* ============================================================================
 * DRIVER INSTANCE
 * ============================================================================ */

static const controller_driver_t dualsense_driver = {
    .info = &dualsense_info,
    .init = dualsense_init,
    .shutdown = dualsense_shutdown,
    .find_device = dualsense_find_device,
    .match_device = dualsense_match_device,
    .process_input = dualsense_process_input,
    .send_output = dualsense_send_output,
    .on_disconnect = dualsense_on_disconnect,
    .enter_low_power = dualsense_enter_low_power
};

const controller_driver_t* dualsense_get_driver(void) {
    return &dualsense_driver;
}

void dualsense_register(void) {
    controller_register(&dualsense_driver);
    printf("[DualSense] Driver registered\n");
}