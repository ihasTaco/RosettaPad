/*
 * RosettaPad - DualSense (PS5) Controller Interface
 * Handles Bluetooth communication with DualSense controllers
 * 
 * LED Control Strategy (VERIFIED WORKING):
 * The hid-playstation kernel driver manages LEDs via sysfs at:
 *   /sys/class/leds/inputN:rgb:indicator/  (lightbar)
 *   /sys/class/leds/inputN:white:player-X/ (player LEDs)
 * 
 * Writing to multi_intensity and brightness triggers the driver to send
 * the actual HID output report to the controller over Bluetooth.
 * 
 * Rumble is sent via hidraw output reports (without LED flags to avoid conflict).
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

// =================================================================
// LED Sysfs Control (Verified Working)
// =================================================================
// The kernel driver exposes LEDs at /sys/class/leds/inputN:rgb:indicator/
// Writing to these files triggers the driver to send HID output reports.

static char g_lightbar_path[256] = "";
static char g_player_led_paths[5][256] = {"", "", "", "", ""};

// Find LED sysfs paths dynamically based on DualSense device
static void find_led_sysfs_paths(void) {
    g_lightbar_path[0] = '\0';
    for (int i = 0; i < 5; i++) {
        g_player_led_paths[i][0] = '\0';
    }
    
    DIR* led_dir = opendir("/sys/class/leds");
    if (!led_dir) {
        printf("[DualSense] Warning: Could not open /sys/class/leds\n");
        return;
    }
    
    struct dirent* entry;
    while ((entry = readdir(led_dir)) != NULL) {
        if (entry->d_name[0] == '.') continue;
        
        char led_path[256];
        char led_link[512];
        snprintf(led_path, sizeof(led_path), "/sys/class/leds/%s", entry->d_name);
        
        // Check if this LED belongs to a DualSense (054C:0CE6)
        ssize_t len = readlink(led_path, led_link, sizeof(led_link) - 1);
        if (len <= 0) continue;
        led_link[len] = '\0';
        
        if (!strstr(led_link, "054C") || !strstr(led_link, "0CE6")) continue;
        
        // Lightbar (multicolor RGB LED)
        if (strstr(entry->d_name, "rgb:indicator")) {
            snprintf(g_lightbar_path, sizeof(g_lightbar_path), 
                     "/sys/class/leds/%s", entry->d_name);
            printf("[DualSense] Found lightbar: %s\n", g_lightbar_path);
        }
        // Player LEDs
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
    
    if (g_lightbar_path[0] == '\0') {
        printf("[DualSense] Warning: Lightbar sysfs path not found!\n");
    }
}

// Set lightbar color via sysfs - this WORKS!
static void set_lightbar_sysfs(uint8_t r, uint8_t g, uint8_t b) {
    if (g_lightbar_path[0] == '\0') {
        // Try to find it again (device might have reconnected with new input number)
        find_led_sysfs_paths();
        if (g_lightbar_path[0] == '\0') return;
    }
    
    char path[320];
    FILE* f;
    int ok = 1;
    
    // Set the color intensities first (format: "R G B")
    snprintf(path, sizeof(path), "%s/multi_intensity", g_lightbar_path);
    f = fopen(path, "w");
    if (f) {
        fprintf(f, "%d %d %d", r, g, b);
        fclose(f);
    } else {
        ok = 0;
        // Path might be stale - clear it so we search again next time
        g_lightbar_path[0] = '\0';
    }
    
    // Then set brightness to trigger the update
    snprintf(path, sizeof(path), "%s/brightness", g_lightbar_path);
    f = fopen(path, "w");
    if (f) {
        fprintf(f, "255");
        fclose(f);
    } else {
        ok = 0;
    }
    
    if (ok) {
        static int log_count = 0;
        if (++log_count <= 5 || log_count % 100 == 0) {
            printf("[DualSense] Lightbar: R=%d G=%d B=%d (via sysfs)\n", r, g, b);
        }
    }
}

// Set player LEDs via sysfs
static void set_player_leds_sysfs(uint8_t player_mask) {
    for (int i = 0; i < 5; i++) {
        if (g_player_led_paths[i][0] == '\0') continue;
        
        char path[320];
        snprintf(path, sizeof(path), "%s/brightness", g_player_led_paths[i]);
        
        FILE* f = fopen(path, "w");
        if (f) {
            fprintf(f, "%d", (player_mask & (1 << i)) ? 255 : 0);
            fclose(f);
        }
    }
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
            printf("[DualSense] Found: %s (%s) bus=%d\n", name, path, info.bustype);
            
            if (info.bustype == 5) {
                printf("[DualSense] Connected via Bluetooth\n");
            } else if (info.bustype == 3) {
                printf("[DualSense] Connected via USB\n");
            }
            
            // Find LED sysfs paths
            find_led_sysfs_paths();
            
            closedir(dir);
            return fd;
        }
        close(fd);
    }
    
    closedir(dir);
    return -1;
}

// =================================================================
// DualSense BT Output Report (Rumble Only)
// =================================================================
// LEDs are handled via sysfs - we only send rumble via hidraw

#define DS_OUTPUT_VALID0_RUMBLE             0x01
#define DS_OUTPUT_VALID0_HAPTICS            0x02

#define DS_OUT_REPORT_ID        0
#define DS_OUT_SEQ_TAG          1
#define DS_OUT_TAG              2
#define DS_OUT_VALID_FLAG0      3
#define DS_OUT_VALID_FLAG1      4
#define DS_OUT_MOTOR_RIGHT      5
#define DS_OUT_MOTOR_LEFT       6

#define DS_OUTPUT_REPORT_BT_SIZE 78

static uint8_t output_seq = 0;

// Cached LED state for change detection
static uint8_t g_last_led_r = 255, g_last_led_g = 255, g_last_led_b = 255;
static uint8_t g_last_player_leds = 0xFF;

void dualsense_send_output(int fd,
    uint8_t right_motor, uint8_t left_motor,
    uint8_t led_r, uint8_t led_g, uint8_t led_b,
    uint8_t player_leds)
{
    // === LED Control via sysfs (only when changed) ===
    if (led_r != g_last_led_r || led_g != g_last_led_g || led_b != g_last_led_b) {
        set_lightbar_sysfs(led_r, led_g, led_b);
        g_last_led_r = led_r;
        g_last_led_g = led_g;
        g_last_led_b = led_b;
    }
    
    if (player_leds != g_last_player_leds) {
        set_player_leds_sysfs(player_leds);
        g_last_player_leds = player_leds;
    }
    
    // === Rumble Control via hidraw ===
    if (fd < 0) return;
    
    // Build output report for rumble ONLY (no LED flags)
    uint8_t report[DS_OUTPUT_REPORT_BT_SIZE] = {0};
    
    report[DS_OUT_REPORT_ID] = 0x31;
    report[DS_OUT_SEQ_TAG] = (output_seq << 4) & 0xF0;
    output_seq = (output_seq + 1) & 0x0F;
    report[DS_OUT_TAG] = 0x10;
    
    // Only rumble flags - no LED flags to avoid conflicting with sysfs
    report[DS_OUT_VALID_FLAG0] = DS_OUTPUT_VALID0_RUMBLE | DS_OUTPUT_VALID0_HAPTICS;
    report[DS_OUT_VALID_FLAG1] = 0;  // No LED flags!
    
    report[DS_OUT_MOTOR_RIGHT] = right_motor;
    report[DS_OUT_MOTOR_LEFT] = left_motor;
    
    // Calculate CRC32
    uint8_t crc_buf[75];
    crc_buf[0] = 0xA2;
    memcpy(&crc_buf[1], report, 74);
    uint32_t crc = dualsense_calc_crc32(crc_buf, 75);
    
    report[74] = crc & 0xFF;
    report[75] = (crc >> 8) & 0xFF;
    report[76] = (crc >> 16) & 0xFF;
    report[77] = (crc >> 24) & 0xFF;
    
    ssize_t written = write(fd, report, sizeof(report));
    if (written < 0 && errno != EAGAIN) {
        static int err_count = 0;
        if (++err_count <= 5) {
            printf("[DualSense] Rumble write error: %s\n", strerror(errno));
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

// Output callback for generic controller system
static void dualsense_output_callback(
    uint8_t rumble_right,
    uint8_t rumble_left,
    uint8_t led_r,
    uint8_t led_g,
    uint8_t led_b,
    uint8_t player_leds)
{
    // Always try LED control via sysfs (works even without hidraw)
    if (led_r != g_last_led_r || led_g != g_last_led_g || led_b != g_last_led_b) {
        set_lightbar_sysfs(led_r, led_g, led_b);
        g_last_led_r = led_r;
        g_last_led_g = led_g;
        g_last_led_b = led_b;
    }
    
    if (player_leds != g_last_player_leds) {
        set_player_leds_sysfs(player_leds);
        g_last_player_leds = player_leds;
    }
    
    // Rumble via hidraw if available
    if (g_hidraw_fd >= 0) {
        // Build minimal rumble-only report
        uint8_t report[DS_OUTPUT_REPORT_BT_SIZE] = {0};
        
        report[DS_OUT_REPORT_ID] = 0x31;
        report[DS_OUT_SEQ_TAG] = (output_seq << 4) & 0xF0;
        output_seq = (output_seq + 1) & 0x0F;
        report[DS_OUT_TAG] = 0x10;
        report[DS_OUT_VALID_FLAG0] = DS_OUTPUT_VALID0_RUMBLE | DS_OUTPUT_VALID0_HAPTICS;
        report[DS_OUT_VALID_FLAG1] = 0;
        report[DS_OUT_MOTOR_RIGHT] = rumble_right;
        report[DS_OUT_MOTOR_LEFT] = rumble_left;
        
        uint8_t crc_buf[75];
        crc_buf[0] = 0xA2;
        memcpy(&crc_buf[1], report, 74);
        uint32_t crc = dualsense_calc_crc32(crc_buf, 75);
        
        report[74] = crc & 0xFF;
        report[75] = (crc >> 8) & 0xFF;
        report[76] = (crc >> 16) & 0xFF;
        report[77] = (crc >> 24) & 0xFF;
        
        write(g_hidraw_fd, report, sizeof(report));
    }
}

// Check for PS button press in standby mode
static int dualsense_check_standby_wake(const uint8_t* buf, size_t len) {
    if (len < 12 || buf[DS_OFF_REPORT_ID] != DS_BT_REPORT_ID) {
        return 0;
    }
    
    uint8_t buttons3 = buf[DS_OFF_BUTTONS3];
    static int ps_was_pressed = 0;
    
    if (buttons3 & DS_BTN3_PS) {
        if (!ps_was_pressed) {
            ps_was_pressed = 1;
            return 1;
        }
    } else {
        ps_was_pressed = 0;
    }
    
    return 0;
}

int dualsense_process_input(const uint8_t* buf, size_t len) {
    if (len < 12 || buf[DS_OFF_REPORT_ID] != DS_BT_REPORT_ID) {
        return -1;
    }
    
    uint8_t lx = buf[DS_OFF_LX];
    uint8_t ly = buf[DS_OFF_LY];
    uint8_t rx = buf[DS_OFF_RX];
    uint8_t ry = buf[DS_OFF_RY];
    uint8_t l2 = buf[DS_OFF_L2];
    uint8_t r2 = buf[DS_OFF_R2];
    uint8_t buttons1 = buf[DS_OFF_BUTTONS1];
    uint8_t buttons2 = buf[DS_OFF_BUTTONS2];
    uint8_t buttons3 = buf[DS_OFF_BUTTONS3];
    
    // Apply deadzone
    #define STICK_CENTER 128
    #define STICK_DEADZONE 6
    
    if (lx >= STICK_CENTER - STICK_DEADZONE && lx <= STICK_CENTER + STICK_DEADZONE) lx = STICK_CENTER;
    if (ly >= STICK_CENTER - STICK_DEADZONE && ly <= STICK_CENTER + STICK_DEADZONE) ly = STICK_CENTER;
    if (rx >= STICK_CENTER - STICK_DEADZONE && rx <= STICK_CENTER + STICK_DEADZONE) rx = STICK_CENTER;
    if (ry >= STICK_CENTER - STICK_DEADZONE && ry <= STICK_CENTER + STICK_DEADZONE) ry = STICK_CENTER;
    
    // Touchpad-as-R3
    #define DS_OFF_TOUCHPAD_BT 34
    
    if (g_touchpad_as_r3 && len >= DS_OFF_TOUCHPAD_BT + 4) {
        const uint8_t* touch = &buf[DS_OFF_TOUCHPAD_BT];
        uint8_t contact = touch[0];
        int touch_active = !(contact & DS_TOUCH_INACTIVE);
        
        if (touch_active) {
            int touch_x = touch[1] | ((touch[2] & 0x0F) << 8);
            int touch_y = (touch[2] >> 4) | (touch[3] << 4);
            
            if (touch_x <= DS_TOUCHPAD_WIDTH && touch_y <= DS_TOUCHPAD_HEIGHT) {
                pthread_mutex_lock(&g_touchpad_mutex);
                if (!g_touchpad_state.active) {
                    g_touchpad_state.active = 1;
                    g_touchpad_state.initial_x = touch_x;
                    g_touchpad_state.initial_y = touch_y;
                }
                g_touchpad_state.current_x = touch_x;
                g_touchpad_state.current_y = touch_y;
                
                int delta_x = touch_x - g_touchpad_state.initial_x;
                int delta_y = touch_y - g_touchpad_state.initial_y;
                pthread_mutex_unlock(&g_touchpad_mutex);
                
                int sensitivity = 400;
                int stick_x = 128 + (delta_x * 127) / sensitivity;
                int stick_y = 128 + (delta_y * 127) / sensitivity;
                
                if (stick_x < 0) stick_x = 0;
                else if (stick_x > 255) stick_x = 255;
                if (stick_y < 0) stick_y = 0;
                else if (stick_y > 255) stick_y = 255;
                
                rx = (uint8_t)stick_x;
                ry = (uint8_t)stick_y;
            }
        } else {
            pthread_mutex_lock(&g_touchpad_mutex);
            if (g_touchpad_state.active) g_touchpad_state.active = 0;
            pthread_mutex_unlock(&g_touchpad_mutex);
        }
    }
    
    // Convert to DS3 format
    uint8_t ds3_btn1 = ds3_convert_dpad(buttons1 & 0x0F);
    uint8_t ds3_btn2 = 0;
    uint8_t ds3_ps = 0;
    
    if (buttons1 & DS_BTN1_SQUARE)   ds3_btn2 |= DS3_BTN_SQUARE;
    if (buttons1 & DS_BTN1_CROSS)    ds3_btn2 |= DS3_BTN_CROSS;
    if (buttons1 & DS_BTN1_CIRCLE)   ds3_btn2 |= DS3_BTN_CIRCLE;
    if (buttons1 & DS_BTN1_TRIANGLE) ds3_btn2 |= DS3_BTN_TRIANGLE;
    
    if (buttons2 & DS_BTN2_L1) ds3_btn2 |= DS3_BTN_L1;
    if (buttons2 & DS_BTN2_R1) ds3_btn2 |= DS3_BTN_R1;
    if (buttons2 & DS_BTN2_L2) ds3_btn2 |= DS3_BTN_L2;
    if (buttons2 & DS_BTN2_R2) ds3_btn2 |= DS3_BTN_R2;
    
    if (buttons2 & DS_BTN2_L3) ds3_btn1 |= DS3_BTN_L3;
    if (buttons2 & DS_BTN2_R3) ds3_btn1 |= DS3_BTN_R3;
    if (buttons2 & DS_BTN2_OPTIONS) ds3_btn1 |= DS3_BTN_START;
    if (buttons2 & DS_BTN2_CREATE)  ds3_btn1 |= DS3_BTN_SELECT;
    
    if (buttons3 & DS_BTN3_PS) ds3_ps = DS3_BTN_PS;
    if (!g_touchpad_as_r3 && (buttons3 & DS_BTN3_TOUCHPAD)) ds3_btn1 |= DS3_BTN_SELECT;
    
    uint8_t triangle_p = (buttons1 & DS_BTN1_TRIANGLE) ? 0xFF : 0;
    uint8_t circle_p   = (buttons1 & DS_BTN1_CIRCLE)   ? 0xFF : 0;
    uint8_t cross_p    = (buttons1 & DS_BTN1_CROSS)    ? 0xFF : 0;
    uint8_t square_p   = (buttons1 & DS_BTN1_SQUARE)   ? 0xFF : 0;
    
    ds3_update_report(ds3_btn1, ds3_btn2, ds3_ps,
                      lx, ly, rx, ry, l2, r2,
                      triangle_p, circle_p, cross_p, square_p);
    
    // Motion data
    if (len >= 28) {
        int16_t ds_gyro_z  = (int16_t)(buf[DS_OFF_GYRO_Z] | (buf[DS_OFF_GYRO_Z + 1] << 8));
        int16_t ds_accel_x = (int16_t)(buf[DS_OFF_ACCEL_X] | (buf[DS_OFF_ACCEL_X + 1] << 8));
        int16_t ds_accel_y = (int16_t)(buf[DS_OFF_ACCEL_Y] | (buf[DS_OFF_ACCEL_Y + 1] << 8));
        int16_t ds_accel_z = (int16_t)(buf[DS_OFF_ACCEL_Z] | (buf[DS_OFF_ACCEL_Z + 1] << 8));
        
        int16_t ds3_accel_x = 512 + (ds_accel_x / 16);
        int16_t ds3_accel_y = 512 + (ds_accel_y / 16);
        int16_t ds3_accel_z = 512 + (ds_accel_z / 16);
        int16_t ds3_gyro_z  = 498 + (ds_gyro_z / 32);
        
        ds3_update_motion(ds3_accel_x, ds3_accel_y, ds3_accel_z, ds3_gyro_z);
    }
    
    // Battery
    if (len >= 55) {
        uint8_t battery_byte = buf[DS_OFF_BATTERY];
        uint8_t battery_level = (battery_byte & 0x0F) * 10;
        int is_charging = (battery_byte & 0x10) ? 1 : 0;
        if (battery_level > 100) battery_level = 100;
        
        ds3_update_battery_from_dualsense(battery_level, is_charging);
        
        uint8_t bt_battery;
        if (is_charging) {
            bt_battery = (battery_level >= 100) ? 0xEF : 0xEE;
        } else if (battery_level <= 5) {
            bt_battery = 0x00;
        } else if (battery_level <= 15) {
            bt_battery = 0x01;
        } else if (battery_level <= 35) {
            bt_battery = 0x02;
        } else if (battery_level <= 60) {
            bt_battery = 0x03;
        } else if (battery_level <= 85) {
            bt_battery = 0x04;
        } else {
            bt_battery = 0x05;
        }
        
        pthread_mutex_lock(&g_rumble_mutex);
        int rumble_active = (g_rumble_right > 0 || g_rumble_left > 0);
        pthread_mutex_unlock(&g_rumble_mutex);
        
        bt_status_update(0x03, bt_battery, rumble_active ? 0x14 : 0x16);
    }
    
    return 0;
}

int dualsense_is_connected(void) {
    return g_hidraw_fd >= 0;
}

void dualsense_shutdown(void) {
    printf("[DualSense] Shutting down...\n");
    
    // Turn off LEDs via sysfs
    set_lightbar_sysfs(0, 0, 0);
    set_player_leds_sysfs(0);
    
    if (g_hidraw_fd >= 0) {
        // Stop rumble
        uint8_t report[DS_OUTPUT_REPORT_BT_SIZE] = {0};
        report[0] = 0x31;
        report[2] = 0x10;
        report[3] = DS_OUTPUT_VALID0_RUMBLE | DS_OUTPUT_VALID0_HAPTICS;
        
        uint8_t crc_buf[75] = {0xA2};
        memcpy(&crc_buf[1], report, 74);
        uint32_t crc = dualsense_calc_crc32(crc_buf, 75);
        report[74] = crc & 0xFF;
        report[75] = (crc >> 8) & 0xFF;
        report[76] = (crc >> 16) & 0xFF;
        report[77] = (crc >> 24) & 0xFF;
        
        write(g_hidraw_fd, report, sizeof(report));
        
        controller_unregister_output();
        close(g_hidraw_fd);
        g_hidraw_fd = -1;
    }
    
    // Reset LED cache
    g_last_led_r = 255;
    g_last_led_g = 255;
    g_last_led_b = 255;
    g_last_player_leds = 0xFF;
    
    printf("[DualSense] Shutdown complete\n");
}

void* dualsense_thread(void* arg) {
    (void)arg;
    
    printf("[DualSense] Input thread started\n");
    
    while (g_running) {
        while (g_running && g_hidraw_fd < 0) {
            g_hidraw_fd = dualsense_find_hidraw();
            if (g_hidraw_fd < 0) {
                sleep(1);
            } else {
                printf("[DualSense] Controller connected!\n");
                controller_register_output(dualsense_output_callback);
                
                // Set initial LED color
                set_lightbar_sysfs(255, 0, 0);  // Red
                g_last_led_r = 255;
                g_last_led_g = 0;
                g_last_led_b = 0;
            }
        }
        
        if (g_hidraw_fd < 0) break;
        
        uint8_t buf[DS_BT_INPUT_SIZE];
        ssize_t n = read(g_hidraw_fd, buf, sizeof(buf));
        
        if (n < 10) {
            if (errno == EAGAIN) {
                usleep(1000);
                continue;
            }
            
            printf("[DualSense] Disconnected\n");
            controller_unregister_output();
            close(g_hidraw_fd);
            g_hidraw_fd = -1;
            
            // Reset LED cache and path (device might get new input number on reconnect)
            g_last_led_r = 255;
            g_last_led_g = 255;
            g_last_led_b = 255;
            g_last_player_leds = 0xFF;
            g_lightbar_path[0] = '\0';
            continue;
        }
        
        if (system_is_standby()) {
            if (dualsense_check_standby_wake(buf, n)) {
                printf("[DualSense] PS button - waking PS3!\n");
                g_controller_wake_requested = 1;
                system_exit_standby();
            }
            continue;
        }
        
        dualsense_process_input(buf, n);
    }
    
    controller_unregister_output();
    return NULL;
}