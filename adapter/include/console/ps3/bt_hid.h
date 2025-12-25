/*
 * RosettaPad - PS3 Bluetooth HID Layer
 * =====================================
 * 
 * Handles L2CAP HID connections for motion data and PS3 wake functionality.
 * 
 * The PS3 requires motion control data (SIXAXIS) over Bluetooth,
 * even when the controller is connected via USB for primary input.
 */

#ifndef ROSETTAPAD_PS3_BT_HID_H
#define ROSETTAPAD_PS3_BT_HID_H

#include <stdint.h>
#include <bluetooth/bluetooth.h>

/* ============================================================================
 * CONFIGURATION
 * ============================================================================ */

/* L2CAP PSM values for HID */
#define L2CAP_PSM_HID_CONTROL   0x0011
#define L2CAP_PSM_HID_INTERRUPT 0x0013

/* Bluetooth HID transaction types */
#define BT_HIDP_DATA_RTYPE_INPUT    0xA1
#define BT_HIDP_DATA_RTYPE_OUTPUT   0xA2
#define BT_HIDP_DATA_RTYPE_FEATURE  0xA3

/* Report sizes over Bluetooth */
#define DS3_BT_INPUT_REPORT_SIZE    50
#define DS3_BT_OUTPUT_REPORT_SIZE   49

/* PS3 MAC file path */
#define PS3_MAC_FILE    "/tmp/rosettapad/ps3_mac"

/* ============================================================================
 * CONNECTION STATE
 * ============================================================================ */

typedef enum {
    BT_STATE_DISCONNECTED = 0,
    BT_STATE_SCANNING,
    BT_STATE_CONNECTING,
    BT_STATE_CONTROL_CONNECTED,
    BT_STATE_INTERRUPT_CONNECTED,
    BT_STATE_READY,
    BT_STATE_ENABLED,      /* PS3 sent 0xF4 enable command */
    BT_STATE_ERROR
} bt_state_t;

/* ============================================================================
 * CONTEXT
 * ============================================================================ */

typedef struct {
    bt_state_t state;
    
    int ctrl_sock;
    int intr_sock;
    
    bdaddr_t local_addr;
    bdaddr_t ps3_addr;
    int ps3_addr_valid;
    
    uint64_t connect_time;
    uint64_t last_send_time;
    
    uint32_t packets_sent;
    uint32_t packets_dropped;
    uint32_t reconnect_count;
} ps3_bt_ctx_t;

extern ps3_bt_ctx_t g_ps3_bt_ctx;

/* ============================================================================
 * FUNCTIONS
 * ============================================================================ */

/**
 * Initialize PS3 Bluetooth HID subsystem.
 * @return 0 on success, -1 on failure
 */
int ps3_bt_init(void);

/**
 * Scan for PS3 console.
 * @param timeout_sec Scan duration in seconds
 * @return 0 if PS3 found, -1 if not
 */
int ps3_bt_scan(int timeout_sec);

/**
 * Set PS3 address manually.
 * @param mac MAC address string "XX:XX:XX:XX:XX:XX"
 * @return 0 on success, -1 on invalid format
 */
int ps3_bt_set_addr(const char* mac);

/**
 * Get PS3 address as string.
 * @param out_mac Buffer for MAC string (18 bytes min)
 * @return 0 on success, -1 if no address set
 */
int ps3_bt_get_addr(char* out_mac);

/**
 * Check if PS3 address is set.
 */
int ps3_bt_has_addr(void);

/**
 * Save PS3 MAC to file.
 */
int ps3_bt_save_addr(void);

/**
 * Load PS3 MAC from file.
 */
int ps3_bt_load_addr(void);

/**
 * Connect to PS3 via Bluetooth.
 * @return 0 on success, -1 on failure
 */
int ps3_bt_connect(void);

/**
 * Disconnect from PS3.
 */
void ps3_bt_disconnect(void);

/**
 * Check if Bluetooth is enabled (PS3 sent 0xF4).
 */
int ps3_bt_is_enabled(void);

/**
 * Get current connection state.
 */
bt_state_t ps3_bt_get_state(void);

/**
 * Get state name for logging.
 */
const char* ps3_bt_state_str(bt_state_t state);

/**
 * Attempt to wake PS3 from standby.
 * @return 0 on success, -1 on failure
 */
int ps3_bt_wake(void);

/**
 * Get local Bluetooth MAC address.
 * @param out_mac 6-byte buffer for MAC
 * @return 0 on success, -1 if not ready
 */
int ps3_bt_get_local_addr(uint8_t* out_mac);

/* ============================================================================
 * THREAD FUNCTIONS
 * ============================================================================ */

/**
 * Bluetooth management thread.
 * Handles scanning, connection, and control channel.
 */
void* ps3_bt_thread(void* arg);

/**
 * Motion data sending thread.
 * Sends input reports when Bluetooth is enabled.
 */
void* ps3_bt_motion_thread(void* arg);

#endif /* ROSETTAPAD_PS3_BT_HID_H */