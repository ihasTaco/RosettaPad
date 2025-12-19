/*
 * RosettaPad - Bluetooth HID Interface for PS3
 * 
 * This module implements Bluetooth connectivity to PS3 WITHOUT requiring
 * MAC address spoofing. The flow works like a real DS3:
 * 
 * 1. Connect to PS3 via USB (already works)
 * 2. PS3 sends SET_REPORT 0xF5 with its Bluetooth MAC
 * 3. Pi stores this MAC address
 * 4. Later, Pi initiates Bluetooth connection to stored PS3 MAC
 * 5. PS3 accepts connection because it registered Pi's MAC during USB pairing
 * 
 * The key insight: When the Pi connects via USB as a DS3, the PS3 records
 * the Pi's Bluetooth MAC from report 0xF2. No spoofing needed!
 */

#ifndef ROSETTAPAD_BT_HID_H
#define ROSETTAPAD_BT_HID_H

#include <stdint.h>

// =================================================================
// Bluetooth HID Constants
// =================================================================

// L2CAP PSM (Protocol Service Multiplexer) channels
#define L2CAP_PSM_HID_CONTROL   0x0011  // HID Control channel
#define L2CAP_PSM_HID_INTERRUPT 0x0013  // HID Interrupt channel

// Bluetooth device class for gamepad
// Class: Peripheral (0x05), Subclass: Gamepad (0x08)
#define BT_CLASS_GAMEPAD        0x002508

// DS3 Bluetooth report sizes
#define DS3_BT_INPUT_REPORT_SIZE    50  // 1 byte header + 49 bytes data
#define DS3_BT_OUTPUT_REPORT_SIZE   49

// HID transaction types (for control channel)
#define HID_TRANS_HANDSHAKE     0x00
#define HID_TRANS_SET_PROTOCOL  0x70
#define HID_TRANS_GET_PROTOCOL  0x30
#define HID_TRANS_SET_REPORT    0x50
#define HID_TRANS_GET_REPORT    0x40
#define HID_TRANS_DATA          0xA0

// HID report types
#define HID_REPORT_INPUT        0x01
#define HID_REPORT_OUTPUT       0x02
#define HID_REPORT_FEATURE      0x03

// Handshake responses
#define HID_HANDSHAKE_SUCCESS   0x00
#define HID_HANDSHAKE_NOT_READY 0x01
#define HID_HANDSHAKE_ERR_INV_REPORT_ID 0x02
#define HID_HANDSHAKE_ERR_UNSUPP_REQ    0x03
#define HID_HANDSHAKE_ERR_INV_PARAM     0x04
#define HID_HANDSHAKE_ERR_UNKNOWN       0x0E
#define HID_HANDSHAKE_ERR_FATAL         0x0F

// =================================================================
// Bluetooth HID State
// =================================================================

typedef enum {
    BT_STATE_IDLE,              // Not initialized
    BT_STATE_WAITING_FOR_MAC,   // Waiting for PS3 MAC from USB pairing
    BT_STATE_READY,             // Have PS3 MAC, ready to connect
    BT_STATE_CONNECTING,        // Connecting to PS3
    BT_STATE_CONNECTED,         // Connected and sending reports
    BT_STATE_ERROR              // Error state
} bt_state_t;

// =================================================================
// Pairing/Address Storage
// =================================================================

// Path to store paired PS3 address
#define BT_PAIRING_FILE "/etc/rosettapad/pairing.conf"

// =================================================================
// Public Functions - Initialization
// =================================================================

/**
 * Initialize Bluetooth HID subsystem
 * @return 0 on success, -1 on failure
 */
int bt_hid_init(void);

/**
 * Cleanup Bluetooth HID subsystem
 */
void bt_hid_cleanup(void);

// =================================================================
// Public Functions - Pairing
// =================================================================

/**
 * Store PS3 Bluetooth address (called when PS3 sends SET_REPORT 0xF5 via USB)
 * This is how the PS3 tells us its Bluetooth MAC during USB connection.
 * @param ps3_mac 6-byte MAC address from SET_REPORT 0xF5
 */
void bt_hid_store_ps3_mac(const uint8_t* ps3_mac);

/**
 * Get our local Bluetooth MAC address (what PS3 sees as controller MAC)
 * @param out_mac Buffer for 6-byte MAC address
 * @return 0 on success, -1 on failure
 */
int bt_hid_get_local_mac(uint8_t* out_mac);

/**
 * Check if we have a stored PS3 MAC address
 * @return 1 if paired, 0 if not
 */
int bt_hid_is_paired(void);

/**
 * Get the stored PS3 MAC address
 * @param out_mac Buffer for 6-byte MAC address
 * @return 0 on success, -1 if not paired
 */
int bt_hid_get_ps3_mac(uint8_t* out_mac);

/**
 * Clear stored pairing information
 */
void bt_hid_clear_pairing(void);

/**
 * Load pairing from persistent storage
 * @return 0 on success, -1 if no stored pairing
 */
int bt_hid_load_pairing(void);

/**
 * Save pairing to persistent storage
 * @return 0 on success, -1 on failure
 */
int bt_hid_save_pairing(void);

// =================================================================
// Public Functions - Connection
// =================================================================

/**
 * Connect to PS3 over Bluetooth
 * Uses the stored PS3 MAC address
 * @return 0 on success, -1 on failure
 */
int bt_hid_connect(void);

/**
 * Disconnect from PS3
 */
void bt_hid_disconnect(void);

/**
 * Check if connected to PS3
 * @return 1 if connected, 0 if not
 */
int bt_hid_is_connected(void);

/**
 * Get current connection state
 * @return Current bt_state_t value
 */
bt_state_t bt_hid_get_state(void);

/**
 * Get state as string
 * @return State name
 */
const char* bt_hid_state_str(void);

// =================================================================
// Public Functions - Data Transfer
// =================================================================

/**
 * Send DS3 input report to PS3
 * @param report 49-byte DS3 input report
 * @return 0 on success, 1 if would block, -1 on failure
 */
int bt_hid_send_input_report(const uint8_t* report);

/**
 * Send DS3 input report to PS3 (blocking version)
 * Uses poll() to wait for socket to be ready
 * @param report 49-byte DS3 input report
 * @return 0 on success, -1 on failure
 */
int bt_hid_send_input_report_blocking(const uint8_t* report);

/**
 * Process incoming data on control channel
 * @return 0 on success, -1 on failure/disconnect
 */
int bt_hid_process_control(void);

/**
 * Process incoming data on interrupt channel
 * @return 0 on success, -1 on failure/disconnect
 */
int bt_hid_process_interrupt(void);

// =================================================================
// Public Functions - Threads
// =================================================================

/**
 * Bluetooth HID output thread (sends reports to PS3)
 * @param arg Unused
 * @return NULL
 */
void* bt_hid_output_thread(void* arg);

/**
 * Bluetooth HID input thread (receives from PS3)
 * @param arg Unused
 * @return NULL
 */
void* bt_hid_input_thread(void* arg);

// =================================================================
// Public Functions - Setup
// =================================================================

/**
 * Set Bluetooth device class to gamepad
 * Should be called once during init
 * @return 0 on success, -1 on failure
 */
int bt_hid_set_device_class(void);

/**
 * Set Bluetooth device name
 * @param name Device name (will appear as controller name)
 * @return 0 on success, -1 on failure
 */
int bt_hid_set_device_name(const char* name);

/**
 * Make device discoverable (for debugging)
 * @param enable 1 to enable, 0 to disable
 * @return 0 on success, -1 on failure
 */
int bt_hid_set_discoverable(int enable);

// =================================================================
// Public Functions - Utility
// =================================================================

/**
 * Format MAC address as string
 * @param mac 6-byte MAC address
 * @param out Buffer for string (at least 18 bytes)
 */
void bt_hid_mac_to_str(const uint8_t* mac, char* out);

/**
 * Parse MAC address from string
 * @param str MAC string (XX:XX:XX:XX:XX:XX)
 * @param out Buffer for 6-byte MAC
 * @return 0 on success, -1 on invalid format
 */
int bt_hid_str_to_mac(const char* str, uint8_t* out);

#endif // ROSETTAPAD_BT_HID_H