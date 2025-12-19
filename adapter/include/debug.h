/*
 * RosettaPad - Unified Debug System
 * 
 * Usage:
 *   debug_set_flags(DBG_INPUT | DBG_MOTION);  // Enable specific categories
 *   debug_print(DBG_INPUT, "Button pressed: %02X", btn);
 *   debug_hex(DBG_USB, "Report", data, len);
 * 
 * Categories can be combined with bitwise OR.
 * Set DBG_ALL to enable everything, DBG_NONE to disable all.
 */

#ifndef ROSETTAPAD_DEBUG_H
#define ROSETTAPAD_DEBUG_H

#include <stdint.h>
#include <stddef.h>
#include <stdio.h>
#include <stdarg.h>
#include <time.h>
#include <pthread.h>

// =================================================================
// Debug Categories (bit flags)
// =================================================================

#define DBG_NONE        0x00000000  // No debug output
#define DBG_ALL         0xFFFFFFFF  // All debug output

// Core systems
#define DBG_USB         0x00000001  // USB gadget events
#define DBG_USB_CTRL    0x00000002  // USB control transfers (ep0)
#define DBG_USB_DATA    0x00000004  // USB data transfers (ep1/ep2)
#define DBG_BT          0x00000008  // Bluetooth general
#define DBG_BT_L2CAP    0x00000010  // Bluetooth L2CAP protocol
#define DBG_BT_HID      0x00000020  // Bluetooth HID transactions

// Controller input
#define DBG_INPUT       0x00000100  // Button/stick input
#define DBG_MOTION      0x00000200  // Accelerometer/gyroscope
#define DBG_TOUCHPAD    0x00000400  // Touchpad data
#define DBG_PRESSURE    0x00000800  // Analog pressure values

// Protocol/Emulation
#define DBG_HANDSHAKE   0x00001000  // PS3 handshake sequence
#define DBG_REPORTS     0x00002000  // HID reports (feature/input/output)
#define DBG_RUMBLE      0x00004000  // Rumble/vibration
#define DBG_LED         0x00008000  // LED/lightbar control

// DualSense specific
#define DBG_DUALSENSE   0x00010000  // DualSense general
#define DBG_DS_RAW      0x00020000  // DualSense raw HID data

// DS3 emulation
#define DBG_DS3         0x00040000  // DS3 emulation general
#define DBG_DS3_RAW     0x00080000  // DS3 raw report data

// System
#define DBG_INIT        0x00100000  // Initialization
#define DBG_ERROR       0x00200000  // Errors (always useful)
#define DBG_WARN        0x00400000  // Warnings
#define DBG_INFO        0x00800000  // General info

// Verbose/Spam (use sparingly)
#define DBG_VERBOSE     0x01000000  // Verbose output
#define DBG_TIMING      0x02000000  // Timing information
#define DBG_PERIODIC    0x04000000  // Periodic status (every N reports)

// Pairing/Connection
#define DBG_PAIRING     0x08000000  // Pairing process

// =================================================================
// Preset Combinations
// =================================================================

// Common debugging scenarios
#define DBG_QUICK       (DBG_ERROR | DBG_WARN | DBG_INFO)
#define DBG_USB_ALL     (DBG_USB | DBG_USB_CTRL | DBG_USB_DATA)
#define DBG_BT_ALL      (DBG_BT | DBG_BT_L2CAP | DBG_BT_HID)
#define DBG_INPUT_ALL   (DBG_INPUT | DBG_MOTION | DBG_TOUCHPAD | DBG_PRESSURE)
#define DBG_PROTOCOL    (DBG_HANDSHAKE | DBG_REPORTS | DBG_PAIRING)

// =================================================================
// Global Debug State
// =================================================================

extern volatile uint32_t g_debug_flags;
extern pthread_mutex_t g_debug_mutex;

// Periodic debug counters (for rate-limiting output)
typedef struct {
    int input_count;
    int motion_count;
    int report_count;
    int bt_count;
} debug_counters_t;

extern debug_counters_t g_debug_counters;

// How often to print periodic debug (every N events)
#define DBG_PERIODIC_INTERVAL 250

// =================================================================
// Debug Functions
// =================================================================

/**
 * Initialize debug system
 * Call once at startup
 */
void debug_init(void);

/**
 * Set debug flags
 * @param flags Combination of DBG_* flags
 */
void debug_set_flags(uint32_t flags);

/**
 * Add debug flags (OR with existing)
 * @param flags Flags to add
 */
void debug_add_flags(uint32_t flags);

/**
 * Remove debug flags
 * @param flags Flags to remove
 */
void debug_remove_flags(uint32_t flags);

/**
 * Get current debug flags
 * @return Current flag combination
 */
uint32_t debug_get_flags(void);

/**
 * Check if a debug category is enabled
 * @param category DBG_* flag to check
 * @return 1 if enabled, 0 if not
 */
static inline int debug_enabled(uint32_t category) {
    return (g_debug_flags & category) != 0;
}

/**
 * Print debug message if category is enabled
 * @param category DBG_* flag
 * @param fmt printf-style format string
 */
void debug_print(uint32_t category, const char* fmt, ...);

/**
 * Print hex dump if category is enabled
 * @param category DBG_* flag
 * @param label Description of the data
 * @param data Pointer to data
 * @param len Length of data
 */
void debug_hex(uint32_t category, const char* label, const uint8_t* data, size_t len);

/**
 * Print hex dump with max length limit
 * @param category DBG_* flag
 * @param label Description
 * @param data Pointer to data
 * @param len Length of data
 * @param max_len Maximum bytes to print
 */
void debug_hex_limit(uint32_t category, const char* label, 
                     const uint8_t* data, size_t len, size_t max_len);

/**
 * Print periodic debug (rate-limited)
 * Only prints every DBG_PERIODIC_INTERVAL calls
 * @param category DBG_* flag  
 * @param counter Pointer to counter to increment
 * @param fmt printf-style format string
 */
void debug_periodic(uint32_t category, int* counter, const char* fmt, ...);

/**
 * Get category name string
 * @param category Single DBG_* flag
 * @return Category name
 */
const char* debug_category_name(uint32_t category);

/**
 * Parse debug flags from string
 * Accepts: "all", "none", "usb,bt,input", "0x1234", etc.
 * @param str String to parse
 * @return Parsed flags
 */
uint32_t debug_parse_flags(const char* str);

/**
 * Print all available debug categories
 */
void debug_print_categories(void);

// =================================================================
// Convenience Macros
// =================================================================

// Simple category-prefixed prints
#define DBG_USB_PRINT(fmt, ...)      debug_print(DBG_USB, "[USB] " fmt, ##__VA_ARGS__)
#define DBG_BT_PRINT(fmt, ...)       debug_print(DBG_BT, "[BT] " fmt, ##__VA_ARGS__)
#define DBG_INPUT_PRINT(fmt, ...)    debug_print(DBG_INPUT, "[Input] " fmt, ##__VA_ARGS__)
#define DBG_MOTION_PRINT(fmt, ...)   debug_print(DBG_MOTION, "[Motion] " fmt, ##__VA_ARGS__)
#define DBG_ERROR_PRINT(fmt, ...)    debug_print(DBG_ERROR, "[ERROR] " fmt, ##__VA_ARGS__)
#define DBG_WARN_PRINT(fmt, ...)     debug_print(DBG_WARN, "[WARN] " fmt, ##__VA_ARGS__)
#define DBG_INFO_PRINT(fmt, ...)     debug_print(DBG_INFO, "[Info] " fmt, ##__VA_ARGS__)

// Conditional execution (only if category enabled)
#define DBG_IF(cat, code) do { if (debug_enabled(cat)) { code; } } while(0)

#endif // ROSETTAPAD_DEBUG_H