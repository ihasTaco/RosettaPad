/*
 * RosettaPad - DualSense (PS5) Controller Interface
 * Handles Bluetooth communication with DualSense controllers
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

#include "common.h"
#include "ds3.h"
#include "dualsense.h"

// =================================================================
// CRC32 for DualSense Bluetooth Output
// =================================================================
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

// =================================================================
// Lightbar IPC
// =================================================================
static const char* LIGHTBAR_IPC_PATH = "/tmp/rosettapad/lightbar_state.json";

static int parse_lightbar_json(const char* json, lightbar_state_t* state) {
    const char* ptr;
    
    ptr = strstr(json, "\"r\":");
    if (ptr) state->r = (uint8_t)atoi(ptr + 4);
    
    ptr = strstr(json, "\"g\":");
    if (ptr) state->g = (uint8_t)atoi(ptr + 4);
    
    ptr = strstr(json, "\"b\":");
    if (ptr) state->b = (uint8_t)atoi(ptr + 4);
    
    ptr = strstr(json, "\"player_leds\":");
    if (ptr) state->player_leds = (uint8_t)atoi(ptr + 14);
    
    ptr = strstr(json, "\"player_led_brightness\":");
    if (ptr) {
        float brightness = atof(ptr + 24);
        state->player_brightness = (uint8_t)(brightness * 255);
    }
    
    return 0;
}

static void read_lightbar_state(void) {
    FILE* f = fopen(LIGHTBAR_IPC_PATH, "r");
    if (!f) return;
    
    char buf[256];
    if (fgets(buf, sizeof(buf), f)) {
        lightbar_state_t new_state;
        if (parse_lightbar_json(buf, &new_state) == 0) {
            pthread_mutex_lock(&g_lightbar_mutex);
            g_lightbar_state = new_state;
            pthread_mutex_unlock(&g_lightbar_mutex);
        }
    }
    fclose(f);
}

// =================================================================
// Public Functions
// =================================================================

void dualsense_init(void) {
    init_crc32_table();
    printf("[DualSense] Controller interface initialized\n");
}

uint32_t dualsense_calc_crc32(const uint8_t* data, size_t len) {
    uint32_t crc = 0xFFFFFFFF;
    for (size_t i = 0; i < len; i++) {
        crc = (crc >> 8) ^ crc32_table[(crc ^ data[i]) & 0xFF];
    }
    return ~crc;
}

int dualsense_find_hidraw(void) {
    DIR* dir = opendir("/dev");
    if (!dir) return -1;
    
    struct dirent* entry;
    while ((entry = readdir(dir)) != NULL) {
        if (strncmp(entry->d_name, "hidraw", 6) != 0) continue;
        
        char path[64];
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
            printf("[DualSense] Found: %s (%s)\n", name, path);
            closedir(dir);
            return fd;
        }
        close(fd);
    }
    
    closedir(dir);
    return -1;
}

void dualsense_send_output(int fd,
    uint8_t right_motor, uint8_t left_motor,
    uint8_t led_r, uint8_t led_g, uint8_t led_b,
    uint8_t player_leds)
{
    static uint8_t seq = 0;
    uint8_t report[DS_BT_OUTPUT_SIZE] = {0};
    
    // Build BT output report
    report[0] = 0x31;                    // Report ID for BT
    report[1] = (seq << 4) | 0x00;       // Sequence number
    seq = (seq + 1) & 0x0F;
    
    report[2] = 0x10;                    // Feature flags
    report[3] = 0x01;                    // More flags
    report[4] = 0x04 | 0x08;             // Enable rumble + lightbar
    
    report[5] = right_motor;             // Right rumble
    report[6] = left_motor;              // Left rumble
    
    report[41] = 0x02;                   // Lightbar setup
    report[43] = 0xFF;                   // Brightness
    report[44] = player_leds;            // Player LEDs
    report[45] = led_r;
    report[46] = led_g;
    report[47] = led_b;
    
    // Calculate CRC32 (BT reports need seed byte 0xA2 prepended)
    uint8_t crc_buf[75];
    crc_buf[0] = 0xA2;
    memcpy(&crc_buf[1], report, 74);
    uint32_t crc = dualsense_calc_crc32(crc_buf, 75);
    
    report[74] = (crc >> 0) & 0xFF;
    report[75] = (crc >> 8) & 0xFF;
    report[76] = (crc >> 16) & 0xFF;
    report[77] = (crc >> 24) & 0xFF;
    
    if (fd >= 0) {
        write(fd, report, sizeof(report));
    }
}

void dualsense_send_rumble(int fd, uint8_t right_motor, uint8_t left_motor) {
    pthread_mutex_lock(&g_lightbar_mutex);
    lightbar_state_t state = g_lightbar_state;
    pthread_mutex_unlock(&g_lightbar_mutex);
    
    dualsense_send_output(fd, right_motor, left_motor,
                          state.r, state.g, state.b, state.player_leds);
}

int dualsense_process_input(const uint8_t* buf, size_t len) {
    // Validate BT input report
    if (len < 12 || buf[DS_OFF_REPORT_ID] != DS_BT_REPORT_ID) {
        return -1;
    }
    
    // Extract raw values
    uint8_t lx = buf[DS_OFF_LX];
    uint8_t ly = buf[DS_OFF_LY];
    uint8_t rx = buf[DS_OFF_RX];
    uint8_t ry = buf[DS_OFF_RY];
    uint8_t l2 = buf[DS_OFF_L2];
    uint8_t r2 = buf[DS_OFF_R2];
    uint8_t buttons1 = buf[DS_OFF_BUTTONS1];
    uint8_t buttons2 = buf[DS_OFF_BUTTONS2];
    uint8_t buttons3 = buf[DS_OFF_BUTTONS3];
    
    // Convert to DS3 format
    uint8_t ds3_btn1 = 0;  // Select, L3, R3, Start, D-pad
    uint8_t ds3_btn2 = 0;  // L2, R2, L1, R1, face buttons
    uint8_t ds3_ps = 0;
    
    // D-pad (low nibble of buttons1)
    ds3_btn1 |= ds3_convert_dpad(buttons1 & 0x0F);
    
    // Face buttons (high nibble of buttons1)
    if (buttons1 & DS_BTN1_SQUARE)   ds3_btn2 |= DS3_BTN_SQUARE;
    if (buttons1 & DS_BTN1_CROSS)    ds3_btn2 |= DS3_BTN_CROSS;
    if (buttons1 & DS_BTN1_CIRCLE)   ds3_btn2 |= DS3_BTN_CIRCLE;
    if (buttons1 & DS_BTN1_TRIANGLE) ds3_btn2 |= DS3_BTN_TRIANGLE;
    
    // Shoulders (buttons2)
    if (buttons2 & DS_BTN2_L1) ds3_btn2 |= DS3_BTN_L1;
    if (buttons2 & DS_BTN2_R1) ds3_btn2 |= DS3_BTN_R1;
    if (buttons2 & DS_BTN2_L2) ds3_btn2 |= DS3_BTN_L2;
    if (buttons2 & DS_BTN2_R2) ds3_btn2 |= DS3_BTN_R2;
    
    // Sticks (buttons2)
    if (buttons2 & DS_BTN2_L3) ds3_btn1 |= DS3_BTN_L3;
    if (buttons2 & DS_BTN2_R3) ds3_btn1 |= DS3_BTN_R3;
    
    // Options -> Start, Create -> Select (buttons2)
    if (buttons2 & DS_BTN2_OPTIONS) ds3_btn1 |= DS3_BTN_START;
    if (buttons2 & DS_BTN2_CREATE)  ds3_btn1 |= DS3_BTN_SELECT;
    
    // PS button (buttons3)
    if (buttons3 & DS_BTN3_PS) ds3_ps = DS3_BTN_PS;
    
    // Touchpad click -> Select (alternate)
    if (buttons3 & DS_BTN3_TOUCHPAD) ds3_btn1 |= DS3_BTN_SELECT;
    
    // Button pressure (full press = 0xFF)
    uint8_t triangle_p = (buttons1 & DS_BTN1_TRIANGLE) ? 0xFF : 0;
    uint8_t circle_p   = (buttons1 & DS_BTN1_CIRCLE)   ? 0xFF : 0;
    uint8_t cross_p    = (buttons1 & DS_BTN1_CROSS)    ? 0xFF : 0;
    uint8_t square_p   = (buttons1 & DS_BTN1_SQUARE)   ? 0xFF : 0;
    
    // Update DS3 report
    ds3_update_report(ds3_btn1, ds3_btn2, ds3_ps,
                      lx, ly, rx, ry, l2, r2,
                      triangle_p, circle_p, cross_p, square_p);
    
    // Process motion data if available
    if (len >= 28) {
        // DualSense gyro/accel are 16-bit signed little-endian
        int16_t ds_gyro_x  = (int16_t)(buf[DS_OFF_GYRO_X] | (buf[DS_OFF_GYRO_X + 1] << 8));
        int16_t ds_gyro_y  = (int16_t)(buf[DS_OFF_GYRO_Y] | (buf[DS_OFF_GYRO_Y + 1] << 8));
        int16_t ds_gyro_z  = (int16_t)(buf[DS_OFF_GYRO_Z] | (buf[DS_OFF_GYRO_Z + 1] << 8));
        int16_t ds_accel_x = (int16_t)(buf[DS_OFF_ACCEL_X] | (buf[DS_OFF_ACCEL_X + 1] << 8));
        int16_t ds_accel_y = (int16_t)(buf[DS_OFF_ACCEL_Y] | (buf[DS_OFF_ACCEL_Y + 1] << 8));
        int16_t ds_accel_z = (int16_t)(buf[DS_OFF_ACCEL_Z] | (buf[DS_OFF_ACCEL_Z + 1] << 8));
        
        // Convert to DS3 format (centered around ~512 for accel, ~498 for gyro)
        // DualSense has different scaling, so we need to convert
        // DS3 accel: ~512 at rest, Â±~400 range
        // DualSense accel: ~0 at rest (after bias), Â±~8192 range
        
        int16_t ds3_accel_x = 512 + (ds_accel_x / 16);
        int16_t ds3_accel_y = 512 + (ds_accel_y / 16);
        int16_t ds3_accel_z = 512 + (ds_accel_z / 16);
        int16_t ds3_gyro_z  = 498 + (ds_gyro_z / 32);  // DS3 only has Z gyro
        
        ds3_update_motion(ds3_accel_x, ds3_accel_y, ds3_accel_z, ds3_gyro_z);
    }
    
    // Process battery status if available (byte 54 in BT report)
    if (len >= 55) {
        // DualSense battery byte format:
        // Bits 0-3: Battery level (0-10, multiply by 10 for percentage)
        // Bit 4: Charging status (1 = charging)
        // Bits 5-7: Power state
        uint8_t battery_byte = buf[DS_OFF_BATTERY];
        uint8_t battery_level = (battery_byte & 0x0F) * 10;  // 0-100%
        int is_charging = (battery_byte & 0x10) ? 1 : 0;
        
        // Cap at 100%
        if (battery_level > 100) battery_level = 100;
        
        // Debug: print battery info periodically
        static int battery_debug_count = 0;
        if (++battery_debug_count >= 250) {
            battery_debug_count = 0;
            printf("[DualSense] Battery raw=0x%02x level=%d%% charging=%d\n",
                   battery_byte, battery_level, is_charging);
        }
        
        ds3_update_battery_from_dualsense(battery_level, is_charging);
    }
    
    return 0;
}

// =================================================================
// Thread Functions
// =================================================================

void* dualsense_thread(void* arg) {
    (void)arg;
    
    printf("[DualSense] Input thread started, waiting for controller...\n");
    
    // Wait for controller connection
    while (g_running && g_hidraw_fd < 0) {
        g_hidraw_fd = dualsense_find_hidraw();
        if (g_hidraw_fd < 0) sleep(1);
    }
    
    if (g_hidraw_fd < 0) return NULL;
    printf("[DualSense] Controller connected!\n");
    
    uint8_t buf[DS_BT_INPUT_SIZE];
    
    while (g_running) {
        ssize_t n = read(g_hidraw_fd, buf, sizeof(buf));
        
        if (n < 10) {
            if (errno == EAGAIN) {
                usleep(1000);
                continue;
            }
            
            printf("[DualSense] Disconnected, reconnecting...\n");
            close(g_hidraw_fd);
            g_hidraw_fd = -1;
            
            while (g_running && g_hidraw_fd < 0) {
                g_hidraw_fd = dualsense_find_hidraw();
                if (g_hidraw_fd < 0) sleep(1);
            }
            
            if (g_hidraw_fd >= 0) {
                printf("[DualSense] Reconnected!\n");
            }
            continue;
        }
        
        dualsense_process_input(buf, n);
    }
    
    return NULL;
}

void* dualsense_output_thread(void* arg) {
    (void)arg;
    
    uint8_t last_right = 0;
    uint8_t last_left = 0;
    lightbar_state_t last_lightbar = {0, 0, 0, 0, 0};
    int update_counter = 0;
    
    printf("[DualSense] Output thread started\n");
    
    while (g_running) {
        uint8_t right, left;
        lightbar_state_t lb_state;
        
        // Get current rumble state
        pthread_mutex_lock(&g_rumble_mutex);
        right = g_rumble_right;
        left = g_rumble_left;
        pthread_mutex_unlock(&g_rumble_mutex);
        
        // Periodically check for lightbar IPC updates
        if (++update_counter >= 10) {
            update_counter = 0;
            read_lightbar_state();
        }
        
        pthread_mutex_lock(&g_lightbar_mutex);
        lb_state = g_lightbar_state;
        pthread_mutex_unlock(&g_lightbar_mutex);
        
        // Check if we need to send an update
        int rumble_changed = (right != last_right || left != last_left);
        int lightbar_changed = (lb_state.r != last_lightbar.r ||
                               lb_state.g != last_lightbar.g ||
                               lb_state.b != last_lightbar.b ||
                               lb_state.player_leds != last_lightbar.player_leds);
        int rumble_active = (right > 0 || left > 0);
        
        if (g_hidraw_fd >= 0 && (rumble_changed || lightbar_changed || rumble_active)) {
            dualsense_send_output(g_hidraw_fd, right, left,
                                 lb_state.r, lb_state.g, lb_state.b,
                                 lb_state.player_leds);
            last_right = right;
            last_left = left;
            last_lightbar = lb_state;
        }
        
        usleep(10000);  // 10ms update rate
    }
    
    // Clear outputs on shutdown
    if (g_hidraw_fd >= 0) {
        dualsense_send_output(g_hidraw_fd, 0, 0, 0, 0, 0, 0);
    }
    
    return NULL;
}