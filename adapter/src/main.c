/*
 * RosettaPad - Universal Controller Adapter
 * ==========================================
 * 
 * Main entry point - thread orchestration and lifecycle management.
 * 
 * ARCHITECTURE:
 * 
 *   Controllers (input)          Console (output)
 *   ==================          =================
 *   DualSense    ─┐              ┌─ PS3 USB Gadget
 *   Xbox         ─┼─► Generic ──►├─ PS3 Bluetooth
 *   8BitDo       ─┤   State      └─ (future: PS4, PS5)
 *   Switch Pro   ─┘
 * 
 * The controller layer translates hardware-specific input into a generic
 * controller_state_t format. The console layer translates that into
 * console-specific protocols.
 * 
 * ADDING A NEW CONTROLLER:
 * 1. Create controller driver in src/controllers/your_controller/
 * 2. Implement the controller_driver_t interface
 * 3. Register in src/controllers/controller_registry.c
 * 
 * ADDING A NEW CONSOLE:
 * 1. Create console emulation in src/console/your_console/
 * 2. Implement translation from controller_state_t
 * 3. Add threads to main.c
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <signal.h>
#include <unistd.h>
#include <pthread.h>
#include <errno.h>

#include "core/common.h"
#include "controllers/controller_interface.h"
#include "controllers/dualsense/dualsense.h"
#include "console/ps3/ds3_emulation.h"
#include "console/ps3/usb_gadget.h"
#include "console/ps3/bt_hid.h"

/* ============================================================================
 * FORWARD DECLARATIONS
 * ============================================================================ */

/* From controller_registry.c */
extern void controller_registry_init(void);
extern void controller_drivers_init(void);
extern void controller_drivers_shutdown(void);
extern int controller_scan_devices(const controller_driver_t** out_driver);
extern void controller_registry_print(void);
extern void controller_set_active_driver(const controller_driver_t* driver);

/* From core/common.c */
extern void controller_set_active(int fd, const controller_driver_t* driver);
extern void controller_clear_active(void);

/* ============================================================================
 * SIGNAL HANDLER
 * ============================================================================ */

static void signal_handler(int sig) {
    (void)sig;
    printf("\n[Main] Shutdown requested...\n");
    g_running = 0;
}

/* ============================================================================
 * BANNER
 * ============================================================================ */

static void print_banner(void) {
    printf("\n");
    printf("╔════════════════════════════════════════════════════════════╗\n");
    printf("║                    RosettaPad v0.9                         ║\n");
    printf("║              Universal Controller Adapter                  ║\n");
    printf("╠════════════════════════════════════════════════════════════╣\n");
    printf("║  Controllers:  DualSense (more coming)                     ║\n");
    printf("║  Consoles:     PlayStation 3                               ║\n");
    printf("║  Features:     USB, Bluetooth, Motion, Rumble, Wake        ║\n");
    printf("╚════════════════════════════════════════════════════════════╝\n");
    printf("\n");
}

/* ============================================================================
 * CONTROLLER INPUT THREAD
 * 
 * Generic controller polling - finds controller, reads input, updates state.
 * Works with any registered controller driver.
 * ============================================================================ */

static int g_controller_fd = -1;
static const controller_driver_t* g_active_driver = NULL;

/* Wake button debouncing */
static uint64_t g_last_home_press_time = 0;
#define HOME_BUTTON_DEBOUNCE_MS 500

void* controller_input_thread(void* arg) {
    (void)arg;
    printf("[Input] Controller input thread started\n");
    
    uint8_t buf[128];
    controller_state_t state;
    int prev_home_pressed = 0;
    
    while (g_running) {
        /* Find controller if not connected */
        while (g_running && g_controller_fd < 0) {
            g_controller_fd = controller_scan_devices(&g_active_driver);
            
            if (g_controller_fd >= 0 && g_active_driver) {
                printf("[Input] Controller connected: %s\n", g_active_driver->info->name);
                controller_set_active(g_controller_fd, g_active_driver);
                controller_set_active_driver(g_active_driver);
            } else {
                sleep(1);
            }
        }
        
        if (g_controller_fd < 0) break;
        
        /* Read input */
        ssize_t n = read(g_controller_fd, buf, sizeof(buf));
        
        if (n < 0) {
            if (errno == EAGAIN) {
                usleep(1000);
                continue;
            }
            
            /* Disconnected */
            printf("[Input] Controller disconnected\n");
            if (g_active_driver && g_active_driver->on_disconnect) {
                g_active_driver->on_disconnect();
            }
            close(g_controller_fd);
            g_controller_fd = -1;
            controller_clear_active();
            controller_set_active_driver(NULL);
            g_active_driver = NULL;
            continue;
        }
        
        if (n == 0) {
            usleep(1000);
            continue;
        }
        
        /* Parse input */
        if (!g_active_driver || !g_active_driver->process_input) {
            continue;
        }
        
        if (g_active_driver->process_input(buf, n, &state) != 0) {
            continue;
        }
        
        /* Handle standby mode - check for wake button with debouncing */
        if (system_is_standby()) {
            int home_pressed = CONTROLLER_BTN_PRESSED(&state, BTN_HOME);
            
            /* Detect rising edge (button just pressed) with debounce */
            if (home_pressed && !prev_home_pressed) {
                uint64_t now = time_get_ms();
                
                if (now - g_last_home_press_time >= HOME_BUTTON_DEBOUNCE_MS) {
                    printf("[Input] Home button pressed - waking PS3\n");
                    g_last_home_press_time = now;
                    system_exit_standby();
                } else {
                    printf("[Input] Home button ignored (debounce)\n");
                }
            }
            
            prev_home_pressed = home_pressed;
            continue;
        }
        
        /* Normal operation - update state */
        prev_home_pressed = CONTROLLER_BTN_PRESSED(&state, BTN_HOME);
        controller_state_update(&state);
    }
    
    /* Cleanup */
    if (g_controller_fd >= 0) {
        close(g_controller_fd);
        g_controller_fd = -1;
    }
    
    printf("[Input] Controller input thread exiting\n");
    return NULL;
}

/* ============================================================================
 * MAIN
 * ============================================================================ */

int main(int argc, char* argv[]) {
    (void)argc;
    (void)argv;
    
    pthread_t input_tid;
    pthread_t output_tid;
    pthread_t usb_ctrl_tid;
    pthread_t usb_in_tid;
    pthread_t usb_out_tid;
    pthread_t bt_tid;
    pthread_t bt_motion_tid;
    
    print_banner();
    
    /* Setup signal handlers */
    signal(SIGINT, signal_handler);
    signal(SIGTERM, signal_handler);
    
    /* Create IPC directory */
    system("mkdir -p /tmp/rosettapad");
    
    /* ========== INITIALIZATION ========== */
    
    printf("[Main] Initializing modules...\n");
    
    /* Initialize controller registry and drivers */
    controller_registry_init();
    controller_drivers_init();
    controller_registry_print();
    
    /* Initialize PS3 emulation */
    ds3_init();
    
    /* Initialize PS3 Bluetooth */
    if (ps3_bt_init() < 0) {
        printf("[Main] Warning: Bluetooth init failed - motion controls disabled\n");
    }
    
    /* Initialize PS3 USB gadget */
    if (ps3_usb_init() < 0) {
        fprintf(stderr, "[Main] Failed to initialize USB gadget\n");
        return 1;
    }
    
    /* Open ep0 and write descriptors */
    g_ep0_fd = ps3_usb_open_endpoint(0);
    if (g_ep0_fd < 0) {
        fprintf(stderr, "[Main] Failed to open ep0\n");
        return 1;
    }
    
    if (ps3_usb_write_descriptors(g_ep0_fd) < 0) {
        fprintf(stderr, "[Main] Failed to write USB descriptors\n");
        close(g_ep0_fd);
        return 1;
    }
    
    /* ========== START THREADS ========== */
    
    printf("[Main] Starting threads...\n");
    
    /* Controller threads */
    pthread_create(&input_tid, NULL, controller_input_thread, NULL);
    pthread_create(&output_tid, NULL, controller_output_thread, NULL);
    
    /* PS3 USB threads */
    pthread_create(&usb_ctrl_tid, NULL, ps3_usb_control_thread, NULL);
    pthread_create(&usb_in_tid, NULL, ps3_usb_input_thread, NULL);
    pthread_create(&usb_out_tid, NULL, ps3_usb_output_thread, NULL);
    
    /* PS3 Bluetooth threads */
    pthread_create(&bt_tid, NULL, ps3_bt_thread, NULL);
    pthread_create(&bt_motion_tid, NULL, ps3_bt_motion_thread, NULL);
    
    /* Bind USB gadget */
    printf("[Main] Binding USB gadget...\n");
    if (ps3_usb_bind() < 0) {
        fprintf(stderr, "[Main] Warning: Failed to bind USB\n");
    }
    
    /* ========== RUNNING ========== */
    
    printf("\n");
    printf("╔════════════════════════════════════════════════════════════╗\n");
    printf("║  RosettaPad running! Press Ctrl+C to stop.                 ║\n");
    printf("║                                                            ║\n");
    printf("║  Connect a supported controller via Bluetooth.             ║\n");
    printf("║  Plug USB into PS3.                                        ║\n");
    printf("╚════════════════════════════════════════════════════════════╝\n");
    printf("\n");
    fflush(stdout);
    
    /* Main loop - just wait for shutdown */
    while (g_running) {
        sleep(1);
    }
    
    /* ========== SHUTDOWN ========== */
    
    printf("[Main] Shutting down...\n");
    
    /* Send stop signal to controller */
    if (g_active_driver && g_active_driver->enter_low_power && g_controller_fd >= 0) {
        g_active_driver->enter_low_power(g_controller_fd);
    }
    
    /* Disconnect Bluetooth */
    ps3_bt_disconnect();
    
    /* Unbind USB gadget */
    ps3_usb_unbind();
    
    /* Wait for threads */
    sleep(1);
    
    /* Cleanup drivers */
    controller_drivers_shutdown();
    
    /* Close file descriptors */
    if (g_ep1_fd >= 0) close(g_ep1_fd);
    if (g_ep2_fd >= 0) close(g_ep2_fd);
    if (g_controller_fd >= 0) close(g_controller_fd);
    if (g_ep0_fd >= 0) close(g_ep0_fd);
    
    printf("[Main] Goodbye!\n");
    return 0;
}