/*
 * RosettaPad - Unified Debug System Implementation
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdarg.h>
#include <ctype.h>
#include <time.h>
#include "debug.h"

// =================================================================
// Global State
// =================================================================

// Default: errors, warnings, and info enabled
volatile uint32_t g_debug_flags = DBG_ERROR | DBG_WARN | DBG_INFO | DBG_INIT;
pthread_mutex_t g_debug_mutex = PTHREAD_MUTEX_INITIALIZER;
debug_counters_t g_debug_counters = {0};

// Category name mapping
typedef struct {
    uint32_t flag;
    const char* name;
    const char* description;
} debug_category_info_t;

static const debug_category_info_t category_info[] = {
    // Core systems
    {DBG_USB,       "usb",       "USB gadget events"},
    {DBG_USB_CTRL,  "usb_ctrl",  "USB control transfers (ep0)"},
    {DBG_USB_DATA,  "usb_data",  "USB data transfers (ep1/ep2)"},
    {DBG_BT,        "bt",        "Bluetooth general"},
    {DBG_BT_L2CAP,  "bt_l2cap",  "Bluetooth L2CAP protocol"},
    {DBG_BT_HID,    "bt_hid",    "Bluetooth HID transactions"},
    
    // Controller input
    {DBG_INPUT,     "input",     "Button/stick input"},
    {DBG_MOTION,    "motion",    "Accelerometer/gyroscope"},
    {DBG_TOUCHPAD,  "touchpad",  "Touchpad data"},
    {DBG_PRESSURE,  "pressure",  "Analog pressure values"},
    
    // Protocol/Emulation
    {DBG_HANDSHAKE, "handshake", "PS3 handshake sequence"},
    {DBG_REPORTS,   "reports",   "HID reports"},
    {DBG_RUMBLE,    "rumble",    "Rumble/vibration"},
    {DBG_LED,       "led",       "LED/lightbar control"},
    
    // DualSense
    {DBG_DUALSENSE, "dualsense", "DualSense general"},
    {DBG_DS_RAW,    "ds_raw",    "DualSense raw HID data"},
    
    // DS3
    {DBG_DS3,       "ds3",       "DS3 emulation general"},
    {DBG_DS3_RAW,   "ds3_raw",   "DS3 raw report data"},
    
    // System
    {DBG_INIT,      "init",      "Initialization"},
    {DBG_ERROR,     "error",     "Errors"},
    {DBG_WARN,      "warn",      "Warnings"},
    {DBG_INFO,      "info",      "General info"},
    
    // Verbose
    {DBG_VERBOSE,   "verbose",   "Verbose output"},
    {DBG_TIMING,    "timing",    "Timing information"},
    {DBG_PERIODIC,  "periodic",  "Periodic status"},
    
    // Pairing
    {DBG_PAIRING,   "pairing",   "Pairing process"},
    
    // Presets (not individual flags)
    {DBG_ALL,       "all",       "All debug output"},
    {DBG_NONE,      "none",      "No debug output"},
    {DBG_QUICK,     "quick",     "Error/warn/info"},
    {DBG_USB_ALL,   "usb_all",   "All USB debug"},
    {DBG_BT_ALL,    "bt_all",    "All Bluetooth debug"},
    {DBG_INPUT_ALL, "input_all", "All input debug"},
    {DBG_PROTOCOL,  "protocol",  "Protocol debug"},
    
    {0, NULL, NULL}  // Terminator
};

// =================================================================
// Implementation
// =================================================================

void debug_init(void) {
    pthread_mutex_init(&g_debug_mutex, NULL);
    memset(&g_debug_counters, 0, sizeof(g_debug_counters));
    
    // Check environment variable for debug flags
    const char* env_flags = getenv("ROSETTAPAD_DEBUG");
    if (env_flags) {
        g_debug_flags = debug_parse_flags(env_flags);
        printf("[Debug] Flags set from environment: 0x%08X\n", g_debug_flags);
    }
}

void debug_set_flags(uint32_t flags) {
    g_debug_flags = flags;
}

void debug_add_flags(uint32_t flags) {
    g_debug_flags |= flags;
}

void debug_remove_flags(uint32_t flags) {
    g_debug_flags &= ~flags;
}

uint32_t debug_get_flags(void) {
    return g_debug_flags;
}

void debug_print(uint32_t category, const char* fmt, ...) {
    if (!(g_debug_flags & category)) return;
    
    pthread_mutex_lock(&g_debug_mutex);
    
    // Timestamp
    struct timespec ts;
    clock_gettime(CLOCK_MONOTONIC, &ts);
    printf("[%5ld.%03ld] ", ts.tv_sec % 100000, ts.tv_nsec / 1000000);
    
    // Message
    va_list args;
    va_start(args, fmt);
    vprintf(fmt, args);
    va_end(args);
    
    printf("\n");
    fflush(stdout);
    
    pthread_mutex_unlock(&g_debug_mutex);
}

void debug_hex(uint32_t category, const char* label, const uint8_t* data, size_t len) {
    debug_hex_limit(category, label, data, len, 64);
}

void debug_hex_limit(uint32_t category, const char* label, 
                     const uint8_t* data, size_t len, size_t max_len) {
    if (!(g_debug_flags & category)) return;
    
    pthread_mutex_lock(&g_debug_mutex);
    
    struct timespec ts;
    clock_gettime(CLOCK_MONOTONIC, &ts);
    
    size_t print_len = (len > max_len) ? max_len : len;
    
    printf("[%5ld.%03ld] %s (%zu bytes):\n", 
           ts.tv_sec % 100000, ts.tv_nsec / 1000000, label, len);
    
    // Print hex with offset
    for (size_t i = 0; i < print_len; i++) {
        if (i % 16 == 0) {
            printf("  %04zx: ", i);
        }
        printf("%02x ", data[i]);
        if (i % 16 == 15 || i == print_len - 1) {
            // Pad if not full line
            if (i % 16 != 15) {
                for (size_t j = i % 16; j < 15; j++) {
                    printf("   ");
                }
            }
            // ASCII representation
            printf(" |");
            size_t line_start = i - (i % 16);
            for (size_t j = line_start; j <= i; j++) {
                char c = (char)data[j];
                printf("%c", (c >= 32 && c < 127) ? c : '.');
            }
            printf("|\n");
        }
    }
    
    if (len > max_len) {
        printf("  ... (%zu more bytes)\n", len - max_len);
    }
    
    fflush(stdout);
    pthread_mutex_unlock(&g_debug_mutex);
}

void debug_periodic(uint32_t category, int* counter, const char* fmt, ...) {
    if (!(g_debug_flags & category)) return;
    
    (*counter)++;
    if (*counter < DBG_PERIODIC_INTERVAL) return;
    *counter = 0;
    
    pthread_mutex_lock(&g_debug_mutex);
    
    struct timespec ts;
    clock_gettime(CLOCK_MONOTONIC, &ts);
    printf("[%5ld.%03ld] ", ts.tv_sec % 100000, ts.tv_nsec / 1000000);
    
    va_list args;
    va_start(args, fmt);
    vprintf(fmt, args);
    va_end(args);
    
    printf("\n");
    fflush(stdout);
    
    pthread_mutex_unlock(&g_debug_mutex);
}

const char* debug_category_name(uint32_t category) {
    for (int i = 0; category_info[i].name != NULL; i++) {
        if (category_info[i].flag == category) {
            return category_info[i].name;
        }
    }
    return "unknown";
}

uint32_t debug_parse_flags(const char* str) {
    if (!str || !*str) return g_debug_flags;
    
    // Handle hex input
    if (str[0] == '0' && (str[1] == 'x' || str[1] == 'X')) {
        // For explicit hex, use as-is but always include errors
        return (uint32_t)strtoul(str, NULL, 16) | DBG_ERROR;
    }
    
    // Handle decimal input
    if (isdigit(str[0])) {
        return (uint32_t)strtoul(str, NULL, 10) | DBG_ERROR;
    }
    
    // Parse comma-separated category names
    // Always include errors and warnings for safety
    uint32_t flags = DBG_ERROR | DBG_WARN;
    char* copy = strdup(str);
    char* token = strtok(copy, ",+| ");
    
    while (token) {
        // Convert to lowercase for comparison
        for (char* p = token; *p; p++) {
            *p = tolower(*p);
        }
        
        // Find matching category
        int found = 0;
        for (int i = 0; category_info[i].name != NULL; i++) {
            if (strcmp(token, category_info[i].name) == 0) {
                flags |= category_info[i].flag;
                found = 1;
                break;
            }
        }
        
        if (!found) {
            fprintf(stderr, "[Debug] Unknown category: %s\n", token);
        }
        
        token = strtok(NULL, ",+| ");
    }
    
    free(copy);
    return flags;
}

void debug_print_categories(void) {
    printf("\nAvailable debug categories:\n");
    printf("----------------------------------------------------------\n");
    
    printf("\n  Core Systems:\n");
    printf("    usb        (0x%08X) - USB gadget events\n", DBG_USB);
    printf("    usb_ctrl   (0x%08X) - USB control transfers\n", DBG_USB_CTRL);
    printf("    usb_data   (0x%08X) - USB data transfers\n", DBG_USB_DATA);
    printf("    bt         (0x%08X) - Bluetooth general\n", DBG_BT);
    printf("    bt_l2cap   (0x%08X) - Bluetooth L2CAP\n", DBG_BT_L2CAP);
    printf("    bt_hid     (0x%08X) - Bluetooth HID\n", DBG_BT_HID);
    
    printf("\n  Controller Input:\n");
    printf("    input      (0x%08X) - Button/stick input\n", DBG_INPUT);
    printf("    motion     (0x%08X) - Accelerometer/gyroscope\n", DBG_MOTION);
    printf("    touchpad   (0x%08X) - Touchpad data\n", DBG_TOUCHPAD);
    printf("    pressure   (0x%08X) - Analog pressure\n", DBG_PRESSURE);
    
    printf("\n  Protocol:\n");
    printf("    handshake  (0x%08X) - PS3 handshake\n", DBG_HANDSHAKE);
    printf("    reports    (0x%08X) - HID reports\n", DBG_REPORTS);
    printf("    rumble     (0x%08X) - Rumble/vibration\n", DBG_RUMBLE);
    printf("    led        (0x%08X) - LED/lightbar\n", DBG_LED);
    printf("    pairing    (0x%08X) - Pairing process\n", DBG_PAIRING);
    
    printf("\n  Controllers:\n");
    printf("    dualsense  (0x%08X) - DualSense general\n", DBG_DUALSENSE);
    printf("    ds_raw     (0x%08X) - DualSense raw data\n", DBG_DS_RAW);
    printf("    ds3        (0x%08X) - DS3 emulation\n", DBG_DS3);
    printf("    ds3_raw    (0x%08X) - DS3 raw data\n", DBG_DS3_RAW);
    
    printf("\n  System:\n");
    printf("    init       (0x%08X) - Initialization\n", DBG_INIT);
    printf("    error      (0x%08X) - Errors\n", DBG_ERROR);
    printf("    warn       (0x%08X) - Warnings\n", DBG_WARN);
    printf("    info       (0x%08X) - General info\n", DBG_INFO);
    printf("    verbose    (0x%08X) - Verbose\n", DBG_VERBOSE);
    printf("    timing     (0x%08X) - Timing info\n", DBG_TIMING);
    printf("    periodic   (0x%08X) - Periodic status\n", DBG_PERIODIC);
    
    printf("\n  Presets:\n");
    printf("    all        (0x%08X) - Everything\n", DBG_ALL);
    printf("    none       (0x%08X) - Nothing\n", DBG_NONE);
    printf("    quick      (0x%08X) - Error/warn/info\n", DBG_QUICK);
    printf("    usb_all    (0x%08X) - All USB\n", DBG_USB_ALL);
    printf("    bt_all     (0x%08X) - All Bluetooth\n", DBG_BT_ALL);
    printf("    input_all  (0x%08X) - All input\n", DBG_INPUT_ALL);
    printf("    protocol   (0x%08X) - Protocol debug\n", DBG_PROTOCOL);
    
    printf("\n  Usage:\n");
    printf("    --debug usb,bt,input\n");
    printf("    --debug all\n");
    printf("    --debug 0x00001234\n");
    printf("    ROSETTAPAD_DEBUG=usb,motion ./rosettapad\n");
    printf("\n");
}