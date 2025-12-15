/*
 * DualSense to PS3 Adapter with Lightbar Support
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <fcntl.h>
#include <unistd.h>
#include <errno.h>
#include <stdint.h>
#include <pthread.h>
#include <dirent.h>
#include <linux/hidraw.h>
#include <linux/usb/functionfs.h>
#include <linux/usb/ch9.h>
#include <sys/ioctl.h>
#include <sys/stat.h>

// File descriptor globals
static int ep0_fd = -1;
static int ep1_fd = -1;
static int ep2_fd = -1;
static int hidraw_fd = -1;

static volatile int running = 1;
static volatile int usb_enabled = 0;

// DS3 input report - 49 bytes
static uint8_t ds3_report[49] = {
    0x01, 0x00, 0x00, 0x00, 0x00, 0x00, 0x80, 0x80,
    0x80, 0x80, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
    0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
    0x00, 0x00, 0x00, 0x00, 0x00, 0x02, 0xee, 0x12,
    0x00, 0x00, 0x00, 0x00, 0x12, 0x04, 0x77, 0x01,
    0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x02
};
static pthread_mutex_t report_mutex = PTHREAD_MUTEX_INITIALIZER;

// Rumble state
static uint8_t rumble_right = 0;
static uint8_t rumble_left = 0;
static pthread_mutex_t rumble_mutex = PTHREAD_MUTEX_INITIALIZER;

// =================================================================
// Lightbar State
// =================================================================
typedef struct {
    uint8_t r;
    uint8_t g;
    uint8_t b;
    uint8_t player_leds;      // Bitmask: 0x01=LED1, 0x02=LED2, etc.
    uint8_t player_brightness; // 0-255
} lightbar_state_t;

static lightbar_state_t lightbar_state = {0, 0, 255, 0, 255}; // Default: blue
static pthread_mutex_t lightbar_mutex = PTHREAD_MUTEX_INITIALIZER;
static const char* LIGHTBAR_IPC_PATH = "/tmp/rosettapad/lightbar_state.json";

// DS3 Feature reports
static uint8_t report_01[64] = {
    0x00, 0x01, 0x04, 0x00, 0x08, 0x0c, 0x01, 0x02, 0x18, 0x18, 0x18, 0x18, 0x09, 0x0a, 0x10, 0x11,
    0x12, 0x13, 0x00, 0x00, 0x00, 0x00, 0x04, 0x00, 0x02, 0x02, 0x02, 0x02, 0x00, 0x00, 0x00, 0x04,
    0x04, 0x04, 0x04, 0x00, 0x00, 0x04, 0x00, 0x01, 0x02, 0x07, 0x00, 0x17, 0x00, 0x00, 0x00, 0x00,
    0x00, 0x00, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff
};
static uint8_t report_f2[64] = {
    0xf2, 0xff, 0xff, 0x00, 0x34, 0xc7, 0x31, 0x25, 0xae, 0x60, 0x00, 0x03, 0x50, 0x81, 0xd8, 0x01,
    0x8a, 0x13, 0x00, 0x00, 0x00, 0x00, 0x04, 0x00, 0x02, 0x02, 0x02, 0x02, 0x00, 0x00, 0x00, 0x04,
    0x04, 0x04, 0x04, 0x00, 0x00, 0x04, 0x00, 0x01, 0x02, 0x07, 0x00, 0x17, 0x00, 0x00, 0x00, 0x00,
    0x00, 0x00, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff
};
static uint8_t report_f5[64] = {
    0x01, 0x00, 0x38, 0x4f, 0xf0, 0x10, 0x02, 0x41, 0xae, 0x60, 0x00, 0x03, 0x50, 0x81, 0xd8, 0x01,
    0x8a, 0x13, 0x00, 0x00, 0x00, 0x00, 0x04, 0x00, 0x02, 0x02, 0x02, 0x02, 0x00, 0x00, 0x00, 0x04,
    0x04, 0x04, 0x04, 0x00, 0x00, 0x04, 0x00, 0x01, 0x02, 0x07, 0x00, 0x17, 0x00, 0x00, 0x00, 0x00,
    0x00, 0x00, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff
};
static uint8_t report_f7[64] = {
    0x01, 0x00, 0xfd, 0x02, 0xf3, 0x01, 0xee, 0xff, 0x10, 0x12, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
    0x00, 0x02, 0x6c, 0x02, 0x6f, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
    0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
    0x05, 0x00, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff
};
static uint8_t report_f8[64] = {
    0x02, 0x01, 0x00, 0x00, 0x08, 0x03, 0x01, 0xb0, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
    0x00, 0x02, 0x6c, 0x02, 0x6f, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
    0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
    0x05, 0x00, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff
};
static uint8_t report_ef[64] = {0};

// USB descriptors (unchanged)
static const struct {
    struct usb_functionfs_descs_head_v2 header;
    __le32 fs_count;
    __le32 hs_count;
    struct {
        struct usb_interface_descriptor intf;
        struct usb_endpoint_descriptor_no_audio ep_in;
        struct usb_endpoint_descriptor_no_audio ep_out;
    } __attribute__((packed)) fs_descs, hs_descs;
} __attribute__((packed)) descriptors = {
    .header = {
        .magic = FUNCTIONFS_DESCRIPTORS_MAGIC_V2,
        .flags = FUNCTIONFS_HAS_FS_DESC | FUNCTIONFS_HAS_HS_DESC,
        .length = sizeof(descriptors),
    },
    .fs_count = 3, .hs_count = 3,
    .fs_descs = {
        .intf = { .bLength = 9, .bDescriptorType = 4, .bNumEndpoints = 2, .bInterfaceClass = 3 },
        .ep_in = { .bLength = 7, .bDescriptorType = 5, .bEndpointAddress = 0x81, .bmAttributes = 3, .wMaxPacketSize = 64, .bInterval = 1 },
        .ep_out = { .bLength = 7, .bDescriptorType = 5, .bEndpointAddress = 0x02, .bmAttributes = 3, .wMaxPacketSize = 64, .bInterval = 1 },
    },
    .hs_descs = {
        .intf = { .bLength = 9, .bDescriptorType = 4, .bNumEndpoints = 2, .bInterfaceClass = 3 },
        .ep_in = { .bLength = 7, .bDescriptorType = 5, .bEndpointAddress = 0x81, .bmAttributes = 3, .wMaxPacketSize = 64, .bInterval = 1 },
        .ep_out = { .bLength = 7, .bDescriptorType = 5, .bEndpointAddress = 0x02, .bmAttributes = 3, .wMaxPacketSize = 64, .bInterval = 1 },
    },
};

static const struct {
    struct usb_functionfs_strings_head header;
    struct { __le16 code; const char str1[10]; } __attribute__((packed)) lang0;
} __attribute__((packed)) strings = {
    .header = { .magic = FUNCTIONFS_STRINGS_MAGIC, .length = sizeof(strings), .str_count = 1, .lang_count = 1 },
    .lang0 = { .code = 0x0409, .str1 = "DS3 Input" },
};

// CRC32 for DualSense BT output
static uint32_t crc32_table[256];
static int crc32_initialized = 0;

void init_crc32_table(void) {
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

uint32_t calc_crc32(const uint8_t *data, size_t len) {
    uint32_t crc = 0xFFFFFFFF;
    for (size_t i = 0; i < len; i++) {
        crc = (crc >> 8) ^ crc32_table[(crc ^ data[i]) & 0xFF];
    }
    return ~crc;
}

// =================================================================
// Lightbar IPC Reader
// =================================================================

// Simple JSON parser for our specific format
// Expects: {"r":255,"g":0,"b":128,"player_leds":0,"player_led_brightness":1.0}
int parse_lightbar_json(const char* json, lightbar_state_t* state) {
    const char* ptr;
    
    // Parse "r"
    ptr = strstr(json, "\"r\":");
    if (ptr) state->r = (uint8_t)atoi(ptr + 4);
    
    // Parse "g"
    ptr = strstr(json, "\"g\":");
    if (ptr) state->g = (uint8_t)atoi(ptr + 4);
    
    // Parse "b"
    ptr = strstr(json, "\"b\":");
    if (ptr) state->b = (uint8_t)atoi(ptr + 4);
    
    // Parse "player_leds"
    ptr = strstr(json, "\"player_leds\":");
    if (ptr) state->player_leds = (uint8_t)atoi(ptr + 14);
    
    // Parse "player_led_brightness" (float 0.0-1.0, convert to 0-255)
    ptr = strstr(json, "\"player_led_brightness\":");
    if (ptr) {
        float brightness = atof(ptr + 24);
        state->player_brightness = (uint8_t)(brightness * 255);
    }
    
    return 0;
}

void read_lightbar_state(void) {
    FILE* f = fopen(LIGHTBAR_IPC_PATH, "r");
    if (!f) return;
    
    char buf[256];
    if (fgets(buf, sizeof(buf), f)) {
        lightbar_state_t new_state;
        if (parse_lightbar_json(buf, &new_state) == 0) {
            pthread_mutex_lock(&lightbar_mutex);
            lightbar_state = new_state;
            pthread_mutex_unlock(&lightbar_mutex);
        }
    }
    
    fclose(f);
}

// =================================================================
// DualSense Output - Rumble + Lightbar + Player LEDs
// =================================================================

/*
 * DualSense BT Output Report (0x31) - 78 bytes
 * 
 * Byte offsets (from BT report start):
 * [0]     = 0x31 (Report ID)
 * [1]     = Sequence number (upper nibble) | 0x00
 * [2]     = 0x10 (tag type)
 * [3]     = valid_flag0: 0x01=motors, 0x02=haptic_triggers
 * [4]     = valid_flag1: 0x01=mic_led, 0x02=audio, 0x04=lightbar, 0x08=player_leds
 * [5]     = rumble_right (0-255)
 * [6]     = rumble_left (0-255)
 * [7-8]   = reserved
 * [9]     = mic_led
 * [10]    = power_save_control
 * [11-38] = adaptive trigger data
 * [39-40] = trigger_unknown
 * [41]    = led_flags: 0x01=???, 0x02=lightbar_setup, 0x04=fade_animation
 * [42]    = led_anim_select (used with fade)
 * [43]    = led_brightness (global)
 * [44]    = player_leds (bitmask)
 * [45]    = lightbar_r
 * [46]    = lightbar_g
 * [47]    = lightbar_b
 * [48-73] = reserved
 * [74-77] = CRC32
 */

void send_dualsense_output(int fd, uint8_t right_motor, uint8_t left_motor,
                           uint8_t led_r, uint8_t led_g, uint8_t led_b,
                           uint8_t player_leds) {
    static uint8_t seq = 0;
    
    uint8_t report[78] = {0};
    
    report[0] = 0x31;                    // Report ID
    report[1] = (seq << 4) | 0x00;       // Sequence in upper nibble
    seq = (seq + 1) & 0x0F;
    report[2] = 0x10;                    // Tag type
    
    // Valid flags
    // flag0: 0x01 = motor rumble, 0x02 = haptic triggers
    // flag1: 0x04 = lightbar, 0x08 = player LEDs
    report[3] = 0x01;                    // Motor rumble enabled
    report[4] = 0x04 | 0x08;             // Lightbar + Player LEDs enabled
    
    // Motor values
    report[5] = right_motor;
    report[6] = left_motor;
    
    // Lightbar control
    report[41] = 0x02;                   // LED flags: lightbar setup
    report[43] = 0xFF;                   // Global LED brightness (max)
    report[44] = player_leds;            // Player LED bitmask
    report[45] = led_r;                  // Lightbar R
    report[46] = led_g;                  // Lightbar G
    report[47] = led_b;                  // Lightbar B
    
    // Calculate CRC32
    uint8_t crc_buf[75];
    crc_buf[0] = 0xA2;  // BT output report seed
    memcpy(&crc_buf[1], report, 74);
    
    uint32_t crc = calc_crc32(crc_buf, 75);
    report[74] = (crc >> 0) & 0xFF;
    report[75] = (crc >> 8) & 0xFF;
    report[76] = (crc >> 16) & 0xFF;
    report[77] = (crc >> 24) & 0xFF;
    
    if (fd >= 0) {
        write(fd, report, sizeof(report));
    }
}

// Legacy function for compatibility
void send_dualsense_rumble(int fd, uint8_t right_motor, uint8_t left_motor) {
    pthread_mutex_lock(&lightbar_mutex);
    lightbar_state_t state = lightbar_state;
    pthread_mutex_unlock(&lightbar_mutex);
    
    send_dualsense_output(fd, right_motor, left_motor,
                          state.r, state.g, state.b, state.player_leds);
}

// =================================================================
// Find DualSense hidraw device
// =================================================================
int find_dualsense_hidraw(void) {
    DIR *dir = opendir("/dev");
    if (!dir) return -1;
    
    struct dirent *entry;
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
        
        // Sony DualSense: VID 0x054c, PID 0x0ce6
        if (info.vendor == 0x054c && info.product == 0x0ce6) {
            char name[256] = "";
            ioctl(fd, HIDIOCGRAWNAME(sizeof(name)), name);
            printf("Found DualSense: %s (%s)\n", name, path);
            closedir(dir);
            return fd;
        }
        close(fd);
    }
    closedir(dir);
    return -1;
}

// =================================================================
// D-pad Conversion
// =================================================================
uint8_t convert_dpad(uint8_t ds_dpad) {
    switch (ds_dpad & 0x0F) {
        case 0: return 0x10;                 // Up
        case 1: return 0x10 | 0x20;          // Up-Right
        case 2: return 0x20;                 // Right
        case 3: return 0x40 | 0x20;          // Down-Right
        case 4: return 0x40;                 // Down
        case 5: return 0x40 | 0x80;          // Down-Left
        case 6: return 0x80;                 // Left
        case 7: return 0x10 | 0x80;          // Up-Left
        default: return 0;                   // Neutral
    }
}

// =================================================================
// DualSense Input Thread
// =================================================================
void* dualsense_thread(void* arg) {
    printf("Waiting for DualSense...\n");
    
    while (running && hidraw_fd < 0) {
        hidraw_fd = find_dualsense_hidraw();
        if (hidraw_fd < 0) sleep(1);
    }
    
    if (hidraw_fd < 0) return NULL;
    printf("DualSense connected!\n");
    
    uint8_t buf[78];
    
    while (running) {
        ssize_t n = read(hidraw_fd, buf, sizeof(buf));
        if (n < 10) {
            if (errno == EAGAIN) {
                usleep(1000);
                continue;
            }
            printf("DualSense disconnected, reconnecting...\n");
            close(hidraw_fd);
            hidraw_fd = -1;
            while (running && hidraw_fd < 0) {
                hidraw_fd = find_dualsense_hidraw();
                if (hidraw_fd < 0) sleep(1);
            }
            if (hidraw_fd >= 0) printf("DualSense reconnected!\n");
            continue;
        }
        
        if (buf[0] != 0x31) continue;
        
        uint8_t lx = buf[2];
        uint8_t ly = buf[3];
        uint8_t rx = buf[4];
        uint8_t ry = buf[5];
        uint8_t l2 = buf[6];
        uint8_t r2 = buf[7];
        uint8_t buttons1 = buf[9];
        uint8_t buttons2 = buf[10];
        uint8_t buttons3 = buf[11];
        
        uint8_t ds3_btn1 = 0;
        uint8_t ds3_btn2 = 0;
        uint8_t ds3_ps = 0;
        
        ds3_btn1 |= convert_dpad(buttons1);
        
        if (buttons1 & 0x10) ds3_btn2 |= 0x80;  // Square
        if (buttons1 & 0x20) ds3_btn2 |= 0x40;  // Cross
        if (buttons1 & 0x40) ds3_btn2 |= 0x20;  // Circle
        if (buttons1 & 0x80) ds3_btn2 |= 0x10;  // Triangle
        
        if (buttons2 & 0x01) ds3_btn2 |= 0x04;  // L1
        if (buttons2 & 0x02) ds3_btn2 |= 0x08;  // R1
        if (buttons2 & 0x04) ds3_btn2 |= 0x01;  // L2 button
        if (buttons2 & 0x08) ds3_btn2 |= 0x02;  // R2 button
        
        if (buttons2 & 0x40) ds3_btn1 |= 0x02;  // L3
        if (buttons2 & 0x80) ds3_btn1 |= 0x04;  // R3
        
        if (buttons2 & 0x10) ds3_btn1 |= 0x01;  // Create -> Select
        if (buttons2 & 0x20) ds3_btn1 |= 0x08;  // Options -> Start
        
        if (buttons3 & 0x01) ds3_ps = 0x01;
        if (buttons3 & 0x02) ds3_btn1 |= 0x01;  // Touchpad -> Select
        
        pthread_mutex_lock(&report_mutex);
        ds3_report[2] = ds3_btn1;
        ds3_report[3] = ds3_btn2;
        ds3_report[4] = ds3_ps;
        ds3_report[6] = lx;
        ds3_report[7] = ly;
        ds3_report[8] = rx;
        ds3_report[9] = ry;
        ds3_report[18] = l2;
        ds3_report[19] = r2;
        ds3_report[22] = (buttons1 & 0x80) ? 0xff : 0;
        ds3_report[23] = (buttons1 & 0x40) ? 0xff : 0;
        ds3_report[24] = (buttons1 & 0x20) ? 0xff : 0;
        ds3_report[25] = (buttons1 & 0x10) ? 0xff : 0;
        pthread_mutex_unlock(&report_mutex);
    }
    
    return NULL;
}

// =================================================================
// Output Thread - Rumble + Lightbar
// =================================================================
void* output_thread(void* arg) {
    uint8_t last_right = 0;
    uint8_t last_left = 0;
    lightbar_state_t last_lightbar = {0, 0, 0, 0, 0};
    int update_counter = 0;
    
    while (running) {
        uint8_t right, left;
        lightbar_state_t lb_state;
        
        // Get current rumble state
        pthread_mutex_lock(&rumble_mutex);
        right = rumble_right;
        left = rumble_left;
        pthread_mutex_unlock(&rumble_mutex);
        
        // Read lightbar state from IPC file periodically
        // (every 100ms = 10 iterations at 100Hz)
        if (++update_counter >= 10) {
            update_counter = 0;
            read_lightbar_state();
        }
        
        // Get current lightbar state
        pthread_mutex_lock(&lightbar_mutex);
        lb_state = lightbar_state;
        pthread_mutex_unlock(&lightbar_mutex);
        
        // Send update if anything changed, or periodically to maintain rumble
        int rumble_changed = (right != last_right || left != last_left);
        int lightbar_changed = (lb_state.r != last_lightbar.r ||
                               lb_state.g != last_lightbar.g ||
                               lb_state.b != last_lightbar.b ||
                               lb_state.player_leds != last_lightbar.player_leds);
        int rumble_active = (right > 0 || left > 0);
        
        if (hidraw_fd >= 0 && (rumble_changed || lightbar_changed || rumble_active)) {
            send_dualsense_output(hidraw_fd, right, left,
                                 lb_state.r, lb_state.g, lb_state.b,
                                 lb_state.player_leds);
            last_right = right;
            last_left = left;
            last_lightbar = lb_state;
        }
        
        usleep(10000);  // 100Hz update rate
    }
    
    // Turn off lightbar and rumble on exit
    if (hidraw_fd >= 0) {
        send_dualsense_output(hidraw_fd, 0, 0, 0, 0, 0, 0);
    }
    
    return NULL;
}

// =================================================================
// USB Input Thread
// =================================================================
void* usb_input_thread(void* arg) {
    ep1_fd = open("/dev/ffs-ds3/ep1", O_RDWR);
    if (ep1_fd < 0) { perror("open ep1"); return NULL; }
    
    uint8_t buf[49];
    while (running) {
        if (usb_enabled) {
            pthread_mutex_lock(&report_mutex);
            memcpy(buf, ds3_report, 49);
            pthread_mutex_unlock(&report_mutex);
            write(ep1_fd, buf, 49);
        }
        usleep(4000);
    }
    return NULL;
}

// =================================================================
// USB Output Thread
// =================================================================
void* usb_output_thread(void* arg) {
    ep2_fd = open("/dev/ffs-ds3/ep2", O_RDWR);
    if (ep2_fd < 0) { perror("open ep2"); return NULL; }
    
    uint8_t buf[64];
    while (running) {
        ssize_t n = read(ep2_fd, buf, sizeof(buf));
        if (n <= 0) {
            if (errno == EAGAIN) {
                usleep(1000);
                continue;
            }
            continue;
        }
        
        if (n >= 6) {
            uint8_t right_power = buf[3];
            uint8_t left_power = buf[5];
            
            uint8_t ds_right = right_power ? 0xFF : 0x00;
            uint8_t ds_left = left_power;
            
            pthread_mutex_lock(&rumble_mutex);
            rumble_right = ds_right;
            rumble_left = ds_left;
            pthread_mutex_unlock(&rumble_mutex);
        }
    }
    return NULL;
}

// =================================================================
// USB Control Thread
// =================================================================
void* usb_control_thread(void* arg) {
    while (running) {
        struct usb_functionfs_event event;
        if (read(ep0_fd, &event, sizeof(event)) < 0) {
            if (errno == EINTR) continue;
            break;
        }
        
        if (event.type == FUNCTIONFS_SETUP) {
            uint8_t bRequest = event.u.setup.bRequest;
            uint16_t wValue = event.u.setup.wValue;
            uint16_t wLength = event.u.setup.wLength;
            uint8_t report_id = wValue & 0xFF;
            
            if (bRequest == 0x0A) {
                read(ep0_fd, NULL, 0);
            } else if (bRequest == 0x01) {
                uint8_t *data = NULL;
                switch (report_id) {
                    case 0x01: data = report_01; break;
                    case 0xF2: data = report_f2; break;
                    case 0xF5: data = report_f5; break;
                    case 0xF7: data = report_f7; break;
                    case 0xF8: data = report_f8; break;
                    case 0xEF: data = report_ef; break;
                }
                if (data) write(ep0_fd, data, 64 < wLength ? 64 : wLength);
                else read(ep0_fd, NULL, 0);
            } else if (bRequest == 0x09) {
                uint8_t buf[64];
                ssize_t r = 0;
                if (wLength > 0) {
                    r = read(ep0_fd, buf, wLength < 64 ? wLength : 64);
                }
                
                if (r > 0 && report_id == 0xEF) {
                    report_ef[0] = 0xef;
                    memcpy(&report_ef[1], buf, r < 63 ? r : 63);
                }
                
                write(ep0_fd, NULL, 0);
            } else {
                read(ep0_fd, NULL, 0);
            }
        } else if (event.type == FUNCTIONFS_ENABLE) {
            printf("PS3 connected!\n");
            usb_enabled = 1;
        } else if (event.type == FUNCTIONFS_DISABLE) {
            printf("PS3 disconnected\n");
            usb_enabled = 0;
            pthread_mutex_lock(&rumble_mutex);
            rumble_right = 0;
            rumble_left = 0;
            pthread_mutex_unlock(&rumble_mutex);
        } else if (event.type == FUNCTIONFS_UNBIND) {
            running = 0;
        }
    }
    return NULL;
}

// =================================================================
// USB Gadget Setup
// =================================================================
int setup_usb_gadget(void) {
    system("modprobe libcomposite 2>/dev/null");
    system("modprobe usb_f_fs 2>/dev/null");
    
    if (access("/sys/kernel/config/usb_gadget/ds3", F_OK) != 0) {
        system("mkdir -p /sys/kernel/config/usb_gadget/ds3");
        system("echo 0x054c > /sys/kernel/config/usb_gadget/ds3/idVendor");
        system("echo 0x0268 > /sys/kernel/config/usb_gadget/ds3/idProduct");
        system("echo 0x0100 > /sys/kernel/config/usb_gadget/ds3/bcdDevice");
        system("echo 0x0200 > /sys/kernel/config/usb_gadget/ds3/bcdUSB");
        system("mkdir -p /sys/kernel/config/usb_gadget/ds3/strings/0x409");
        system("echo '123456' > /sys/kernel/config/usb_gadget/ds3/strings/0x409/serialnumber");
        system("echo 'Sony' > /sys/kernel/config/usb_gadget/ds3/strings/0x409/manufacturer");
        system("echo 'PLAYSTATION(R)3 Controller' > /sys/kernel/config/usb_gadget/ds3/strings/0x409/product");
        system("mkdir -p /sys/kernel/config/usb_gadget/ds3/configs/c.1/strings/0x409");
        system("echo 'DS3' > /sys/kernel/config/usb_gadget/ds3/configs/c.1/strings/0x409/configuration");
        system("echo 500 > /sys/kernel/config/usb_gadget/ds3/configs/c.1/MaxPower");
        system("mkdir -p /sys/kernel/config/usb_gadget/ds3/functions/ffs.usb0");
        system("ln -sf /sys/kernel/config/usb_gadget/ds3/functions/ffs.usb0 /sys/kernel/config/usb_gadget/ds3/configs/c.1/ 2>/dev/null");
    }
    
    system("mkdir -p /dev/ffs-ds3");
    system("umount /dev/ffs-ds3 2>/dev/null");
    system("mount -t functionfs usb0 /dev/ffs-ds3");
    
    // Create IPC directory
    system("mkdir -p /tmp/rosettapad");
    
    return 0;
}

// =================================================================
// Main
// =================================================================
int main(void) {
    pthread_t ds_tid, usb_in_tid, usb_out_tid, usb_ctrl_tid, output_tid;
    
    printf("=== RosettaPad: DualSense to PS3 Adapter ===\n");
    printf("    With Lightbar Support\n\n");
    
    init_crc32_table();
    
    setup_usb_gadget();
    
    ep0_fd = open("/dev/ffs-ds3/ep0", O_RDWR);
    if (ep0_fd < 0) { perror("open ep0"); return 1; }
    
    write(ep0_fd, &descriptors, sizeof(descriptors));
    write(ep0_fd, &strings, sizeof(strings));
    
    report_ef[0] = 0xef;
    
    pthread_create(&ds_tid, NULL, dualsense_thread, NULL);
    pthread_create(&usb_in_tid, NULL, usb_input_thread, NULL);
    pthread_create(&usb_out_tid, NULL, usb_output_thread, NULL);
    pthread_create(&usb_ctrl_tid, NULL, usb_control_thread, NULL);
    pthread_create(&output_tid, NULL, output_thread, NULL);
    
    printf("Binding to USB...\n");
    system("echo '3f980000.usb' > /sys/kernel/config/usb_gadget/ds3/UDC");
    
    printf("\nAdapter running! Press Ctrl+C to stop.\n");
    printf("Lightbar IPC: %s\n\n", LIGHTBAR_IPC_PATH);
    
    while (running) {
        sleep(1);
        
        uint8_t r_right, r_left;
        pthread_mutex_lock(&rumble_mutex);
        r_right = rumble_right;
        r_left = rumble_left;
        pthread_mutex_unlock(&rumble_mutex);
        
        lightbar_state_t lb;
        pthread_mutex_lock(&lightbar_mutex);
        lb = lightbar_state;
        pthread_mutex_unlock(&lightbar_mutex);
        
        printf("\r[DS: %s] [PS3: %s] [Rumble: L=%3d R=%3d] [LED: #%02x%02x%02x]       ", 
               hidraw_fd >= 0 ? "OK" : "--",
               usb_enabled ? "OK" : "--",
               r_left, r_right,
               lb.r, lb.g, lb.b);
        fflush(stdout);
    }
    
    printf("\nShutting down...\n");
    
    if (hidraw_fd >= 0) {
        send_dualsense_output(hidraw_fd, 0, 0, 0, 0, 0, 0);
    }
    
    system("echo '' > /sys/kernel/config/usb_gadget/ds3/UDC");
    
    if (ep1_fd >= 0) close(ep1_fd);
    if (ep2_fd >= 0) close(ep2_fd);
    if (hidraw_fd >= 0) close(hidraw_fd);
    close(ep0_fd);
    
    return 0;
}