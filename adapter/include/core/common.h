/*
 * RosettaPad - Core Common Definitions
 * =====================================
 * 
 * Shared state, utilities, and IPC mechanisms used across all modules.
 * This is the "glue" that connects controllers to console emulation.
 */

#ifndef ROSETTAPAD_CORE_COMMON_H
#define ROSETTAPAD_CORE_COMMON_H

#include <stdint.h>
#include <stddef.h>
#include <pthread.h>

#include "controllers/controller_interface.h"

/* ============================================================================
 * GLOBAL STATE
 * ============================================================================ */

/* Main run flag - set to 0 to trigger shutdown */
extern volatile int g_running;

/* ============================================================================
 * SYSTEM STATE MACHINE
 * 
 * Manages power states for PS3 standby/wake functionality.
 * ============================================================================ */

typedef enum {
    SYSTEM_STATE_ACTIVE = 0,      /* Normal operation */
    SYSTEM_STATE_STANDBY,         /* PS3 off, waiting for wake */
    SYSTEM_STATE_WAKING           /* Wake in progress */
} system_state_t;

void system_set_state(system_state_t state);
system_state_t system_get_state(void);
int system_is_standby(void);
void system_enter_standby(void);
void system_exit_standby(void);

/* ============================================================================
 * CONTROLLER STATE MANAGEMENT
 * 
 * The bridge between controller drivers and console emulation.
 * Controllers write to this; console layers read from it.
 * ============================================================================ */

/* Global controller state - updated by active controller driver */
extern controller_state_t g_controller_state;
extern pthread_mutex_t g_controller_state_mutex;

/**
 * Update controller state (thread-safe).
 * Called by controller drivers after processing input.
 */
void controller_state_update(const controller_state_t* state);

/**
 * Copy current controller state (thread-safe).
 * Called by console emulation layers.
 */
void controller_state_copy(controller_state_t* out_state);

/* ============================================================================
 * OUTPUT STATE MANAGEMENT
 * 
 * Rumble and LED state from console, to be sent to controller.
 * ============================================================================ */

extern controller_output_t g_controller_output;
extern pthread_mutex_t g_controller_output_mutex;

/**
 * Update output state (thread-safe).
 * Called by console emulation when it receives output commands.
 */
void controller_output_update(const controller_output_t* output);

/**
 * Copy current output state (thread-safe).
 * Called by controller output thread.
 */
void controller_output_copy(controller_output_t* out_output);

/**
 * Check if output has changed since last copy.
 * Avoids unnecessary output sends.
 */
int controller_output_changed(void);

/* ============================================================================
 * CONTROLLER OUTPUT THREAD
 * 
 * Generic output thread that reads from g_controller_output and calls
 * the active controller's send_output() function.
 * ============================================================================ */

/**
 * Controller output thread function.
 * Monitors output state and forwards to active controller.
 */
void* controller_output_thread(void* arg);

/* ============================================================================
 * LIGHTBAR IPC
 * 
 * Web interface can control lightbar via file-based IPC.
 * ============================================================================ */

#define LIGHTBAR_IPC_PATH "/tmp/rosettapad/lightbar_state.json"

/**
 * Read lightbar state from IPC file.
 * Called periodically by output thread.
 */
void lightbar_read_ipc(controller_output_t* output);

/* ============================================================================
 * TOUCHPAD-AS-STICK CONFIGURATION
 * ============================================================================ */

extern volatile int g_touchpad_as_right_stick;  /* 0=disabled, 1=enabled */

/* ============================================================================
 * DEBUG UTILITIES
 * ============================================================================ */

/**
 * Print hex dump of data.
 */
void debug_print_hex(const char* label, const uint8_t* data, size_t len);

/**
 * Get current time in milliseconds.
 */
uint64_t time_get_ms(void);

#endif /* ROSETTAPAD_CORE_COMMON_H */