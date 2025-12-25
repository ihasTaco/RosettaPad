/*
 * RosettaPad - PS3 USB Gadget Interface
 * ======================================
 * 
 * Handles USB FunctionFS setup and communication with PS3.
 * Emulates a DualShock 3 USB HID device.
 */

#ifndef ROSETTAPAD_PS3_USB_GADGET_H
#define ROSETTAPAD_PS3_USB_GADGET_H

#include <stdint.h>

/* ============================================================================
 * CONFIGURATION
 * ============================================================================ */

#define USB_GADGET_PATH     "/sys/kernel/config/usb_gadget/ds3"
#define USB_FFS_PATH        "/dev/ffs-ds3"

/* UDC is auto-detected at runtime - works on any Pi model */

/* DS3 USB identifiers */
#define DS3_USB_VID         0x054C  /* Sony */
#define DS3_USB_PID         0x0268  /* DualShock 3 */

/* Endpoint configuration */
#define EP_IN_ADDR          0x81    /* Interrupt IN */
#define EP_OUT_ADDR         0x02    /* Interrupt OUT */
#define EP_MAX_PACKET       64
#define EP_INTERVAL         1       /* 1ms polling */

/* ============================================================================
 * GLOBAL STATE
 * ============================================================================ */

/* USB enabled flag - set when PS3 sends ENABLE event */
extern volatile int g_usb_enabled;

/* Endpoint file descriptors */
extern int g_ep0_fd;
extern int g_ep1_fd;
extern int g_ep2_fd;

/* ============================================================================
 * FUNCTIONS
 * ============================================================================ */

/**
 * Initialize USB gadget subsystem.
 * Auto-detects UDC, creates ConfigFS gadget structure and mounts FunctionFS.
 * @return 0 on success, -1 on failure
 */
int ps3_usb_init(void);

/**
 * Write USB descriptors to ep0.
 * Must be called after opening ep0 but before binding UDC.
 * @param ep0_fd File descriptor for ep0
 * @return 0 on success, -1 on failure
 */
int ps3_usb_write_descriptors(int ep0_fd);

/**
 * Bind gadget to UDC (makes it visible to host).
 * @return 0 on success, -1 on failure
 */
int ps3_usb_bind(void);

/**
 * Unbind gadget from UDC.
 */
int ps3_usb_unbind(void);

/**
 * Open USB endpoint.
 * @param endpoint_num 0, 1, or 2
 * @return File descriptor on success, -1 on failure
 */
int ps3_usb_open_endpoint(int endpoint_num);

/**
 * Cleanup USB gadget (unbind, unmount, remove).
 */
void ps3_usb_cleanup(void);

/* ============================================================================
 * THREAD FUNCTIONS
 * ============================================================================ */

/**
 * USB control endpoint (ep0) handler thread.
 * Handles SETUP packets, feature reports, etc.
 */
void* ps3_usb_control_thread(void* arg);

/**
 * USB input endpoint (ep1) thread.
 * Sends DS3 input reports to PS3.
 */
void* ps3_usb_input_thread(void* arg);

/**
 * USB output endpoint (ep2) thread.
 * Receives LED/rumble commands from PS3.
 */
void* ps3_usb_output_thread(void* arg);

#endif /* ROSETTAPAD_PS3_USB_GADGET_H */