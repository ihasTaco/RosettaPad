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

static int ep0_fd = -1;
static int ep1_fd = -1;
static int ep2_fd = -1;
static int hidraw_fd = -1;

static volatile int running = 1;
static volatile int usb_enabled = 0;

// Enable motion debug output (set to 0 to disable)
#define MOTION_DEBUG 1

// Credit to eleccelerator.com for helping figure out most of this.
// DS3 HID Report
// Original | Description
// 01 | Byte 0: Report ID (0x01)
// 00 | Byte 1: Reserved (0x00)
// 
// 00 | Byte 2: Released (0x00), Select (0x01), L3 (0x02), R3 (0x04), Start (0x08), D Up (0x10), D Right (0x20), D Down (0x40), D Left (0x80)
// 00 | Byte 3: Released (0x00), L2 (0x01), R2 (0x02), L1 (0x04), R1 (0x08), Triangle (0x10), Circle (0x20), Cross (0x40), Square (0x80)
// 00 | Byte 4: Released (0x00), PS (0x01)
// 00 | Byte 5: ????
// 
// 7e | Byte 6: Left analog stick X axis (0x00 - 0xFF)
// 7e | Byte 7: Left analog stick Y axis (0x00 - 0xFF)
// 
// 7d | Byte 8: Right analog stick X axis (0x00 - 0xFF)
// 7f | Byte 9: Right analog stick Y axis (0x00 - 0xFF)
// 
// 00 00 00 | Byte 10 - 12: ????
// 
// # Pressure Controls (0x00 is released, 0xFF is fully pressed)
// 00 | Byte 13: D Up Pressure         (0x00 - 0xFF)
// 00 | Byte 14: D Right Pressure      (0x00 - 0xFF)
// 00 | Byte 15: D Down Pressure       (0x00 - 0xFF)
// 00 | Byte 16: D Left Pressure       (0x00 - 0xFF)
// 00 | Byte 17: L2 Pressure           (0x00 - 0xFF)
// 00 | Byte 18: R2 Pressure           (0x00 - 0xFF)
// 00 | Byte 19: L1 Pressure           (0x00 - 0xFF)
// 00 | Byte 20: R1 Pressure           (0x00 - 0xFF)
// 00 | Byte 21: Triangle Pressure     (0x00 - 0xFF)
// 00 | Byte 22: Circle Pressure       (0x00 - 0xFF)
// 00 | Byte 23: Cross Pressure        (0x00 - 0xFF)
// 00 | Byte 24: Square Pressure       (0x00 - 0xFF)
// 
// 00 00 00 00 03 ef 16 00 00 00 00 33 04 77 01 | Byte 25 - 39: ????
// 
// c0 02 | Byte 40 - 41: Accelerometer X Axis, LE 10bit unsigned
// 0f 02 | Byte 42 - 43: Accelerometer Y Axis, LE 10bit unsigned
// 00 01 | Byte 44 - 45: Accelerometer Z Axis, LE 10bit unsigned
// 8d 00 | Byte 46 - 47: Gyroscope, LE 10bit unsigned
// 
// 02 | Byte 48: ????

// DS3 input report (49 bytes)
// Motion data at bytes 40-47, LITTLE-ENDIAN (low byte first):
//  40: Accel X low, 41: Accel X high
//  42: Accel Y low, 43: Accel Y high  
//  44: Accel Z low, 45: Accel Z high
//  46: Gyro Z low,  47: Gyro Z high
// Values are 10-bit unsigned (0-1023), centered at ~512
// Gravity shows as offset from 512 on the axis pointing up/down
static uint8_t ds3_report[49] = {
    0x01, 0x00, 0x00, 0x00, 0x00, 0x00, 0x80, 0x80,     // 0-7
    0x80, 0x80, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,     // 8-15
    0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,     // 16-23
    0x00, 0x00, 0x00, 0x00, 0x00, 0x02, 0x00, 0x12,     // 24-31
    0x00, 0x00, 0x00, 0x00, 0x12, 0x04, 0x77, 0x01,     // 32-39
    0x00, 0x02,                                         // 40: Accel X low, 41: Accel X high
    0x00, 0x02,                                         // 42: Accel Y low, 43: Accel Y high  
    0x00, 0x02,                                         // 44: Accel Z low, 45: Accel Z high
    0x8d, 0x00,                                         // 46: Gyro Z low,  47: Gyro Z high
    0x02
};
static pthread_mutex_t report_mutex = PTHREAD_MUTEX_INITIALIZER;

// Rumble state
static uint8_t rumble_right = 0;
static uint8_t rumble_left = 0;
static pthread_mutex_t rumble_mutex = PTHREAD_MUTEX_INITIALIZER;

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

// USB descriptors
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

// CRC32 for DualSense BT
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

void send_dualsense_rumble(int fd, uint8_t right_motor, uint8_t left_motor) {
    static uint8_t seq = 0;
    uint8_t report[78] = {0};
    
    report[0] = 0x31;
    report[1] = (seq << 4) | 0x00;
    seq = (seq + 1) & 0x0F;
    report[2] = 0x10;
    report[3] = 0x03;
    report[4] = 0x00;
    report[5] = right_motor;
    report[6] = left_motor;
    
    uint8_t crc_buf[75];
    crc_buf[0] = 0xA2;
    memcpy(&crc_buf[1], report, 74);
    uint32_t crc = calc_crc32(crc_buf, 75);
    report[74] = (crc >> 0) & 0xFF;
    report[75] = (crc >> 8) & 0xFF;
    report[76] = (crc >> 16) & 0xFF;
    report[77] = (crc >> 24) & 0xFF;
    
    if (fd >= 0) write(fd, report, sizeof(report));
}

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

uint8_t convert_dpad(uint8_t ds_dpad) {
    switch (ds_dpad & 0x0F) {
        case 0: return 0x10;
        case 1: return 0x10 | 0x20;
        case 2: return 0x20;
        case 3: return 0x40 | 0x20;
        case 4: return 0x40;
        case 5: return 0x40 | 0x80;
        case 6: return 0x80;
        case 7: return 0x10 | 0x80;
        default: return 0;
    }
}

// Convert DualSense accelerometer to DS3 format
// DualSense: signed 16-bit, ~8192 per 1g (can be negative)
// DS3: unsigned 10-bit (0-1023), centered at 512, ~113 per 1g
//
// When controller is flat face-up:
// - DualSense: Y axis shows ~8192 (gravity pointing down)
// - DS3: Y axis should show ~512 + 113 ≈ 625
uint16_t convert_accel(int16_t value) {
    // DualSense: ±8192 per g
    // DS3: ~113 per g, centered at 512
    // Scale factor: 8192 / 113 ≈ 72.5
    
    int32_t scaled = value / 72;  // Convert to DS3 scale
    scaled += 512;                 // Center at 512
    
    // Clamp to 10-bit range (0-1023)
    if (scaled < 0) scaled = 0;
    if (scaled > 1023) scaled = 1023;
    
    return (uint16_t)scaled;
}

// Convert DualSense gyroscope to DS3 format
// DualSense: signed 16-bit, high resolution (~1024 per deg/s?)
// DS3: 16-bit little-endian, centered at ~0x02C0 (704) based on real captures
//
// At rest, DS3 gyro shows: c0 02 (little-endian) = 0x02C0 = 704
uint16_t convert_gyro(int16_t value) {
    // DualSense gyro is high resolution
    // Scale down significantly and center at 0x02C0 (704)
    
    int32_t scaled = value / 64;  // Reduce sensitivity
    scaled += 0x02C0;              // Center at 704 (from real DS3 captures)
    
    // Clamp to reasonable range
    if (scaled < 0) scaled = 0;
    if (scaled > 0xFFFF) scaled = 0xFFFF;
    
    return (uint16_t)scaled;
}

void* dualsense_thread(void* arg) {
    printf("Waiting for DualSense...\n");
    
    while (running && hidraw_fd < 0) {
        hidraw_fd = find_dualsense_hidraw();
        if (hidraw_fd < 0) sleep(1);
    }
    
    if (hidraw_fd < 0) return NULL;
    printf("DualSense connected!\n");
    
    uint8_t buf[78];
    int debug_counter = 0;
    
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
        
        // DualSense BT report (0x31):
        // [0]=0x31, [1]=counter, [2-5]=sticks, [6-7]=triggers
        // [8]=counter, [9]=dpad+face, [10]=shoulders, [11]=PS/touch
        // [16-21]=Gyro X,Y,Z (signed 16-bit LE)
        // [22-27]=Accel X,Y,Z (signed 16-bit LE)
        
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
        
        // Motion data from DualSense BT report 0x31 (signed 16-bit little-endian)
        // BT report has extra header byte, so offsets are +1 from USB
        // Gyro: bytes 17-22, Accel: bytes 23-28
        int16_t gyro_x = (int16_t)(buf[17] | (buf[18] << 8));
        int16_t gyro_y = (int16_t)(buf[19] | (buf[20] << 8));
        int16_t gyro_z = (int16_t)(buf[21] | (buf[22] << 8));
        int16_t accel_x = (int16_t)(buf[23] | (buf[24] << 8));
        int16_t accel_y = (int16_t)(buf[25] | (buf[26] << 8));
        int16_t accel_z = (int16_t)(buf[27] | (buf[28] << 8));
        
        // Convert to DS3 format (10-bit, 0-1023, centered at 512 for accel)
        uint16_t ds3_accel_x = convert_accel(accel_x);
        uint16_t ds3_accel_y = convert_accel(accel_y);
        uint16_t ds3_accel_z = convert_accel(accel_z);
        // DS3 has one gyro axis at bytes 46-47
        uint16_t ds3_gyro = convert_gyro(gyro_z);    // Gyro Z -> bytes 46-47
        
#if MOTION_DEBUG
        debug_counter++;
        if (debug_counter >= 250) {  // ~1 second at 250Hz
            printf("\n[Motion Debug] Report size: %zd\n", n);
            printf("  Raw bytes 0-31: ");
            for (int i = 0; i < 32 && i < n; i++) printf("%02X ", buf[i]);
            printf("\n");
            printf("  DualSense raw: Accel(%6d, %6d, %6d) Gyro(%6d, %6d, %6d)\n",
                   accel_x, accel_y, accel_z, gyro_x, gyro_y, gyro_z);
            printf("  DS3 converted: Accel(%4d, %4d, %4d) Gyro(%4d)\n",
                   ds3_accel_x, ds3_accel_y, ds3_accel_z, ds3_gyro);
            
            // Show the actual bytes at motion positions in DS3 report
            pthread_mutex_lock(&report_mutex);
            printf("  DS3 bytes 40-47: %02X %02X %02X %02X | %02X %02X %02X %02X\n",
                   ds3_report[40], ds3_report[41], ds3_report[42], ds3_report[43],
                   ds3_report[44], ds3_report[45], ds3_report[46], ds3_report[47]);
            pthread_mutex_unlock(&report_mutex);
            
            debug_counter = 0;
        }
#endif
        
        // Build buttons
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
        if (buttons2 & 0x04) ds3_btn2 |= 0x01;  // L2
        if (buttons2 & 0x08) ds3_btn2 |= 0x02;  // R2
        
        if (buttons2 & 0x40) ds3_btn1 |= 0x02;  // L3
        if (buttons2 & 0x80) ds3_btn1 |= 0x04;  // R3
        
        if (buttons2 & 0x10) ds3_btn1 |= 0x01;  // Create -> Select
        if (buttons2 & 0x20) ds3_btn1 |= 0x08;  // Options -> Start
        
        if (buttons3 & 0x01) ds3_ps = 0x01;     // PS button
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
        
        // Face button pressures
        ds3_report[22] = (buttons1 & 0x80) ? 0xff : 0;  // Triangle
        ds3_report[23] = (buttons1 & 0x40) ? 0xff : 0;  // Circle
        ds3_report[24] = (buttons1 & 0x20) ? 0xff : 0;  // Cross
        ds3_report[25] = (buttons1 & 0x10) ? 0xff : 0;  // Square
        
        // Motion data - DS3 format based on real controller captures:
        // Accel: bytes 40-45 (three axis, 10-bit little-endian)
        // Gyro: bytes 46-47 (one axis, 10-bit little-endian)

        // Accel X at bytes 40-41
        ds3_report[40] = ds3_accel_x & 0xFF;
        ds3_report[41] = (ds3_accel_x >> 8) & 0xFF;

        // Accel Y at bytes 42-43
        ds3_report[42] = ds3_accel_y & 0xFF;
        ds3_report[43] = (ds3_accel_y >> 8) & 0xFF;

        // Accel Z at bytes 44-45
        ds3_report[44] = ds3_accel_z & 0xFF;
        ds3_report[45] = (ds3_accel_z >> 8) & 0xFF;

        // Gyro at bytes 46-47
        ds3_report[46] = ds3_gyro & 0xFF;
        ds3_report[47] = (ds3_gyro >> 8) & 0xFF;
        
        pthread_mutex_unlock(&report_mutex);
    }
    
    return NULL;
}

void* rumble_thread(void* arg) {
    uint8_t last_right = 0, last_left = 0;
    
    while (running) {
        uint8_t right, left;
        pthread_mutex_lock(&rumble_mutex);
        right = rumble_right;
        left = rumble_left;
        pthread_mutex_unlock(&rumble_mutex);
        
        if (hidraw_fd >= 0 && (right != last_right || left != last_left || right > 0 || left > 0)) {
            send_dualsense_rumble(hidraw_fd, right, left);
            last_right = right;
            last_left = left;
        }
        usleep(10000);
    }
    
    if (hidraw_fd >= 0) send_dualsense_rumble(hidraw_fd, 0, 0);
    return NULL;
}

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

void* usb_output_thread(void* arg) {
    ep2_fd = open("/dev/ffs-ds3/ep2", O_RDWR);
    if (ep2_fd < 0) { perror("open ep2"); return NULL; }
    
    uint8_t buf[64];
    while (running) {
        ssize_t n = read(ep2_fd, buf, sizeof(buf));
        if (n <= 0) {
            if (errno == EAGAIN) { usleep(1000); continue; }
            continue;
        }
        
        if (n >= 6) {
            uint8_t right_power = buf[3];
            uint8_t left_power = buf[5];
            
            pthread_mutex_lock(&rumble_mutex);
            rumble_right = right_power ? 0xFF : 0x00;
            rumble_left = left_power;
            pthread_mutex_unlock(&rumble_mutex);
        }
    }
    return NULL;
}

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
                if (wLength > 0) r = read(ep0_fd, buf, wLength < 64 ? wLength : 64);
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
    
    return 0;
}

int main(void) {
    pthread_t ds_tid, usb_in_tid, usb_out_tid, usb_ctrl_tid, rumble_tid;
    
    printf("=== DualSense to PS3 Adapter (with SIXAXIS) ===\n\n");
    
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
    pthread_create(&rumble_tid, NULL, rumble_thread, NULL);
    
    printf("Binding to USB...\n");
    system("echo '3f980000.usb' > /sys/kernel/config/usb_gadget/ds3/UDC");
    
    printf("\nAdapter running! Press Ctrl+C to stop.\n");
    printf("Motion debug output enabled - watch for [Motion Debug] lines.\n\n");
    
    while (running) {
        sleep(1);
        uint8_t r_right, r_left;
        pthread_mutex_lock(&rumble_mutex);
        r_right = rumble_right;
        r_left = rumble_left;
        pthread_mutex_unlock(&rumble_mutex);
        
        printf("\r[DS: %s] [PS3: %s] [Rumble: L=%3d R=%3d]       ", 
               hidraw_fd >= 0 ? "OK" : "--",
               usb_enabled ? "OK" : "--",
               r_left, r_right);
        fflush(stdout);
    }
    
    printf("\nShutting down...\n");
    if (hidraw_fd >= 0) send_dualsense_rumble(hidraw_fd, 0, 0);
    system("echo '' > /sys/kernel/config/usb_gadget/ds3/UDC");
    
    if (ep1_fd >= 0) close(ep1_fd);
    if (ep2_fd >= 0) close(ep2_fd);
    if (hidraw_fd >= 0) close(hidraw_fd);
    close(ep0_fd);
    
    return 0;
}