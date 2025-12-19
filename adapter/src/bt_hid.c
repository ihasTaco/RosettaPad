/*
 * RosettaPad - Bluetooth HID Interface for PS3
 * 
 * NO MAC SPOOFING REQUIRED!
 * 
 * This works because during USB connection:
 * 1. PS3 reads our Bluetooth MAC via GET_REPORT 0xF2
 * 2. PS3 sends its MAC via SET_REPORT 0xF5
 * 3. PS3 registers our MAC as a paired controller
 * 
 * Later, we connect to PS3 via Bluetooth using the stored MAC.
 * PS3 accepts because it already knows our MAC from USB pairing.
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <errno.h>
#include <fcntl.h>
#include <poll.h>
#include <pthread.h>
#include <sys/socket.h>
#include <sys/ioctl.h>
#include <sys/stat.h>
#include <linux/sockios.h>  // For SIOCOUTQ

#include <bluetooth/bluetooth.h>
#include <bluetooth/l2cap.h>
#include <bluetooth/hci.h>
#include <bluetooth/hci_lib.h>

#include "common.h"
#include "ds3.h"
#include "bt_hid.h"
#include "debug.h"

// =================================================================
// BT_POWER Socket Option
// =================================================================
// This option controls power management of the ACL link.
// When force_active=1, BlueZ exits SNIFF mode before sending data,
// which is critical for low-latency HID gaming input.
//
// Note: struct bt_power and related constants should be in
// <bluetooth/bluetooth.h> on modern systems. We only define
// fallbacks for older headers that may be missing them.

// =================================================================
// Internal State
// =================================================================

typedef struct {
    bt_state_t state;
    
    // Sockets
    int control_sock;       // L2CAP PSM 0x11
    int interrupt_sock;     // L2CAP PSM 0x13
    
    // Addresses
    uint8_t ps3_mac[6];     // PS3's Bluetooth MAC (received via USB SET_REPORT 0xF5)
    uint8_t local_mac[6];   // Our Bluetooth MAC
    int has_ps3_mac;        // Have we received PS3's MAC?
    
    // HCI device
    int hci_dev_id;
    int hci_sock;
    
    // Handshake tracking
    int handshake_complete; // PS3 has sent at least one control message
    int f4_acknowledged;    // PS3 has acknowledged our F4 enable report
    int ps3_enabled;        // PS3 has sent SET_REPORT F4 to enable us (controller fully active)
    int reports_sent;       // Number of reports sent (for initial delay)
    
    // Thread sync
    pthread_mutex_t mutex;
    
} bt_internal_state_t;

static bt_internal_state_t bt_state = {
    .state = BT_STATE_IDLE,
    .control_sock = -1,
    .interrupt_sock = -1,
    .has_ps3_mac = 0,
    .hci_dev_id = -1,
    .hci_sock = -1,
    .handshake_complete = 0,
    .f4_acknowledged = 0,
    .ps3_enabled = 0,
    .reports_sent = 0,
    .mutex = PTHREAD_MUTEX_INITIALIZER
};

// =================================================================
// Utility Functions
// =================================================================

void bt_hid_mac_to_str(const uint8_t* mac, char* out) {
    sprintf(out, "%02X:%02X:%02X:%02X:%02X:%02X",
            mac[0], mac[1], mac[2], mac[3], mac[4], mac[5]);
}

int bt_hid_str_to_mac(const char* str, uint8_t* out) {
    unsigned int tmp[6];
    if (sscanf(str, "%02X:%02X:%02X:%02X:%02X:%02X",
               &tmp[0], &tmp[1], &tmp[2], &tmp[3], &tmp[4], &tmp[5]) != 6) {
        return -1;
    }
    for (int i = 0; i < 6; i++) {
        out[i] = (uint8_t)tmp[i];
    }
    return 0;
}

// =================================================================
// Initialization
// =================================================================

int bt_hid_init(void) {
    debug_print(DBG_INIT | DBG_BT, "[BT] Initializing Bluetooth HID...");
    
    pthread_mutex_lock(&bt_state.mutex);
    
    // Find Bluetooth adapter
    bt_state.hci_dev_id = hci_get_route(NULL);
    if (bt_state.hci_dev_id < 0) {
        debug_print(DBG_ERROR, "[BT] No Bluetooth adapter found!");
        pthread_mutex_unlock(&bt_state.mutex);
        return -1;
    }
    
    // Open HCI socket
    bt_state.hci_sock = hci_open_dev(bt_state.hci_dev_id);
    if (bt_state.hci_sock < 0) {
        debug_print(DBG_ERROR, "[BT] Failed to open HCI device: %s", strerror(errno));
        pthread_mutex_unlock(&bt_state.mutex);
        return -1;
    }
    
    // Get local MAC address
    bdaddr_t local_bdaddr;
    if (hci_read_bd_addr(bt_state.hci_sock, &local_bdaddr, 1000) < 0) {
        debug_print(DBG_ERROR, "[BT] Failed to read local address: %s", strerror(errno));
        hci_close_dev(bt_state.hci_sock);
        pthread_mutex_unlock(&bt_state.mutex);
        return -1;
    }
    
    // Convert bdaddr to our format (reverse byte order)
    for (int i = 0; i < 6; i++) {
        bt_state.local_mac[i] = local_bdaddr.b[5 - i];
    }
    
    char mac_str[18];
    bt_hid_mac_to_str(bt_state.local_mac, mac_str);
    debug_print(DBG_INIT | DBG_BT, "[BT] Local Bluetooth MAC: %s", mac_str);
    
    // Try to load saved pairing
    if (bt_hid_load_pairing() == 0) {
        bt_state.state = BT_STATE_READY;
        bt_hid_mac_to_str(bt_state.ps3_mac, mac_str);
        debug_print(DBG_INIT | DBG_PAIRING, "[BT] Loaded paired PS3 MAC: %s", mac_str);
    } else {
        bt_state.state = BT_STATE_WAITING_FOR_MAC;
        debug_print(DBG_INIT | DBG_PAIRING, "[BT] No saved pairing - waiting for USB pairing");
    }
    
    pthread_mutex_unlock(&bt_state.mutex);
    
    // Set device class to gamepad
    bt_hid_set_device_class();
    
    // Set device name
    bt_hid_set_device_name("PLAYSTATION(R)3 Controller");
    
    debug_print(DBG_INIT | DBG_BT, "[BT] Bluetooth HID initialized");
    return 0;
}

void bt_hid_cleanup(void) {
    debug_print(DBG_BT, "[BT] Cleaning up...");
    
    bt_hid_disconnect();
    
    pthread_mutex_lock(&bt_state.mutex);
    
    if (bt_state.hci_sock >= 0) {
        hci_close_dev(bt_state.hci_sock);
        bt_state.hci_sock = -1;
    }
    
    bt_state.state = BT_STATE_IDLE;
    
    pthread_mutex_unlock(&bt_state.mutex);
}

// =================================================================
// Pairing Functions
// =================================================================

void bt_hid_store_ps3_mac(const uint8_t* ps3_mac) {
    pthread_mutex_lock(&bt_state.mutex);
    
    memcpy(bt_state.ps3_mac, ps3_mac, 6);
    bt_state.has_ps3_mac = 1;
    
    char mac_str[18];
    bt_hid_mac_to_str(ps3_mac, mac_str);
    debug_print(DBG_PAIRING | DBG_BT, "[BT] Stored PS3 MAC from USB pairing: %s", mac_str);
    
    // Save to persistent storage
    bt_hid_save_pairing();
    
    if (bt_state.state == BT_STATE_WAITING_FOR_MAC) {
        bt_state.state = BT_STATE_READY;
        debug_print(DBG_PAIRING, "[BT] Ready to connect via Bluetooth!");
    }
    
    pthread_mutex_unlock(&bt_state.mutex);
}

int bt_hid_get_local_mac(uint8_t* out_mac) {
    pthread_mutex_lock(&bt_state.mutex);
    memcpy(out_mac, bt_state.local_mac, 6);
    pthread_mutex_unlock(&bt_state.mutex);
    return 0;
}

int bt_hid_is_paired(void) {
    pthread_mutex_lock(&bt_state.mutex);
    int paired = bt_state.has_ps3_mac;
    pthread_mutex_unlock(&bt_state.mutex);
    return paired;
}

int bt_hid_get_ps3_mac(uint8_t* out_mac) {
    pthread_mutex_lock(&bt_state.mutex);
    if (!bt_state.has_ps3_mac) {
        pthread_mutex_unlock(&bt_state.mutex);
        return -1;
    }
    memcpy(out_mac, bt_state.ps3_mac, 6);
    pthread_mutex_unlock(&bt_state.mutex);
    return 0;
}

void bt_hid_clear_pairing(void) {
    pthread_mutex_lock(&bt_state.mutex);
    memset(bt_state.ps3_mac, 0, 6);
    bt_state.has_ps3_mac = 0;
    bt_state.state = BT_STATE_WAITING_FOR_MAC;
    pthread_mutex_unlock(&bt_state.mutex);
    
    // Remove saved file
    unlink(BT_PAIRING_FILE);
    debug_print(DBG_PAIRING, "[BT] Cleared pairing");
}

int bt_hid_load_pairing(void) {
    FILE* f = fopen(BT_PAIRING_FILE, "r");
    if (!f) return -1;
    
    char line[256];
    while (fgets(line, sizeof(line), f)) {
        if (strncmp(line, "PS3_MAC=", 8) == 0) {
            char* mac_str = line + 8;
            // Trim newline
            char* nl = strchr(mac_str, '\n');
            if (nl) *nl = '\0';
            
            if (bt_hid_str_to_mac(mac_str, bt_state.ps3_mac) == 0) {
                bt_state.has_ps3_mac = 1;
                fclose(f);
                return 0;
            }
        }
    }
    
    fclose(f);
    return -1;
}

int bt_hid_save_pairing(void) {
    // Create directory if needed
    mkdir("/etc/rosettapad", 0755);
    
    FILE* f = fopen(BT_PAIRING_FILE, "w");
    if (!f) {
        debug_print(DBG_ERROR, "[BT] Failed to save pairing: %s", strerror(errno));
        return -1;
    }
    
    char mac_str[18];
    bt_hid_mac_to_str(bt_state.ps3_mac, mac_str);
    
    fprintf(f, "# RosettaPad Bluetooth Pairing\n");
    fprintf(f, "PS3_MAC=%s\n", mac_str);
    
    char local_str[18];
    bt_hid_mac_to_str(bt_state.local_mac, local_str);
    fprintf(f, "LOCAL_MAC=%s\n", local_str);
    
    fclose(f);
    
    debug_print(DBG_PAIRING, "[BT] Saved pairing to %s", BT_PAIRING_FILE);
    return 0;
}

// =================================================================
// Connection Functions
// =================================================================

// Connection timeout in seconds
#define L2CAP_CONNECT_TIMEOUT_SEC  10

static int l2cap_connect_psm(const uint8_t* dest_mac, uint16_t psm) {
    struct sockaddr_l2 addr;
    int sock;
    
    sock = socket(AF_BLUETOOTH, SOCK_SEQPACKET, BTPROTO_L2CAP);
    if (sock < 0) {
        debug_print(DBG_ERROR | DBG_BT_L2CAP, "[BT] socket() failed: %s", strerror(errno));
        return -1;
    }
    
    // Bind to local adapter
    memset(&addr, 0, sizeof(addr));
    addr.l2_family = AF_BLUETOOTH;
    bacpy(&addr.l2_bdaddr, BDADDR_ANY);
    addr.l2_psm = 0;
    
    if (bind(sock, (struct sockaddr*)&addr, sizeof(addr)) < 0) {
        debug_print(DBG_ERROR | DBG_BT_L2CAP, "[BT] bind() failed: %s", strerror(errno));
        close(sock);
        return -1;
    }
    
    // Set socket to non-blocking for connect with timeout
    int flags = fcntl(sock, F_GETFL, 0);
    if (flags < 0 || fcntl(sock, F_SETFL, flags | O_NONBLOCK) < 0) {
        debug_print(DBG_WARN | DBG_BT_L2CAP, "[BT] Could not set non-blocking: %s", strerror(errno));
        // Continue anyway, connect will just block
    }
    
    // Convert our MAC format to bdaddr (reverse order)
    bdaddr_t dest_bdaddr;
    for (int i = 0; i < 6; i++) {
        dest_bdaddr.b[i] = dest_mac[5 - i];
    }
    
    // Connect
    memset(&addr, 0, sizeof(addr));
    addr.l2_family = AF_BLUETOOTH;
    bacpy(&addr.l2_bdaddr, &dest_bdaddr);
    addr.l2_psm = htobs(psm);
    
    debug_print(DBG_BT_L2CAP, "[BT] Connecting to PSM 0x%04X (timeout=%ds)...", psm, L2CAP_CONNECT_TIMEOUT_SEC);
    
    int ret = connect(sock, (struct sockaddr*)&addr, sizeof(addr));
    
    if (ret < 0) {
        if (errno == EINPROGRESS || errno == EAGAIN) {
            // Non-blocking connect in progress - wait with timeout
            struct pollfd pfd;
            pfd.fd = sock;
            pfd.events = POLLOUT;
            
            debug_print(DBG_BT_L2CAP, "[BT] Waiting for PSM 0x%04X connection...", psm);
            
            ret = poll(&pfd, 1, L2CAP_CONNECT_TIMEOUT_SEC * 1000);
            
            if (ret == 0) {
                debug_print(DBG_ERROR | DBG_BT_L2CAP, 
                    "[BT] connect() to PSM 0x%04X TIMEOUT after %ds - PS3 not responding", 
                    psm, L2CAP_CONNECT_TIMEOUT_SEC);
                debug_print(DBG_ERROR | DBG_BT_L2CAP,
                    "[BT] >>> Make sure PS3 is ON and you paired via USB first <<<");
                close(sock);
                return -1;
            }
            
            if (ret < 0) {
                debug_print(DBG_ERROR | DBG_BT_L2CAP, "[BT] poll() failed: %s", strerror(errno));
                close(sock);
                return -1;
            }
            
            // Check if connect actually succeeded
            int error = 0;
            socklen_t len = sizeof(error);
            if (getsockopt(sock, SOL_SOCKET, SO_ERROR, &error, &len) < 0) {
                debug_print(DBG_ERROR | DBG_BT_L2CAP, "[BT] getsockopt() failed: %s", strerror(errno));
                close(sock);
                return -1;
            }
            
            if (error != 0) {
                debug_print(DBG_ERROR | DBG_BT_L2CAP, 
                    "[BT] connect() to PSM 0x%04X failed: %s (error=%d)", 
                    psm, strerror(error), error);
                if (error == ECONNREFUSED) {
                    debug_print(DBG_ERROR | DBG_BT_L2CAP,
                        "[BT] >>> Connection refused - re-pair via USB (./rosettapad -u) <<<");
                }
                close(sock);
                return -1;
            }
        } else {
            debug_print(DBG_ERROR | DBG_BT_L2CAP, "[BT] connect() to PSM 0x%04X failed: %s", 
                        psm, strerror(errno));
            close(sock);
            return -1;
        }
    }
    
    // Keep NON-BLOCKING mode - we use SIOCOUTQ to check buffer status
    // and only send when buffer is nearly empty for minimal latency
    debug_print(DBG_BT_L2CAP, "[BT] Socket in non-blocking mode with SIOCOUTQ flow control");
    
    // Minimize send buffer to reduce latency from queueing
    // Smaller buffer = fewer packets in flight = lower latency
    int sndbuf = 0;  // Request minimum - kernel will give us its minimum
    setsockopt(sock, SOL_SOCKET, SO_SNDBUF, &sndbuf, sizeof(sndbuf));
    
    // Check what we actually got
    socklen_t optlen = sizeof(sndbuf);
    getsockopt(sock, SOL_SOCKET, SO_SNDBUF, &sndbuf, &optlen);
    debug_print(DBG_BT_L2CAP, "[BT] SO_SNDBUF = %d bytes", sndbuf);
    
    // Try to set high priority for gaming traffic
    int priority = 6;
    if (setsockopt(sock, SOL_SOCKET, SO_PRIORITY, &priority, sizeof(priority)) >= 0) {
        debug_print(DBG_BT_L2CAP, "[BT] Set SO_PRIORITY=%d for low latency", priority);
    }
    
    // CRITICAL: Force Active mode to prevent SNIFF-induced latency
    // PS3 puts connection in SNIFF mode (~53ms intervals) which causes ~1 second
    // input lag due to packet queuing. BT_POWER with force_active=1 tells BlueZ
    // to exit SNIFF mode when sending data, keeping connection in Active mode.
    struct bt_power pwr;
    pwr.force_active = BT_POWER_FORCE_ACTIVE_ON;
    if (setsockopt(sock, SOL_BLUETOOTH, BT_POWER, &pwr, sizeof(pwr)) >= 0) {
        debug_print(DBG_BT_L2CAP, "[BT] Set BT_POWER force_active=ON for PSM 0x%04X", psm);
    } else {
        debug_print(DBG_WARN | DBG_BT_L2CAP, "[BT] Failed to set BT_POWER: %s (continuing anyway)", strerror(errno));
    }
    
    debug_print(DBG_BT_L2CAP, "[BT] Connected to PSM 0x%04X (fd=%d)", psm, sock);
    return sock;
}

int bt_hid_connect(void) {
    pthread_mutex_lock(&bt_state.mutex);
    
    if (!bt_state.has_ps3_mac) {
        debug_print(DBG_ERROR | DBG_BT, "[BT] Cannot connect: No PS3 MAC stored");
        debug_print(DBG_ERROR | DBG_BT, "[BT] Connect via USB first to pair!");
        pthread_mutex_unlock(&bt_state.mutex);
        return -1;
    }
    
    if (bt_state.state == BT_STATE_CONNECTED) {
        debug_print(DBG_WARN | DBG_BT, "[BT] Already connected");
        pthread_mutex_unlock(&bt_state.mutex);
        return 0;
    }
    
    char mac_str[18];
    bt_hid_mac_to_str(bt_state.ps3_mac, mac_str);
    debug_print(DBG_BT, "[BT] Connecting to PS3 at %s...", mac_str);
    
    bt_state.state = BT_STATE_CONNECTING;
    pthread_mutex_unlock(&bt_state.mutex);
    
    // Connect HID Control channel (PSM 0x11)
    int ctrl = l2cap_connect_psm(bt_state.ps3_mac, L2CAP_PSM_HID_CONTROL);
    if (ctrl < 0) {
        pthread_mutex_lock(&bt_state.mutex);
        bt_state.state = BT_STATE_ERROR;
        pthread_mutex_unlock(&bt_state.mutex);
        return -1;
    }
    
    // Short delay between connections
    usleep(100000);
    
    // Connect HID Interrupt channel (PSM 0x13)
    int intr = l2cap_connect_psm(bt_state.ps3_mac, L2CAP_PSM_HID_INTERRUPT);
    if (intr < 0) {
        close(ctrl);
        pthread_mutex_lock(&bt_state.mutex);
        bt_state.state = BT_STATE_ERROR;
        pthread_mutex_unlock(&bt_state.mutex);
        return -1;
    }
    
    pthread_mutex_lock(&bt_state.mutex);
    bt_state.control_sock = ctrl;
    bt_state.interrupt_sock = intr;
    bt_state.state = BT_STATE_CONNECTED;
    bt_state.handshake_complete = 0;  // Reset - wait for handshake
    bt_state.f4_acknowledged = 0;     // Reset - wait for F4 ack
    bt_state.ps3_enabled = 0;         // Reset - wait for PS3 to enable us
    bt_state.reports_sent = 0;
    pthread_mutex_unlock(&bt_state.mutex);
    
    debug_print(DBG_BT, "[BT] *** Connected to PS3 via Bluetooth! ***");
    debug_print(DBG_BT, "[BT] Will send F4 enable report to activate controller...");
    return 0;
}

void bt_hid_disconnect(void) {
    pthread_mutex_lock(&bt_state.mutex);
    
    if (bt_state.interrupt_sock >= 0) {
        close(bt_state.interrupt_sock);
        bt_state.interrupt_sock = -1;
    }
    
    if (bt_state.control_sock >= 0) {
        close(bt_state.control_sock);
        bt_state.control_sock = -1;
    }
    
    if (bt_state.state == BT_STATE_CONNECTED || bt_state.state == BT_STATE_CONNECTING) {
        bt_state.state = bt_state.has_ps3_mac ? BT_STATE_READY : BT_STATE_WAITING_FOR_MAC;
    }
    
    pthread_mutex_unlock(&bt_state.mutex);
    
    debug_print(DBG_BT, "[BT] Disconnected");
}

int bt_hid_is_connected(void) {
    pthread_mutex_lock(&bt_state.mutex);
    int connected = (bt_state.state == BT_STATE_CONNECTED);
    pthread_mutex_unlock(&bt_state.mutex);
    return connected;
}

bt_state_t bt_hid_get_state(void) {
    pthread_mutex_lock(&bt_state.mutex);
    bt_state_t state = bt_state.state;
    pthread_mutex_unlock(&bt_state.mutex);
    return state;
}

const char* bt_hid_state_str(void) {
    switch (bt_hid_get_state()) {
        case BT_STATE_IDLE:            return "IDLE";
        case BT_STATE_WAITING_FOR_MAC: return "WAITING_FOR_USB_PAIRING";
        case BT_STATE_READY:           return "READY";
        case BT_STATE_CONNECTING:      return "CONNECTING";
        case BT_STATE_CONNECTED:       return "CONNECTED";
        case BT_STATE_ERROR:           return "ERROR";
        default:                       return "UNKNOWN";
    }
}

// Called by ds3.c when PS3 sends SET_REPORT F4 to enable the controller
void bt_hid_set_ps3_enabled(int enabled) {
    pthread_mutex_lock(&bt_state.mutex);
    bt_state.ps3_enabled = enabled;
    if (enabled) {
        debug_print(DBG_BT | DBG_HANDSHAKE, "[BT] *** PS3 has enabled controller - full speed input active! ***");
    }
    pthread_mutex_unlock(&bt_state.mutex);
}

int bt_hid_is_ps3_enabled(void) {
    pthread_mutex_lock(&bt_state.mutex);
    int enabled = bt_state.ps3_enabled;
    pthread_mutex_unlock(&bt_state.mutex);
    return enabled;
}

// =================================================================
// Data Transfer
// =================================================================

// Send SET_REPORT 0xF4 to PS3 to enable controller input
// This is required for Bluetooth - the controller sends this to the host!
// Data: 0x42, 0x03, 0x00, 0x00
static int bt_hid_send_enable_report(void) {
    pthread_mutex_lock(&bt_state.mutex);
    int sock = bt_state.control_sock;
    pthread_mutex_unlock(&bt_state.mutex);
    
    if (sock < 0) return -1;
    
    // SET_REPORT (0x50) | Feature (0x03) = 0x53
    // Report ID 0xF4, then data: 0x42, 0x03, 0x00, 0x00
    uint8_t enable_report[] = {
        0x53,  // SET_REPORT | Feature
        0xF4,  // Report ID
        0x42, 0x03, 0x00, 0x00  // Enable payload
    };
    
    debug_print(DBG_BT | DBG_HANDSHAKE, "[BT] Sending SET_REPORT 0xF4 to enable controller...");
    debug_hex(DBG_BT_HID, "Enable report", enable_report, sizeof(enable_report));
    
    ssize_t sent = send(sock, enable_report, sizeof(enable_report), MSG_NOSIGNAL);
    if (sent < 0) {
        debug_print(DBG_ERROR | DBG_BT, "[BT] Failed to send enable report: %s", strerror(errno));
        return -1;
    }
    
    debug_print(DBG_BT | DBG_HANDSHAKE, "[BT] Enable report sent (%zd bytes)", sent);
    return 0;
}

int bt_hid_send_input_report(const uint8_t* report) {
    (void)report;  // We'll get report directly from ds3_copy_bt_report
    
    pthread_mutex_lock(&bt_state.mutex);
    
    if (bt_state.state != BT_STATE_CONNECTED || bt_state.interrupt_sock < 0) {
        pthread_mutex_unlock(&bt_state.mutex);
        return -1;
    }
    
    // Get BT-formatted report (0xA1 + 49 bytes with proper endianness)
    uint8_t bt_report[DS3_BT_INPUT_REPORT_SIZE];
    ds3_copy_bt_report(bt_report);
    
    int sock = bt_state.interrupt_sock;
    pthread_mutex_unlock(&bt_state.mutex);
    
    // Simple non-blocking send
    ssize_t sent = send(sock, bt_report, DS3_BT_INPUT_REPORT_SIZE, MSG_NOSIGNAL | MSG_DONTWAIT);
    
    if (sent < 0) {
        if (errno == EAGAIN || errno == EWOULDBLOCK) {
            return 1;  // Blocked - caller may retry
        }
        debug_print(DBG_ERROR | DBG_BT_HID, "[BT] Send failed: %s", strerror(errno));
        return -1;  // Error
    }
    
    return 0;  // Success
}

int bt_hid_process_control(void) {
    uint8_t buf[256];
    
    pthread_mutex_lock(&bt_state.mutex);
    int sock = bt_state.control_sock;
    pthread_mutex_unlock(&bt_state.mutex);
    
    if (sock < 0) return -1;
    
    ssize_t len = recv(sock, buf, sizeof(buf), MSG_DONTWAIT);
    
    if (len < 0) {
        if (errno == EAGAIN || errno == EWOULDBLOCK) {
            return 0;
        }
        debug_print(DBG_ERROR | DBG_BT_L2CAP, "[BT] Control recv error: %s", strerror(errno));
        return -1;
    }
    
    if (len == 0) {
        debug_print(DBG_BT_L2CAP, "[BT] Control channel closed");
        return -1;
    }
    
    debug_hex(DBG_BT_HID | DBG_REPORTS, "BT Control RX", buf, len);
    
    // Mark handshake as started - PS3 is talking to us!
    pthread_mutex_lock(&bt_state.mutex);
    if (!bt_state.handshake_complete) {
        bt_state.handshake_complete = 1;
        debug_print(DBG_BT | DBG_HANDSHAKE, "[BT] *** PS3 handshake started - enabling input reports ***");
    }
    pthread_mutex_unlock(&bt_state.mutex);
    
    // Parse HID transaction
    uint8_t trans = buf[0] & 0xF0;
    uint8_t param = buf[0] & 0x0F;
    
    debug_print(DBG_BT_HID, "[BT] Control: trans=0x%02X param=0x%02X (raw header=0x%02X)", 
                trans, param, buf[0]);
    
    switch (trans) {
        case HID_TRANS_GET_REPORT: {
            uint8_t report_id = (len > 1) ? buf[1] : 0;
            debug_print(DBG_BT_HID | DBG_REPORTS, "[BT] GET_REPORT type=0x%02X id=0x%02X", param, report_id);
            
            // PS3 uses various param values (0x03 for feature, but also 0x0B seen in practice)
            // Respond to any GET_REPORT with the appropriate feature report
            const char* name = NULL;
            const uint8_t* data = ds3_get_feature_report(report_id, &name);
            
            if (data) {
                uint8_t response[65];
                // Response header: DATA transaction with feature report type
                response[0] = HID_TRANS_DATA | HID_REPORT_FEATURE;
                memcpy(&response[1], data, 64);
                ssize_t sent = send(sock, response, 65, MSG_NOSIGNAL);
                debug_print(DBG_BT_HID | DBG_REPORTS, "[BT] Sent feature report 0x%02X (%s), %zd bytes", 
                            report_id, name, sent);
                debug_hex(DBG_REPORTS | DBG_VERBOSE, "Feature report response", response, 65);
            } else {
                debug_print(DBG_BT_HID | DBG_REPORTS, "[BT] Unknown report 0x%02X, sending HANDSHAKE error", report_id);
                uint8_t err = HID_TRANS_HANDSHAKE | HID_HANDSHAKE_ERR_INV_REPORT_ID;
                send(sock, &err, 1, MSG_NOSIGNAL);
            }
            break;
        }
        
        case HID_TRANS_SET_REPORT: {
            uint8_t report_id = (len > 1) ? buf[1] : 0;
            debug_print(DBG_BT_HID | DBG_REPORTS, "[BT] SET_REPORT id=0x%02X len=%zd", report_id, len);
            
            if (len > 2) {
                ds3_handle_set_report(report_id, &buf[2], len - 2);
            }
            
            uint8_t ack = HID_TRANS_HANDSHAKE | HID_HANDSHAKE_SUCCESS;
            send(sock, &ack, 1, MSG_NOSIGNAL);
            break;
        }
        
        case HID_TRANS_SET_PROTOCOL: {
            debug_print(DBG_BT_HID, "[BT] SET_PROTOCOL: %d", param);
            uint8_t ack = HID_TRANS_HANDSHAKE | HID_HANDSHAKE_SUCCESS;
            send(sock, &ack, 1, MSG_NOSIGNAL);
            break;
        }
        
        case HID_TRANS_HANDSHAKE: {
            // PS3's response to our SET_REPORT (e.g., F4 enable)
            if (param == HID_HANDSHAKE_SUCCESS) {
                pthread_mutex_lock(&bt_state.mutex);
                if (!bt_state.f4_acknowledged) {
                    bt_state.f4_acknowledged = 1;
                    debug_print(DBG_BT | DBG_HANDSHAKE, "[BT] *** PS3 acknowledged F4 enable - controller fully active! ***");
                } else {
                    debug_print(DBG_BT | DBG_HANDSHAKE, "[BT] PS3 HANDSHAKE success");
                }
                pthread_mutex_unlock(&bt_state.mutex);
            } else {
                debug_print(DBG_WARN | DBG_BT, "[BT] PS3 HANDSHAKE error: 0x%02X", param);
            }
            break;
        }
        
        default:
            debug_print(DBG_BT_HID, "[BT] Unknown transaction: 0x%02X", trans);
            break;
    }
    
    return 0;
}

int bt_hid_process_interrupt(void) {
    uint8_t buf[256];
    
    pthread_mutex_lock(&bt_state.mutex);
    int sock = bt_state.interrupt_sock;
    pthread_mutex_unlock(&bt_state.mutex);
    
    if (sock < 0) return -1;
    
    ssize_t len = recv(sock, buf, sizeof(buf), MSG_DONTWAIT);
    
    if (len < 0) {
        if (errno == EAGAIN || errno == EWOULDBLOCK) {
            return 0;
        }
        debug_print(DBG_ERROR | DBG_BT_L2CAP, "[BT] Interrupt recv error: %s", strerror(errno));
        return -1;
    }
    
    if (len == 0) {
        debug_print(DBG_BT_L2CAP, "[BT] Interrupt channel closed");
        return -1;
    }
    
    debug_hex(DBG_BT_HID | DBG_VERBOSE, "BT Interrupt RX", buf, len);
    
    // Output reports (rumble/LED) come on interrupt channel
    if (buf[0] == (HID_TRANS_DATA | HID_REPORT_OUTPUT) && len >= 6) {
        uint8_t right = buf[3];
        uint8_t left = buf[5];
        
        pthread_mutex_lock(&g_rumble_mutex);
        g_rumble_right = right ? 0xFF : 0x00;
        g_rumble_left = left;
        pthread_mutex_unlock(&g_rumble_mutex);
        
        debug_print(DBG_RUMBLE, "[BT] Rumble: weak=%d strong=%d", right, left);
    }
    
    return 0;
}

// =================================================================
// Thread Functions
// =================================================================

void* bt_hid_output_thread(void* arg) {
    (void)arg;
    
    debug_print(DBG_INIT | DBG_BT, "[BT] Output thread started");
    
    uint8_t report[49];
    int reports_sent = 0;
    int enable_sent = 0;
    int init_wait_count = 0;
    
    while (g_running) {
        if (!bt_hid_is_connected()) {
            usleep(100000);
            reports_sent = 0;
            enable_sent = 0;
            init_wait_count = 0;
            continue;
        }
        
        // Step 1: Send F4 enable after connection
        if (!enable_sent) {
            debug_print(DBG_BT | DBG_HANDSHAKE, "[BT] Sending F4 enable report...");
            usleep(100000);  // 100ms settle time
            
            if (bt_hid_send_enable_report() == 0) {
                enable_sent = 1;
                debug_print(DBG_BT | DBG_HANDSHAKE, "[BT] F4 enable sent - waiting for PS3 init");
            } else {
                debug_print(DBG_ERROR | DBG_BT, "[BT] Failed to send F4 enable, retrying...");
                usleep(500000);
            }
            continue;
        }
        
        // Step 2: Wait for PS3 SET_REPORT F4 (slow 10Hz during init)
        if (!bt_hid_is_ps3_enabled()) {
            init_wait_count++;
            
            if (init_wait_count % 10 == 0) {
                ds3_copy_report(report);
                bt_hid_send_input_report(report);
                
                if (init_wait_count % 100 == 0) {
                    debug_print(DBG_BT | DBG_HANDSHAKE, 
                        "[BT] Waiting for PS3 init... (%d sec)", init_wait_count / 100);
                }
            }
            
            if (init_wait_count >= 6000) {
                debug_print(DBG_WARN | DBG_BT, "[BT] PS3 init timeout - starting anyway");
                bt_hid_set_ps3_enabled(1);
            }
            
            usleep(10000);
            continue;
        }
        
        // =============================================================
        // Step 3: LOW-LATENCY SEND WITH BUFFER CHECK
        // =============================================================
        // The PS3 Bluetooth link accepts ~19 packets/sec.
        // To minimize latency, we ONLY send when the buffer is empty/low.
        // This ensures each sent packet contains the freshest state.
        // =============================================================
        
        pthread_mutex_lock(&bt_state.mutex);
        int sock = bt_state.interrupt_sock;
        pthread_mutex_unlock(&bt_state.mutex);
        
        if (sock < 0) {
            usleep(10000);
            continue;
        }
        
        // Check how many bytes are pending in the output queue
        // Only send if buffer is nearly empty (< 1 packet worth)
        int pending = 0;
        static int siocoutq_works = 1;  // Assume it works initially
        static int siocoutq_logged = 0;
        
        if (siocoutq_works && ioctl(sock, SIOCOUTQ, &pending) == 0) {
            if (!siocoutq_logged) {
                debug_print(DBG_BT, "[BT] SIOCOUTQ working - using buffer-aware flow control");
                siocoutq_logged = 1;
            }
            
            if (pending >= DS3_BT_INPUT_REPORT_SIZE) {
                // Buffer has data - skip this cycle, let it drain
                usleep(5000);  // 5ms
                continue;
            }
        } else if (siocoutq_works) {
            // SIOCOUTQ failed - fall back to simple rate limiting
            siocoutq_works = 0;
            debug_print(DBG_WARN | DBG_BT, "[BT] SIOCOUTQ not supported - using 50ms rate limit");
        }
        
        // Grab FRESHEST state and send
        uint8_t bt_report[DS3_BT_INPUT_REPORT_SIZE];
        ds3_copy_bt_report(bt_report);
        
        ssize_t sent = send(sock, bt_report, DS3_BT_INPUT_REPORT_SIZE, MSG_NOSIGNAL | MSG_DONTWAIT);
        
        if (sent < 0) {
            if (errno == EINTR) continue;
            if (errno == EAGAIN || errno == EWOULDBLOCK) {
                // Buffer full - wait and retry
                usleep(10000);
                continue;
            }
            debug_print(DBG_ERROR | DBG_BT, "[BT] Send failed: %s", strerror(errno));
            bt_hid_disconnect();
            continue;
        }
        
        reports_sent++;
        
        // Debug: log button state changes
        static uint8_t last_buttons[3] = {0};
        if (bt_report[1+2] != last_buttons[0] || bt_report[1+3] != last_buttons[1] || bt_report[1+4] != last_buttons[2]) {
            debug_print(DBG_BT | DBG_INPUT, "[BT] Buttons: %02X/%02X/%02X (#%d, pending=%d)",
                        bt_report[1+2], bt_report[1+3], bt_report[1+4], reports_sent, pending);
            last_buttons[0] = bt_report[1+2];
            last_buttons[1] = bt_report[1+3];
            last_buttons[2] = bt_report[1+4];
        }
        
        // Stats every 500 reports
        if (reports_sent % 500 == 0) {
            debug_print(DBG_BT | DBG_PERIODIC, "[BT] Sent %d reports", reports_sent);
        }
        
        // If SIOCOUTQ doesn't work, use fixed rate limiting
        if (!siocoutq_works) {
            usleep(50000);  // 50ms = ~20Hz to match PS3 acceptance rate
        } else {
            usleep(2000);  // 2ms yield when using SIOCOUTQ
        }
    }
    
    debug_print(DBG_BT, "[BT] Output thread exiting");
    return NULL;
}

void* bt_hid_input_thread(void* arg) {
    (void)arg;
    
    debug_print(DBG_INIT | DBG_BT, "[BT] Input thread started");
    
    struct pollfd fds[2];
    
    while (g_running) {
        if (!bt_hid_is_connected()) {
            usleep(100000);
            continue;
        }
        
        pthread_mutex_lock(&bt_state.mutex);
        fds[0].fd = bt_state.control_sock;
        fds[0].events = POLLIN;
        fds[1].fd = bt_state.interrupt_sock;
        fds[1].events = POLLIN;
        pthread_mutex_unlock(&bt_state.mutex);
        
        int ret = poll(fds, 2, 100);
        
        if (ret < 0) {
            if (errno == EINTR) continue;
            debug_print(DBG_ERROR | DBG_BT, "[BT] poll error: %s", strerror(errno));
            break;
        }
        
        if (ret == 0) continue;
        
        if (fds[0].revents & POLLIN) {
            if (bt_hid_process_control() < 0) {
                bt_hid_disconnect();
                continue;
            }
        }
        
        if (fds[0].revents & (POLLERR | POLLHUP)) {
            debug_print(DBG_BT, "[BT] Control channel error/hangup");
            bt_hid_disconnect();
            continue;
        }
        
        if (fds[1].revents & POLLIN) {
            if (bt_hid_process_interrupt() < 0) {
                bt_hid_disconnect();
                continue;
            }
        }
        
        if (fds[1].revents & (POLLERR | POLLHUP)) {
            debug_print(DBG_BT, "[BT] Interrupt channel error/hangup");
            bt_hid_disconnect();
            continue;
        }
    }
    
    debug_print(DBG_BT, "[BT] Input thread exiting");
    return NULL;
}

// =================================================================
// Setup Functions
// =================================================================

int bt_hid_set_device_class(void) {
    char cmd[128];
    snprintf(cmd, sizeof(cmd), "hciconfig hci%d class 0x%06X 2>/dev/null", 
             bt_state.hci_dev_id, BT_CLASS_GAMEPAD);
    
    debug_print(DBG_BT, "[BT] Setting device class to 0x%06X", BT_CLASS_GAMEPAD);
    int ret = system(cmd);
    
    return (ret == 0) ? 0 : -1;
}

int bt_hid_set_device_name(const char* name) {
    char cmd[256];
    snprintf(cmd, sizeof(cmd), "hciconfig hci%d name '%s' 2>/dev/null", 
             bt_state.hci_dev_id, name);
    
    debug_print(DBG_BT, "[BT] Setting device name: %s", name);
    int ret = system(cmd);
    
    return (ret == 0) ? 0 : -1;
}

int bt_hid_set_discoverable(int enable) {
    char cmd[128];
    snprintf(cmd, sizeof(cmd), "hciconfig hci%d %s 2>/dev/null", 
             bt_state.hci_dev_id, enable ? "piscan" : "noscan");
    
    debug_print(DBG_BT, "[BT] Setting discoverable: %s", enable ? "yes" : "no");
    int ret = system(cmd);
    
    return (ret == 0) ? 0 : -1;
}