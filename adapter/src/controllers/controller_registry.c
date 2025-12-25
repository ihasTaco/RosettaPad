/*
 * RosettaPad - Controller Registry
 * =================================
 * 
 * Manages controller driver registration and device discovery.
 * 
 * HOW TO ADD A NEW CONTROLLER:
 * ----------------------------
 * 
 * 1. Create your driver in controllers/your_controller/
 * 2. Implement the controller_driver_t interface
 * 3. Add your registration call to controller_registry_init()
 * 
 * Example:
 *   #include "controllers/xbox/xbox.h"
 *   ...
 *   void controller_registry_init(void) {
 *       dualsense_register();
 *       xbox_register();      // Add your registration here
 *   }
 */

#include <stdio.h>
#include <string.h>

#include "controllers/controller_interface.h"
#include "controllers/dualsense/dualsense.h"
/* Add new controller includes here */
/* #include "controllers/xbox/xbox.h" */
/* #include "controllers/8bitdo/8bitdo.h" */
/* #include "controllers/switch_pro/switch_pro.h" */

/* ============================================================================
 * DRIVER REGISTRY
 * ============================================================================ */

#define MAX_DRIVERS 16

static const controller_driver_t* g_drivers[MAX_DRIVERS];
static int g_driver_count = 0;
static const controller_driver_t* g_active_driver = NULL;

int controller_register(const controller_driver_t* driver) {
    if (g_driver_count >= MAX_DRIVERS) {
        printf("[Registry] Error: Driver registry full\n");
        return -1;
    }
    
    if (!driver || !driver->info) {
        printf("[Registry] Error: Invalid driver\n");
        return -1;
    }
    
    g_drivers[g_driver_count++] = driver;
    printf("[Registry] Registered: %s (VID=%04X PID=%04X)\n",
           driver->info->name,
           driver->info->vendor_id,
           driver->info->product_id);
    
    return 0;
}

const controller_driver_t* controller_find_driver(uint16_t vid, uint16_t pid) {
    for (int i = 0; i < g_driver_count; i++) {
        if (g_drivers[i]->match_device && g_drivers[i]->match_device(vid, pid)) {
            return g_drivers[i];
        }
    }
    return NULL;
}

const controller_driver_t* controller_get_active(void) {
    return g_active_driver;
}

void controller_set_active_driver(const controller_driver_t* driver) {
    g_active_driver = driver;
}

/* ============================================================================
 * REGISTRY INITIALIZATION
 * 
 * Add your controller registration calls here!
 * ============================================================================ */

void controller_registry_init(void) {
    printf("[Registry] Initializing controller registry...\n");
    
    /* Register all supported controllers */
    dualsense_register();
    
    /* Add new controllers here: */
    /* xbox_register(); */
    /* eightbitdo_register(); */
    /* switch_pro_register(); */
    
    printf("[Registry] %d controller(s) registered\n", g_driver_count);
}

/* ============================================================================
 * DRIVER INITIALIZATION
 * ============================================================================ */

void controller_drivers_init(void) {
    printf("[Registry] Initializing drivers...\n");
    
    for (int i = 0; i < g_driver_count; i++) {
        if (g_drivers[i]->init) {
            g_drivers[i]->init();
        }
    }
}

void controller_drivers_shutdown(void) {
    printf("[Registry] Shutting down drivers...\n");
    
    for (int i = 0; i < g_driver_count; i++) {
        if (g_drivers[i]->shutdown) {
            g_drivers[i]->shutdown();
        }
    }
}

/* ============================================================================
 * DEVICE SCANNING
 * ============================================================================ */

/**
 * Scan for any supported controller.
 * Tries each registered driver's find_device() function.
 * 
 * @param out_driver Output: the driver that matched
 * @return File descriptor on success, -1 if no controller found
 */
int controller_scan_devices(const controller_driver_t** out_driver) {
    for (int i = 0; i < g_driver_count; i++) {
        if (g_drivers[i]->find_device) {
            int fd = g_drivers[i]->find_device();
            if (fd >= 0) {
                if (out_driver) *out_driver = g_drivers[i];
                return fd;
            }
        }
    }
    
    if (out_driver) *out_driver = NULL;
    return -1;
}

/* ============================================================================
 * DEBUG INFO
 * ============================================================================ */

void controller_registry_print(void) {
    printf("\n=== Registered Controllers ===\n");
    for (int i = 0; i < g_driver_count; i++) {
        const controller_info_t* info = g_drivers[i]->info;
        printf("  [%d] %s (%s)\n", i + 1, info->name, info->manufacturer);
        printf("      VID=%04X PID=%04X\n", info->vendor_id, info->product_id);
        printf("      Capabilities:");
        if (info->capabilities & CONTROLLER_CAP_MOTION) printf(" Motion");
        if (info->capabilities & CONTROLLER_CAP_TOUCHPAD) printf(" Touchpad");
        if (info->capabilities & CONTROLLER_CAP_RUMBLE) printf(" Rumble");
        if (info->capabilities & CONTROLLER_CAP_LIGHTBAR) printf(" Lightbar");
        printf("\n");
    }
    printf("==============================\n\n");
}