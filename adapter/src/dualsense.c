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
#include <time.h>
#include <linux/hidraw.h>
#include <sys/ioctl.h>
#include <poll.h>

#include "common.h"
#include "ds3.h"
#include "dualsense.h"
#include "debug.h"

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
    debug_print(DBG_INIT | DBG_DUALSENSE, "[DualSense] Controller interface initialized");
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
        if (fd < 0) {
            debug_print(DBG_DUALSENSE | DBG_VERBOSE, "[DualSense] Cannot open %s: %s", path, strerror(errno));
            continue;
        }
        
        struct hidraw_devinfo info;
        if (ioctl(fd, HIDIOCGRAWINFO, &info) < 0) {
            close(fd);
            continue;
        }
        
        debug_print(DBG_DUALSENSE | DBG_VERBOSE, "[DualSense] %s: VID=%04x PID=%04x", 
                    path, info.vendor, info.product);
        
        if (info.vendor == DUALSENSE_VID && info.product == DUALSENSE_PID) {
            char name[256] = "";
            ioctl(fd, HIDIOCGRAWNAME(sizeof(name)), name);
            debug_print(DBG_DUALSENSE | DBG_INFO, "[DualSense] Found: %s (%s)", name, path);
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
//     [46]  led_brightness
//     [47]  player_leds
//     [48]  lightbar_red
//     [49]  lightbar_green
//     [50]  lightbar_blue
//   [50-73] reserved[24]
//   [74-77] crc32 (little-endian)

// Valid flag 0 bits
#define DS_OUTPUT_VALID0_RUMBLE             0x01
#define DS_OUTPUT_VALID0_HAPTICS            0x02
#define DS_OUTPUT_VALID0_RIGHT_TRIGGER      0x04
#define DS_OUTPUT_VALID0_LEFT_TRIGGER       0x08
#define DS_OUTPUT_VALID0_HEADPHONE_VOLUME   0x10
#define DS_OUTPUT_VALID0_SPEAKER_VOLUME     0x20
#define DS_OUTPUT_VALID0_MIC_VOLUME         0x40
#define DS_OUTPUT_VALID0_AUDIO_CONTROL      0x80

// Valid flag 1 bits
#define DS_OUTPUT_VALID1_MIC_MUTE_LED       0x01
#define DS_OUTPUT_VALID1_POWER_SAVE         0x02
#define DS_OUTPUT_VALID1_LIGHTBAR           0x04
#define DS_OUTPUT_VALID1_RELEASE_LED        0x08
#define DS_OUTPUT_VALID1_PLAYER_INDICATOR   0x10
#define DS_OUTPUT_VALID1_UNKNOWN_1          0x20
#define DS_OUTPUT_VALID1_FIRMWARE_CONTROL   0x40
#define DS_OUTPUT_VALID1_UNKNOWN_2          0x80

// Valid flag 2 bits
#define DS_OUTPUT_VALID2_LIGHTBAR_SETUP     0x02

// Byte offsets in full BT output report
#define DS_OUT_HID_DATA             0
#define DS_OUT_REPORT_ID            1
#define DS_OUT_SEQ_TAG              2
#define DS_OUT_TAG                  3
#define DS_OUT_VALID_FLAG0          4
#define DS_OUT_VALID_FLAG1          5
#define DS_OUT_MOTOR_RIGHT          6
#define DS_OUT_MOTOR_LEFT           7
#define DS_OUT_MIC_MUTE_LED         12
#define DS_OUT_ADAPTIVE_TRIG_R      14
#define DS_OUT_ADAPTIVE_TRIG_L      25
#define DS_OUT_VALID_FLAG2          42
#define DS_OUT_LIGHTBAR_PULSE       45
#define DS_OUT_LIGHTBAR_BRIGHTNESS  46
#define DS_OUT_PLAYER_LEDS          47
#define DS_OUT_LIGHTBAR_RED         48
#define DS_OUT_LIGHTBAR_GREEN       49
#define DS_OUT_LIGHTBAR_BLUE        50

// Report size
#define DS_OUTPUT_REPORT_BT_SIZE 79

// Sequence counter for BT reports
static uint8_t output_seq = 0;

void dualsense_send_output(int fd,
    uint8_t right_motor, uint8_t left_motor,
    uint8_t led_r, uint8_t led_g, uint8_t led_b,
    uint8_t player_leds)
{
    uint8_t report[DS_OUTPUT_REPORT_BT_SIZE] = {0};
    
    // Header
    report[DS_OUT_HID_DATA] = 0xA2;  // HID BT DATA (0xA0) | Report Type (Output 0x02)
    report[DS_OUT_REPORT_ID] = 0x31;
    report[DS_OUT_SEQ_TAG] = (output_seq++ << 4) | 0x00;
    report[DS_OUT_TAG] = 0x10;  // Magic tag value
    
    // Valid flags
    report[DS_OUT_VALID_FLAG0] = DS_OUTPUT_VALID0_RUMBLE | DS_OUTPUT_VALID0_HAPTICS | DS_OUTPUT_VALID0_RIGHT_TRIGGER | DS_OUTPUT_VALID0_LEFT_TRIGGER | DS_OUTPUT_VALID0_HEADPHONE_VOLUME | DS_OUTPUT_VALID0_SPEAKER_VOLUME | DS_OUTPUT_VALID0_MIC_VOLUME | DS_OUTPUT_VALID0_AUDIO_CONTROL;
    report[DS_OUT_VALID_FLAG1] = DS_OUTPUT_VALID1_MIC_MUTE_LED | DS_OUTPUT_VALID1_LIGHTBAR | DS_OUTPUT_VALID1_RELEASE_LED | DS_OUTPUT_VALID1_PLAYER_INDICATOR | DS_OUTPUT_VALID1_UNKNOWN_1 | DS_OUTPUT_VALID1_FIRMWARE_CONTROL | DS_OUTPUT_VALID1_UNKNOWN_2;
    report[DS_OUT_VALID_FLAG2] = DS_OUTPUT_VALID2_LIGHTBAR_SETUP;

    // Rumble motors
    report[DS_OUT_MOTOR_RIGHT] = right_motor;
    report[DS_OUT_MOTOR_LEFT] = left_motor;

    report[DS_OUT_MIC_MUTE_LED] = 0x01;

    // Adaptive triggers
    // Source: https://github.com/felis/USB_Host_Shield_2.0/blob/master/PS5Trigger.h
    // Offset   | Name      | Description
    // -----------------------------------------------------------
    // 0        | mode      | Effect type
    // 1-10     | params    | Mode specific parameters
    //
    // Value    | Mode      | Description 
    // -----------------------------------------------------------
    // 0x00     | Off       | No effect
    // 0x01     | Rigid     | Constant resistance
    // 0x02     | Pulse     | Vibrating/Pulsing effect
    // 0x21     | Rigid A   | Resistance in a region
    // 0x22     | Rigid B   | Resistance with strength
    // 0x23     | Rigid AB  | Resistance with strength and region
    // 0x25     | Pulse A   | Pulse in a region
    // 0x26     | Pulse B   | Pulse with strength
    // 0x27     | Pulse AB  | Pulse with strength and region

    // report[DS_OUT_ADAPTIVE_TRIG_R] = 0x00;
    // report[DS_OUT_ADAPTIVE_TRIG_L] = 0x00
    
    // Lightbar control
    report[DS_OUT_LIGHTBAR_PULSE] = 0x02;           // 0x00 = No pulse, 0x01 = pulse, 0x02 = fade in
    report[DS_OUT_LIGHTBAR_BRIGHTNESS] = 0x00;      // 0 = full, 1= medium, 2 = low
    report[DS_OUT_PLAYER_LEDS] = player_leds;
    report[DS_OUT_LIGHTBAR_RED] = led_r;
    report[DS_OUT_LIGHTBAR_GREEN] = led_g;
    report[DS_OUT_LIGHTBAR_BLUE] = led_b;
    
    // Calculate CRC32
    // For BT output reports, CRC is calculated over: 0xA2 seed byte + report[0..73]
    uint32_t crc = dualsense_calc_crc32(report, 75);
    
    // Store CRC at end (little-endian)
    report[75] = (crc >> 0) & 0xFF;
    report[76] = (crc >> 8) & 0xFF;
    report[77] = (crc >> 16) & 0xFF;
    report[78] = (crc >> 24) & 0xFF;
    
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
    // Determine report format based on report ID
    uint8_t report_id = buf[0];
    int is_usb_format = 0;
    int is_bt_format = 0;
    
    // USB format: Report ID 0x01, minimum ~10 bytes for basic controls
    if (report_id == DS_USB_REPORT_ID && len >= 10) {
        is_usb_format = 1;
    }
    // BT format: Report ID 0x31, typically 78 bytes
    else if (report_id == DS_BT_REPORT_ID && len >= 12) {
        is_bt_format = 1;
    }
    else {
        // Unknown format
        static int unknown_report_count = 0;
        if (++unknown_report_count <= 5) {
            debug_print(DBG_WARN | DBG_DUALSENSE, 
                        "[DualSense] Unknown report: id=0x%02X len=%zu", report_id, len);
        }
        return -1;
    }
    
    // Extract raw values based on format
    uint8_t lx, ly, rx, ry, l2, r2;
    uint8_t buttons1, buttons2, buttons3;
    
    if (is_usb_format) {
        // 10-byte USB report format from kernel hid-playstation:
        // [0]=0x01 [1]=LX [2]=LY [3]=RX [4]=RY [5]=BTN1 [6]=L2 [7]=CTR [8]=R2 [9]=BTN2
        lx = buf[1];
        ly = buf[2];
        rx = buf[3];
        ry = buf[4];
        buttons1 = buf[5];  // D-pad (low nibble) + face buttons (high nibble)
        l2 = buf[6];
        r2 = buf[8];
        buttons2 = buf[9];
        buttons3 = 0;  // Not in 10-byte report
        
        // Debug: show when buttons change or periodically
        static uint8_t last_btn1 = 0, last_btn2 = 0;
        static int periodic_counter = 0;
        if (buttons1 != last_btn1 || buttons2 != last_btn2 || ++periodic_counter >= 500) {
            debug_print(DBG_DUALSENSE, "[DualSense] USB: LX=%d LY=%d RX=%d RY=%d L2=%d R2=%d btn1=0x%02X btn2=0x%02X",
                        lx, ly, rx, ry, l2, r2, buttons1, buttons2);
            last_btn1 = buttons1;
            last_btn2 = buttons2;
            periodic_counter = 0;
        }
    }
    else { // BT format
        // BT format offsets (has counter byte at offset 1)
        lx = buf[DS_OFF_LX];
        ly = buf[DS_OFF_LY];
        rx = buf[DS_OFF_RX];
        ry = buf[DS_OFF_RY];
        l2 = buf[DS_OFF_L2];
        r2 = buf[DS_OFF_R2];
        buttons1 = buf[DS_OFF_BUTTONS1];
        buttons2 = buf[DS_OFF_BUTTONS2];
        buttons3 = buf[DS_OFF_BUTTONS3];
    }
    
    // Process touchpad-as-R3 if enabled and touchpad data is available
    // Touchpad data is at offset 34 in BT report (verified via debug comparison)
    // For USB format, touchpad is at offset 33
    int touchpad_offset = is_bt_format ? 34 : 33;
    
    if (g_touchpad_as_r3 && len >= (size_t)(touchpad_offset + 4)) {
        const uint8_t* touch = &buf[touchpad_offset];
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
    // USB format: motion at offset 15, BT format: motion at offset 16
    // Require at least 27 bytes for USB or 28 bytes for BT
    int motion_offset = is_usb_format ? 15 : 16;
    if ((is_usb_format && len >= 27) || (is_bt_format && len >= 28)) {
        // DualSense gyro/accel are 16-bit signed little-endian
        int16_t ds_gyro_x  = (int16_t)(buf[motion_offset + 0] | (buf[motion_offset + 1] << 8));
        int16_t ds_gyro_y  = (int16_t)(buf[motion_offset + 2] | (buf[motion_offset + 3] << 8));
        int16_t ds_gyro_z  = (int16_t)(buf[motion_offset + 4] | (buf[motion_offset + 5] << 8));
        int16_t ds_accel_x = (int16_t)(buf[motion_offset + 6] | (buf[motion_offset + 7] << 8));
        int16_t ds_accel_y = (int16_t)(buf[motion_offset + 8] | (buf[motion_offset + 9] << 8));
        int16_t ds_accel_z = (int16_t)(buf[motion_offset + 10] | (buf[motion_offset + 11] << 8));
        
        // Convert to DS3 format (centered around ~512 for accel, ~498 for gyro)
        int16_t ds3_accel_x = 512 + (ds_accel_x / 16);
        int16_t ds3_accel_y = 512 + (ds_accel_y / 16);
        int16_t ds3_accel_z = 512 + (ds_accel_z / 16);
        int16_t ds3_gyro_z  = 498 + (ds_gyro_z / 32);  // DS3 only has Z gyro
        
        // Suppress unused variable warnings
        (void)ds_gyro_x;
        (void)ds_gyro_y;
        
        ds3_update_motion(ds3_accel_x, ds3_accel_y, ds3_accel_z, ds3_gyro_z);
    }
    
    // Process battery status if available
    // USB format: battery at offset 52, BT format: battery at offset 54
    int battery_offset = is_usb_format ? 52 : 54;
    if (len > (size_t)(battery_offset)) {
        // DualSense battery byte format:
        // Bits 0-3: Battery level (0-10, multiply by 10 for percentage)
        // Bit 4: Charging status (1 = charging)
        // Bits 5-7: Power state
        uint8_t battery_byte = buf[battery_offset];
        uint8_t battery_level = (battery_byte & 0x0F) * 10;  // 0-100%
        int is_charging = (battery_byte & 0x10) ? 1 : 0;
        
        // Cap at 100%
        if (battery_level > 100) battery_level = 100;
        
        // Debug: print battery info periodically
        static int battery_debug_counter = 0;
        debug_periodic(DBG_DUALSENSE | DBG_PERIODIC, &battery_debug_counter,
                       "[DualSense] Battery: raw=0x%02X level=%d%% charging=%d",
                       battery_byte, battery_level, is_charging);
        
        ds3_update_battery_from_dualsense(battery_level, is_charging);
    }
    
    return 0;
}

// =================================================================
// Thread Functions
// =================================================================

void* dualsense_thread(void* arg) {
    (void)arg;
    
    debug_print(DBG_INIT | DBG_DUALSENSE, "[DualSense] Input thread started, waiting for controller...");
    
    // Wait for controller connection
    while (g_running && g_hidraw_fd < 0) {
        g_hidraw_fd = dualsense_find_hidraw();
        if (g_hidraw_fd < 0) sleep(1);
    }
    
    if (g_hidraw_fd < 0) return NULL;
    debug_print(DBG_DUALSENSE, "[DualSense] Controller connected!");
    
    // Set non-blocking to allow buffer draining
    int flags = fcntl(g_hidraw_fd, F_GETFL, 0);
    fcntl(g_hidraw_fd, F_SETFL, flags | O_NONBLOCK);
    
    uint8_t buf[DS_BT_INPUT_SIZE];
    static int read_count = 0;
    static int process_count = 0;
    static struct timespec last_button_change = {0};
    static uint8_t last_buttons1 = 0, last_buttons2 = 0, last_buttons3 = 0;
    
    while (g_running) {
        // Use poll to wait for data (avoids spinning on EAGAIN)
        struct pollfd pfd = { .fd = g_hidraw_fd, .events = POLLIN };
        int ret = poll(&pfd, 1, 20);  // 20ms timeout for faster response
        
        if (ret < 0) {
            if (errno == EINTR) continue;
            debug_print(DBG_WARN | DBG_DUALSENSE, "[DualSense] poll error: %s", strerror(errno));
            continue;
        }
        
        if (ret == 0) continue;  // Timeout, no data
        
        // Check for errors/hangup
        if (pfd.revents & (POLLERR | POLLHUP)) {
            debug_print(DBG_WARN | DBG_DUALSENSE, "[DualSense] Disconnected, reconnecting...");
            close(g_hidraw_fd);
            g_hidraw_fd = -1;
            
            while (g_running && g_hidraw_fd < 0) {
                g_hidraw_fd = dualsense_find_hidraw();
                if (g_hidraw_fd < 0) sleep(1);
            }
            
            if (g_hidraw_fd >= 0) {
                debug_print(DBG_DUALSENSE, "[DualSense] Reconnected!");
                flags = fcntl(g_hidraw_fd, F_GETFL, 0);
                fcntl(g_hidraw_fd, F_SETFL, flags | O_NONBLOCK);
            }
            continue;
        }
        
        // Drain buffer - read all available reports, keep only the last one
        ssize_t n = 0;
        int drained = 0;
        uint8_t temp[DS_BT_INPUT_SIZE];
        
        while ((n = read(g_hidraw_fd, temp, sizeof(temp))) > 0) {
            memcpy(buf, temp, n);  // Keep latest report
            drained++;
        }
        
        if (drained == 0) {
            continue;  // No data despite poll saying ready
        }
        
        read_count += drained;
        
        // Log if we're draining a lot (indicates lag)
        static int total_drained = 0;
        total_drained += drained;
        if (total_drained % 100 < drained) {
            debug_print(DBG_DUALSENSE, "[DualSense] Drain stats: this_cycle=%d total=%d", drained, total_drained);
        }
        
        // Show raw input periodically
        if (read_count % 100 == 1) {
            if (buf[0] == DS_USB_REPORT_ID) {
                debug_print(DBG_DUALSENSE, "[DualSense] USB input: LX=%d LY=%d RX=%d RY=%d btn=0x%02X/0x%02X",
                            buf[1], buf[2], buf[3], buf[4], buf[5], buf[9]);
            } else {
                debug_print(DBG_DUALSENSE, "[DualSense] BT input: LX=%d LY=%d RX=%d RY=%d btn=0x%02X/0x%02X/0x%02X",
                            buf[2], buf[3], buf[4], buf[5], buf[9], buf[10], buf[11]);
            }
        }
        
        // Track button changes with timestamps for latency debugging
        uint8_t cur_btn1, cur_btn2, cur_btn3;
        if (buf[0] == DS_USB_REPORT_ID) {
            cur_btn1 = buf[5];
            cur_btn2 = buf[9];
            cur_btn3 = 0;
        } else {
            cur_btn1 = buf[9];
            cur_btn2 = buf[10];
            cur_btn3 = buf[11];
        }
        
        if (cur_btn1 != last_buttons1 || cur_btn2 != last_buttons2 || cur_btn3 != last_buttons3) {
            struct timespec now;
            clock_gettime(CLOCK_MONOTONIC, &now);
            
            // Calculate time since last button change
            long diff_ms = 0;
            if (last_button_change.tv_sec != 0) {
                diff_ms = (now.tv_sec - last_button_change.tv_sec) * 1000 + 
                          (now.tv_nsec - last_button_change.tv_nsec) / 1000000;
            }
            
            debug_print(DBG_INPUT, "[DualSense] Button change: %02X/%02X/%02X -> %02X/%02X/%02X (drained=%d, since_last=%ldms)",
                        last_buttons1, last_buttons2, last_buttons3,
                        cur_btn1, cur_btn2, cur_btn3, drained, diff_ms);
            
            last_buttons1 = cur_btn1;
            last_buttons2 = cur_btn2;
            last_buttons3 = cur_btn3;
            last_button_change = now;
        }
        
        // Process the last report we read
        int result = dualsense_process_input(buf, sizeof(buf));
        if (result == 0) {
            process_count++;
            if (process_count % 500 == 1) {
                debug_print(DBG_DUALSENSE, "[DualSense] Processed %d reports", process_count);
            }
        } else {
            debug_print(DBG_WARN | DBG_DUALSENSE, "[DualSense] process_input failed (report_id=0x%02X)", buf[0]);
        }
    }
    
    return NULL;
}

// =================================================================
// DualSense Output Thread (rumble/LED updates)
// =================================================================

void* dualsense_output_thread(void* arg) {
    (void)arg;
    
    debug_print(DBG_INIT | DBG_DUALSENSE, "[DualSense] Output thread started");
    
    uint8_t last_right = 0, last_left = 0;
    lightbar_state_t last_state = {0};
    
    while (g_running) {
        // Check if we have a DualSense connected
        if (g_hidraw_fd < 0) {
            usleep(100000);  // 100ms
            continue;
        }
        
        // Get current rumble state
        uint8_t right, left;
        pthread_mutex_lock(&g_rumble_mutex);
        right = g_rumble_right;
        left = g_rumble_left;
        pthread_mutex_unlock(&g_rumble_mutex);
        
        // Get current lightbar state
        lightbar_state_t state;
        pthread_mutex_lock(&g_lightbar_mutex);
        state = g_lightbar_state;
        pthread_mutex_unlock(&g_lightbar_mutex);
        
        // Only send if something changed or we need to keepalive
        static int keepalive_counter = 0;
        int changed = (right != last_right) || (left != last_left) ||
                      (state.r != last_state.r) || (state.g != last_state.g) ||
                      (state.b != last_state.b) || (state.player_leds != last_state.player_leds);
        
        if (changed || ++keepalive_counter >= 100) {
            dualsense_send_output(g_hidraw_fd, right, left,
                                  state.r, state.g, state.b, state.player_leds);
            
            last_right = right;
            last_left = left;
            last_state = state;
            keepalive_counter = 0;
        }
        
        usleep(10000);  // 10ms = ~100Hz
    }
    
    debug_print(DBG_DUALSENSE, "[DualSense] Output thread exiting");
    return NULL;
}