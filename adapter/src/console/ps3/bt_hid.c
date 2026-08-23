/*
 * RosettaPad - PS3 Bluetooth HID Layer
 * =====================================
 * 
 * L2CAP HID connections for motion data and wake functionality.
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <ctype.h>
#include <unistd.h>
#include <alloca.h>
#include <errno.h>
#include <fcntl.h>
#include <poll.h>
#include <sys/socket.h>
#include <sys/time.h>
#include <sys/ioctl.h>
#include <bluetooth/bluetooth.h>
#include <bluetooth/l2cap.h>
#include <bluetooth/hci.h>
#include <bluetooth/hci_lib.h>

#include "core/common.h"
#include "console/ps3/ds3_emulation.h"
#include "console/ps3/bt_hid.h"
#include "console/ps3/usb_gadget.h"

/* ============================================================================
 * GLOBAL CONTEXT
 * ============================================================================ */

ps3_bt_ctx_t g_ps3_bt_ctx = {
    .state = BT_STATE_DISCONNECTED,
    .ctrl_sock = -1,
    .intr_sock = -1,
    .ps3_addr_valid = 0,
    .packets_sent = 0,
    .packets_dropped = 0,
    .reconnect_count = 0
};

static int g_bt_adapter_ready = 0;

/* Sony OUI prefixes */
static const uint8_t SONY_OUI[][3] = {
    {0x00, 0x1E, 0xA9}, {0x00, 0x19, 0xC1}, {0x00, 0x1D, 0xD9},
    {0x00, 0x24, 0x8D}, {0x00, 0x26, 0x43}, {0xAC, 0x89, 0x95},
    {0x70, 0x9E, 0x29}, {0x78, 0xC8, 0x81}, {0xF8, 0xD0, 0xAC},
};
#define NUM_SONY_OUI (sizeof(SONY_OUI) / sizeof(SONY_OUI[0]))

/* Calibration data for Report 0xEF */
static uint8_t report_ef_a0[64] = {
    0xEF, 0x04, 0x00, 0x08, 0x03, 0x01, 0xA0, 0x00,
    0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
    0x01, 0xFD, 0x01, 0x8C, 0x02, 0x00, 0x01, 0x8E,
    0x01, 0xFE, 0x01, 0x8B, 0x02, 0x00, 0x00, 0x7B,
    0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
    0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x05,
    0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
    0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00
};

static uint8_t report_ef_b0[64] = {
    0xEF, 0x04, 0x00, 0x08, 0x03, 0x01, 0xB0, 0x00,
    0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
    0x02, 0x6C, 0x02, 0x6F, 0x00, 0x00, 0x00, 0x00,
    0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
    0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
    0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x05,
    0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
    0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00
};

static uint8_t last_ef_config = 0xA0;

/* ============================================================================
 * UTILITIES
 * ============================================================================ */

const char* ps3_bt_state_str(bt_state_t state) {
    const char* names[] = {
        "Disconnected", "Scanning", "Connecting", "Control Connected",
        "Interrupt Connected", "Ready", "Enabled", "Error"
    };
    return (state < sizeof(names)/sizeof(names[0])) ? names[state] : "Unknown";
}

static int is_sony_oui(const bdaddr_t* addr) {
    uint8_t mac[6];
    baswap((bdaddr_t*)mac, addr);
    
    for (size_t i = 0; i < NUM_SONY_OUI; i++) {
        if (memcmp(mac, SONY_OUI[i], 3) == 0) return 1;
    }
    return 0;
}

/* Portable case-insensitive substring search.
 * Many platforms (including some musl-based systems) lack strcasestr.
 * Provide a simple implementation and use it where needed.
 */
static char* strcasestr_compat(const char* haystack, const char* needle) {
    if (!haystack || !needle) return NULL;
    size_t needle_len = strlen(needle);
    if (needle_len == 0) return (char*)haystack;

    for (const char* p = haystack; *p; p++) {
        size_t i;
        for (i = 0; i < needle_len; i++) {
            char a = p[i];
            char b = needle[i];
            if (a == '\0') break;
            if (tolower((unsigned char)a) != tolower((unsigned char)b)) break;
        }
        if (i == needle_len) return (char*)p;
        if (p[0] == '\0') break;
    }
    return NULL;
}

/* ============================================================================
 * ADDRESS MANAGEMENT
 * ============================================================================ */

int ps3_bt_set_addr(const char* mac) {
    if (str2ba(mac, &g_ps3_bt_ctx.ps3_addr) < 0) return -1;
    g_ps3_bt_ctx.ps3_addr_valid = 1;
    return 0;
}

int ps3_bt_get_addr(char* out_mac) {
    if (!g_ps3_bt_ctx.ps3_addr_valid) return -1;
    ba2str(&g_ps3_bt_ctx.ps3_addr, out_mac);
    return 0;
}

int ps3_bt_has_addr(void) {
    return g_ps3_bt_ctx.ps3_addr_valid;
}

int ps3_bt_save_addr(void) {
    if (!g_ps3_bt_ctx.ps3_addr_valid) return -1;
    
    FILE* f = fopen(PS3_MAC_FILE, "w");
    if (!f) return -1;
    
    char mac[18];
    ba2str(&g_ps3_bt_ctx.ps3_addr, mac);
    fprintf(f, "%s\n", mac);
    fclose(f);
    
    printf("[BT] Saved PS3 MAC: %s\n", mac);
    return 0;
}

int ps3_bt_load_addr(void) {
    FILE* f = fopen(PS3_MAC_FILE, "r");
    if (!f) return -1;
    
    char mac[18] = {0};
    if (fgets(mac, sizeof(mac), f)) {
        char* nl = strchr(mac, '\n');
        if (nl) *nl = '\0';
        
        if (str2ba(mac, &g_ps3_bt_ctx.ps3_addr) == 0) {
            g_ps3_bt_ctx.ps3_addr_valid = 1;
            printf("[BT] Loaded PS3 MAC: %s\n", mac);
            fclose(f);
            return 0;
        }
    }
    
    fclose(f);
    return -1;
}

int ps3_bt_get_local_addr(uint8_t* out_mac) {
    if (!g_bt_adapter_ready) return -1;
    
    out_mac[0] = g_ps3_bt_ctx.local_addr.b[5];
    out_mac[1] = g_ps3_bt_ctx.local_addr.b[4];
    out_mac[2] = g_ps3_bt_ctx.local_addr.b[3];
    out_mac[3] = g_ps3_bt_ctx.local_addr.b[2];
    out_mac[4] = g_ps3_bt_ctx.local_addr.b[1];
    out_mac[5] = g_ps3_bt_ctx.local_addr.b[0];
    
    return 0;
}

/* ============================================================================
 * ADAPTER CONFIGURATION
 * ============================================================================ */

static int configure_adapter(void) {
    printf("[BT] Configuring adapter...\n");
    
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
    
    /* Set device class to Gamepad */
    hci_write_class_of_dev(sock, 0x002508, 1000);
    
    /* Make adapter connectable */
    struct hci_dev_req dr = {.dev_id = dev_id, .dev_opt = SCAN_PAGE};
    ioctl(sock, HCISETSCAN, (unsigned long)&dr);
    
    /* Default link policy: allow role switch + sniff so become_master() can
     * request master on the PS3 link */
    struct hci_dev_req lp = {.dev_id = dev_id, .dev_opt = HCI_LP_RSWITCH | HCI_LP_SNIFF};
    ioctl(sock, HCISETLINKPOL, (unsigned long)&lp);
    
    /* Read local address */
    if (hci_read_bd_addr(sock, &g_ps3_bt_ctx.local_addr, 1000) < 0) {
        perror("[BT] Failed to read local address");
        hci_close_dev(sock);
        return -1;
    }
    
    char addr_str[18];
    ba2str(&g_ps3_bt_ctx.local_addr, addr_str);
    printf("[BT] Local adapter: %s\n", addr_str);
    
    /* Set Pi's MAC in DS3 Report 0xF5 */
    uint8_t mac[6];
    baswap((bdaddr_t*)mac, &g_ps3_bt_ctx.local_addr);
    ds3_set_host_mac(mac);
    
    g_bt_adapter_ready = 1;
    hci_close_dev(sock);
    return 0;
}

/* ============================================================================
 * SCANNING
 * ============================================================================ */

int ps3_bt_scan(int timeout_sec) {
    printf("[BT] Scanning for PS3 (%d seconds)...\n", timeout_sec);
    g_ps3_bt_ctx.state = BT_STATE_SCANNING;
    
    int dev_id = hci_get_route(NULL);
    if (dev_id < 0) return -1;
    
    int sock = hci_open_dev(dev_id);
    if (sock < 0) return -1;
    
    int max_rsp = 20;
    inquiry_info* devices = malloc(max_rsp * sizeof(inquiry_info));
    if (!devices) {
        hci_close_dev(sock);
        return -1;
    }
    
    int num_rsp = hci_inquiry(dev_id, timeout_sec, max_rsp, NULL, &devices, IREQ_CACHE_FLUSH);
    
    int found = 0;
    for (int i = 0; i < num_rsp && !found; i++) {
        char name[248] = {0};
        hci_read_remote_name(sock, &devices[i].bdaddr, sizeof(name), name, 0);
        
        if (strcasestr_compat(name, "playstation") || strcasestr_compat(name, "PS3") || 
            strcasestr_compat(name, "sony") || is_sony_oui(&devices[i].bdaddr)) {
            bacpy(&g_ps3_bt_ctx.ps3_addr, &devices[i].bdaddr);
            g_ps3_bt_ctx.ps3_addr_valid = 1;
            found = 1;
            
            char addr[18];
            ba2str(&devices[i].bdaddr, addr);
            printf("[BT] Found PS3: %s\n", addr);
            ps3_bt_save_addr();
        }
    }
    
    free(devices);
    hci_close_dev(sock);
    g_ps3_bt_ctx.state = BT_STATE_DISCONNECTED;
    
    return found ? 0 : -1;
}

/* ============================================================================
 * L2CAP CONNECTION
 * ============================================================================ */

static int create_l2cap_socket(uint16_t psm, const bdaddr_t* dest) {
    int sock = socket(AF_BLUETOOTH, SOCK_SEQPACKET, BTPROTO_L2CAP);
    if (sock < 0) return -1;
    
    /* Socket options for low latency */
    int priority = 6;
    setsockopt(sock, SOL_SOCKET, SO_PRIORITY, &priority, sizeof(priority));
    
    int sndbuf = 0;
    setsockopt(sock, SOL_SOCKET, SO_SNDBUF, &sndbuf, sizeof(sndbuf));
    
    struct l2cap_options opts = {0};
    socklen_t optlen = sizeof(opts);
    if (getsockopt(sock, SOL_L2CAP, L2CAP_OPTIONS, &opts, &optlen) == 0) {
        opts.omtu = 50;
        opts.imtu = 64;
        opts.flush_to = 1;
        setsockopt(sock, SOL_L2CAP, L2CAP_OPTIONS, &opts, sizeof(opts));
    }
    
    /* The PS3 puts the HID link into sniff mode (11.25ms interval) like it
     * does with a real DS3. By default the kernel exits sniff every time we
     * have data to send, the PS3 re-requests it, and the link thrashes
     * between modes with a stall at each transition. force_active=0 tells
     * the kernel to leave the link in sniff and transmit at the anchor
     * points the PS3 scheduled. */
    struct bt_power pwr = {.force_active = 0};
    if (setsockopt(sock, SOL_BLUETOOTH, BT_POWER, &pwr, sizeof(pwr)) < 0) {
        perror("[BT] BT_POWER");
    }
    
    struct timeval tv = {.tv_sec = 5, .tv_usec = 0};
    setsockopt(sock, SOL_SOCKET, SO_RCVTIMEO, &tv, sizeof(tv));
    setsockopt(sock, SOL_SOCKET, SO_SNDTIMEO, &tv, sizeof(tv));
    
    struct sockaddr_l2 local = {
        .l2_family = AF_BLUETOOTH,
        .l2_psm = 0
    };
    bacpy(&local.l2_bdaddr, &g_ps3_bt_ctx.local_addr);
    
    if (bind(sock, (struct sockaddr*)&local, sizeof(local)) < 0) {
        close(sock);
        return -1;
    }
    
    struct sockaddr_l2 remote = {
        .l2_family = AF_BLUETOOTH,
        .l2_psm = htobs(psm)
    };
    bacpy(&remote.l2_bdaddr, dest);
    
    if (connect(sock, (struct sockaddr*)&remote, sizeof(remote)) < 0) {
        close(sock);
        return -1;
    }
    
    return sock;
}

/*
 * Become master of the PS3 link.
 *
 * The Pi is already master of its piconet with the DualSense. If the PS3 is
 * master of the Pi<->PS3 link, the Pi is a slave in a second piconet
 * (scatternet) and the radio has to time-slice between two hop sequences -
 * btmon shows the PS3 link only getting serviced every 60-300ms. With the Pi
 * as master of both, it schedules both slaves itself.
 */
static void become_master(void) {
    int dev_id = hci_get_route(NULL);
    int dd = hci_open_dev(dev_id);
    if (dd < 0) {
        perror("[BT] hci_open_dev for role switch");
        return;
    }
    
    /* Find the ACL handle for the PS3 */
    struct hci_conn_info_req* cr = alloca(sizeof(*cr) + sizeof(struct hci_conn_info));
    bacpy(&cr->bdaddr, &g_ps3_bt_ctx.ps3_addr);
    cr->type = ACL_LINK;
    if (ioctl(dd, HCIGETCONNINFO, (unsigned long)cr) < 0) {
        perror("[BT] HCIGETCONNINFO");
        hci_close_dev(dd);
        return;
    }
    uint16_t handle = cr->conn_info->handle;
    int is_master = cr->conn_info->link_mode & HCI_LM_MASTER;
    printf("[BT] PS3 link handle=0x%03x role=%s\n", handle, is_master ? "MASTER" : "SLAVE");
    
    if (!is_master) {
        if (hci_switch_role(dd, &g_ps3_bt_ctx.ps3_addr, 0x00 /* master */, 2000) < 0) {
            perror("[BT] Role switch to master failed");
        } else {
            /* Re-read - the PS3 can refuse */
            if (ioctl(dd, HCIGETCONNINFO, (unsigned long)cr) == 0) {
                is_master = cr->conn_info->link_mode & HCI_LM_MASTER;
                printf("[BT] After role switch: %s\n", is_master ? "MASTER" : "SLAVE (PS3 refused)");
            }
        }
    }
    
    /* Clear the role-switch bit on this link so the PS3 can't flip it back.
     * Keep SNIFF allowed so we don't break anything the PS3 expects. */
    if (is_master) {
        if (hci_write_link_policy(dd, handle, HCI_LP_SNIFF, 1000) < 0) {
            perror("[BT] write_link_policy");
        }
    }
    
    hci_close_dev(dd);
}

/* ============================================================================
 * CONTROL CHANNEL PROTOCOL
 * ============================================================================ */

/* errno from the recv() that observed the link drop. The kernel maps the
 * HCI disconnect reason onto it (bt_to_errno): 0x08 Connection Timeout ->
 * ETIMEDOUT, 0x13 Remote User Terminated -> ECONNRESET. 0 means recv()
 * returned 0 with no error. */
static int g_drop_errno = 0;

/* errno from the last failed connect attempt (refused vs timeout) */
static int g_connect_errno = 0;

static int handle_get_report(int sock, uint8_t report_id) {
    uint8_t response[68];
    size_t resp_len = 0;
    
    response[0] = BT_HIDP_DATA_RTYPE_FEATURE;
    response[1] = report_id;
    
    switch (report_id) {
        case 0x01: {
            const uint8_t* data = ds3_get_feature_report(0x01, NULL);
            if (data) {
                memcpy(&response[2], &data[1], 63);
                resp_len = 65;
            }
            break;
        }
        case 0xF2: {
            const uint8_t* data = ds3_get_feature_report(0xF2, NULL);
            if (data) {
                memcpy(&response[2], &data[1], 17);
                resp_len = 19;
            }
            break;
        }
        case 0xEF: {
            const uint8_t* src = (last_ef_config == 0xB0) ? report_ef_b0 : report_ef_a0;
            memcpy(&response[2], src, 63);
            resp_len = 65;
            break;
        }
        case 0xF7:
            memcpy(&response[2], (uint8_t[]){0x00,0x02,0xEC,0x02,0xD4,0x01,0x05,0xFF,0x14,0x33,0x00}, 11);
            resp_len = 13;
            break;
        case 0xF8:
            memcpy(&response[2], (uint8_t[]){0x01,0x00,0x00,0x00}, 4);
            resp_len = 6;
            break;
    }
    
    if (resp_len > 0) {
        send(sock, response, resp_len, 0);
        return 0;
    }
    return -1;
}

static int process_control(void) {
    if (g_ps3_bt_ctx.ctrl_sock < 0) return -1;
    
    struct pollfd pfd = {.fd = g_ps3_bt_ctx.ctrl_sock, .events = POLLIN};
    if (poll(&pfd, 1, 0) <= 0) return 0;
    
    uint8_t buf[128];
    ssize_t n = recv(g_ps3_bt_ctx.ctrl_sock, buf, sizeof(buf), MSG_DONTWAIT);
    if (n <= 0) {
        if (n < 0 && errno == EAGAIN) return 0;
        g_drop_errno = (n < 0) ? errno : 0;
        return -1;
    }
    
    uint8_t trans = buf[0];
    
    if (trans == 0x4B && n >= 2) {
        handle_get_report(g_ps3_bt_ctx.ctrl_sock, buf[1]);
    }
    else if ((trans == 0x52 || trans == 0x53) && n >= 2) {
        uint8_t report_id = buf[1];
        
        if (report_id == 0xEF && n >= 9) {
            last_ef_config = buf[8];
        }
        else if (report_id == 0xF4) {
            printf("[BT] *** Received F4 ENABLE ***\n");
            g_ps3_bt_ctx.state = BT_STATE_ENABLED;
        }
        
        uint8_t ack = 0x00;
        send(g_ps3_bt_ctx.ctrl_sock, &ack, 1, 0);
    }
    
    return 0;
}

/* ============================================================================
 * INTERRUPT CHANNEL
 * ============================================================================ */

static uint64_t g_last_send_time = 0;

static int send_input(void) {
    if (g_ps3_bt_ctx.state != BT_STATE_ENABLED || g_ps3_bt_ctx.intr_sock < 0) {
        return -1;
    }
    
    uint64_t now = time_get_ms();
    if (now - g_last_send_time < 40) return 0;  /* 25Hz */
    
    /* Get current controller state and build DS3 report */
    controller_state_t state;
    controller_state_copy(&state);
    
    uint8_t ds3_report[DS3_INPUT_REPORT_SIZE];
    ds3_build_input_report(&state, ds3_report);
    
    /* Build BT report */
    uint8_t report[DS3_BT_INPUT_REPORT_SIZE];
    report[0] = BT_HIDP_DATA_RTYPE_INPUT;
    memcpy(&report[1], ds3_report, DS3_INPUT_REPORT_SIZE);
    
    /* Override status for BT connection */
    report[30] = DS3_STATUS_UNPLUGGED;
    report[32] = DS3_CONN_BT;
    
    ssize_t sent = send(g_ps3_bt_ctx.intr_sock, report, sizeof(report), MSG_DONTWAIT | MSG_NOSIGNAL);
    g_last_send_time = now;
    
    if (sent < 0) {
        if (errno == EAGAIN || errno == EWOULDBLOCK) {
            g_ps3_bt_ctx.packets_dropped++;
            return 0;
        }
        return -1;
    }
    
    g_ps3_bt_ctx.packets_sent++;
    return 0;
}

static int process_interrupt(void) {
    if (g_ps3_bt_ctx.intr_sock < 0) return -1;
    
    struct pollfd pfd = {.fd = g_ps3_bt_ctx.intr_sock, .events = POLLIN};
    if (poll(&pfd, 1, 0) <= 0) return 0;
    
    uint8_t buf[64];
    ssize_t n = recv(g_ps3_bt_ctx.intr_sock, buf, sizeof(buf), MSG_DONTWAIT);
    if (n <= 0) {
        if (n < 0 && errno == EAGAIN) return 0;
        g_drop_errno = (n < 0) ? errno : 0;
        return -1;
    }
    
    /* Handle rumble from PS3 */
    if (n >= 7 && buf[0] == BT_HIDP_DATA_RTYPE_OUTPUT && buf[1] == 0x01) {
        controller_output_t output;
        controller_output_copy(&output);
        output.rumble_right = buf[4] ? 0xFF : 0x00;
        output.rumble_left = buf[6];
        controller_output_update(&output);
    }
    
    return 0;
}

/* ============================================================================
 * PUBLIC API
 * ============================================================================ */

int ps3_bt_init(void) {
    printf("[BT] Initializing...\n");
    
    if (configure_adapter() < 0) return -1;
    
    ps3_bt_load_addr();
    g_ps3_bt_ctx.state = BT_STATE_DISCONNECTED;
    return 0;
}

int ps3_bt_connect(void) {
    if (!g_bt_adapter_ready) {
        printf("[BT] Connect requested but adapter not ready\n");
        return -1;
    }
    
    /* Try to get PS3 MAC from USB handshake first */
    if (!g_ps3_bt_ctx.ps3_addr_valid && ds3_has_ps3_mac()) {
        uint8_t mac[6];
        ds3_get_ps3_mac(mac);
        
        g_ps3_bt_ctx.ps3_addr.b[5] = mac[0];
        g_ps3_bt_ctx.ps3_addr.b[4] = mac[1];
        g_ps3_bt_ctx.ps3_addr.b[3] = mac[2];
        g_ps3_bt_ctx.ps3_addr.b[2] = mac[3];
        g_ps3_bt_ctx.ps3_addr.b[1] = mac[4];
        g_ps3_bt_ctx.ps3_addr.b[0] = mac[5];
        g_ps3_bt_ctx.ps3_addr_valid = 1;
        
        ps3_bt_save_addr();
    }
    
    if (!g_ps3_bt_ctx.ps3_addr_valid) {
        if (ps3_bt_scan(8) < 0) return -1;
    }
    
    if (g_ps3_bt_ctx.state != BT_STATE_DISCONNECTED) return -1;
    
    g_ps3_bt_ctx.state = BT_STATE_CONNECTING;
    
    g_ps3_bt_ctx.ctrl_sock = create_l2cap_socket(L2CAP_PSM_HID_CONTROL, &g_ps3_bt_ctx.ps3_addr);
    if (g_ps3_bt_ctx.ctrl_sock < 0) {
        g_connect_errno = errno;
        printf("[BT] Control channel connect failed: %s\n", strerror(errno));
        g_ps3_bt_ctx.state = BT_STATE_ERROR;
        return -1;
    }
    
    usleep(20000);
    
    g_ps3_bt_ctx.intr_sock = create_l2cap_socket(L2CAP_PSM_HID_INTERRUPT, &g_ps3_bt_ctx.ps3_addr);
    if (g_ps3_bt_ctx.intr_sock < 0) {
        g_connect_errno = errno;
        printf("[BT] Interrupt channel connect failed: %s\n", strerror(errno));
        close(g_ps3_bt_ctx.ctrl_sock);
        g_ps3_bt_ctx.ctrl_sock = -1;
        g_ps3_bt_ctx.state = BT_STATE_ERROR;
        return -1;
    }
    
    g_connect_errno = 0;
    g_ps3_bt_ctx.state = BT_STATE_READY;
    g_ps3_bt_ctx.connect_time = time_get_ms();
    
    printf("[BT] Connected to PS3\n");
    become_master();
    
    /* Send initial reports */
    controller_state_t state;
    controller_state_copy(&state);
    uint8_t ds3_report[DS3_INPUT_REPORT_SIZE];
    ds3_build_input_report(&state, ds3_report);
    
    uint8_t init_report[DS3_BT_INPUT_REPORT_SIZE];
    init_report[0] = BT_HIDP_DATA_RTYPE_INPUT;
    memcpy(&init_report[1], ds3_report, DS3_INPUT_REPORT_SIZE);
    
    for (int i = 0; i < 3; i++) {
        send(g_ps3_bt_ctx.intr_sock, init_report, sizeof(init_report), MSG_NOSIGNAL);
        usleep(20000);
    }
    
    return 0;
}

void ps3_bt_disconnect(void) {
    if (g_ps3_bt_ctx.state == BT_STATE_DISCONNECTED) {
        return;  /* Already disconnected */
    }
    
    printf("[BT] Disconnecting...\n");
    
    /* Clear rumble */
    controller_output_t output;
    controller_output_copy(&output);
    output.rumble_left = 0;
    output.rumble_right = 0;
    controller_output_update(&output);
    
    if (g_ps3_bt_ctx.intr_sock >= 0) {
        close(g_ps3_bt_ctx.intr_sock);
        g_ps3_bt_ctx.intr_sock = -1;
    }
    
    if (g_ps3_bt_ctx.ctrl_sock >= 0) {
        close(g_ps3_bt_ctx.ctrl_sock);
        g_ps3_bt_ctx.ctrl_sock = -1;
    }
    
    g_ps3_bt_ctx.state = BT_STATE_DISCONNECTED;
}

int ps3_bt_is_enabled(void) {
    return g_ps3_bt_ctx.state == BT_STATE_ENABLED;
}

bt_state_t ps3_bt_get_state(void) {
    return g_ps3_bt_ctx.state;
}

int ps3_bt_wake(void) {
    printf("[BT] Attempting to wake PS3...\n");
    
    for (int attempt = 0; attempt < 5; attempt++) {
        if (g_ps3_bt_ctx.state == BT_STATE_ERROR) {
            ps3_bt_disconnect();
        }
        
        if (g_ps3_bt_ctx.state == BT_STATE_DISCONNECTED) {
            if (ps3_bt_connect() == 0) break;
        }
        
        usleep(1500000);
    }
    
    if (g_ps3_bt_ctx.intr_sock < 0) return -1;
    
    /* Send PS button press */
    uint8_t wake_report[DS3_BT_INPUT_REPORT_SIZE] = {0};
    wake_report[0] = BT_HIDP_DATA_RTYPE_INPUT;
    wake_report[1] = 0x01;
    wake_report[5] = DS3_BTN_PS;
    wake_report[7] = 0x80;
    wake_report[8] = 0x80;
    wake_report[9] = 0x80;
    wake_report[10] = 0x80;
    
    send(g_ps3_bt_ctx.intr_sock, wake_report, sizeof(wake_report), 0);
    usleep(150000);
    
    wake_report[5] = 0;
    send(g_ps3_bt_ctx.intr_sock, wake_report, sizeof(wake_report), 0);
    
    printf("[BT] Wake signal sent\n");
    return 0;
}

/* ============================================================================
 * THREAD FUNCTIONS
 * ============================================================================ */

/*
 * Connection policy
 * -----------------
 * The PS3 treats a USB-connected controller as authoritative and will not
 * drive it over Bluetooth while it is enumerated (same as a real DS3 with
 * the cable in). USB is therefore used for the pairing handshake only: once
 * the PS3 sends F4 over USB, the gadget is unbound from the UDC (a software
 * unplug) and the HID session is established over Bluetooth. Bluetooth then
 * carries everything, including wake from standby.
 *
 * With USB gone, a Bluetooth link drop is classified by its cause:
 *
 *   ETIMEDOUT  (HCI 0x08, supervision timeout)   Interference or range.
 *                                                The PS3 is still on; reconnect.
 *   ECONNRESET (HCI 0x13, remote terminated)     The PS3 closed the link,
 *                                                i.e. it is shutting down.
 *                                                Enter standby. Do not page
 *                                                the PS3: a page from a paired
 *                                                controller is exactly what
 *                                                wakes it.
 *   anything else                                Enter standby. The PS button
 *                                                recovers from a wrong guess.
 *
 * If the PS3 never sends F4 over Bluetooth, after BT_FALLBACK_FAILS attempts
 * the gadget is rebound and input stays on USB until the next USB session
 * (PS3 power cycle or cable replug).
 */
#define BT_HANDOFF_SETTLE_MS    500    /* F4 over USB -> soft unplug */
#define BT_CONNECT_DELAY_MS     250    /* soft unplug -> first BT connect */
#define BT_RETRY_FAST_MS        250    /* reconnect pacing: fast for the first few, then slow */
#define BT_RETRY_FAST_COUNT     3
#define BT_RETRY_SLOW_MS        1000
#define BT_F4_TIMEOUT_MS        2000   /* reconnect if no F4 within this of connect */
#define BT_FALLBACK_FAILS       4      /* consecutive no-F4 reconnects before USB fallback */
#define BT_REFUSED_FALLBACK_FAILS 2    /* consecutive ECONNREFUSED connects before USB fallback */
#define BT_ADAPTER_RETRY_MS     2000   /* retry interval while hci0 is absent */

void* ps3_bt_thread(void* arg) {
    (void)arg;
    printf("[BT] Management thread started\n");
    
    uint64_t next_attempt_ms = 0;
    int attempts = 0;              /* consecutive connect() failures */
    int f4_fails = 0;              /* consecutive no-F4 reconnects */
    int refused_fails = 0;         /* consecutive ECONNREFUSED connects */
    uint64_t unplug_time = 0;      /* time of soft unplug, 0 = USB still bound */
    uint32_t handed_off_session = 0;  /* USB session already handed off or fallen back on */
    int fallback = 0;              /* input is on USB because BT never got F4 */
    int await_fallback_session = 0; /* fallback rebind done, its ENABLE not yet seen */
    int had_ps3 = 0;               /* PS3 seen (USB alive or BT ready) since last standby */
    uint64_t next_adapter_try_ms = 0;
    
    while (g_running) {
        ps3_usb_check_suspend_timeout();  /* USB threads block while the host is quiet */
        
        /* hci0 is often not up yet when the service starts (rfkill unblock
         * runs immediately before it), and the WiFi toggle chord can take it
         * down later. Retry until it is available. */
        if (!g_bt_adapter_ready) {
            uint64_t t = time_get_ms();
            if (t >= next_adapter_try_ms) {
                next_adapter_try_ms = t + BT_ADAPTER_RETRY_MS;
                if (configure_adapter() == 0) {
                    printf("[BT] Adapter ready\n");
                    ps3_bt_load_addr();
                }
            }
            usleep(100000);
            continue;
        }
        
        if (system_is_standby()) {
            had_ps3 = 0;
            /* Console off - clear fallback state so the next boot retries BT */
            fallback = 0;
            await_fallback_session = 0;
            f4_fails = 0;
            refused_fails = 0;
            usleep(100000);
            continue;
        }
        
        uint64_t now = time_get_ms();
        int bt_alive = g_ps3_bt_ctx.state >= BT_STATE_READY &&
                       g_ps3_bt_ctx.state != BT_STATE_ERROR;
        if (ps3_usb_is_alive() || bt_alive) had_ps3 = 1;
        
        /* A new USB session (PS3 power cycle, cable replug, or fallback
         * rebind) resets the handoff bookkeeping. */
        uint32_t session = ps3_usb_session_id();
        if (ps3_usb_is_bound() && session != handed_off_session && fallback) {
            if (await_fallback_session) {
                /* Our own fallback rebind, not a power cycle - adopt it */
                await_fallback_session = 0;
                handed_off_session = session;
                printf("[BT] Fallback session established - staying on USB\n");
            } else {
                printf("[BT] New USB session - fallback cleared, will try BT again\n");
                fallback = 0;
                f4_fails = 0;
                refused_fails = 0;
            }
        }
        
        /* Bound to USB (handshake pending, or fallback). The only event of
         * interest is the PS3 turning off, which shows as USB dead with no
         * Bluetooth link. */
        if (ps3_usb_is_bound()) {
            /* Clear any stale socket from a wake attempt or failed connect so
             * the next handoff starts from DISCONNECTED. */
            if (g_ps3_bt_ctx.state == BT_STATE_ERROR) ps3_bt_disconnect();
            
            if (had_ps3 && ps3_usb_is_dead() && !bt_alive) {
                printf("[BT] USB dead while on USB - PS3 is off, entering standby\n");
                system_enter_standby();
                had_ps3 = 0;
                continue;
            }
            
            /* Handoff: F4 over USB means the PS3 has registered this
             * controller and its MAC. Settle briefly, then unbind. */
            uint64_t f4 = ps3_usb_handshake_done_ms();
            
            if (g_verbose) {
                static uint64_t wait_log_ms = 0;
                if (now - wait_log_ms >= 1000) {
                    wait_log_ms = now;
                    printf("[BT] Waiting for handoff: usb=%s session=%u(done=%u) f4=%s mac=%d fallback=%d\n",
                           ps3_usb_is_alive() ? "alive" : ps3_usb_is_dead() ? "dead" : "susp",
                           session, handed_off_session,
                           f4 ? "yes" : "no", ds3_has_ps3_mac(), fallback);
                }
            }
            if (!fallback && session != handed_off_session && f4 &&
                now - f4 >= BT_HANDOFF_SETTLE_MS && ds3_has_ps3_mac()) {
                handed_off_session = session;
                if (ps3_usb_soft_unplug() == 0) {
                    unplug_time = now;
                    next_attempt_ms = now + BT_CONNECT_DELAY_MS;
                    attempts = 0;
                    f4_fails = 0;
                    refused_fails = 0;
                }
            }
            usleep(50000);
            continue;
        }
        
        /* --- USB unbound: Bluetooth owns the session ------------------------ */
        switch (g_ps3_bt_ctx.state) {
            case BT_STATE_DISCONNECTED:
                if (fallback) { usleep(100000); break; }
                
                if (ds3_has_ps3_mac() && unplug_time && now >= next_attempt_ms) {
                    attempts++;
                    if (ps3_bt_connect() == 0) {
                        attempts = 0;
                    } else {
                        next_attempt_ms = now + (attempts <= BT_RETRY_FAST_COUNT
                                                 ? BT_RETRY_FAST_MS : BT_RETRY_SLOW_MS);
                    }
                }
                usleep(50000);
                break;
                
            case BT_STATE_READY:
            case BT_STATE_ENABLED:
                /* The PS3 only honors input after it sends F4. If it never
                 * does (seen after an unclean previous session), a fresh
                 * connection fixes it. It accepted this connect, so it is on
                 * and paging it again is safe. */
                if (g_ps3_bt_ctx.state == BT_STATE_READY &&
                    now - g_ps3_bt_ctx.connect_time >= BT_F4_TIMEOUT_MS) {
                    f4_fails++;
                    printf("[BT] No F4 enable from PS3 within %d ms - reconnecting (%d/%d)\n",
                           BT_F4_TIMEOUT_MS, f4_fails, BT_FALLBACK_FAILS);
                    ps3_bt_disconnect();
                    
                    if (f4_fails >= BT_FALLBACK_FAILS) {
                        printf("[BT] Giving up on BT for this session - rebinding USB\n");
                        fallback = 1;
                        if (ps3_usb_bind() < 0) {
                            printf("[BT] USB rebind failed - will keep trying BT\n");
                            fallback = 0;
                            f4_fails = 0;
                        } else {
                            await_fallback_session = 1;
                        }
                        continue;
                    }
                    next_attempt_ms = now + BT_RETRY_FAST_MS;
                    continue;
                }
                
                if (process_control() < 0 || process_interrupt() < 0) {
                    int err = g_drop_errno;
                    g_drop_errno = 0;
                    ps3_bt_disconnect();
                    g_ps3_bt_ctx.reconnect_count++;
                    
                    if (err == ETIMEDOUT) {
                        printf("[BT] Link dropped: supervision timeout - PS3 still on, reconnecting\n");
                        next_attempt_ms = now + BT_RETRY_FAST_MS;
                        attempts = 0;
                    } else {
                        printf("[BT] Link dropped: %s (errno %d) - PS3 turned off, entering standby\n",
                               err ? strerror(err) : "closed by peer", err);
                        system_enter_standby();
                        had_ps3 = 0;
                    }
                    continue;
                }
                
                /* Once-a-second link stats. outq = bytes still queued in the
                 * kernel L2CAP send buffer; est.queue = how long a report sent
                 * now waits behind that backlog. est.in->air adds the 25Hz
                 * gate (~20ms avg) and DS5->Pi (~4ms); PS3-side is unknown. */
                if (g_verbose) {
                    static uint64_t stat_last_ms = 0;
                    static uint32_t stat_last_sent = 0, stat_last_drop = 0;
                    static int stat_last_outq = 0;
                    if (stat_last_ms == 0) stat_last_ms = now;
                    if (g_ps3_bt_ctx.state == BT_STATE_ENABLED && now - stat_last_ms >= 1000) {
                        int outq = 0;
                        ioctl(g_ps3_bt_ctx.intr_sock, TIOCOUTQ, &outq);
                        uint32_t sent = g_ps3_bt_ctx.packets_sent - stat_last_sent;
                        uint32_t drop = g_ps3_bt_ctx.packets_dropped - stat_last_drop;
                        double secs = (now - stat_last_ms) / 1000.0;
                        int sent_bytes = sent * DS3_BT_INPUT_REPORT_SIZE;
                        int drained = sent_bytes - (outq - stat_last_outq);
                        double drain_rate = drained > 0 ? drained / secs : 0;
                        double q_delay = drain_rate > 0 ? outq / drain_rate : (outq ? 99.0 : 0);
                        printf("[BT STAT] tx=%.0f/s drop=%.0f/s outq=%dB drain=%.0fB/s  "
                               "est.queue=%.2fs  est.in->air=%.2fs\n",
                               sent / secs, drop / secs, outq, drain_rate,
                               q_delay, q_delay + 0.024);
                        stat_last_ms = now;
                        stat_last_sent = g_ps3_bt_ctx.packets_sent;
                        stat_last_drop = g_ps3_bt_ctx.packets_dropped;
                        stat_last_outq = outq;
                    }
                }
                usleep(10000);
                break;
                
            case BT_STATE_ERROR:
                ps3_bt_disconnect();
                
                /* Refused = BT stack not accepting (safe mode); won't fix
                 * itself, fall back to USB. Other errnos keep retrying. */
                if (g_connect_errno == ECONNREFUSED) {
                    refused_fails++;
                    if (refused_fails >= BT_REFUSED_FALLBACK_FAILS) {
                        printf("[BT] PS3 refusing BT (%d/%d) - likely safe mode, rebinding USB\n",
                               refused_fails, BT_REFUSED_FALLBACK_FAILS);
                        fallback = 1;
                        refused_fails = 0;
                        if (ps3_usb_bind() < 0) {
                            printf("[BT] USB rebind failed - will keep trying BT\n");
                            fallback = 0;
                        } else {
                            await_fallback_session = 1;
                        }
                        break;
                    }
                } else {
                    refused_fails = 0;
                }
                
                next_attempt_ms = now + (attempts <= BT_RETRY_FAST_COUNT
                                         ? BT_RETRY_FAST_MS : BT_RETRY_SLOW_MS);
                break;
                
            default:
                usleep(100000);
                break;
        }
    }
    
    ps3_bt_disconnect();
    printf("[BT] Management thread exiting\n");
    return NULL;
}

void* ps3_bt_motion_thread(void* arg) {
    (void)arg;
    printf("[BT] Motion thread started\n");
    
    while (g_running) {
        if (system_is_standby()) {
            usleep(100000);
            continue;
        }
        
        if (g_ps3_bt_ctx.state == BT_STATE_ENABLED) {
            send_input();
        }
        
        usleep(500);  /* Fast poll for responsive input */
    }
    
    printf("[BT] Motion thread exiting\n");
    return NULL;
}