/*
 * RosettaPad - Common utilities and global state
 */

#include <stdio.h>
#include <stdint.h>
#include <pthread.h>
#include "common.h"

// =================================================================
// Global State Definitions
// =================================================================
volatile int g_running = 1;
volatile int g_usb_enabled = 0;

// File descriptors
int g_ep0_fd = -1;
int g_ep1_fd = -1;
int g_ep2_fd = -1;
int g_hidraw_fd = -1;

// DS3 input report - 49 bytes, initialized to neutral state
// Based on real DS3 capture from DS3_USB_Log_0001.txt
uint8_t g_ds3_report[DS3_REPORT_SIZE] = {
    // Bytes 0-15
    0x01,       // [0]  Report ID
    0x00,       // [1]  Reserved
    0x00,       // [2]  Buttons1: Select, L3, R3, Start, D-pad
    0x00,       // [3]  Buttons2: L2, R2, L1, R1, Triangle, Circle, Cross, Square
    0x00,       // [4]  PS button
    0x00,       // [5]  Reserved
    0x80,       // [6]  Left stick X (centered)
    0x80,       // [7]  Left stick Y (centered)
    0x80,       // [8]  Right stick X (centered)
    0x80,       // [9]  Right stick Y (centered)
    0x00,       // [10] D-pad Up pressure
    0x00,       // [11] D-pad Right pressure
    0x00,       // [12] D-pad Down pressure
    0x00,       // [13] D-pad Left pressure
    0x00,       // [14] Reserved
    0x00,       // [15] Reserved
    // Bytes 16-31
    0x00,       // [16] Reserved
    0x00,       // [17] Reserved
    0x00,       // [18] L2 pressure
    0x00,       // [19] R2 pressure
    0x00,       // [20] L1 pressure
    0x00,       // [21] R1 pressure
    0x00,       // [22] Triangle pressure
    0x00,       // [23] Circle pressure
    0x00,       // [24] Cross pressure
    0x00,       // [25] Square pressure
    0x00,       // [26] Reserved
    0x00,       // [27] Reserved
    0x00,       // [28] Reserved
    0x02,       // [29] Plugged status: 0x02=Plugged, 0x03=Unplugged
    0xee,       // [30] Battery: 0x00-0x05=capacity, 0xEE=charging, 0xEF=full, 0xF1=error
    0x12,       // [31] Connection: 0x10=USB+Rumble, 0x12=USB, 0x14=BT+Rumble, 0x16=BT
    // Bytes 32-48
    0x00,       // [32] Reserved
    0x00,       // [33] Reserved
    0x00,       // [34] Reserved
    0x00,       // [35] Reserved
    0x33, 0x04, // [36-37] Unknown status (from capture)
    0x77, 0x01, // [38-39] Unknown status (from capture)
    0xde, 0x02, // [40-41] Accelerometer X (rest ~734 = 0x02de)
    0x35, 0x02, // [42-43] Accelerometer Y (rest ~565 = 0x0235)
    0x08, 0x01, // [44-45] Accelerometer Z (rest ~264 = 0x0108)
    0x94, 0x00, // [46-47] Gyroscope Z (rest ~148 = 0x0094)
    0x02        // [48] Final byte
};
pthread_mutex_t g_report_mutex = PTHREAD_MUTEX_INITIALIZER;

// Rumble state
uint8_t g_rumble_right = 0;
uint8_t g_rumble_left = 0;
pthread_mutex_t g_rumble_mutex = PTHREAD_MUTEX_INITIALIZER;

// Lightbar state (default: blue)
lightbar_state_t g_lightbar_state = {0, 0, 255, 0, 255};
pthread_mutex_t g_lightbar_mutex = PTHREAD_MUTEX_INITIALIZER;

// =================================================================
// Debug Utilities
// =================================================================
void print_hex(const char* label, const uint8_t* data, size_t len) {
    printf("%s (%zu bytes):\n  ", label, len);
    for (size_t i = 0; i < len && i < 64; i++) {
        printf("%02x ", data[i]);
        if ((i + 1) % 16 == 0 && i + 1 < len) printf("\n  ");
    }
    printf("\n");
    fflush(stdout);
}