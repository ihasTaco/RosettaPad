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

/* Poll from a non-blocking loop; clears g_usb_enabled on unplug, returns 1 if it did */
int ps3_usb_check_suspend_timeout(void);

/*
 * USB liveness, as seen from the PS3 side. Only meaningful while bound.
 *   alive - enabled and not suspended: the PS3 is definitely on
 *   dead  - disabled, or suspended past SUSPEND_DISCONNECT_MS
 */
int ps3_usb_is_bound(void);
int ps3_usb_is_alive(void);
int ps3_usb_is_dead(void);

/* ms since the current USB session was ENABLED, 0 if not enabled */
uint64_t ps3_usb_enabled_for_ms(void);

/* Timestamp (ms) of the F4 enable the PS3 sent over USB this session, 0 if
 * it hasn't yet. F4 means the PS3 has fully registered the controller and
 * its MAC - the point after which USB has done its job. */
uint64_t ps3_usb_handshake_done_ms(void);

/* Counts ENABLE events - lets the BT thread tell a new USB session from the
 * one it already handed off. */
uint32_t ps3_usb_session_id(void);

/*
 * "Pull the cable" in software: unbind the UDC. The PS3 won't drive a
 * controller over BT while it's also enumerated on USB, so this is what
 * turns BT input on. Rebind with ps3_usb_bind() to fall back to USB.
 */
int ps3_usb_soft_unplug(void);

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