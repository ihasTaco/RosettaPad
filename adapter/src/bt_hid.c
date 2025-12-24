/*
 * RosettaPad - Bluetooth HID Layer for PS3
 * Auto-discovery and automatic connection
 * 
 * Features:
 * - Scans for PS3 automatically using Sony OUI detection
 * - Saves discovered MAC for future connections
 * - Handles full HID protocol over L2CAP
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <errno.h>
#include <fcntl.h>
#include <poll.h>
#include <sys/socket.h>
#include <sys/time.h>
#include <sys/ioctl.h>
#include <linux/sockios.h>
#include <bluetooth/bluetooth.h>
#include <bluetooth/l2cap.h>
#include <bluetooth/hci.h>
#include <bluetooth/hci_lib.h>

#include "common.h"
#include "ds3.h"
#include "bt_hid.h"

// =================================================================
// Global Context
// =================================================================
bt_hid_ctx_t g_bt_ctx = {
    .state = BT_STATE_DISCONNECTED,
    .ctrl_sock = -1,
    .intr_sock = -1,
    .ps3_addr_valid = 0,
    .packets_sent = 0,
    .packets_dropped = 0,
    .reconnect_count = 0
};

// Flag to indicate BT adapter is ready and local_addr is valid
static int g_bt_adapter_ready = 0;

// =================================================================
// Get Local Bluetooth MAC (for Report 0xF5)
// =================================================================
int bt_hid_get_local_addr(uint8_t* out_mac) {
    if (!g_bt_adapter_ready) {
        return -1;
    }
    
    // BlueZ stores MAC in reverse order, we need it in network order
    // bdaddr_t is stored as: [5][4][3][2][1][0]
    // We need: [0][1][2][3][4][5]
    out_mac[0] = g_bt_ctx.local_addr.b[5];
    out_mac[1] = g_bt_ctx.local_addr.b[4];
    out_mac[2] = g_bt_ctx.local_addr.b[3];
    out_mac[3] = g_bt_ctx.local_addr.b[2];
    out_mac[4] = g_bt_ctx.local_addr.b[1];
    out_mac[5] = g_bt_ctx.local_addr.b[0];
    
    return 0;
}

// =================================================================
// Constants
// =================================================================
#define BT_MOTION_INTERVAL_MS   20
#define BT_CTRL_POLL_MS         10
#define PS3_MAC_FILE            "/tmp/rosettapad/ps3_mac"

// Sony OUI prefixes (first 3 bytes of MAC)
static const uint8_t SONY_OUI[][3] = {
    {0x00, 0x1E, 0xA9},  // Most common for PS3
    {0x00, 0x19, 0xC1},
    {0x00, 0x1D, 0xD9},
    {0x00, 0x24, 0x8D},
    {0x00, 0x26, 0x43},
    {0xAC, 0x89, 0x95},
    {0x70, 0x9E, 0x29},
    {0x78, 0xC8, 0x81},
    {0xF8, 0xD0, 0xAC},
};
#define NUM_SONY_OUI (sizeof(SONY_OUI) / sizeof(SONY_OUI[0]))

// =================================================================
// Calibration Data for Report 0xEF (Bluetooth format)
// From BT capture PS3-DS3_BT_0001.log:
//
// A0 config response (after A3 EF header):
// EF 04 00 08 03 01 A0 00 00 00 00 00 00 00 00 00 01 FD 01 8C 02 00 01 8E 01 FE 01 8B 02 00 00 7B...
//
// B0 config response (after A3 EF header):
// EF 04 00 08 03 01 B0 00 00 00 00 00 00 00 00 00 02 6C 02 6F 00 00 00 00...
// =================================================================

// Response for 0xA0 config request (gyroscope calibration)
// This is the data AFTER the A3 EF header (handle_get_report adds A3 + report_id)
static uint8_t report_ef_a0[64] = {
    0xEF, 0x04, 0x00, 0x08, 0x03, 0x01, 0xA0, 0x00,  // Header with config byte A0
    0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,  // Padding
    0x01, 0xFD, 0x01, 0x8C, 0x02, 0x00, 0x01, 0x8E,  // Gyro calibration from capture
    0x01, 0xFE, 0x01, 0x8B, 0x02, 0x00, 0x00, 0x7B,  // More calibration data
    0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
    0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x05,
    0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
    0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00
};

// Response for 0xB0 config request (accelerometer calibration)
static uint8_t report_ef_b0[64] = {
    0xEF, 0x04, 0x00, 0x08, 0x03, 0x01, 0xB0, 0x00,  // Header with config byte B0
    0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,  // Padding
    0x02, 0x6C, 0x02, 0x6F, 0x00, 0x00, 0x00, 0x00,  // Accel calibration from capture
    0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
    0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
    0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x05,
    0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
    0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00
};

static uint8_t last_ef_config = 0xA0;

// =================================================================
// Utility Functions
// =================================================================

static uint64_t get_time_ms(void) {
    struct timeval tv;
    gettimeofday(&tv, NULL);
    return (uint64_t)tv.tv_sec * 1000ULL + tv.tv_usec / 1000;
}

const char* bt_hid_state_str(bt_state_t state) {
    switch (state) {
        case BT_STATE_DISCONNECTED: return "Disconnected";
        case BT_STATE_SCANNING:     return "Scanning";
        case BT_STATE_CONNECTING:   return "Connecting";
        case BT_STATE_CONTROL_CONNECTED: return "Control Connected";
        case BT_STATE_INTERRUPT_CONNECTED: return "Interrupt Connected";
        case BT_STATE_READY:        return "Ready";
        case BT_STATE_ENABLED:      return "Enabled";
        case BT_STATE_ERROR:        return "Error";
        default:                    return "Unknown";
    }
}

static int is_sony_oui(const bdaddr_t* addr) {
    uint8_t mac[6];
    baswap((bdaddr_t*)mac, addr);
    
    for (size_t i = 0; i < NUM_SONY_OUI; i++) {
        if (mac[0] == SONY_OUI[i][0] &&
            mac[1] == SONY_OUI[i][1] &&
            mac[2] == SONY_OUI[i][2]) {
            return 1;
        }
    }
    return 0;
}

// =================================================================
// PS3 Address Management
// =================================================================

int bt_hid_set_ps3_addr(const char* mac) {
    if (str2ba(mac, &g_bt_ctx.ps3_addr) < 0) {
        return -1;
    }
    g_bt_ctx.ps3_addr_valid = 1;
    return 0;
}

int bt_hid_get_ps3_addr(char* out_mac) {
    if (!g_bt_ctx.ps3_addr_valid) {
        return -1;
    }
    ba2str(&g_bt_ctx.ps3_addr, out_mac);
    return 0;
}

int bt_hid_has_ps3_addr(void) {
    return g_bt_ctx.ps3_addr_valid;
}

int bt_hid_save_ps3_addr(void) {
    if (!g_bt_ctx.ps3_addr_valid) {
        return -1;
    }
    
    FILE* f = fopen(PS3_MAC_FILE, "w");
    if (!f) {
        printf("[BT] Could not save PS3 MAC to %s\n", PS3_MAC_FILE);
        return -1;
    }
    
    char mac[18];
    ba2str(&g_bt_ctx.ps3_addr, mac);
    fprintf(f, "%s\n", mac);
    fclose(f);
    
    printf("[BT] Saved PS3 MAC: %s\n", mac);
    return 0;
}

int bt_hid_load_ps3_addr(void) {
    FILE* f = fopen(PS3_MAC_FILE, "r");
    if (!f) {
        return -1;
    }
    
    char mac[18] = {0};
    if (fgets(mac, sizeof(mac), f)) {
        // Trim newline
        char* nl = strchr(mac, '\n');
        if (nl) *nl = '\0';
        
        if (str2ba(mac, &g_bt_ctx.ps3_addr) == 0) {
            g_bt_ctx.ps3_addr_valid = 1;
            printf("[BT] Loaded PS3 MAC: %s\n", mac);
            fclose(f);
            return 0;
        }
    }
    
    fclose(f);
    return -1;
}

// =================================================================
// Bluetooth Adapter Configuration
// =================================================================

int bt_configure_adapter(void) {
    printf("[BT] Configuring Bluetooth adapter...\n");
    
    int dev_id = hci_get_route(NULL);
    if (dev_id < 0) {
        perror("[BT] No Bluetooth adapter found");
        return -1;
    }
    
    int sock = hci_open_dev(dev_id);
    if (sock < 0) {
        perror("[BT] Failed to open HCI device");
        return -1;
    }
    
    // Set device class to Gamepad (0x002508)
    // Major class: Peripheral (0x05), Minor class: Gamepad (0x08)
    uint32_t cod = 0x002508;
    if (hci_write_class_of_dev(sock, cod, 1000) < 0) {
        printf("[BT] Warning: Could not set device class\n");
    } else {
        printf("[BT] Device class set to Gamepad (0x%06X)\n", cod);
    }
    
    // Make adapter connectable (required for PS3 to accept our connection)
    struct hci_dev_req dr;
    dr.dev_id = dev_id;
    dr.dev_opt = SCAN_PAGE;  // Page scan = connectable
    if (ioctl(sock, HCISETSCAN, (unsigned long)&dr) < 0) {
        printf("[BT] Warning: Could not set scan mode\n");
    } else {
        printf("[BT] Adapter set to connectable mode\n");
    }
    
    // Read local address
    if (hci_read_bd_addr(sock, &g_bt_ctx.local_addr, 1000) < 0) {
        perror("[BT] Failed to read local address");
        hci_close_dev(sock);
        return -1;
    }
    
    char addr_str[18];
    ba2str(&g_bt_ctx.local_addr, addr_str);
    printf("[BT] Local adapter: %s\n", addr_str);
    
    // Set the Pi's MAC in DS3 Report 0xF5
    // This tells the PS3 which Bluetooth address to expect connections from
    // BlueZ stores MAC in reverse byte order, so we swap it
    uint8_t mac[6];
    baswap((bdaddr_t*)mac, &g_bt_ctx.local_addr);
    ds3_set_host_mac(mac);
    
    g_bt_adapter_ready = 1;
    
    hci_close_dev(sock);
    return 0;
}

// =================================================================
// PS3 Auto-Discovery
// =================================================================

int bt_hid_scan_for_ps3(int timeout_sec) {
    printf("[BT] Scanning for PS3 console (%d seconds)...\n", timeout_sec);
    g_bt_ctx.state = BT_STATE_SCANNING;
    
    int dev_id = hci_get_route(NULL);
    if (dev_id < 0) {
        perror("[BT] No Bluetooth adapter");
        return -1;
    }
    
    int sock = hci_open_dev(dev_id);
    if (sock < 0) {
        perror("[BT] Failed to open HCI device");
        return -1;
    }
    
    // Scan parameters
    int max_rsp = 20;
    int flags = IREQ_CACHE_FLUSH;
    inquiry_info* devices = malloc(max_rsp * sizeof(inquiry_info));
    if (!devices) {
        hci_close_dev(sock);
        return -1;
    }
    
    // Perform inquiry (timeout is in 1.28 second units)
    int num_rsp = hci_inquiry(dev_id, timeout_sec, max_rsp, NULL, &devices, flags);
    
    if (num_rsp < 0) {
        perror("[BT] Inquiry failed");
        free(devices);
        hci_close_dev(sock);
        return -1;
    }
    
    printf("[BT] Found %d device(s)\n", num_rsp);
    
    int found = 0;
    
    for (int i = 0; i < num_rsp && !found; i++) {
        char addr[18];
        char name[248] = {0};
        
        ba2str(&devices[i].bdaddr, addr);
        
        // Try to get device name
        if (hci_read_remote_name(sock, &devices[i].bdaddr, sizeof(name), name, 0) < 0) {
            strcpy(name, "[unknown]");
        }
        
        printf("[BT]   %s - %s", addr, name);
        
        // Check if this is a PS3
        int is_ps3 = 0;
        
        // Method 1: Check name
        if (strcasestr(name, "playstation") || strcasestr(name, "PS3") || strcasestr(name, "sony")) {
            is_ps3 = 1;
            printf(" <- PS3 (name match)");
        }
        // Method 2: Check Sony OUI
        else if (is_sony_oui(&devices[i].bdaddr)) {
            is_ps3 = 1;
            printf(" <- PS3 (Sony OUI)");
        }
        
        printf("\n");
        
        if (is_ps3) {
            bacpy(&g_bt_ctx.ps3_addr, &devices[i].bdaddr);
            g_bt_ctx.ps3_addr_valid = 1;
            found = 1;
            
            printf("[BT] *** Found PS3: %s ***\n", addr);
            bt_hid_save_ps3_addr();
        }
    }
    
    free(devices);
    hci_close_dev(sock);
    
    if (!found) {
        printf("[BT] No PS3 found in scan\n");
        g_bt_ctx.state = BT_STATE_DISCONNECTED;
        return -1;
    }
    
    g_bt_ctx.state = BT_STATE_DISCONNECTED;
    return 0;
}

// =================================================================
// L2CAP Connection
// =================================================================

static int create_l2cap_socket(uint16_t psm, const bdaddr_t* dest) {
    int sock = socket(AF_BLUETOOTH, SOCK_SEQPACKET, BTPROTO_L2CAP);
    if (sock < 0) {
        perror("[BT] socket");
        return -1;
    }
    
    // Set socket options for minimal latency
    int priority = 6;  // High priority (0-6, 6 is highest)
    setsockopt(sock, SOL_SOCKET, SO_PRIORITY, &priority, sizeof(priority));
    
    // MINIMIZE send buffer - ask for 0, get kernel minimum
    int sndbuf = 0;
    setsockopt(sock, SOL_SOCKET, SO_SNDBUF, &sndbuf, sizeof(sndbuf));
    
    // Check what we actually got
    socklen_t buflen = sizeof(sndbuf);
    getsockopt(sock, SOL_SOCKET, SO_SNDBUF, &sndbuf, &buflen);
    printf("[BT] Actual SO_SNDBUF: %d bytes\n", sndbuf);
    
    // Minimize receive buffer too
    int rcvbuf = 0;
    setsockopt(sock, SOL_SOCKET, SO_RCVBUF, &rcvbuf, sizeof(rcvbuf));
    
    // Disable linger - discard unsent data on close
    struct linger ling = {1, 0};
    setsockopt(sock, SOL_SOCKET, SO_LINGER, &ling, sizeof(ling));
    
    // Set L2CAP options for HID - AGGRESSIVE flush to kill buffered data
    struct l2cap_options opts = {0};
    socklen_t optlen = sizeof(opts);
    if (getsockopt(sock, SOL_L2CAP, L2CAP_OPTIONS, &opts, &optlen) == 0) {
        opts.omtu = 50;       // Output MTU - exactly one HID report size
        opts.imtu = 64;       // Input MTU
        opts.flush_to = 1;    // Flush timeout = 1 slot (0.625ms)!
                              // This tells BT stack to DISCARD packets that can't
                              // be sent within 0.625ms - prevents buffer buildup
        opts.mode = L2CAP_MODE_BASIC;
        if (setsockopt(sock, SOL_L2CAP, L2CAP_OPTIONS, &opts, sizeof(opts)) == 0) {
            printf("[BT] L2CAP flush_to=1 (0.625ms) - stale packets will be dropped\n");
        }
    }
    
    // Short timeouts for connect
    struct timeval tv;
    tv.tv_sec = 5;
    tv.tv_usec = 0;
    setsockopt(sock, SOL_SOCKET, SO_RCVTIMEO, &tv, sizeof(tv));
    setsockopt(sock, SOL_SOCKET, SO_SNDTIMEO, &tv, sizeof(tv));
    
    struct sockaddr_l2 local = {0};
    local.l2_family = AF_BLUETOOTH;
    local.l2_psm = 0;
    bacpy(&local.l2_bdaddr, &g_bt_ctx.local_addr);
    
    if (bind(sock, (struct sockaddr*)&local, sizeof(local)) < 0) {
        perror("[BT] bind");
        close(sock);
        return -1;
    }
    
    struct sockaddr_l2 remote = {0};
    remote.l2_family = AF_BLUETOOTH;
    remote.l2_psm = htobs(psm);
    bacpy(&remote.l2_bdaddr, dest);
    
    char dest_str[18];
    ba2str(dest, dest_str);
    printf("[BT] Connecting to %s PSM 0x%04X...\n", dest_str, psm);
    
    if (connect(sock, (struct sockaddr*)&remote, sizeof(remote)) < 0) {
        int err = errno;
        printf("[BT] connect failed: %s (errno=%d)\n", strerror(err), err);
        
        // Provide more context for common errors
        if (err == EHOSTDOWN) {
            printf("[BT] -> PS3 Bluetooth may not be ready or is rejecting connection\n");
            printf("[BT] -> Try: 1) Restart PS3  2) Check if another controller is connected\n");
        } else if (err == ECONNREFUSED) {
            printf("[BT] -> PS3 actively refused the connection\n");
        } else if (err == ETIMEDOUT) {
            printf("[BT] -> Connection timed out - PS3 may be out of range\n");
        }
        
        close(sock);
        return -1;
    }
    
    printf("[BT] Connected to PSM 0x%04X\n", psm);
    return sock;
}

// =================================================================
// Control Channel Protocol
// =================================================================

static int handle_get_report(int sock, uint8_t report_id) {
    uint8_t response[68];
    size_t resp_len = 0;
    
    response[0] = BT_HIDP_DATA_RTYPE_FEATURE;  // 0xA3
    response[1] = report_id;
    
    const char* name = "unknown";
    
    switch (report_id) {
        case 0x01: {
            // BT capture: A3 01 01 04 00 08 0C 01 02 18 18 18 18... (65 bytes total)
            const uint8_t* data = ds3_get_feature_report(0x01, &name);
            if (data) {
                // data[0] is 0x00, data[1] is 0x01 - copy from data[1] onward
                memcpy(&response[2], &data[1], 63);
                resp_len = 65;
            }
            break;
        }
        
        case 0xF2: {
            // BT capture: A3 F2 FF FF 00 34 C7 31 25 AE 60 00 03 50 81 C0 01 8A 09 (19 bytes)
            const uint8_t* data = ds3_get_feature_report(0xF2, &name);
            if (data) {
                // data starts with F2 FF FF 00 [MAC]... - copy from data[1]
                memcpy(&response[2], &data[1], 17);
                resp_len = 19;
            }
            break;
        }
        
        case 0xEF: {
            // BT capture: A3 EF EF 04 00 08 03 01 A0/B0 00 00... (65 bytes total)
            name = "EF Calibration";
            const uint8_t* src = (last_ef_config == 0xB0) ? report_ef_b0 : report_ef_a0;
            // report_ef arrays already start with EF 04 00 08 03 01 [config]...
            memcpy(&response[2], src, 63);
            resp_len = 65;
            break;
        }
        
        case 0xF7: {
            // BT capture: A3 F7 00 02 EC 02 D4 01 05 FF 14 33 00 (13 bytes total)
            name = "Calibration";
            // Build response matching capture exactly
            response[2] = 0x00;
            response[3] = 0x02;
            response[4] = 0xEC;
            response[5] = 0x02;
            response[6] = 0xD4;
            response[7] = 0x01;
            response[8] = 0x05;
            response[9] = 0xFF;
            response[10] = 0x14;
            response[11] = 0x33;
            response[12] = 0x00;
            resp_len = 13;
            break;
        }
        
        case 0xF8: {
            // BT capture: A3 F8 01 00 00 00 (6 bytes total)
            name = "Status";
            response[2] = 0x01;
            response[3] = 0x00;
            response[4] = 0x00;
            response[5] = 0x00;
            resp_len = 6;
            break;
        }
        
        default:
            printf("[BT] GET_REPORT 0x%02X - unknown\n", report_id);
            return -1;
    }
    
    if (resp_len > 0) {
        printf("[BT] GET_REPORT 0x%02X (%s) -> %zu bytes\n", report_id, name, resp_len);
        ssize_t sent = send(sock, response, resp_len, 0);
        if (sent < 0) {
            perror("[BT] send GET_REPORT response");
            return -1;
        }
        return 0;
    }
    
    return -1;
}

static int handle_set_report(const uint8_t* data, size_t len) {
    if (len < 2) return -1;
    
    uint8_t report_id = data[1];
    
    printf("[BT] SET_REPORT 0x%02X (%zu bytes)\n", report_id, len);
    
    switch (report_id) {
        case 0xEF:
            // BT packet format: [0]=trans_type, [1]=report_id, [2+]=report_data
            // USB log shows: EF 00 00 00 00 03 01 A0 00...
            // So config byte (A0/B0) is at offset 7 in report data = data[2+7] = data[9]
            // But looking at actual BT RX: 53 EF 00 00 00 00 03 01 A0
            // Config byte is at data[8]
            if (len >= 9) {
                last_ef_config = data[8];
                printf("[BT] EF config mode: 0x%02X\n", last_ef_config);
            }
            break;
            
        case 0xF4:
            printf("[BT] *** Received F4 ENABLE command! ***\n");
            printf("[BT] Motion data will now be sent to PS3\n");
            g_bt_ctx.state = BT_STATE_ENABLED;
            break;
            
        case 0x01:
            printf("[BT] Output report init\n");
            break;
    }
    
    return 0;
}

int bt_hid_process_control(void) {
    if (g_bt_ctx.ctrl_sock < 0) return -1;
    
    struct pollfd pfd = {
        .fd = g_bt_ctx.ctrl_sock,
        .events = POLLIN
    };
    
    if (poll(&pfd, 1, 0) <= 0) {
        return 0;
    }
    
    uint8_t buf[128];
    ssize_t n = recv(g_bt_ctx.ctrl_sock, buf, sizeof(buf), MSG_DONTWAIT);
    
    if (n <= 0) {
        if (errno == EAGAIN || errno == EWOULDBLOCK) return 0;
        printf("[BT] Control channel error: %s\n", strerror(errno));
        return -1;
    }
    
    printf("[BT] CTRL RX (%zd):", n);
    for (ssize_t i = 0; i < n && i < 20; i++) printf(" %02X", buf[i]);
    if (n > 20) printf(" ...");
    printf("\n");
    
    uint8_t trans = buf[0];
    
    if (trans == 0x4B && n >= 2) {
        uint8_t report_id = buf[1];
        handle_get_report(g_bt_ctx.ctrl_sock, report_id);
    }
    else if ((trans == 0x52 || trans == 0x53) && n >= 2) {
        handle_set_report(buf, n);
        uint8_t ack = 0x00;
        send(g_bt_ctx.ctrl_sock, &ack, 1, 0);
    }
    
    return 0;
}

// =================================================================
// Interrupt Channel
// =================================================================

static uint64_t g_last_send_time = 0;

int bt_hid_send_input(void) {
    if (g_bt_ctx.state != BT_STATE_ENABLED || g_bt_ctx.intr_sock < 0) {
        return -1;
    }
    
    uint64_t now = get_time_ms();
    
    // Safety valve: if we've been connected for a while and drops are accumulating,
    // the buffer might be filling up. Track consecutive drops.
    static int consecutive_drops = 0;
    static uint64_t last_success_time = 0;
    
    if (last_success_time == 0) {
        last_success_time = now;
    }
    
    // If we haven't successfully sent in 500ms, something is wrong - buffer is full
    // This shouldn't happen with flush_to=1, but just in case
    if (now - last_success_time > 500 && consecutive_drops > 10) {
        printf("[BT] Buffer appears stuck (%d consecutive drops), will reconnect\n", consecutive_drops);
        // Signal need for reconnect by returning error
        consecutive_drops = 0;
        return -1;
    }
    
    // Fixed 40ms interval (~25Hz) - match PS3 SNIFF mode rate
    if (now - g_last_send_time < 40) {
        return 0;
    }
    
    // Get CURRENT state right before sending
    uint8_t current_report[49];
    ds3_copy_report(current_report);
    
    uint8_t report[50];
    report[0] = BT_HIDP_DATA_RTYPE_INPUT;  // 0xA1
    memcpy(&report[1], current_report, 49);
    
    // Override status bytes for Bluetooth connection
    // Read from shared state (populated by controller module)
    bt_status_t bt_status;
    bt_status_get(&bt_status);
    report[30] = bt_status.plugged_status;
    report[31] = bt_status.battery_status;
    report[32] = bt_status.connection_type;
    
    ssize_t sent = send(g_bt_ctx.intr_sock, report, sizeof(report), MSG_DONTWAIT | MSG_NOSIGNAL);
    
    // Always update time
    g_last_send_time = now;
    
    if (sent < 0) {
        if (errno == EAGAIN || errno == EWOULDBLOCK) {
            g_bt_ctx.packets_dropped++;
            consecutive_drops++;
            return 0;
        }
        return -1;
    }
    
    // Success - reset drop counter
    consecutive_drops = 0;
    last_success_time = now;
    g_bt_ctx.packets_sent++;
    return 0;
}

static int process_interrupt_input(void) {
    if (g_bt_ctx.intr_sock < 0) return -1;
    
    struct pollfd pfd = {
        .fd = g_bt_ctx.intr_sock,
        .events = POLLIN
    };
    
    if (poll(&pfd, 1, 0) <= 0) return 0;
    
    uint8_t buf[64];
    ssize_t n = recv(g_bt_ctx.intr_sock, buf, sizeof(buf), MSG_DONTWAIT);
    
    if (n <= 0) {
        if (errno == EAGAIN || errno == EWOULDBLOCK) return 0;
        return -1;
    }
    
    // Debug: log all received packets
    static int intr_rx_count = 0;
    intr_rx_count++;
    
    // Always log output reports (A2), but limit other messages
    if (buf[0] == BT_HIDP_DATA_RTYPE_OUTPUT) {
        printf("[BT] INTR RX OUTPUT (%zd bytes):", n);
        for (ssize_t i = 0; i < n && i < 16; i++) {
            printf(" %02X", buf[i]);
        }
        printf("\n");
    } else if (intr_rx_count <= 10 || intr_rx_count % 100 == 0) {
        printf("[BT] INTR RX type=0x%02X (%zd bytes)\n", buf[0], n);
    }
    
    // Output report: A2 01 XX [rumble data]
    // Format: [0]=0xA2 trans, [1]=0x01 report_id, [2]=pad,
    //         [3]=weak_duration, [4]=weak_power, [5]=strong_duration, [6]=strong_power
    if (n >= 7 && buf[0] == BT_HIDP_DATA_RTYPE_OUTPUT && buf[1] == 0x01) {
        uint8_t weak_duration = buf[3];
        uint8_t weak_power = buf[4];    // Weak motor: 0 or 1 (on/off)
        uint8_t strong_duration = buf[5];
        uint8_t strong_power = buf[6];  // Strong motor: 0-255
        
        printf("[BT] Rumble parsed: weak_dur=%d weak_pwr=%d strong_dur=%d strong_pwr=%d\n",
               weak_duration, weak_power, strong_duration, strong_power);
        
        // Convert to DualSense format
        // Weak motor (right) = binary on/off -> full strength if on
        // Strong motor (left) = variable intensity
        uint8_t ds_right = weak_power ? 0xFF : 0x00;
        uint8_t ds_left = strong_power;
        
        pthread_mutex_lock(&g_rumble_mutex);
        // Only log if values changed
        static uint8_t last_right = 0, last_left = 0;
        if (ds_right != last_right || ds_left != last_left) {
            printf("[BT] Rumble CHANGED: ds_right=%d ds_left=%d (was %d/%d)\n", 
                   ds_right, ds_left, last_right, last_left);
            last_right = ds_right;
            last_left = ds_left;
        }
        g_rumble_right = ds_right;
        g_rumble_left = ds_left;
        pthread_mutex_unlock(&g_rumble_mutex);
    }
    
    return 0;
}

// =================================================================
// Public API
// =================================================================

int bt_hid_init(void) {
    printf("[BT] Initializing Bluetooth HID subsystem\n");
    
    if (bt_configure_adapter() < 0) {
        return -1;
    }
    
    // Try to load saved PS3 MAC
    if (bt_hid_load_ps3_addr() == 0) {
        char mac[18];
        ba2str(&g_bt_ctx.ps3_addr, mac);
        printf("[BT] Using saved PS3 address: %s\n", mac);
    } else {
        printf("[BT] No saved PS3 address, will scan when USB connects\n");
    }
    
    g_bt_ctx.state = BT_STATE_DISCONNECTED;
    return 0;
}

int bt_hid_connect(void) {
    // First, check if we captured PS3's MAC from SET_REPORT 0xF5
    if (!g_bt_ctx.ps3_addr_valid && ds3_has_ps3_mac()) {
        uint8_t mac[6];
        ds3_get_ps3_mac(mac);
        
        // Convert to bdaddr_t (reverse byte order)
        g_bt_ctx.ps3_addr.b[5] = mac[0];
        g_bt_ctx.ps3_addr.b[4] = mac[1];
        g_bt_ctx.ps3_addr.b[3] = mac[2];
        g_bt_ctx.ps3_addr.b[2] = mac[3];
        g_bt_ctx.ps3_addr.b[1] = mac[4];
        g_bt_ctx.ps3_addr.b[0] = mac[5];
        g_bt_ctx.ps3_addr_valid = 1;
        
        char mac_str[18];
        ba2str(&g_bt_ctx.ps3_addr, mac_str);
        printf("[BT] Using PS3 MAC from SET_REPORT 0xF5: %s\n", mac_str);
        
        // Save for future use
        bt_hid_save_ps3_addr();
    }
    
    // If still no address, try scanning (fallback)
    if (!g_bt_ctx.ps3_addr_valid) {
        printf("[BT] No PS3 address from USB, trying scan (PS3 may not advertise)...\n");
        if (bt_hid_scan_for_ps3(8) < 0) {
            printf("[BT] Could not find PS3. Make sure it's ON.\n");
            return -1;
        }
    }
    
    if (g_bt_ctx.state != BT_STATE_DISCONNECTED) {
        printf("[BT] Already connected or connecting\n");
        return -1;
    }
    
    g_bt_ctx.state = BT_STATE_CONNECTING;
    
    char ps3_str[18];
    ba2str(&g_bt_ctx.ps3_addr, ps3_str);
    printf("[BT] Connecting to PS3 at %s\n", ps3_str);
    
    // Connect Control channel (PSM 0x11)
    g_bt_ctx.ctrl_sock = create_l2cap_socket(L2CAP_PSM_HID_CONTROL, &g_bt_ctx.ps3_addr);
    if (g_bt_ctx.ctrl_sock < 0) {
        g_bt_ctx.state = BT_STATE_ERROR;
        return -1;
    }
    g_bt_ctx.state = BT_STATE_CONTROL_CONNECTED;
    
    usleep(20000);  // 20ms between L2CAP connections
    
    // Connect Interrupt channel (PSM 0x13)
    g_bt_ctx.intr_sock = create_l2cap_socket(L2CAP_PSM_HID_INTERRUPT, &g_bt_ctx.ps3_addr);
    if (g_bt_ctx.intr_sock < 0) {
        close(g_bt_ctx.ctrl_sock);
        g_bt_ctx.ctrl_sock = -1;
        g_bt_ctx.state = BT_STATE_ERROR;
        return -1;
    }
    
    g_bt_ctx.state = BT_STATE_READY;
    g_bt_ctx.connect_time = get_time_ms();
    
    printf("[BT] Both channels connected! Waiting for PS3 handshake...\n");
    
    // Try to configure connection for lower latency
    int dev_id = hci_get_route(NULL);
    if (dev_id >= 0) {
        int hci_sock = hci_open_dev(dev_id);
        if (hci_sock >= 0) {
            // Get connection handle for PS3
            struct hci_conn_info_req *cr;
            cr = malloc(sizeof(*cr) + sizeof(struct hci_conn_info));
            if (cr) {
                bacpy(&cr->bdaddr, &g_bt_ctx.ps3_addr);
                cr->type = ACL_LINK;
                if (ioctl(hci_sock, HCIGETCONNINFO, cr) == 0) {
                    uint16_t handle = cr->conn_info->handle;
                    printf("[BT] Connection handle: 0x%04x\n", handle);
                    
                    // Try Sniff Subrating - request more frequent transmission slots
                    // HCI_Sniff_Subrating: OCF=0x0011, OGF=0x02 (Link Policy)
                    // Parameters: handle(2), max_latency(2), min_remote_timeout(2), min_local_timeout(2)
                    struct {
                        uint8_t type;
                        uint16_t opcode;
                        uint8_t plen;
                        uint16_t handle;
                        uint16_t max_latency;      // Max time between subrate transmissions
                        uint16_t min_remote_to;    // Min sniff timeout for remote
                        uint16_t min_local_to;     // Min sniff timeout for local
                    } __attribute__((packed)) sniff_sub_cmd = {
                        .type = HCI_COMMAND_PKT,
                        .opcode = htobs(0x0811),  // Sniff Subrating
                        .plen = 8,
                        .handle = htobs(handle),
                        .max_latency = htobs(2),      // 2 * 0.625ms = 1.25ms max latency
                        .min_remote_to = htobs(2),
                        .min_local_to = htobs(2)
                    };
                    
                    if (write(hci_sock, &sniff_sub_cmd, sizeof(sniff_sub_cmd)) > 0) {
                        printf("[BT] Sent Sniff Subrating request\n");
                    }
                    
                    // Also try to exit SNIFF mode
                    struct {
                        uint8_t type;
                        uint16_t opcode;
                        uint8_t plen;
                        uint16_t handle;
                    } __attribute__((packed)) exit_sniff_cmd = {
                        .type = HCI_COMMAND_PKT,
                        .opcode = htobs(0x0804),  // Exit Sniff Mode
                        .plen = 2,
                        .handle = htobs(handle)
                    };
                    
                    if (write(hci_sock, &exit_sniff_cmd, sizeof(exit_sniff_cmd)) > 0) {
                        printf("[BT] Sent Exit SNIFF Mode command\n");
                    }
                }
                free(cr);
            }
            hci_close_dev(hci_sock);
        }
    }
    
    // Send an initial input report to trigger PS3 handshake
    // Real DS3 does this - it starts sending reports immediately after connecting
    uint8_t init_report[50];
    init_report[0] = BT_HIDP_DATA_RTYPE_INPUT;  // 0xA1
    ds3_copy_report(&init_report[1]);
    
    // Send a few initial reports to get PS3's attention
    for (int i = 0; i < 3; i++) {
        send(g_bt_ctx.intr_sock, init_report, sizeof(init_report), MSG_NOSIGNAL);
        usleep(20000);  // 20ms between reports
    }
    printf("[BT] Sent initial input reports\n");
    
    return 0;
}

void bt_hid_disconnect(void) {
    printf("[BT] Disconnecting...\n");
    
    // Clear rumble state on disconnect
    pthread_mutex_lock(&g_rumble_mutex);
    g_rumble_right = 0;
    g_rumble_left = 0;
    pthread_mutex_unlock(&g_rumble_mutex);
    
    if (g_bt_ctx.intr_sock >= 0) {
        close(g_bt_ctx.intr_sock);
        g_bt_ctx.intr_sock = -1;
    }
    
    if (g_bt_ctx.ctrl_sock >= 0) {
        close(g_bt_ctx.ctrl_sock);
        g_bt_ctx.ctrl_sock = -1;
    }
    
    g_bt_ctx.state = BT_STATE_DISCONNECTED;
}

int bt_hid_is_enabled(void) {
    return g_bt_ctx.state == BT_STATE_ENABLED;
}

bt_state_t bt_hid_get_state(void) {
    return g_bt_ctx.state;
}

int bt_hid_wake_ps3(void) {
    printf("[BT] Attempting to wake PS3...\n");
    
    // PS3 in standby mode listens for connection attempts from paired controllers
    // We need to retry the connection - PS3 uses the connection attempt itself as wake trigger
    int max_attempts = 5;
    int attempt;
    
    for (attempt = 0; attempt < max_attempts; attempt++) {
        printf("[BT] Wake attempt %d/%d...\n", attempt + 1, max_attempts);
        
        // Disconnect if we're in an error state
        if (g_bt_ctx.state == BT_STATE_ERROR) {
            bt_hid_disconnect();
        }
        
        if (g_bt_ctx.state == BT_STATE_DISCONNECTED) {
            if (bt_hid_connect() == 0) {
                // Connection succeeded! PS3 is awake
                printf("[BT] Connection succeeded on attempt %d - PS3 is awake!\n", attempt + 1);
                break;
            }
            // Connection failed - PS3 might still be waking up
            // The connection attempt itself may have triggered wake
            printf("[BT] Connection attempt %d failed, PS3 may be waking...\n", attempt + 1);
        }
        
        // Wait before retry - PS3 needs time to wake its BT radio
        if (attempt < max_attempts - 1) {
            usleep(1500000);  // 1.5 seconds between attempts
        }
    }
    
    // Wait for connection to be ready
    for (int i = 0; i < 30 && g_bt_ctx.state < BT_STATE_READY; i++) {
        usleep(100000);
    }
    
    if (g_bt_ctx.intr_sock < 0) {
        printf("[BT] Wake failed - could not establish connection after %d attempts\n", max_attempts);
        return -1;
    }
    
    // Send PS button press to complete wake
    uint8_t wake_report[50] = {0};
    wake_report[0] = BT_HIDP_DATA_RTYPE_INPUT;
    wake_report[1] = 0x01;
    wake_report[5] = DS3_BTN_PS;
    wake_report[7] = 0x80;
    wake_report[8] = 0x80;
    wake_report[9] = 0x80;
    wake_report[10] = 0x80;
    
    send(g_bt_ctx.intr_sock, wake_report, sizeof(wake_report), 0);
    usleep(150000);
    
    wake_report[5] = 0;
    send(g_bt_ctx.intr_sock, wake_report, sizeof(wake_report), 0);
    
    printf("[BT] Wake signal sent successfully\n");
    return 0;
}

// =================================================================
// Thread Functions
// =================================================================


void* bt_hid_thread(void* arg) {
    (void)arg;
    
    printf("[BT] Management thread started\n");
    
    int connect_requested = 0;
    int retry_count = 0;
    int max_retries = 3;
    int was_usb_connected = 0;  // Track if USB was ever connected
    int printed_usb_msg = 0;    // Only print USB message once
    int tried_auto_connect = 0; // Track if we tried auto-connect on startup
    
    while (g_running) {
        // Check if we're in standby mode - don't try to connect to PS3
        if (system_is_standby()) {
            // In standby, just wait - don't try to connect to PS3
            usleep(100000);  // 100ms
            continue;
        }
        
        switch (g_bt_ctx.state) {
            case BT_STATE_DISCONNECTED:
                // Track USB connection state
                if (g_usb_enabled) {
                    was_usb_connected = 1;
                    if (!printed_usb_msg) {
                        printf("[BT] USB connected, will initiate BT after USB disconnects\n");
                        printf("[BT] (Unplug USB cable to switch to Bluetooth mode)\n");
                        printed_usb_msg = 1;
                    }
                }
                
                // Auto-connect on startup if we have saved PS3 MAC and USB is not connected
                // BUT NOT if we're in standby mode
                if (!tried_auto_connect && !g_usb_enabled && !connect_requested && 
                    bt_hid_has_ps3_addr() && !system_is_standby()) {
                    tried_auto_connect = 1;
                    printf("[BT] Found saved PS3 MAC, attempting Bluetooth connection...\n");
                    
                    // Brief delay for system to settle on startup
                    usleep(500000);  // 500ms
                    
                    // Check USB didn't connect while we waited AND we're not in standby
                    if (!g_usb_enabled && !system_is_standby()) {
                        if (bt_hid_connect() == 0) {
                            connect_requested = 1;
                            retry_count = 0;
                        } else {
                            // PS3 appears to be off - enter standby mode
                            // This allows PS button wake to work on fresh boot
                            printf("[BT] Auto-connect failed (PS3 likely off), entering standby mode\n");
                            system_enter_standby();
                        }
                    }
                }
                
                // Connect BT after USB disconnects (user unplugged cable)
                // BUT NOT if we're in standby mode (PS3 is off)
                // This mimics real DS3 behavior
                if (was_usb_connected && !g_usb_enabled && !connect_requested && 
                    ds3_has_ps3_mac() && !system_is_standby()) {
                    printf("[BT] USB disconnected, initiating Bluetooth connection...\n");
                    
                    // Brief delay to let PS3 notice USB disconnect
                    usleep(200000);  // 200ms
                    
                    // Double-check standby state after delay
                    if (system_is_standby()) {
                        printf("[BT] Entered standby mode, skipping BT connection\n");
                        break;
                    }
                    
                    if (bt_hid_connect() == 0) {
                        connect_requested = 1;
                        retry_count = 0;
                    } else {
                        retry_count++;
                        if (retry_count < max_retries) {
                            printf("[BT] Connection failed, retry %d/%d in 3 seconds...\n", 
                                   retry_count, max_retries);
                            sleep(3);
                        } else {
                            printf("[BT] Max retries reached, giving up\n");
                            connect_requested = 1;
                        }
                    }
                }
                usleep(100000);
                break;
                
            case BT_STATE_SCANNING:
            case BT_STATE_CONNECTING:
                usleep(100000);
                break;
                
            case BT_STATE_CONTROL_CONNECTED:
            case BT_STATE_INTERRUPT_CONNECTED:
                usleep(50000);
                break;
                
            case BT_STATE_READY:
            case BT_STATE_ENABLED:
                // Check for timeout - if PS3 hasn't sent F4 after 500ms, start sending anyway
                // This handles cases where PS3 has already paired and doesn't repeat handshake
                if (g_bt_ctx.state == BT_STATE_READY) {
                    static int ready_count = 0;
                    ready_count++;
                    if (ready_count == 1) {
                        printf("[BT] Waiting for PS3 handshake (will auto-enable after 500ms)...\n");
                    }
                    if (ready_count >= 50) {  // 50 * 10ms = 500ms
                        printf("[BT] No F4 received, auto-enabling motion data\n");
                        g_bt_ctx.state = BT_STATE_ENABLED;
                        ready_count = 0;
                    }
                }
                
                if (bt_hid_process_control() < 0) {
                    printf("[BT] Control channel error, reconnecting...\n");
                    bt_hid_disconnect();
                    connect_requested = 0;
                    retry_count = 0;
                    g_bt_ctx.reconnect_count++;
                    continue;
                }
                
                if (process_interrupt_input() < 0) {
                    printf("[BT] Interrupt channel error, reconnecting...\n");
                    bt_hid_disconnect();
                    connect_requested = 0;
                    retry_count = 0;
                    continue;
                }
                
                usleep(BT_CTRL_POLL_MS * 1000);
                break;
                
            case BT_STATE_ERROR:
                printf("[BT] Error state, waiting to retry...\n");
                bt_hid_disconnect();
                connect_requested = 0;
                sleep(5);
                break;
        }
        
        // Reset if USB reconnects while we're on Bluetooth
        // (User plugged cable back in)
        if (g_usb_enabled && g_bt_ctx.state >= BT_STATE_READY) {
            printf("[BT] USB reconnected, disconnecting Bluetooth\n");
            bt_hid_disconnect();
            connect_requested = 0;
            printed_usb_msg = 0;  // Allow message to print again
            was_usb_connected = 1;
        }
    }
    
    bt_hid_disconnect();
    printf("[BT] Management thread exiting\n");
    return NULL;
}

void* bt_motion_thread(void* arg) {
    (void)arg;
    
    printf("[BT] Motion thread started\n");
    
    uint64_t last_log_time = 0;
    int sends_this_second = 0;
    int drops_this_second = 0;
    
    while (g_running) {
        // Don't send data in standby mode
        if (system_is_standby()) {
            usleep(100000);  // 100ms - slow poll while in standby
            continue;
        }
        
        if (g_bt_ctx.state == BT_STATE_ENABLED) {
            uint32_t before_sent = g_bt_ctx.packets_sent;
            uint32_t before_dropped = g_bt_ctx.packets_dropped;
            
            bt_hid_send_input();
            
            if (g_bt_ctx.packets_sent > before_sent) {
                sends_this_second++;
            }
            if (g_bt_ctx.packets_dropped > before_dropped) {
                drops_this_second++;
            }
            
            // Log actual throughput every second
            uint64_t now = get_time_ms();
            if (now - last_log_time >= 1000) {
                printf("[BT] Throughput: %d/sec sent, %d/sec dropped (total: %u sent, %u dropped)\n",
                       sends_this_second, drops_this_second,
                       g_bt_ctx.packets_sent, g_bt_ctx.packets_dropped);
                
                sends_this_second = 0;
                drops_this_second = 0;
                last_log_time = now;
            }
        }
        
        // Poll very frequently to catch socket ready ASAP (same as your original)
        usleep(500);  // 0.5ms = 2000Hz polling
    }
    
    printf("[BT] Motion thread exiting\n");
    return NULL;
}