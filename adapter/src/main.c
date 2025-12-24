/*
 * RosettaPad - DualSense to PS3 Controller Adapter
 * Main entry point
 * 
 * v0.8 - Standby disconnects controller (saves battery), wake on reconnect
 * 
 * Architecture:
 *   - common.c/h    : Shared state and utilities
 *   - ds3.c/h       : PS3/DualShock 3 emulation layer
 *   - dualsense.c/h : PS5/DualSense controller interface
 *   - usb_gadget.c/h: USB FunctionFS handling
 *   - bt_hid.c/h    : Bluetooth HID for motion controls
 *   - main.c        : Thread orchestration and lifecycle
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <signal.h>
#include <unistd.h>
#include <pthread.h>
#include <fcntl.h>

#include "common.h"
#include "ds3.h"
#include "dualsense.h"
#include "usb_gadget.h"
#include "bt_hid.h"

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
    printf("============================================================\n");
    printf("                     RosettaPad v0.8                        \n");
    printf("           DualSense to PS3 Controller Adapter              \n");
    printf("============================================================\n");
    printf("  Modules:                                                  \n");
    printf("    - DS3 Emulation    : PlayStation 3 protocol             \n");
    printf("    - DualSense Input  : PS5 controller via Bluetooth       \n");
    printf("    - USB Gadget       : FunctionFS to PS3                  \n");
    printf("    - Bluetooth HID    : Motion controls & PS3 wake         \n");
    printf("    - Standby Mode     : Controller off, wake on reconnect  \n");
    printf("============================================================\n");
    printf("\n");
}

// =================================================================
// Main
// =================================================================
int main(int argc, char* argv[]) {
    (void)argc;
    (void)argv;
    
    pthread_t ds_input_tid;
    pthread_t ctrl_output_tid;
    pthread_t usb_ctrl_tid;
    pthread_t usb_in_tid;
    pthread_t usb_out_tid;
    pthread_t bt_hid_tid;
    pthread_t bt_motion_tid;
    
    print_banner();
    
    // Setup signal handlers
    signal(SIGINT, signal_handler);
    signal(SIGTERM, signal_handler);
    
    // Create IPC directory
    system("mkdir -p /tmp/rosettapad");
    
    // Initialize modules
    printf("[Main] Initializing modules...\n");
    ds3_init();
    dualsense_init();
    
    // Initialize Bluetooth (this sets Pi's MAC in DS3 Report 0xF5 automatically)
    if (bt_hid_init() < 0) {
        printf("[Main] Warning: Bluetooth init failed - motion controls disabled\n");
    } else {
        printf("[Main] Bluetooth initialized (Pi's MAC auto-configured in Report 0xF5)\n");
    }
    
    // Setup USB gadget
    if (usb_gadget_init() < 0) {
        fprintf(stderr, "[Main] Failed to initialize USB gadget\n");
        return 1;
    }
    
    // Open ep0 and write descriptors
    g_ep0_fd = usb_open_endpoint(0);
    if (g_ep0_fd < 0) {
        fprintf(stderr, "[Main] Failed to open ep0\n");
        return 1;
    }
    
    if (usb_gadget_write_descriptors(g_ep0_fd) < 0) {
        fprintf(stderr, "[Main] Failed to write USB descriptors\n");
        close(g_ep0_fd);
        return 1;
    }
    
    // Start threads
    printf("[Main] Starting threads...\n");
    
    pthread_create(&ds_input_tid, NULL, dualsense_thread, NULL);
    pthread_create(&ctrl_output_tid, NULL, controller_output_thread, NULL);
    pthread_create(&usb_ctrl_tid, NULL, usb_control_thread, NULL);
    pthread_create(&usb_in_tid, NULL, usb_input_thread, NULL);
    pthread_create(&usb_out_tid, NULL, usb_output_thread, NULL);
    pthread_create(&bt_hid_tid, NULL, bt_hid_thread, NULL);
    pthread_create(&bt_motion_tid, NULL, bt_motion_thread, NULL);
    
    // Bind to UDC
    printf("[Main] Binding to USB...\n");
    if (usb_gadget_bind() < 0) {
        fprintf(stderr, "[Main] Warning: Failed to bind to UDC\n");
    }
    
    printf("\n");
    printf("============================================================\n");
    printf("  Adapter running! Press Ctrl+C to stop.\n");
    printf("  \n");
    printf("  Standby: When PS3 powers off, controller disconnects.\n");
    printf("           Turn controller back on to wake PS3.\n");
    printf("============================================================\n");
    printf("\n");
    fflush(stdout);
    
    // Main loop - just wait for shutdown
    while (g_running) {
        sleep(1);
    }
    
    // Shutdown
    printf("[Main] Shutting down...\n");
    
    // Stop rumble/LEDs on DualSense
    if (g_hidraw_fd >= 0) {
        dualsense_send_output(g_hidraw_fd, 0, 0, 0, 0, 0, 0);
    }
    
    // Disconnect Bluetooth
    bt_hid_disconnect();
    
    // Unbind USB gadget
    usb_gadget_unbind();
    
    // Wait for threads
    sleep(1);
    
    // Cleanup file descriptors
    if (g_ep1_fd >= 0) close(g_ep1_fd);
    if (g_ep2_fd >= 0) close(g_ep2_fd);
    if (g_hidraw_fd >= 0) close(g_hidraw_fd);
    if (g_ep0_fd >= 0) close(g_ep0_fd);
    
    printf("[Main] Goodbye!\n");
    return 0;
}