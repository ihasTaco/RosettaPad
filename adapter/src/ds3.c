/*
 * RosettaPad - DS3 Emulation Layer
 * Handles all PlayStation 3 / DualShock 3 protocol emulation
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <pthread.h>
#include <sys/stat.h>
#include "common.h"
#include "ds3.h"
#include "debug.h"

// Forward declaration for Bluetooth integration
#ifdef ENABLE_BLUETOOTH
extern void bt_hid_store_ps3_mac(const uint8_t* ps3_mac);
#endif

// =================================================================
// Pairing Configuration
// =================================================================
#define PAIRING_CONFIG_DIR  "/etc/rosettapad"
#define PAIRING_CONFIG_FILE "/etc/rosettapad/pairing.conf"

// Storage for Pi's own Bluetooth MAC (set during init)
static char g_local_bt_mac[18] = "00:00:00:00:00:00";
static char g_ps3_bt_mac[18] = "";

// =================================================================
// DS3 Feature Reports - From real DS3 captures
// =================================================================

// Report 0x01 (Capabilities) - First byte MUST be report ID for BT protocol
static uint8_t report_01[DS3_FEATURE_REPORT_SIZE] = {
    0x01, 0x01, 0x04, 0x00, 0x08, 0x0c, 0x01, 0x02, 0x18, 0x18, 0x18, 0x18, 0x09, 0x0a, 0x10, 0x11,
    0x12, 0x13, 0x00, 0x00, 0x00, 0x00, 0x04, 0x00, 0x02, 0x02, 0x02, 0x02, 0x00, 0x00, 0x00, 0x04,
    0x04, 0x04, 0x04, 0x00, 0x00, 0x04, 0x00, 0x01, 0x02, 0x07, 0x00, 0x17, 0x00, 0x00, 0x00, 0x00,
    0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00
};

// Report 0xF2 (Controller Bluetooth MAC)
static uint8_t report_f2[DS3_FEATURE_REPORT_SIZE] = {
    0xf2, 0xff, 0xff, 0x00, 0x34, 0xc7, 0x31, 0x25, 0xae, 0x60, 0x00, 0x03, 0x50, 0x81, 0xd8, 0x01,
    0x8a, 0x13, 0x00, 0x00, 0x00, 0x00, 0x04, 0x00, 0x02, 0x02, 0x02, 0x02, 0x00, 0x00, 0x00, 0x04,
    0x04, 0x04, 0x04, 0x00, 0x00, 0x04, 0x00, 0x01, 0x02, 0x07, 0x00, 0x17, 0x00, 0x00, 0x00, 0x00,
    0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00
};

// Report 0xF5 (Host Bluetooth MAC / Pairing)
// First byte MUST be report ID for BT protocol
// After pairing, bytes 2-7 will contain the PS3's Bluetooth MAC
static uint8_t report_f5[DS3_FEATURE_REPORT_SIZE] = {
    0xf5, 0x00, 
    0x00, 0x00, 0x00, 0x00, 0x00, 0x00,  // PS3 MAC - initially zeros (unpaired)
    0x00, 0x00, 0x00, 0x03, 0x50, 0x81, 0xd8, 0x01,
    0x8a, 0x13, 0x00, 0x00, 0x00, 0x00, 0x04, 0x00, 0x02, 0x02, 0x02, 0x02, 0x00, 0x00, 0x00, 0x04,
    0x04, 0x04, 0x04, 0x00, 0x00, 0x04, 0x00, 0x01, 0x02, 0x07, 0x00, 0x17, 0x00, 0x00, 0x00, 0x00,
    0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00
};

// Report 0xF7 (Calibration data) - First byte MUST be report ID
static uint8_t report_f7[DS3_FEATURE_REPORT_SIZE] = {
    0xf7, 0x02, 0x01, 0x02, 0xcb, 0x01, 0xef, 0xff, 0x14, 0x33, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
    0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
    0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
    0x05, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00
};

// Report 0xF8 (Status) - First byte MUST be report ID
static uint8_t report_f8[DS3_FEATURE_REPORT_SIZE] = {
    0xf8, 0x00, 0x01, 0x00, 0x00, 0x08, 0x00, 0x03, 0x01, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
    0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
    0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
    0x05, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00
};

static uint8_t report_ef[DS3_FEATURE_REPORT_SIZE] = {0xEF};  // First byte is report ID

// Report 0xF4 (Enable/Connection Control)
// This is what we send via SET_REPORT to enable the controller
// PS3 may query this via GET_REPORT to check our status
static uint8_t report_f4[DS3_FEATURE_REPORT_SIZE] = {
    0xF4, 0x42, 0x03, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
    0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
    0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
    0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00
};

// =================================================================
// Internal Pairing Functions
// =================================================================

// Parse MAC address string to bytes
static int parse_mac(const char* str, uint8_t* out) {
    unsigned int bytes[6];
    if (sscanf(str, "%02x:%02x:%02x:%02x:%02x:%02x",
               &bytes[0], &bytes[1], &bytes[2], &bytes[3], &bytes[4], &bytes[5]) != 6) {
        return -1;
    }
    for (int i = 0; i < 6; i++) out[i] = (uint8_t)bytes[i];
    return 0;
}

// Read local Bluetooth MAC address
static void ds3_read_local_bt_mac(void) {
    FILE* f = fopen("/sys/class/bluetooth/hci0/address", "r");
    if (!f) {
        debug_print(DBG_BT | DBG_WARN, "[DS3] Cannot open /sys/class/bluetooth/hci0/address - BT may not be ready");
        return;
    }
    
    char mac_buf[32] = {0};
    if (!fgets(mac_buf, sizeof(mac_buf), f)) {
        debug_print(DBG_WARN, "[DS3] Could not read BT MAC from sysfs");
        fclose(f);
        return;
    }
    fclose(f);
    
    // Remove trailing newline
    char* nl = strchr(mac_buf, '\n');
    if (nl) *nl = '\0';
    
    // Convert to uppercase for consistency
    for (char* p = mac_buf; *p; p++) {
        if (*p >= 'a' && *p <= 'f') *p -= 32;
    }
    
    // Parse and update report 0xF2 with our actual MAC
    // F2 format: bytes 4-9 are the device MAC
    uint8_t mac[6];
    if (parse_mac(mac_buf, mac) == 0) {
        report_f2[4] = mac[0];
        report_f2[5] = mac[1];
        report_f2[6] = mac[2];
        report_f2[7] = mac[3];
        report_f2[8] = mac[4];
        report_f2[9] = mac[5];
        
        strncpy(g_local_bt_mac, mac_buf, sizeof(g_local_bt_mac) - 1);
        debug_print(DBG_BT, "[DS3] ds3_init: F2 report MAC set to: %s", g_local_bt_mac);
    } else {
        debug_print(DBG_WARN, "[DS3] Failed to parse BT MAC: '%s'", mac_buf);
    }
}

// Load saved pairing configuration
static void ds3_load_pairing(void) {
    FILE* f = fopen(PAIRING_CONFIG_FILE, "r");
    if (!f) return;
    
    char line[256];
    while (fgets(line, sizeof(line), f)) {
        if (line[0] == '#' || line[0] == '\n') continue;
        
        char* eq = strchr(line, '=');
        if (!eq) continue;
        
        *eq = '\0';
        char* key = line;
        char* value = eq + 1;
        
        // Trim newline
        char* nl = strchr(value, '\n');
        if (nl) *nl = '\0';
        
        if (strcmp(key, "PS3_MAC") == 0) {
            strncpy(g_ps3_bt_mac, value, sizeof(g_ps3_bt_mac) - 1);
            
            // Update report 0xF5 with stored PS3 address
            uint8_t mac[6];
            if (parse_mac(g_ps3_bt_mac, mac) == 0) {
                report_f5[2] = mac[0];
                report_f5[3] = mac[1];
                report_f5[4] = mac[2];
                report_f5[5] = mac[3];
                report_f5[6] = mac[4];
                report_f5[7] = mac[5];
            }
        }
    }
    fclose(f);
}

// Save pairing configuration
void ds3_save_pairing(const char* ps3_addr) {
    // Create directory if needed
    mkdir(PAIRING_CONFIG_DIR, 0755);
    
    FILE* f = fopen(PAIRING_CONFIG_FILE, "w");
    if (!f) {
        debug_print(DBG_WARN | DBG_PAIRING, "[DS3] Could not save pairing to %s", PAIRING_CONFIG_FILE);
        return;
    }
    
    fprintf(f, "# RosettaPad Pairing Configuration\n");
    fprintf(f, "# Auto-generated during USB pairing\n");
    fprintf(f, "PS3_MAC=%s\n", ps3_addr);
    fprintf(f, "LOCAL_MAC=%s\n", g_local_bt_mac);
    
    fclose(f);
    
    strncpy(g_ps3_bt_mac, ps3_addr, sizeof(g_ps3_bt_mac) - 1);
    debug_print(DBG_PAIRING, "[DS3] Pairing saved to %s", PAIRING_CONFIG_FILE);
}

// =================================================================
// Public Functions
// =================================================================

void ds3_init(void) {
    // Read our local Bluetooth MAC
    ds3_read_local_bt_mac();
    debug_print(DBG_INIT, "[DS3] Local Bluetooth MAC: %s", g_local_bt_mac);
    
    // Load any saved pairing
    ds3_load_pairing();
    if (strlen(g_ps3_bt_mac) > 0) {
        debug_print(DBG_INIT | DBG_PAIRING, "[DS3] Loaded PS3 pairing: %s", g_ps3_bt_mac);
    } else {
        debug_print(DBG_INIT, "[DS3] No saved pairing found");
    }
    
    debug_print(DBG_INIT, "[DS3] Emulation layer initialized");
}

// Get the stored PS3 Bluetooth address (for BT mode)
const char* ds3_get_ps3_address(void) {
    return strlen(g_ps3_bt_mac) > 0 ? g_ps3_bt_mac : NULL;
}

// Get our local Bluetooth address
const char* ds3_get_local_address(void) {
    return g_local_bt_mac;
}

// Update F2 report with correct local Bluetooth MAC
void ds3_set_local_bt_mac(const uint8_t* mac) {
    // Update report F2 (bytes 4-9)
    report_f2[4] = mac[0];
    report_f2[5] = mac[1];
    report_f2[6] = mac[2];
    report_f2[7] = mac[3];
    report_f2[8] = mac[4];
    report_f2[9] = mac[5];
    
    // Update string representation
    snprintf(g_local_bt_mac, sizeof(g_local_bt_mac), 
             "%02X:%02X:%02X:%02X:%02X:%02X",
             mac[0], mac[1], mac[2], mac[3], mac[4], mac[5]);
    
    // Use DBG_BT so it shows with bt_all
    debug_print(DBG_BT, "[DS3] *** F2 report updated with MAC: %s ***", g_local_bt_mac);
}

const uint8_t* ds3_get_feature_report(uint8_t report_id, const char** out_name) {
    const char* name = "UNKNOWN";
    const uint8_t* data = NULL;
    
    switch (report_id) {
        case DS3_REPORT_CAPABILITIES:
            data = report_01;
            name = "Capabilities";
            break;
        case DS3_REPORT_BT_MAC:
            data = report_f2;
            name = "BT MAC";
            // CRITICAL DEBUG: Show what MAC we're reporting
            debug_print(DBG_BT | DBG_HANDSHAKE, 
                "[DS3] F2 MAC being sent: %02X:%02X:%02X:%02X:%02X:%02X",
                report_f2[4], report_f2[5], report_f2[6], 
                report_f2[7], report_f2[8], report_f2[9]);
            break;
        case DS3_REPORT_PAIRING:
            data = report_f5;
            name = "Pairing";
            break;
        case DS3_REPORT_CALIBRATION:
            data = report_f7;
            name = "Calibration";
            break;
        case DS3_REPORT_STATUS:
            data = report_f8;
            name = "Status";
            break;
        case DS3_REPORT_EF:
            data = report_ef;
            name = "EF Config";
            break;
        case 0xF4:  // Enable/Connection Control
            data = report_f4;
            name = "Enable (F4)";
            break;
    }
    
    if (out_name) *out_name = name;
    return data;
}

void ds3_handle_set_report(uint8_t report_id, const uint8_t* data, size_t len) {
    debug_print(DBG_REPORTS | DBG_HANDSHAKE, "[DS3] SET_REPORT 0x%02X (%zu bytes)", report_id, len);
    debug_hex(DBG_REPORTS, "SET_REPORT data", data, len);
    
    if (report_id == DS3_REPORT_EF) {
        // PS3 sends SET_REPORT 0xEF during init
        // Store the data and prepend 0xEF for GET_REPORT response
        report_ef[0] = 0xEF;
        size_t copy_len = (len > DS3_FEATURE_REPORT_SIZE - 1) ? DS3_FEATURE_REPORT_SIZE - 1 : len;
        memcpy(&report_ef[1], data, copy_len);
        debug_print(DBG_HANDSHAKE, "[DS3] Config 0xEF stored (%zu bytes)", len);
    }
    else if (report_id == 0xF4 && len >= 2) {
        // PS3 sends SET_REPORT 0xF4 to enable controller features
        // Format: 0x42, flags, 0x00, 0x00
        // flags: bit1=rumble, bit3=LEDs/motion
        // 0x42 0x0a = enable rumble + motion
        // 0x42 0x0c = enable all features
        if (data[0] == 0x42) {
            uint8_t flags = data[1];
            debug_print(DBG_HANDSHAKE | DBG_BT, 
                "[DS3] *** PS3 ENABLE: flags=0x%02X (rumble=%d, motion=%d) ***",
                flags, (flags & 0x02) ? 1 : 0, (flags & 0x08) ? 1 : 0);
            
            // Store in our F4 report for any GET_REPORT queries
            report_f4[1] = 0x42;
            report_f4[2] = flags;
            
            // Signal to Bluetooth module that PS3 has enabled us
#ifdef ENABLE_BLUETOOTH
            extern void bt_hid_set_ps3_enabled(int enabled);
            bt_hid_set_ps3_enabled(1);
#endif
        }
    }
    else if (report_id == DS3_REPORT_PAIRING && len >= 8) {
        // PS3 sends SET_REPORT 0xF5 with its Bluetooth MAC address
        // Format: [01 00] [6-byte MAC]
        // First 2 bytes are header, MAC starts at byte 2
        char ps3_addr[18];
        snprintf(ps3_addr, sizeof(ps3_addr), "%02X:%02X:%02X:%02X:%02X:%02X",
                 data[2], data[3], data[4], data[5], data[6], data[7]);
        
        debug_print(DBG_HANDSHAKE | DBG_PAIRING, 
                    "[DS3] *** PS3 PAIRING: Host MAC = %s ***", ps3_addr);
        
        // Store in our F5 response (bytes 2-7 are the host address)
        report_f5[2] = data[2];
        report_f5[3] = data[3];
        report_f5[4] = data[4];
        report_f5[5] = data[5];
        report_f5[6] = data[6];
        report_f5[7] = data[7];
        
        // Save to config file for Bluetooth mode
        ds3_save_pairing(ps3_addr);
        
        // Notify Bluetooth module if enabled
#ifdef ENABLE_BLUETOOTH
        bt_hid_store_ps3_mac(&data[2]);  // Pass MAC starting at correct offset
        debug_print(DBG_PAIRING, "[DS3] Notified Bluetooth module of PS3 MAC");
#endif
        
        // Signal that pairing is complete - main loop can switch to BT mode
        g_pairing_complete = 1;
        debug_print(DBG_PAIRING, "[DS3] Pairing complete - ready to switch to Bluetooth mode");
    }
    else if (report_id == 0x01) {
        // Output report (rumble/LED init) - PS3 sends this before 0xF7
        debug_print(DBG_HANDSHAKE, "[DS3] Output report 0x01 received (rumble/LED init)");
    }
}

uint8_t ds3_convert_dpad(uint8_t hat_value) {
    // Convert hat switch (0-7 clockwise from up, 8=center) to DS3 d-pad mask
    switch (hat_value & 0x0F) {
        case 0: return DS3_BTN_DPAD_UP;                              // Up
        case 1: return DS3_BTN_DPAD_UP | DS3_BTN_DPAD_RIGHT;        // Up-Right
        case 2: return DS3_BTN_DPAD_RIGHT;                          // Right
        case 3: return DS3_BTN_DPAD_DOWN | DS3_BTN_DPAD_RIGHT;      // Down-Right
        case 4: return DS3_BTN_DPAD_DOWN;                           // Down
        case 5: return DS3_BTN_DPAD_DOWN | DS3_BTN_DPAD_LEFT;       // Down-Left
        case 6: return DS3_BTN_DPAD_LEFT;                           // Left
        case 7: return DS3_BTN_DPAD_UP | DS3_BTN_DPAD_LEFT;         // Up-Left
        default: return 0;                                          // Center/released
    }
}

void ds3_update_report(
    uint8_t buttons1, uint8_t buttons2, uint8_t ps_button,
    uint8_t lx, uint8_t ly, uint8_t rx, uint8_t ry,
    uint8_t l2, uint8_t r2,
    uint8_t triangle_p, uint8_t circle_p, uint8_t cross_p, uint8_t square_p)
{
    pthread_mutex_lock(&g_report_mutex);
    
    g_ds3_report[DS3_OFF_BUTTONS1] = buttons1;
    g_ds3_report[DS3_OFF_BUTTONS2] = buttons2;
    g_ds3_report[DS3_OFF_PS_BUTTON] = ps_button;
    
    g_ds3_report[DS3_OFF_LX] = lx;
    g_ds3_report[DS3_OFF_LY] = ly;
    g_ds3_report[DS3_OFF_RX] = rx;
    g_ds3_report[DS3_OFF_RY] = ry;
    
    g_ds3_report[DS3_OFF_L2_PRESSURE] = l2;
    g_ds3_report[DS3_OFF_R2_PRESSURE] = r2;
    
    g_ds3_report[DS3_OFF_TRIANGLE_P] = triangle_p;
    g_ds3_report[DS3_OFF_CIRCLE_P] = circle_p;
    g_ds3_report[DS3_OFF_CROSS_P] = cross_p;
    g_ds3_report[DS3_OFF_SQUARE_P] = square_p;
    
    pthread_mutex_unlock(&g_report_mutex);
}

void ds3_update_motion(int16_t accel_x, int16_t accel_y, int16_t accel_z, int16_t gyro_z) {
    pthread_mutex_lock(&g_report_mutex);
    
    // DS3 motion data is little-endian 16-bit values
    // Accelerometer centered around ~512 (0x200), gyro around ~498
    g_ds3_report[DS3_OFF_ACCEL_X]     = accel_x & 0xFF;
    g_ds3_report[DS3_OFF_ACCEL_X + 1] = (accel_x >> 8) & 0xFF;
    
    g_ds3_report[DS3_OFF_ACCEL_Y]     = accel_y & 0xFF;
    g_ds3_report[DS3_OFF_ACCEL_Y + 1] = (accel_y >> 8) & 0xFF;
    
    g_ds3_report[DS3_OFF_ACCEL_Z]     = accel_z & 0xFF;
    g_ds3_report[DS3_OFF_ACCEL_Z + 1] = (accel_z >> 8) & 0xFF;
    
    g_ds3_report[DS3_OFF_GYRO_Z]     = gyro_z & 0xFF;
    g_ds3_report[DS3_OFF_GYRO_Z + 1] = (gyro_z >> 8) & 0xFF;
    
    pthread_mutex_unlock(&g_report_mutex);
}

void ds3_copy_report(uint8_t* out_buf) {
    pthread_mutex_lock(&g_report_mutex);
    memcpy(out_buf, g_ds3_report, DS3_INPUT_REPORT_SIZE);
    pthread_mutex_unlock(&g_report_mutex);
}

void ds3_copy_bt_report(uint8_t* out_buf) {
    pthread_mutex_lock(&g_report_mutex);
    
    // BT format: 0xA1 header + 49 bytes of report data
    out_buf[0] = 0xA1;  // HID DATA | INPUT
    memcpy(&out_buf[1], g_ds3_report, DS3_INPUT_REPORT_SIZE);
    
    // Fix connection status byte - report we're on Bluetooth, not USB
    // Byte 31 in USB report = byte 32 in BT report (after 0xA1 header)
    int rumble_active = (g_rumble_right > 0 || g_rumble_left > 0);
    out_buf[1 + DS3_OFF_CONNECTION] = rumble_active ? DS3_CONN_BT_RUMBLE : DS3_CONN_BT;
    
    // Fix plugged status - report unplugged for BT
    out_buf[1 + DS3_OFF_BATTERY] = DS3_STATUS_UNPLUGGED;  // 0x03 = Bluetooth
    
    // Fix charge byte - must be valid battery level for BT (not charging codes)
    // If currently showing charging (0xEE/0xEF), convert to battery level
    uint8_t charge = out_buf[1 + DS3_OFF_CHARGE];
    if (charge >= 0xEE) {
        out_buf[1 + DS3_OFF_CHARGE] = DS3_BATTERY_FULL;  // Show full when was charging/charged
    }
    
    // Keep original bytes 36-39 from real DS3 capture (0x33 0x04 0x77 0x01)
    // Don't overwrite these - PS3 may validate them
    
    // Motion sensor bytes need to be byte-swapped for Bluetooth
    // USB: little-endian, BT: big-endian (based on hid-sony.c swaps)
    // Accel X at offset 40-41 (report offset + 1 for 0xA1 header)
    uint8_t tmp;
    tmp = out_buf[1 + 40]; out_buf[1 + 40] = out_buf[1 + 41]; out_buf[1 + 41] = tmp;
    tmp = out_buf[1 + 42]; out_buf[1 + 42] = out_buf[1 + 43]; out_buf[1 + 43] = tmp;
    tmp = out_buf[1 + 44]; out_buf[1 + 44] = out_buf[1 + 45]; out_buf[1 + 45] = tmp;
    tmp = out_buf[1 + 46]; out_buf[1 + 46] = out_buf[1 + 47]; out_buf[1 + 47] = tmp;
    
    pthread_mutex_unlock(&g_report_mutex);
}

void ds3_update_battery(uint8_t plugged, uint8_t battery, uint8_t connection) {
    pthread_mutex_lock(&g_report_mutex);
    g_ds3_report[DS3_OFF_BATTERY] = plugged;
    g_ds3_report[DS3_OFF_CHARGE] = battery;
    g_ds3_report[DS3_OFF_CONNECTION] = connection;
    pthread_mutex_unlock(&g_report_mutex);
}

void ds3_update_battery_from_dualsense(uint8_t ds_battery_level, int ds_charging) {
    uint8_t battery_status;
    
    if (ds_charging) {
        // When charging via USB, show charging status
        if (ds_battery_level >= 100) {
            battery_status = DS3_BATTERY_CHARGED;   // 0xEF - fully charged
        } else {
            battery_status = DS3_BATTERY_CHARGING;  // 0xEE - charging
        }
    } else {
        // Convert DualSense percentage to DS3 battery level
        // DS3 uses: 0x00=0%, 0x01=1%, 0x02=25%, 0x03=50%, 0x04=75%, 0x05=100%
        if (ds_battery_level <= 5) {
            battery_status = DS3_BATTERY_SHUTDOWN;  // 0x00
        } else if (ds_battery_level <= 15) {
            battery_status = DS3_BATTERY_DYING;     // 0x01
        } else if (ds_battery_level <= 35) {
            battery_status = DS3_BATTERY_LOW;       // 0x02
        } else if (ds_battery_level <= 60) {
            battery_status = DS3_BATTERY_MEDIUM;    // 0x03
        } else if (ds_battery_level <= 85) {
            battery_status = DS3_BATTERY_HIGH;      // 0x04
        } else {
            battery_status = DS3_BATTERY_FULL;      // 0x05
        }
    }
    
    // We're always USB-connected to PS3, check if rumble is active
    uint8_t connection;
    pthread_mutex_lock(&g_rumble_mutex);
    int rumble_active = (g_rumble_right > 0 || g_rumble_left > 0);
    pthread_mutex_unlock(&g_rumble_mutex);
    
    connection = rumble_active ? DS3_CONN_USB_RUMBLE : DS3_CONN_USB;
    
    ds3_update_battery(DS3_STATUS_PLUGGED, battery_status, connection);
}