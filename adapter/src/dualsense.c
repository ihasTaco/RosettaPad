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

// =================================================================
// DualSense BT Output Report Structure (from Linux hid-playstation.c)
// =================================================================
// 
// struct dualsense_output_report_bt (78 bytes total):
//   [0]     report_id (0x31)
//   [1]     seq_tag   (upper nibble = sequence 0-15, lower = 0)
//   [2]     tag       (0x10 magic value)
//   [3-49]  common    (47 bytes):
//     [3]   valid_flag0 - bit0=rumble, bit1=haptics, bit2=right trigger, bit3=left trigger
//     [4]   valid_flag1 - bit0=mic LED, bit1=power save, bit2=lightbar, bit3=player LEDs
//     [5]   motor_right
//     [6]   motor_left
//     [7-10]  reserved[4] (audio)
//     [11]  mute_button_led
//     [12]  power_save_control
//     [13-40] reserved2[28]
//     [41]  valid_flag2 - bit1=lightbar setup
//     [42-43] reserved3[2]
//     [44]  lightbar_setup (0x02 = fade in)
//     [45]  led_brightness
//     [46]  player_leds
//     [47]  lightbar_red
//     [48]  lightbar_green
//     [49]  lightbar_blue
//   [50-73] reserved[24]
//   [74-77] crc32 (little-endian)

// Valid flag 0 bits
#define DS_OUTPUT_VALID0_RUMBLE             0x01
#define DS_OUTPUT_VALID0_HAPTICS            0x02
#define DS_OUTPUT_VALID0_RIGHT_TRIGGER      0x04
#define DS_OUTPUT_VALID0_LEFT_TRIGGER       0x08

// Valid flag 1 bits
#define DS_OUTPUT_VALID1_MIC_MUTE_LED       0x01
#define DS_OUTPUT_VALID1_POWER_SAVE         0x02
#define DS_OUTPUT_VALID1_LIGHTBAR           0x04
#define DS_OUTPUT_VALID1_PLAYER_LEDS        0x08
#define DS_OUTPUT_VALID1_RELEASE_LED        0x10

// Valid flag 2 bits
#define DS_OUTPUT_VALID2_LIGHTBAR_SETUP     0x02

// Byte offsets in full BT output report
#define DS_OUT_REPORT_ID        0
#define DS_OUT_SEQ_TAG          1
#define DS_OUT_TAG              2
#define DS_OUT_VALID_FLAG0      3
#define DS_OUT_VALID_FLAG1      4
#define DS_OUT_MOTOR_RIGHT      5
#define DS_OUT_MOTOR_LEFT       6
#define DS_OUT_VALID_FLAG2      41
#define DS_OUT_LIGHTBAR_SETUP   44
#define DS_OUT_LED_BRIGHTNESS   45
#define DS_OUT_PLAYER_LEDS      46
#define DS_OUT_LIGHTBAR_RED     47
#define DS_OUT_LIGHTBAR_GREEN   48
#define DS_OUT_LIGHTBAR_BLUE    49

// Report size
#define DS_OUTPUT_REPORT_BT_SIZE 78

// Sequence counter for BT reports
static uint8_t output_seq = 0;

void dualsense_send_output(int fd,
    uint8_t right_motor, uint8_t left_motor,
    uint8_t led_r, uint8_t led_g, uint8_t led_b,
    uint8_t player_leds)
{
    uint8_t report[DS_OUTPUT_REPORT_BT_SIZE] = {0};
    
    // Header
    report[DS_OUT_REPORT_ID] = 0x31;
    report[DS_OUT_SEQ_TAG] = (output_seq << 4) | 0x00;
    output_seq = (output_seq + 1) & 0x0F;
    report[DS_OUT_TAG] = 0x10;  // Magic tag value
    
    // Valid flags - tell controller which features we're setting
    report[DS_OUT_VALID_FLAG0] = DS_OUTPUT_VALID0_RUMBLE | DS_OUTPUT_VALID0_HAPTICS;
    report[DS_OUT_VALID_FLAG1] = DS_OUTPUT_VALID1_LIGHTBAR | DS_OUTPUT_VALID1_PLAYER_LEDS;
    
    // Rumble motors
    report[DS_OUT_MOTOR_RIGHT] = right_motor;
    report[DS_OUT_MOTOR_LEFT] = left_motor;
    
    // Lightbar control
    report[DS_OUT_VALID_FLAG2] = DS_OUTPUT_VALID2_LIGHTBAR_SETUP;
    report[DS_OUT_LIGHTBAR_SETUP] = 0x02;  // Enable lightbar fade-in
    report[DS_OUT_LED_BRIGHTNESS] = 0xFF;  // Full brightness
    report[DS_OUT_PLAYER_LEDS] = player_leds;
    report[DS_OUT_LIGHTBAR_RED] = led_r;
    report[DS_OUT_LIGHTBAR_GREEN] = led_g;
    report[DS_OUT_LIGHTBAR_BLUE] = led_b;
    
    // Calculate CRC32
    // For BT output reports, CRC is calculated over: 0xA2 seed byte + report[0..73]
    uint8_t crc_buf[75];
    crc_buf[0] = 0xA2;  // Output report seed
    memcpy(&crc_buf[1], report, 74);
    uint32_t crc = dualsense_calc_crc32(crc_buf, 75);
    
    // Store CRC at end (little-endian)
    report[74] = (crc >> 0) & 0xFF;
    report[75] = (crc >> 8) & 0xFF;
    report[76] = (crc >> 16) & 0xFF;
    report[77] = (crc >> 24) & 0xFF;
    
    if (fd >= 0) {
        ssize_t written = write(fd, report, sizeof(report));
        if (written < 0) {
            static int err_count = 0;
            if (++err_count <= 5) {
                printf("[DualSense] Output write error: %s\n", strerror(errno));
            }
        }
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
    
    // Process touchpad-as-R3 if enabled and touchpad data is available
    // Touchpad data is at offset 34 in BT report (verified via debug comparison)
    #define DS_OFF_TOUCHPAD_BT 34
    
    if (g_touchpad_as_r3 && len >= DS_OFF_TOUCHPAD_BT + 4) {
        const uint8_t* touch = &buf[DS_OFF_TOUCHPAD_BT];
        uint8_t contact = touch[0];
        int touch_active = !(contact & DS_TOUCH_INACTIVE);
        
        if (touch_active) {
            // Extract 12-bit X and Y coordinates
            int touch_x = touch[1] | ((touch[2] & 0x0F) << 8);
            int touch_y = (touch[2] >> 4) | (touch[3] << 4);
            
            // Sanity check - values should be within touchpad bounds
            if (touch_x > DS_TOUCHPAD_WIDTH || touch_y > DS_TOUCHPAD_HEIGHT) {
                // Invalid data, don't update - keep physical R3 values
                // (rx and ry already have the physical stick values from above)
            } else {
                pthread_mutex_lock(&g_touchpad_mutex);
                if (!g_touchpad_state.active) {
                    // First touch - record initial position as the "virtual center"
                    g_touchpad_state.active = 1;
                    g_touchpad_state.initial_x = touch_x;
                    g_touchpad_state.initial_y = touch_y;
                }
                g_touchpad_state.current_x = touch_x;
                g_touchpad_state.current_y = touch_y;
                
                // Calculate delta from initial touch position
                int delta_x = touch_x - g_touchpad_state.initial_x;
                int delta_y = touch_y - g_touchpad_state.initial_y;
                pthread_mutex_unlock(&g_touchpad_mutex);
                
                // Scale touchpad delta to stick range
                // Touchpad is 1920x1080, higher sensitivity value = slower movement
                // 400 means ~400 pixels of movement = full stick deflection
                int sensitivity = 400;
                
                int stick_x = 128 + (delta_x * 127) / sensitivity;
                int stick_y = 128 + (delta_y * 127) / sensitivity;
                
                // Clamp to valid range [0, 255]
                if (stick_x < 0) stick_x = 0;
                else if (stick_x > 255) stick_x = 255;
                if (stick_y < 0) stick_y = 0;
                else if (stick_y > 255) stick_y = 255;
                
                // Override right stick with touchpad values
                rx = (uint8_t)stick_x;
                ry = (uint8_t)stick_y;
            }
        } else {
            // Touch not active - reset state and let physical R3 work
            pthread_mutex_lock(&g_touchpad_mutex);
            if (g_touchpad_state.active) {
                g_touchpad_state.active = 0;
            }
            pthread_mutex_unlock(&g_touchpad_mutex);
            
            // DON'T override rx/ry here - keep the physical stick values
            // that were read at the start of this function
        }
    }
    
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
    
    // Touchpad click -> Select (alternate) - only if not using touchpad as R3
    if (!g_touchpad_as_r3 && (buttons3 & DS_BTN3_TOUCHPAD)) {
        ds3_btn1 |= DS3_BTN_SELECT;
    }
    
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
        
        // Suppress unused variable warnings
        (void)ds_gyro_x;
        (void)ds_gyro_y;
        
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
    
    printf("[DualSense] Output thread started\n");
    
    uint8_t last_right = 0, last_left = 0;
    lightbar_state_t last_lightbar = {0, 0, 0, 0, 0};
    int update_count = 0;
    
    while (g_running) {
        // Check for lightbar config changes periodically
        if (++update_count >= 50) {  // Every ~500ms
            update_count = 0;
            read_lightbar_state();
        }
        
        // Get current rumble state
        pthread_mutex_lock(&g_rumble_mutex);
        uint8_t right = g_rumble_right;
        uint8_t left = g_rumble_left;
        pthread_mutex_unlock(&g_rumble_mutex);
        
        // Get current lightbar state
        pthread_mutex_lock(&g_lightbar_mutex);
        lightbar_state_t lightbar = g_lightbar_state;
        pthread_mutex_unlock(&g_lightbar_mutex);
        
        // Only send if something changed
        int rumble_changed = (right != last_right || left != last_left);
        int lightbar_changed = (lightbar.r != last_lightbar.r ||
                                lightbar.g != last_lightbar.g ||
                                lightbar.b != last_lightbar.b ||
                                lightbar.player_leds != last_lightbar.player_leds);
        
        if (g_hidraw_fd >= 0 && (rumble_changed || lightbar_changed)) {
            dualsense_send_output(g_hidraw_fd, right, left,
                                  lightbar.r, lightbar.g, lightbar.b,
                                  lightbar.player_leds);
            
            last_right = right;
            last_left = left;
            last_lightbar = lightbar;
        }
        
        usleep(10000);  // 100Hz output rate
    }
    
    return NULL;
}