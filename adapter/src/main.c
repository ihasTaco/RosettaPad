/*
 * RosettaPad - DualSense to PS3 Controller Adapter
 * Main entry point with USB/Bluetooth mode selection
 * 
 * =================================================================
 * MODE SELECTION - Change these flags to switch modes:
 * =================================================================
 */

// Set to 1 for Bluetooth mode, 0 for USB mode
#define USE_BLUETOOTH_MODE  0

// Debug flags - set to desired categories (see debug.h for all options)
// Examples:
//   DBG_QUICK                    - Errors, warnings, info only
//   DBG_ALL                      - Everything (very verbose!)
//   DBG_HANDSHAKE | DBG_PAIRING  - Protocol debugging
//   DBG_MOTION                   - Motion sensor debugging
//   DBG_USB_ALL | DBG_REPORTS    - USB protocol debugging
//   DBG_BT_ALL | DBG_PAIRING     - Bluetooth debugging
#define DEFAULT_DEBUG_FLAGS  (DBG_ERROR | DBG_WARN | DBG_INFO | DBG_INIT)

/*
 * =================================================================
 * 
 * Or use config file: /etc/rosettapad/config
 *   MODE=usb        (or MODE=bluetooth)
 *   DEBUG=handshake,pairing
 * 
 * Or use environment variables:
 *   ROSETTAPAD_MODE=bluetooth
 *   ROSETTAPAD_DEBUG=all
 * 
 * Or use command line (if not running as service):
 *   ./rosettapad --bluetooth --debug handshake
 * 
 * Architecture:
 *   - common.c/h    : Shared state and utilities
 *   - debug.c/h     : Unified debug system
 *   - ds3.c/h       : PS3/DualShock 3 emulation layer
 *   - dualsense.c/h : PS5/DualSense controller interface
 *   - usb_gadget.c/h: USB FunctionFS handling
 *   - bt_hid.c/h    : Bluetooth HID handling
 *   - main.c        : Thread orchestration and lifecycle
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h>
#include <signal.h>
#include <unistd.h>
#include <pthread.h>
#include <fcntl.h>
#include <getopt.h>

#include "common.h"
#include "debug.h"
#include "ds3.h"
#include "dualsense.h"

#ifdef ENABLE_USB
#include "usb_gadget.h"
#endif

#ifdef ENABLE_BLUETOOTH
#include "bt_hid.h"
#endif

// =================================================================
// Mode Selection
// =================================================================
typedef enum {
    MODE_USB,
    MODE_BLUETOOTH,
    MODE_AUTO          // Start USB, switch to BT once paired
} adapter_mode_t;

// Default from compile-time flag
#if USE_BLUETOOTH_MODE == 1
static adapter_mode_t g_mode = MODE_BLUETOOTH;
#else
static adapter_mode_t g_mode = MODE_AUTO;  // Auto is the smart default
#endif

// =================================================================
// Config File Reader
// =================================================================
#define CONFIG_FILE "/etc/rosettapad/config"

static void load_config(void) {
    // First check environment variables (highest priority)
    const char* env_mode = getenv("ROSETTAPAD_MODE");
    if (env_mode) {
        if (strcasecmp(env_mode, "bluetooth") == 0 || strcasecmp(env_mode, "bt") == 0) {
#ifdef ENABLE_BLUETOOTH
            g_mode = MODE_BLUETOOTH;
#endif
        } else if (strcasecmp(env_mode, "usb") == 0) {
#ifdef ENABLE_USB
            g_mode = MODE_USB;
#endif
        } else if (strcasecmp(env_mode, "auto") == 0) {
            g_mode = MODE_AUTO;
        }
    }
    
    const char* env_debug = getenv("ROSETTAPAD_DEBUG");
    if (env_debug) {
        debug_set_flags(debug_parse_flags(env_debug));
    }
    
    // Then check config file (lower priority than env)
    FILE* f = fopen(CONFIG_FILE, "r");
    if (!f) return;
    
    char line[256];
    while (fgets(line, sizeof(line), f)) {
        // Skip comments and empty lines
        if (line[0] == '#' || line[0] == '\n' || line[0] == '\0') continue;
        
        char* eq = strchr(line, '=');
        if (!eq) continue;
        
        *eq = '\0';
        char* key = line;
        char* value = eq + 1;
        
        // Trim whitespace/newline from value
        char* nl = strchr(value, '\n');
        if (nl) *nl = '\0';
        while (*value == ' ') value++;
        
        // Only apply if env var wasn't set
        if (strcasecmp(key, "MODE") == 0 && !env_mode) {
            if (strcasecmp(value, "bluetooth") == 0 || strcasecmp(value, "bt") == 0) {
#ifdef ENABLE_BLUETOOTH
                g_mode = MODE_BLUETOOTH;
#endif
            } else if (strcasecmp(value, "usb") == 0) {
#ifdef ENABLE_USB
                g_mode = MODE_USB;
#endif
            } else if (strcasecmp(value, "auto") == 0) {
                g_mode = MODE_AUTO;
            }
        }
        else if (strcasecmp(key, "DEBUG") == 0 && !env_debug) {
            debug_set_flags(debug_parse_flags(value));
        }
    }
    fclose(f);
}

// =================================================================
// Signal Handler
// =================================================================
static void signal_handler(int sig) {
    (void)sig;
    printf("\n[Main] Shutdown requested...\n");
    g_running = 0;
}

// =================================================================
// Banner
// =================================================================
static void print_banner(void) {
    printf("\n");
    printf("--------------------------------------------------------------\n");
    printf("|                     RosettaPad v0.3                        |\n");
    printf("|            DualSense to PS3 Controller Adapter             |\n");
    printf("--------------------------------------------------------------\n");
    printf("|  Modules:                                                  |\n");
    printf("|    - DS3 Emulation    - PlayStation 3 protocol             |\n");
    printf("|    - DualSense Input  - PS5 controller via Bluetooth       |\n");
#ifdef ENABLE_USB
    printf("|    - USB Gadget       - FunctionFS to PS3                  |\n");
#endif
#ifdef ENABLE_BLUETOOTH
    printf("|    - Bluetooth HID    - Wireless PS3 connection            |\n");
#endif
    printf("--------------------------------------------------------------\n");
    printf("\n");
}

// =================================================================
// Help
// =================================================================
static void print_help(const char* prog) {
    printf("Usage: %s [options]\n\n", prog);
    printf("Options:\n");
#ifdef ENABLE_USB
    printf("  -u, --usb              USB gadget mode\n");
#endif
#ifdef ENABLE_BLUETOOTH
    printf("  -b, --bluetooth        Bluetooth HID mode\n");
#endif
    printf("  -a, --auto             Auto mode (BT if paired, else USB) [default]\n");
    printf("  -d, --debug CATS       Enable debug categories (comma-separated)\n");
    printf("  -l, --list-debug       List available debug categories\n");
    printf("  -h, --help             Show this help\n");
    printf("\n");
    printf("Auto mode (default):\n");
    printf("  - If PS3 pairing exists: connects via Bluetooth\n");
    printf("  - If no pairing: uses USB mode to pair with PS3\n");
    printf("  - After pairing via USB, restart to use Bluetooth\n");
    printf("\n");
    printf("Examples:\n");
    printf("  %s                         # Auto mode (recommended)\n", prog);
    printf("  %s --debug handshake       # Auto mode with handshake debug\n", prog);
    printf("  %s -b -d bt,pairing        # Force Bluetooth mode\n", prog);
    printf("\n");
    printf("Config file: /etc/rosettapad/config\n");
    printf("  MODE=auto|usb|bluetooth\n");
    printf("  DEBUG=error,warn,info\n");
    printf("\n");
}

// =================================================================
// USB Mode
// =================================================================
#ifdef ENABLE_USB
static int run_usb_mode(void) {
    pthread_t ds_input_tid;
    pthread_t ds_output_tid;
    pthread_t usb_ctrl_tid;
    pthread_t usb_in_tid;
    pthread_t usb_out_tid;
    
    debug_print(DBG_INFO, "[Main] Starting in USB mode");
    
    // Setup USB gadget
    if (usb_gadget_init() < 0) {
        debug_print(DBG_ERROR, "[Main] Failed to initialize USB gadget");
        return 1;
    }
    
    // Open ep0 and write descriptors
    g_ep0_fd = usb_open_endpoint(0);
    if (g_ep0_fd < 0) {
        debug_print(DBG_ERROR, "[Main] Failed to open ep0");
        return 1;
    }
    
    if (usb_gadget_write_descriptors(g_ep0_fd) < 0) {
        debug_print(DBG_ERROR, "[Main] Failed to write USB descriptors");
        close(g_ep0_fd);
        return 1;
    }
    
    // Start threads
    debug_print(DBG_INFO, "[Main] Starting threads...");
    
    pthread_create(&ds_input_tid, NULL, dualsense_thread, NULL);
    pthread_create(&ds_output_tid, NULL, dualsense_output_thread, NULL);
    pthread_create(&usb_ctrl_tid, NULL, usb_control_thread, NULL);
    pthread_create(&usb_in_tid, NULL, usb_input_thread, NULL);
    pthread_create(&usb_out_tid, NULL, usb_output_thread, NULL);
    
    // Bind to UDC
    debug_print(DBG_INFO, "[Main] Binding to USB...");
    if (usb_gadget_bind() < 0) {
        debug_print(DBG_WARN, "[Main] Failed to bind to UDC");
    }
    
    printf("\n");
    printf("----------------------------------------------------------------\n");
    printf("  USB Adapter running! Press Ctrl+C to stop.\n");
    printf("  Connect Pi to PS3 via USB cable.\n");
    printf("----------------------------------------------------------------\n");
    printf("\n");
    fflush(stdout);
    
    // Wait for shutdown or pairing complete
    while (g_running && !g_pairing_complete) {
        sleep(1);
    }
    
    int switch_to_bt = g_pairing_complete;
    
    // Cleanup
    if (switch_to_bt) {
        debug_print(DBG_INFO, "[Main] Pairing complete! Switching to Bluetooth mode...");
        printf("\n");
        printf("----------------------------------------------------------------\n");
        printf("  Pairing complete! Switching to Bluetooth mode...\n");
        printf("  >>> UNPLUG THE USB CABLE NOW <<<\n");
        printf("----------------------------------------------------------------\n");
        printf("\n");
        fflush(stdout);
        
        // Wait a moment for user to unplug USB
        sleep(3);
        
        // Set flag so UNBIND doesn't kill everything
        g_mode_switching = 1;
    } else {
        debug_print(DBG_INFO, "[Main] Shutting down USB mode...");
    }
    
    if (g_hidraw_fd >= 0) {
        dualsense_send_output(g_hidraw_fd, 0, 0, 0, 0, 0, 0);
    }
    
    usb_gadget_unbind();
    
    // Close file descriptors
    if (g_ep1_fd >= 0) { close(g_ep1_fd); g_ep1_fd = -1; }
    if (g_ep2_fd >= 0) { close(g_ep2_fd); g_ep2_fd = -1; }
    if (g_ep0_fd >= 0) { close(g_ep0_fd); g_ep0_fd = -1; }
    // Don't close hidraw - we'll reuse it for BT mode
    
    // Clear mode switching flag
    g_mode_switching = 0;
    
    // Return 2 to signal "switch to BT mode"
    return switch_to_bt ? 2 : 0;
}
#endif

// =================================================================
// Bluetooth Mode
// =================================================================
#ifdef ENABLE_BLUETOOTH
static int run_bluetooth_mode(void) {
    pthread_t ds_input_tid;
    pthread_t ds_output_tid;
    pthread_t bt_out_tid;
    pthread_t bt_in_tid;
    
    debug_print(DBG_INFO, "[Main] Starting in Bluetooth mode");
    
    // Initialize Bluetooth HID
    if (bt_hid_init() < 0) {
        debug_print(DBG_ERROR, "[Main] Failed to initialize Bluetooth HID");
        return 1;
    }
    
    // Check if we have pairing info
    if (!bt_hid_is_paired()) {
        printf("\n");
        printf("----------------------------------------------------------------\n");
        printf("  No PS3 pairing found!\n");
        printf("\n");
        printf("  To pair with your PS3:\n");
        printf("    1. Run: ./rosettapad --usb\n");
        printf("    2. Connect Pi to PS3 via USB cable\n");
        printf("    3. Wait for PS3 to recognize the controller\n");
        printf("    4. The PS3's Bluetooth MAC will be saved automatically\n");
        printf("    5. Then you can use: ./rosettapad --bluetooth\n");
        printf("----------------------------------------------------------------\n");
        printf("\n");
        bt_hid_cleanup();
        return 1;
    }
    
    // Start DualSense threads
    debug_print(DBG_INFO, "[Main] Starting DualSense threads...");
    pthread_create(&ds_input_tid, NULL, dualsense_thread, NULL);
    pthread_create(&ds_output_tid, NULL, dualsense_output_thread, NULL);
    
    // Connect to PS3 via Bluetooth
    debug_print(DBG_INFO, "[Main] Connecting to PS3 via Bluetooth...");
    
    // Get PS3 MAC for display
    uint8_t ps3_mac[6];
    char ps3_mac_str[18] = "unknown";
    if (bt_hid_get_ps3_mac(ps3_mac) == 0) {
        snprintf(ps3_mac_str, sizeof(ps3_mac_str), "%02X:%02X:%02X:%02X:%02X:%02X",
                 ps3_mac[0], ps3_mac[1], ps3_mac[2], ps3_mac[3], ps3_mac[4], ps3_mac[5]);
    }
    
    if (bt_hid_connect() < 0) {
        printf("\n");
        printf("----------------------------------------------------------------\n");
        printf("  Failed to connect to PS3!\n");
        printf("\n");
        printf("  PS3 MAC: %s\n", ps3_mac_str);
        printf("\n");
        printf("  Troubleshooting:\n");
        printf("    1. Is PS3 powered ON (not standby)?\n");
        printf("    2. Did you pair via USB first?\n");
        printf("       Run: sudo ./rosettapad --usb\n");
        printf("       Connect Pi to PS3 via USB, wait for recognition\n");
        printf("    3. Is another controller connected to PS3?\n");
        printf("       Try disconnecting other controllers first\n");
        printf("    4. Check Bluetooth adapter:\n");
        printf("       Run: hciconfig hci0\n");
        printf("       Should show UP RUNNING\n");
        printf("    5. Clear pairing and re-pair:\n");
        printf("       sudo rm /etc/rosettapad/pairing.conf\n");
        printf("       Then re-pair via USB\n");
        printf("----------------------------------------------------------------\n");
        printf("\n");
        g_running = 0;
    } else {
        // Start Bluetooth threads
        pthread_create(&bt_out_tid, NULL, bt_hid_output_thread, NULL);
        pthread_create(&bt_in_tid, NULL, bt_hid_input_thread, NULL);
        
        printf("\n");
        printf("----------------------------------------------------------------\n");
        printf("  Bluetooth Adapter running! Press Ctrl+C to stop.\n");
        printf("  Connected to PS3 wirelessly.\n");
        printf("----------------------------------------------------------------\n");
        printf("\n");
        fflush(stdout);
    }
    
    // Wait for shutdown
    while (g_running) {
        sleep(1);
        
        // Reconnect if disconnected
        if (!bt_hid_is_connected() && g_running) {
            debug_print(DBG_BT, "[Main] Connection lost, attempting reconnect...");
            if (bt_hid_connect() == 0) {
                debug_print(DBG_BT, "[Main] Reconnected!");
            }
            sleep(2);  // Wait before retry
        }
    }
    
    // Cleanup
    debug_print(DBG_INFO, "[Main] Shutting down Bluetooth mode...");
    
    if (g_hidraw_fd >= 0) {
        dualsense_send_output(g_hidraw_fd, 0, 0, 0, 0, 0, 0);
    }
    
    bt_hid_disconnect();
    bt_hid_cleanup();
    
    if (g_hidraw_fd >= 0) close(g_hidraw_fd);
    
    return 0;
}
#endif

// =================================================================
// Main
// =================================================================
int main(int argc, char* argv[]) {
    // Set default debug flags from compile-time constant
    debug_set_flags(DEFAULT_DEBUG_FLAGS);
    
    // Load config from file and/or environment
    load_config();
    
    // Command line args override everything (if running interactively)
    static struct option long_options[] = {
#ifdef ENABLE_USB
        {"usb",        no_argument,       0, 'u'},
#endif
#ifdef ENABLE_BLUETOOTH
        {"bluetooth",  no_argument,       0, 'b'},
#endif
        {"auto",       no_argument,       0, 'a'},
        {"debug",      required_argument, 0, 'd'},
        {"list-debug", no_argument,       0, 'l'},
        {"help",       no_argument,       0, 'h'},
        {0, 0, 0, 0}
    };
    
    // Parse command line
    int opt;
    while ((opt = getopt_long(argc, argv, "ubad:lh", long_options, NULL)) != -1) {
        switch (opt) {
#ifdef ENABLE_USB
            case 'u':
                g_mode = MODE_USB;
                break;
#endif
#ifdef ENABLE_BLUETOOTH
            case 'b':
                g_mode = MODE_BLUETOOTH;
                break;
#endif
            case 'a':
                g_mode = MODE_AUTO;
                break;
            case 'd':
                debug_set_flags(debug_parse_flags(optarg));
                break;
            case 'l':
                debug_print_categories();
                return 0;
            case 'h':
                print_help(argv[0]);
                return 0;
            default:
                print_help(argv[0]);
                return 1;
        }
    }
    
    print_banner();
    
    // Setup signal handlers
    signal(SIGINT, signal_handler);
    signal(SIGTERM, signal_handler);
    
    // Initialize debug system (already have flags set)
    debug_init();
    
    // Show current configuration
    const char* mode_str = "USB";
    if (g_mode == MODE_BLUETOOTH) mode_str = "BLUETOOTH";
    else if (g_mode == MODE_AUTO) mode_str = "AUTO";
    debug_print(DBG_INFO, "[Main] Mode: %s", mode_str);
    debug_print(DBG_INFO, "[Main] Debug flags: 0x%08X", debug_get_flags());
    
    // Initialize common modules
    debug_print(DBG_INFO, "[Main] Initializing modules...");
    ds3_init();
    dualsense_init();
    
#ifdef ENABLE_BLUETOOTH
    // ALWAYS initialize BT briefly to get our MAC address for the F2 report
    // This is critical - the PS3 reads our BT MAC during USB pairing
    // If we report the wrong MAC, BT mode won't work properly later
    if (bt_hid_init() == 0) {
        uint8_t local_mac[6];
        if (bt_hid_get_local_mac(local_mac) == 0) {
            ds3_set_local_bt_mac(local_mac);
            debug_print(DBG_INFO, "[Main] BT MAC configured for F2 report");
        }
        
        // If not in BT mode, cleanup (will re-init later if needed)
        if (g_mode != MODE_BLUETOOTH && g_mode != MODE_AUTO) {
            bt_hid_cleanup();
        }
    } else {
        debug_print(DBG_WARN, "[Main] Could not init BT to get MAC - F2 report may have wrong MAC!");
    }
#endif
    
    // Run selected mode
    int result = 0;
    
    // AUTO mode: check if paired, use BT if yes, USB if no
    if (g_mode == MODE_AUTO) {
#if defined(ENABLE_USB) && defined(ENABLE_BLUETOOTH)
        // Re-initialize BT if we cleaned it up above, and check pairing
        if (bt_hid_init() == 0) {
            if (bt_hid_is_paired()) {
                debug_print(DBG_INFO, "[Main] AUTO: Pairing found, using Bluetooth mode");
                bt_hid_cleanup();  // Will be re-initialized in run_bluetooth_mode
                g_mode = MODE_BLUETOOTH;
            } else {
                debug_print(DBG_INFO, "[Main] AUTO: No pairing found, using USB mode for pairing");
                bt_hid_cleanup();
                g_mode = MODE_USB;
            }
        } else {
            debug_print(DBG_WARN, "[Main] AUTO: BT init failed, using USB mode");
            g_mode = MODE_USB;
        }
#elif defined(ENABLE_USB)
        g_mode = MODE_USB;
#elif defined(ENABLE_BLUETOOTH)
        g_mode = MODE_BLUETOOTH;
#endif
    }
    
run_mode:
    switch (g_mode) {
#ifdef ENABLE_USB
        case MODE_USB:
            result = run_usb_mode();
            // If result is 2, USB pairing completed - switch to BT
            if (result == 2) {
#ifdef ENABLE_BLUETOOTH
                debug_print(DBG_INFO, "[Main] Switching to Bluetooth mode after pairing...");
                sleep(2);  // Give PS3 a moment to process
                g_mode = MODE_BLUETOOTH;
                g_pairing_complete = 0;  // Reset flag
                goto run_mode;
#else
                debug_print(DBG_INFO, "[Main] Pairing saved. Restart to use Bluetooth mode.");
                result = 0;
#endif
            }
            break;
#endif
#ifdef ENABLE_BLUETOOTH
        case MODE_BLUETOOTH:
            result = run_bluetooth_mode();
            break;
#endif
        default:
            debug_print(DBG_ERROR, "[Main] No valid mode available!");
            result = 1;
            break;
    }
    
    debug_print(DBG_INFO, "[Main] Goodbye!");
    return result;
}