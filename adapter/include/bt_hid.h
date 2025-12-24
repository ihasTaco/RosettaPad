/*
 * RosettaPad - Bluetooth HID Layer for PS3
 * Handles L2CAP HID connections for motion data and wake functionality
 * 
 * Features:
 * - Auto-discovery of PS3 via Bluetooth scan
 * - Automatic connection after USB is established
 * - No manual MAC configuration required
 */

#ifndef ROSETTAPAD_BT_HID_H
#define ROSETTAPAD_BT_HID_H

#include <stdint.h>
#include <bluetooth/bluetooth.h>

// =================================================================
// PS3 Bluetooth HID Configuration
// =================================================================

// L2CAP PSM values
#define L2CAP_PSM_HID_CONTROL   0x0011
#define L2CAP_PSM_HID_INTERRUPT 0x0013

// Bluetooth HID transaction types
#define BT_HIDP_DATA_RTYPE_INPUT    0xA1
#define BT_HIDP_DATA_RTYPE_OUTPUT   0xA2
#define BT_HIDP_DATA_RTYPE_FEATURE  0xA3

// Report sizes over Bluetooth
#define DS3_BT_INPUT_REPORT_SIZE    50
#define DS3_BT_OUTPUT_REPORT_SIZE   49

// =================================================================
// Connection State
// =================================================================
typedef enum {
    BT_STATE_DISCONNECTED = 0,
    BT_STATE_SCANNING,             // Scanning for PS3
    BT_STATE_CONNECTING,
    BT_STATE_CONTROL_CONNECTED,
    BT_STATE_INTERRUPT_CONNECTED,
    BT_STATE_READY,
    BT_STATE_ENABLED,              // PS3 sent 0xF4 enable
    BT_STATE_ERROR
} bt_state_t;

// =================================================================
// Bluetooth HID Context
// =================================================================
typedef struct {
    bt_state_t state;
    
    int ctrl_sock;
    int intr_sock;
    
    bdaddr_t local_addr;
    bdaddr_t ps3_addr;
    int ps3_addr_valid;            // 1 if we have a valid PS3 address
    
    uint64_t connect_time;
    uint64_t last_send_time;
    
    uint32_t packets_sent;
    uint32_t packets_dropped;
    uint32_t reconnect_count;
} bt_hid_ctx_t;

extern bt_hid_ctx_t g_bt_ctx;

// =================================================================
// Functions
// =================================================================

/**
 * Initialize Bluetooth HID subsystem
 * Configures the adapter, no PS3 MAC required
 * @return 0 on success, -1 on failure
 */
int bt_hid_init(void);

/**
 * Scan for PS3 console
 * Looks for devices with Sony OUI or "PLAYSTATION" name
 * @param timeout_sec How long to scan (default 8 seconds)
 * @return 0 if PS3 found, -1 if not found
 */
int bt_hid_scan_for_ps3(int timeout_sec);

/**
 * Set PS3 address manually (if known)
 * @param mac MAC address string "XX:XX:XX:XX:XX:XX"
 * @return 0 on success, -1 on invalid format
 */
int bt_hid_set_ps3_addr(const char* mac);

/**
 * Get PS3 address as string
 * @param out_mac Buffer for MAC string (18 bytes min)
 * @return 0 on success, -1 if no address set
 */
int bt_hid_get_ps3_addr(char* out_mac);

/**
 * Save PS3 MAC to file for persistence
 * @return 0 on success, -1 on failure
 */
int bt_hid_save_ps3_addr(void);

/**
 * Load PS3 MAC from file
 * @return 0 on success, -1 if not found
 */
int bt_hid_load_ps3_addr(void);

/**
 * Connect to PS3 via Bluetooth
 * Will scan for PS3 first if address not known
 * @return 0 on success, -1 on failure
 */
int bt_hid_connect(void);

/**
 * Disconnect from PS3
 */
void bt_hid_disconnect(void);

/**
 * Process incoming data on control channel
 * @return 0 on success, -1 on error
 */
int bt_hid_process_control(void);

/**
 * Send input report over interrupt channel
 * @return 0 on success, -1 on error
 */
int bt_hid_send_input(void);

/**
 * Check if Bluetooth connection is enabled
 * @return 1 if PS3 has sent 0xF4 enable, 0 otherwise
 */
int bt_hid_is_enabled(void);

/**
 * Check if we have a valid PS3 address
 * @return 1 if address is set, 0 otherwise
 */
int bt_hid_has_ps3_addr(void);

/**
 * Get current connection state
 */
bt_state_t bt_hid_get_state(void);

/**
 * Get state as string for logging
 */
const char* bt_hid_state_str(bt_state_t state);

/**
 * Attempt to wake PS3 from standby
 * @return 0 on success, -1 on failure
 */
int bt_hid_wake_ps3(void);

/**
 * Configure Bluetooth adapter
 * @return 0 on success, -1 on failure
 */
int bt_configure_adapter(void);

/**
 * Get local Bluetooth MAC address (for Report 0xF5)
 * This is the address PS3 will use to connect back to us
 * @param out_mac 6-byte buffer for MAC in network order
 * @return 0 on success, -1 if adapter not ready
 */
int bt_hid_get_local_addr(uint8_t* out_mac);

/**
 * Bluetooth management thread
 * Handles scanning, connection, and control channel
 */
void* bt_hid_thread(void* arg);

/**
 * Motion/input sending thread
 * Sends input reports when enabled
 */
void* bt_motion_thread(void* arg);

#endif // ROSETTAPAD_BT_HID_H