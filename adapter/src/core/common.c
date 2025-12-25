/*
 * RosettaPad - Core Common Implementation
 * ========================================
 * 
 * Shared state management and utilities.
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/time.h>
#include <unistd.h>

#include "core/common.h"

/* ============================================================================
 * GLOBAL STATE
 * ============================================================================ */

volatile int g_running = 1;

/* ============================================================================
 * SYSTEM STATE MACHINE
 * ============================================================================ */

static volatile system_state_t g_system_state = SYSTEM_STATE_ACTIVE;
static pthread_mutex_t g_system_state_mutex = PTHREAD_MUTEX_INITIALIZER;
static uint64_t g_last_state_change_time = 0;

/* Minimum time between state changes (ms) - prevents rapid oscillation */
#define STATE_CHANGE_DEBOUNCE_MS 2000

static const char* state_names[] = {"ACTIVE", "STANDBY", "WAKING"};

void system_set_state(system_state_t state) {
    pthread_mutex_lock(&g_system_state_mutex);
    system_state_t old_state = g_system_state;
    g_system_state = state;
    g_last_state_change_time = time_get_ms();
    pthread_mutex_unlock(&g_system_state_mutex);
    
    printf("[System] State: %s -> %s\n", state_names[old_state], state_names[state]);
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

/* Check if we can change state (debounce) */
static int can_change_state(void) {
    pthread_mutex_lock(&g_system_state_mutex);
    uint64_t now = time_get_ms();
    uint64_t elapsed = now - g_last_state_change_time;
    pthread_mutex_unlock(&g_system_state_mutex);
    
    return elapsed >= STATE_CHANGE_DEBOUNCE_MS;
}

/* Forward declarations for console-specific functions */
extern void ps3_bt_disconnect(void);
extern int ps3_bt_wake(void);

void system_enter_standby(void) {
    /* Debounce - don't enter standby if we just changed state */
    if (!can_change_state()) {
        printf("[System] Ignoring standby request (debounce)\n");
        return;
    }
    
    /* Don't enter standby if we're already in standby or waking */
    system_state_t current = system_get_state();
    if (current != SYSTEM_STATE_ACTIVE) {
        printf("[System] Ignoring standby request (not active, state=%s)\n", 
               state_names[current]);
        return;
    }
    
    printf("[System] *** ENTERING STANDBY MODE ***\n");
    
    system_set_state(SYSTEM_STATE_STANDBY);
    
    /* Disconnect Bluetooth to PS3 */
    ps3_bt_disconnect();
    
    /* Set dim amber lightbar to indicate standby */
    pthread_mutex_lock(&g_controller_output_mutex);
    g_controller_output.rumble_left = 0;
    g_controller_output.rumble_right = 0;
    g_controller_output.led_r = 30;
    g_controller_output.led_g = 15;
    g_controller_output.led_b = 0;
    g_controller_output.player_leds = 0;
    pthread_mutex_unlock(&g_controller_output_mutex);
    
    printf("[System] Standby active - press PS button to wake\n");
}

void system_exit_standby(void) {
    /* Debounce - don't wake if we just changed state */
    if (!can_change_state()) {
        printf("[System] Ignoring wake request (debounce)\n");
        return;
    }
    
    /* Only exit standby if we're actually in standby */
    if (system_get_state() != SYSTEM_STATE_STANDBY) {
        printf("[System] Ignoring wake request (not in standby)\n");
        return;
    }
    
    printf("[System] *** EXITING STANDBY MODE ***\n");
    
    system_set_state(SYSTEM_STATE_WAKING);
    
    /* Restore normal lightbar (red) */
    pthread_mutex_lock(&g_controller_output_mutex);
    g_controller_output.led_r = 255;
    g_controller_output.led_g = 0;
    g_controller_output.led_b = 0;
    pthread_mutex_unlock(&g_controller_output_mutex);
    
    /* Try to wake PS3 via Bluetooth */
    printf("[System] Sending wake signal to PS3...\n");
    if (ps3_bt_wake() < 0) {
        printf("[System] Warning: Wake signal failed\n");
    }
    
    system_set_state(SYSTEM_STATE_ACTIVE);
}

/* ============================================================================
 * CONTROLLER STATE MANAGEMENT
 * ============================================================================ */

controller_state_t g_controller_state = {
    .buttons = 0,
    .left_stick_x = 128,
    .left_stick_y = 128,
    .right_stick_x = 128,
    .right_stick_y = 128,
    .left_trigger = 0,
    .right_trigger = 0,
    .accel_x = 0,
    .accel_y = 0,
    .accel_z = 0,
    .gyro_x = 0,
    .gyro_y = 0,
    .gyro_z = 0,
    .touch = {{0, 0, 0}, {0, 0, 0}},
    .battery_level = 100,
    .battery_charging = 0,
    .timestamp_ms = 0
};
pthread_mutex_t g_controller_state_mutex = PTHREAD_MUTEX_INITIALIZER;

void controller_state_update(const controller_state_t* state) {
    pthread_mutex_lock(&g_controller_state_mutex);
    memcpy(&g_controller_state, state, sizeof(controller_state_t));
    pthread_mutex_unlock(&g_controller_state_mutex);
}

void controller_state_copy(controller_state_t* out_state) {
    pthread_mutex_lock(&g_controller_state_mutex);
    memcpy(out_state, &g_controller_state, sizeof(controller_state_t));
    pthread_mutex_unlock(&g_controller_state_mutex);
}

/* ============================================================================
 * OUTPUT STATE MANAGEMENT
 * ============================================================================ */

controller_output_t g_controller_output = {
    .rumble_left = 0,
    .rumble_right = 0,
    .led_r = 255,
    .led_g = 0,
    .led_b = 0,
    .player_leds = 0,
    .player_brightness = 255
};
pthread_mutex_t g_controller_output_mutex = PTHREAD_MUTEX_INITIALIZER;

static int g_output_changed = 0;

void controller_output_update(const controller_output_t* output) {
    pthread_mutex_lock(&g_controller_output_mutex);
    
    /* Check if anything changed */
    if (memcmp(&g_controller_output, output, sizeof(controller_output_t)) != 0) {
        memcpy(&g_controller_output, output, sizeof(controller_output_t));
        g_output_changed = 1;
    }
    
    pthread_mutex_unlock(&g_controller_output_mutex);
}

void controller_output_copy(controller_output_t* out_output) {
    pthread_mutex_lock(&g_controller_output_mutex);
    memcpy(out_output, &g_controller_output, sizeof(controller_output_t));
    pthread_mutex_unlock(&g_controller_output_mutex);
}

int controller_output_changed(void) {
    pthread_mutex_lock(&g_controller_output_mutex);
    int changed = g_output_changed;
    g_output_changed = 0;
    pthread_mutex_unlock(&g_controller_output_mutex);
    return changed;
}

/* ============================================================================
 * LIGHTBAR IPC
 * ============================================================================ */

void lightbar_read_ipc(controller_output_t* output) {
    /* Don't read IPC in standby - we control the lightbar */
    if (system_is_standby()) {
        return;
    }
    
    FILE* f = fopen(LIGHTBAR_IPC_PATH, "r");
    if (!f) return;
    
    char buf[256];
    if (fgets(buf, sizeof(buf), f)) {
        /* Simple JSON parsing */
        const char* ptr;
        
        ptr = strstr(buf, "\"r\":");
        if (ptr) output->led_r = (uint8_t)atoi(ptr + 4);
        
        ptr = strstr(buf, "\"g\":");
        if (ptr) output->led_g = (uint8_t)atoi(ptr + 4);
        
        ptr = strstr(buf, "\"b\":");
        if (ptr) output->led_b = (uint8_t)atoi(ptr + 4);
        
        ptr = strstr(buf, "\"player_leds\":");
        if (ptr) output->player_leds = (uint8_t)atoi(ptr + 14);
        
        ptr = strstr(buf, "\"player_led_brightness\":");
        if (ptr) {
            float brightness = atof(ptr + 24);
            output->player_brightness = (uint8_t)(brightness * 255);
        }
    }
    fclose(f);
}

/* ============================================================================
 * CONTROLLER OUTPUT THREAD
 * ============================================================================ */

/* Forward declaration - set by controller driver when connected */
static int g_controller_fd = -1;
static const controller_driver_t* g_active_driver = NULL;

void controller_set_active(int fd, const controller_driver_t* driver) {
    g_controller_fd = fd;
    g_active_driver = driver;
}

void controller_clear_active(void) {
    g_controller_fd = -1;
    g_active_driver = NULL;
}

void* controller_output_thread(void* arg) {
    (void)arg;
    
    printf("[Output] Controller output thread started\n");
    
    controller_output_t last_output = {0};
    int ipc_counter = 0;
    int consecutive_failures = 0;
    
    while (g_running) {
        /* Check for lightbar IPC updates every ~500ms */
        if (++ipc_counter >= 50) {
            ipc_counter = 0;
            
            controller_output_t output;
            controller_output_copy(&output);
            lightbar_read_ipc(&output);
            controller_output_update(&output);
        }
        
        /* Get current output state */
        controller_output_t output;
        controller_output_copy(&output);
        
        /* Check if anything changed */
        int changed = (
            output.rumble_left != last_output.rumble_left ||
            output.rumble_right != last_output.rumble_right ||
            output.led_r != last_output.led_r ||
            output.led_g != last_output.led_g ||
            output.led_b != last_output.led_b ||
            output.player_leds != last_output.player_leds
        );
        
        /* Send output if changed and we have an active controller */
        if (changed && g_active_driver && g_controller_fd >= 0) {
            if (g_active_driver->send_output) {
                int ret = g_active_driver->send_output(g_controller_fd, &output);
                if (ret < 0) {
                    consecutive_failures++;
                    /* Only log after several failures to reduce noise */
                    if (consecutive_failures == 5) {
                        printf("[Output] Warning: Multiple output send failures\n");
                    }
                    /* Don't update last_output so we retry */
                } else {
                    if (consecutive_failures >= 5) {
                        printf("[Output] Output send recovered\n");
                    }
                    consecutive_failures = 0;
                    last_output = output;
                }
            } else {
                last_output = output;
            }
        }
        
        usleep(10000);  /* 100Hz */
    }
    
    printf("[Output] Controller output thread exiting\n");
    return NULL;
}

/* ============================================================================
 * CONFIGURATION
 * ============================================================================ */

volatile int g_touchpad_as_right_stick = 1;  /* Enabled by default */

/* ============================================================================
 * DEBUG UTILITIES
 * ============================================================================ */

void debug_print_hex(const char* label, const uint8_t* data, size_t len) {
    printf("%s (%zu bytes):\n  ", label, len);
    for (size_t i = 0; i < len && i < 64; i++) {
        printf("%02x ", data[i]);
        if ((i + 1) % 16 == 0 && i + 1 < len) printf("\n  ");
    }
    printf("\n");
    fflush(stdout);
}

uint64_t time_get_ms(void) {
    struct timeval tv;
    gettimeofday(&tv, NULL);
    return (uint64_t)tv.tv_sec * 1000ULL + tv.tv_usec / 1000;
}