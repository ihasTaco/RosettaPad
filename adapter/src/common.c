/*
 * RosettaPad - Common utilities and global state
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdint.h>
#include <unistd.h>
#include <pthread.h>
#include "common.h"

// Forward declarations for bt_hid functions
extern void bt_hid_disconnect(void);
extern int bt_hid_wake_ps3(void);

// =================================================================
// Global State Definitions
// =================================================================
volatile int g_running = 1;
volatile int g_usb_enabled = 0;

// =================================================================
// Standby Mode State
// =================================================================
volatile system_state_t g_system_state = SYSTEM_STATE_ACTIVE;
pthread_mutex_t g_system_state_mutex = PTHREAD_MUTEX_INITIALIZER;

// Controller wake request - user pressed PS button in standby
volatile int g_controller_wake_requested = 0;

void system_set_state(system_state_t state) {
    pthread_mutex_lock(&g_system_state_mutex);
    system_state_t old_state = g_system_state;
    g_system_state = state;
    pthread_mutex_unlock(&g_system_state_mutex);
    
    const char* state_names[] = {"ACTIVE", "STANDBY", "WAKING"};
    printf("[System] State change: %s -> %s\n", state_names[old_state], state_names[state]);
}

system_state_t system_get_state(void) {
    pthread_mutex_lock(&g_system_state_mutex);
    system_state_t state = g_system_state;
    pthread_mutex_unlock(&g_system_state_mutex);
    return state;
}

int system_is_standby(void) {
    return system_get_state() == SYSTEM_STATE_STANDBY;
}

void system_enter_standby(void) {
    printf("[System] *** ENTERING STANDBY MODE ***\n");
    printf("[System] PS3 power lost - entering low-power standby\n");
    
    // Set state first to prevent races
    system_set_state(SYSTEM_STATE_STANDBY);
    
    // Clear wake request
    g_controller_wake_requested = 0;
    
    // Disconnect any active BT connection to PS3
    bt_hid_disconnect();
    
    // Clear rumble
    pthread_mutex_lock(&g_rumble_mutex);
    g_rumble_right = 0;
    g_rumble_left = 0;
    pthread_mutex_unlock(&g_rumble_mutex);
    
    // Set lightbar to dim amber to indicate standby (low power)
    pthread_mutex_lock(&g_lightbar_mutex);
    g_lightbar_state.r = 30;
    g_lightbar_state.g = 15;
    g_lightbar_state.b = 0;
    g_lightbar_state.player_leds = 0;
    pthread_mutex_unlock(&g_lightbar_mutex);
    
    printf("[System] Standby mode active - press PS button to wake PS3\n");
}

void system_exit_standby(void) {
    printf("[System] *** EXITING STANDBY MODE ***\n");
    
    system_set_state(SYSTEM_STATE_WAKING);
    
    // Clear wake request
    g_controller_wake_requested = 0;
    
    // Restore lightbar to normal (red)
    pthread_mutex_lock(&g_lightbar_mutex);
    g_lightbar_state.r = 255;
    g_lightbar_state.g = 0;
    g_lightbar_state.b = 0;
    g_lightbar_state.player_leds = 0;
    pthread_mutex_unlock(&g_lightbar_mutex);
    
    // Try to wake PS3 via Bluetooth
    printf("[System] Sending wake signal to PS3...\n");
    if (bt_hid_wake_ps3() < 0) {
        printf("[System] Warning: Could not send BT wake signal\n");
    }
    
    // Go to active state (USB ENABLE will confirm when PS3 responds)
    system_set_state(SYSTEM_STATE_ACTIVE);
    printf("[System] Wake signal sent - waiting for PS3\n");
}

// File descriptors
int g_ep0_fd = -1;
int g_ep1_fd = -1;
int g_ep2_fd = -1;
int g_hidraw_fd = -1;

// DS3 input report - 49 bytes, initialized to neutral state
// Based on real DS3 capture from DS3_USB_Log_0001.txt
uint8_t g_ds3_report[DS3_REPORT_SIZE] = {
    // Bytes 0-15
    0x01,       // [0]  Report ID
    0x00,       // [1]  Reserved
    0x00,       // [2]  Buttons1: Select, L3, R3, Start, D-pad
    0x00,       // [3]  Buttons2: L2, R2, L1, R1, Triangle, Circle, Cross, Square
    0x00,       // [4]  PS button
    0x00,       // [5]  Reserved
    0x80,       // [6]  Left stick X (centered)
    0x80,       // [7]  Left stick Y (centered)
    0x80,       // [8]  Right stick X (centered)
    0x80,       // [9]  Right stick Y (centered)
    0x00,       // [10] D-pad Up pressure
    0x00,       // [11] D-pad Right pressure
    0x00,       // [12] D-pad Down pressure
    0x00,       // [13] D-pad Left pressure
    0x00,       // [14] Reserved
    0x00,       // [15] Reserved
    // Bytes 16-31
    0x00,       // [16] Reserved
    0x00,       // [17] Reserved
    0x00,       // [18] L2 pressure
    0x00,       // [19] R2 pressure
    0x00,       // [20] L1 pressure
    0x00,       // [21] R1 pressure
    0x00,       // [22] Triangle pressure
    0x00,       // [23] Circle pressure
    0x00,       // [24] Cross pressure
    0x00,       // [25] Square pressure
    0x00,       // [26] Reserved
    0x00,       // [27] Reserved
    0x00,       // [28] Reserved
    0x02,       // [29] Plugged status: 0x02=Plugged, 0x03=Unplugged
    0xee,       // [30] Battery: 0x00-0x05=capacity, 0xEE=charging, 0xEF=full, 0xF1=error
    0x12,       // [31] Connection: 0x10=USB+Rumble, 0x12=USB, 0x14=BT+Rumble, 0x16=BT
    // Bytes 32-48
    0x00,       // [32] Reserved
    0x00,       // [33] Reserved
    0x00,       // [34] Reserved
    0x00,       // [35] Reserved
    0x33, 0x04, // [36-37] Unknown status (from capture)
    0x77, 0x01, // [38-39] Unknown status (from capture)
    0xde, 0x02, // [40-41] Accelerometer X (rest ~734 = 0x02de)
    0x35, 0x02, // [42-43] Accelerometer Y (rest ~565 = 0x0235)
    0x08, 0x01, // [44-45] Accelerometer Z (rest ~264 = 0x0108)
    0x94, 0x00, // [46-47] Gyroscope Z (rest ~148 = 0x0094)
    0x02        // [48] Final byte
};
pthread_mutex_t g_report_mutex = PTHREAD_MUTEX_INITIALIZER;

// Rumble state
uint8_t g_rumble_right = 0;
uint8_t g_rumble_left = 0;
pthread_mutex_t g_rumble_mutex = PTHREAD_MUTEX_INITIALIZER;

// Lightbar state (default: red)
lightbar_state_t g_lightbar_state = {255, 0, 0, 0, 255};
pthread_mutex_t g_lightbar_mutex = PTHREAD_MUTEX_INITIALIZER;

// Touchpad-as-R3 configuration
volatile int g_touchpad_as_r3 = 1;  // Enabled by default
touchpad_state_t g_touchpad_state = {0, 0, 0, 0, 0};
pthread_mutex_t g_touchpad_mutex = PTHREAD_MUTEX_INITIALIZER;

// Bluetooth status for BT HID reports
// Default: unplugged, full battery, BT connection (matches real DS3 over BT)
bt_status_t g_bt_status = {
    .plugged_status = 0x03,   // DS3_STATUS_UNPLUGGED
    .battery_status = 0x05,   // DS3_BATTERY_FULL
    .connection_type = 0x16   // DS3_CONN_BT
};
pthread_mutex_t g_bt_status_mutex = PTHREAD_MUTEX_INITIALIZER;

void bt_status_update(uint8_t plugged, uint8_t battery, uint8_t connection) {
    pthread_mutex_lock(&g_bt_status_mutex);
    g_bt_status.plugged_status = plugged;
    g_bt_status.battery_status = battery;
    g_bt_status.connection_type = connection;
    pthread_mutex_unlock(&g_bt_status_mutex);
}

void bt_status_get(bt_status_t* out_status) {
    pthread_mutex_lock(&g_bt_status_mutex);
    *out_status = g_bt_status;
    pthread_mutex_unlock(&g_bt_status_mutex);
}

// =================================================================
// Controller Output Callback
// =================================================================
static controller_output_fn g_controller_output_handler = NULL;
static pthread_mutex_t g_controller_output_mutex = PTHREAD_MUTEX_INITIALIZER;

void controller_register_output(controller_output_fn handler) {
    pthread_mutex_lock(&g_controller_output_mutex);
    g_controller_output_handler = handler;
    pthread_mutex_unlock(&g_controller_output_mutex);
    printf("[Controller] Output handler registered\n");
}

void controller_unregister_output(void) {
    pthread_mutex_lock(&g_controller_output_mutex);
    g_controller_output_handler = NULL;
    pthread_mutex_unlock(&g_controller_output_mutex);
    printf("[Controller] Output handler unregistered\n");
}

void controller_send_output(
    uint8_t rumble_right,
    uint8_t rumble_left,
    uint8_t led_r,
    uint8_t led_g,
    uint8_t led_b,
    uint8_t player_leds)
{
    pthread_mutex_lock(&g_controller_output_mutex);
    if (g_controller_output_handler) {
        static int send_count = 0;
        if (++send_count <= 5 || send_count % 100 == 0) {
            printf("[Controller] Sending output: rumble=%d/%d LED=%d,%d,%d players=0x%02X\n",
                   rumble_right, rumble_left, led_r, led_g, led_b, player_leds);
        }
        g_controller_output_handler(rumble_right, rumble_left, led_r, led_g, led_b, player_leds);
    }
    pthread_mutex_unlock(&g_controller_output_mutex);
}

int controller_has_output_handler(void) {
    pthread_mutex_lock(&g_controller_output_mutex);
    int has_handler = (g_controller_output_handler != NULL);
    pthread_mutex_unlock(&g_controller_output_mutex);
    return has_handler;
}

// =================================================================
// Lightbar IPC (reads from web interface JSON file)
// =================================================================
static const char* LIGHTBAR_IPC_PATH = "/tmp/rosettapad/lightbar_state.json";

static int parse_lightbar_json(const char* json, lightbar_state_t* state) {
    const char* ptr;
    
    ptr = strstr(json, "\"r\":");
    if (ptr) state->r = (uint8_t)atoi(ptr + 4);
    
    ptr = strstr(json, "\"g\":");
    if (ptr) state->g = (uint8_t)atoi(ptr + 4);
    
    ptr = strstr(json, "\"b\":");
    if (ptr) state->b = (uint8_t)atoi(ptr + 4);
    
    ptr = strstr(json, "\"player_leds\":");
    if (ptr) state->player_leds = (uint8_t)atoi(ptr + 14);
    
    ptr = strstr(json, "\"player_led_brightness\":");
    if (ptr) {
        float brightness = atof(ptr + 24);
        state->player_brightness = (uint8_t)(brightness * 255);
    }
    
    return 0;
}

static void read_lightbar_state(void) {
    // Don't read lightbar config while in standby - we control it
    if (system_is_standby()) {
        return;
    }
    
    FILE* f = fopen(LIGHTBAR_IPC_PATH, "r");
    if (!f) return;
    
    char buf[256];
    if (fgets(buf, sizeof(buf), f)) {
        lightbar_state_t new_state;
        if (parse_lightbar_json(buf, &new_state) == 0) {
            pthread_mutex_lock(&g_lightbar_mutex);
            g_lightbar_state = new_state;
            pthread_mutex_unlock(&g_lightbar_mutex);
        }
    }
    fclose(f);
}

void* controller_output_thread(void* arg) {
    (void)arg;
    
    printf("[Controller] Generic output thread started\n");
    
    uint8_t last_right = 0, last_left = 0;
    lightbar_state_t last_lightbar = {0, 0, 0, 0, 0};
    int update_count = 0;
    int had_handler = 0;  // Track if we previously had a handler
    int keepalive_count = 0;  // Force periodic sends
    int rumble_debug_count = 0;  // Debug counter
    
    while (g_running) {
        // Check for lightbar config changes periodically (but not in standby)
        if (++update_count >= 50) {  // Every ~500ms
            update_count = 0;
            read_lightbar_state();
        }
        
        // Get current rumble state
        pthread_mutex_lock(&g_rumble_mutex);
        uint8_t right = g_rumble_right;
        uint8_t left = g_rumble_left;
        pthread_mutex_unlock(&g_rumble_mutex);
        
        // Get current lightbar state
        pthread_mutex_lock(&g_lightbar_mutex);
        lightbar_state_t lightbar = g_lightbar_state;
        pthread_mutex_unlock(&g_lightbar_mutex);
        
        int has_handler = controller_has_output_handler();
        
        // Check if something changed
        int rumble_changed = (right != last_right || left != last_left);
        int lightbar_changed = (lightbar.r != last_lightbar.r ||
                                lightbar.g != last_lightbar.g ||
                                lightbar.b != last_lightbar.b ||
                                lightbar.player_leds != last_lightbar.player_leds);
        
        // Debug: log rumble state periodically
        if (++rumble_debug_count >= 500) {  // Every ~5 seconds
            rumble_debug_count = 0;
            printf("[Controller] State check: rumble=%d/%d (last=%d/%d) handler=%d standby=%d\n",
                   right, left, last_right, last_left, has_handler, system_is_standby());
        }
        
        // Send if: (something changed AND we have handler) OR (handler just registered)
        int handler_just_registered = (has_handler && !had_handler);
        
        // Also send periodically as keep-alive (every ~1 second)
        int keepalive = 0;
        if (has_handler && ++keepalive_count >= 100) {
            keepalive_count = 0;
            keepalive = 1;
        }
        
        if (rumble_changed && has_handler) {
            printf("[Controller] Rumble changed! %d/%d -> %d/%d\n",
                   last_right, last_left, right, left);
        }
        
        if (has_handler && (rumble_changed || lightbar_changed || handler_just_registered || keepalive)) {
            controller_send_output(right, left,
                                   lightbar.r, lightbar.g, lightbar.b,
                                   lightbar.player_leds);
            
            last_right = right;
            last_left = left;
            last_lightbar = lightbar;
        }
        
        had_handler = has_handler;
        usleep(10000);  // 100Hz output rate
    }
    
    printf("[Controller] Generic output thread exiting\n");
    return NULL;
}

// =================================================================
// Debug Utilities
// =================================================================
void print_hex(const char* label, const uint8_t* data, size_t len) {
    printf("%s (%zu bytes):\n  ", label, len);
    for (size_t i = 0; i < len && i < 64; i++) {
        printf("%02x ", data[i]);
        if ((i + 1) % 16 == 0 && i + 1 < len) printf("\n  ");
    }
    printf("\n");
    fflush(stdout);
}