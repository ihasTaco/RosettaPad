/*
 * RosettaPad - USB Gadget Interface
 * Handles USB FunctionFS setup and communication with PS3
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <fcntl.h>
#include <unistd.h>
#include <errno.h>
#include <linux/usb/functionfs.h>
#include <linux/usb/ch9.h>

#include "common.h"
#include "ds3.h"
#include "usb_gadget.h"

// =================================================================
// USB Descriptors
// =================================================================

// HID descriptor for DS3 (embedded in interface descriptor response)
static const uint8_t hid_descriptor[] = {
    0x09,        // bLength
    0x21,        // bDescriptorType (HID)
    0x11, 0x01,  // bcdHID 1.11
    0x00,        // bCountryCode
    0x01,        // bNumDescriptors
    0x22,        // bDescriptorType (Report)
    0x94, 0x00   // wDescriptorLength (148 bytes)
};

// USB descriptors for FunctionFS
static const struct {
    struct usb_functionfs_descs_head_v2 header;
    __le32 fs_count;
    __le32 hs_count;
    struct {
        struct usb_interface_descriptor intf;
        struct usb_endpoint_descriptor_no_audio ep_in;
        struct usb_endpoint_descriptor_no_audio ep_out;
    } __attribute__((packed)) fs_descs, hs_descs;
} __attribute__((packed)) usb_descriptors = {
    .header = {
        .magic = FUNCTIONFS_DESCRIPTORS_MAGIC_V2,
        .flags = FUNCTIONFS_HAS_FS_DESC | FUNCTIONFS_HAS_HS_DESC,
        .length = sizeof(usb_descriptors),
    },
    .fs_count = 3,
    .hs_count = 3,
    .fs_descs = {
        .intf = {
            .bLength = sizeof(struct usb_interface_descriptor),
            .bDescriptorType = USB_DT_INTERFACE,
            .bInterfaceNumber = 0,
            .bAlternateSetting = 0,
            .bNumEndpoints = 2,
            .bInterfaceClass = USB_CLASS_HID,
            .bInterfaceSubClass = 0,
            .bInterfaceProtocol = 0,
            .iInterface = 1,
        },
        .ep_in = {
            .bLength = sizeof(struct usb_endpoint_descriptor_no_audio),
            .bDescriptorType = USB_DT_ENDPOINT,
            .bEndpointAddress = EP_IN_ADDR,
            .bmAttributes = USB_ENDPOINT_XFER_INT,
            .wMaxPacketSize = EP_MAX_PACKET,
            .bInterval = EP_INTERVAL,
        },
        .ep_out = {
            .bLength = sizeof(struct usb_endpoint_descriptor_no_audio),
            .bDescriptorType = USB_DT_ENDPOINT,
            .bEndpointAddress = EP_OUT_ADDR,
            .bmAttributes = USB_ENDPOINT_XFER_INT,
            .wMaxPacketSize = EP_MAX_PACKET,
            .bInterval = EP_INTERVAL,
        },
    },
    .hs_descs = {
        .intf = {
            .bLength = sizeof(struct usb_interface_descriptor),
            .bDescriptorType = USB_DT_INTERFACE,
            .bInterfaceNumber = 0,
            .bAlternateSetting = 0,
            .bNumEndpoints = 2,
            .bInterfaceClass = USB_CLASS_HID,
            .bInterfaceSubClass = 0,
            .bInterfaceProtocol = 0,
            .iInterface = 1,
        },
        .ep_in = {
            .bLength = sizeof(struct usb_endpoint_descriptor_no_audio),
            .bDescriptorType = USB_DT_ENDPOINT,
            .bEndpointAddress = EP_IN_ADDR,
            .bmAttributes = USB_ENDPOINT_XFER_INT,
            .wMaxPacketSize = EP_MAX_PACKET,
            .bInterval = EP_INTERVAL,
        },
        .ep_out = {
            .bLength = sizeof(struct usb_endpoint_descriptor_no_audio),
            .bDescriptorType = USB_DT_ENDPOINT,
            .bEndpointAddress = EP_OUT_ADDR,
            .bmAttributes = USB_ENDPOINT_XFER_INT,
            .wMaxPacketSize = EP_MAX_PACKET,
            .bInterval = EP_INTERVAL,
        },
    },
};

// USB strings
static const struct {
    struct usb_functionfs_strings_head header;
    struct {
        __le16 code;
        const char str1[10];
    } __attribute__((packed)) lang0;
} __attribute__((packed)) usb_strings = {
    .header = {
        .magic = FUNCTIONFS_STRINGS_MAGIC,
        .length = sizeof(usb_strings),
        .str_count = 1,
        .lang_count = 1,
    },
    .lang0 = {
        .code = 0x0409,  // English
        .str1 = "DS3 Input",
    },
};

// =================================================================
// Gadget Setup
// =================================================================

int usb_gadget_init(void) {
    printf("[USB] Initializing USB gadget...\n");
    
    // Load required kernel modules
    system("modprobe libcomposite 2>/dev/null");
    system("modprobe usb_f_fs 2>/dev/null");
    
    // Create gadget if it doesn't exist
    if (access(USB_GADGET_PATH, F_OK) != 0) {
        printf("[USB] Creating gadget configuration...\n");
        
        system("mkdir -p " USB_GADGET_PATH);
        
        // Set USB IDs (Sony DualShock 3)
        char cmd[256];
        snprintf(cmd, sizeof(cmd), "echo 0x%04x > %s/idVendor", DS3_USB_VID, USB_GADGET_PATH);
        system(cmd);
        snprintf(cmd, sizeof(cmd), "echo 0x%04x > %s/idProduct", DS3_USB_PID, USB_GADGET_PATH);
        system(cmd);
        
        system("echo 0x0100 > " USB_GADGET_PATH "/bcdDevice");
        system("echo 0x0200 > " USB_GADGET_PATH "/bcdUSB");
        
        // Create strings
        system("mkdir -p " USB_GADGET_PATH "/strings/0x409");
        system("echo '123456' > " USB_GADGET_PATH "/strings/0x409/serialnumber");
        system("echo 'Sony' > " USB_GADGET_PATH "/strings/0x409/manufacturer");
        system("echo 'PLAYSTATION(R)3 Controller' > " USB_GADGET_PATH "/strings/0x409/product");
        
        // Create configuration
        system("mkdir -p " USB_GADGET_PATH "/configs/c.1/strings/0x409");
        system("echo 'DS3 Config' > " USB_GADGET_PATH "/configs/c.1/strings/0x409/configuration");
        system("echo 500 > " USB_GADGET_PATH "/configs/c.1/MaxPower");
        
        // Create FunctionFS function
        system("mkdir -p " USB_GADGET_PATH "/functions/ffs.usb0");
        
        // Link function to configuration
        system("ln -sf " USB_GADGET_PATH "/functions/ffs.usb0 " USB_GADGET_PATH "/configs/c.1/ 2>/dev/null");
    }
    
    // Mount FunctionFS
    system("mkdir -p " USB_FFS_PATH);
    system("umount " USB_FFS_PATH " 2>/dev/null");
    
    int ret = system("mount -t functionfs usb0 " USB_FFS_PATH);
    if (ret != 0) {
        printf("[USB] Warning: mount returned %d\n", ret);
    }
    
    // Create IPC directory
    system("mkdir -p /tmp/rosettapad");
    
    printf("[USB] Gadget initialized\n");
    return 0;
}

int usb_gadget_write_descriptors(int ep0_fd) {
    ssize_t written;
    
    written = write(ep0_fd, &usb_descriptors, sizeof(usb_descriptors));
    if (written != sizeof(usb_descriptors)) {
        perror("[USB] Failed to write descriptors");
        return -1;
    }
    
    written = write(ep0_fd, &usb_strings, sizeof(usb_strings));
    if (written != sizeof(usb_strings)) {
        perror("[USB] Failed to write strings");
        return -1;
    }
    
    printf("[USB] Descriptors written to ep0\n");
    return 0;
}

int usb_gadget_bind(void) {
    char cmd[128];
    snprintf(cmd, sizeof(cmd), "echo '%s' > %s/UDC", USB_UDC_NAME, USB_GADGET_PATH);
    int ret = system(cmd);
    if (ret == 0) {
        printf("[USB] Bound to UDC %s\n", USB_UDC_NAME);
    }
    return ret == 0 ? 0 : -1;
}

int usb_gadget_unbind(void) {
    char cmd[128];
    snprintf(cmd, sizeof(cmd), "echo '' > %s/UDC", USB_GADGET_PATH);
    system(cmd);
    printf("[USB] Unbound from UDC\n");
    return 0;
}

void usb_gadget_cleanup(void) {
    usb_gadget_unbind();
    // Note: We don't remove the gadget config to allow quick restarts
}

int usb_open_endpoint(int endpoint_num) {
    char path[64];
    snprintf(path, sizeof(path), USB_FFS_PATH "/ep%d", endpoint_num);
    
    int fd = open(path, O_RDWR);
    if (fd < 0) {
        perror(path);
    }
    return fd;
}

// =================================================================
// Thread Functions
// =================================================================

void* usb_control_thread(void* arg) {
    (void)arg;
    
    printf("[USB] Control thread started\n");
    
    while (g_running) {
        struct usb_functionfs_event event;
        
        if (read(g_ep0_fd, &event, sizeof(event)) < 0) {
            if (errno == EINTR) continue;
            perror("[USB] read ep0");
            break;
        }
        
        switch (event.type) {
            case FUNCTIONFS_SETUP: {
                uint8_t bmRequestType = event.u.setup.bRequestType;
                uint8_t bRequest = event.u.setup.bRequest;
                uint16_t wValue = event.u.setup.wValue;
                uint16_t wLength = event.u.setup.wLength;
                uint8_t report_id = wValue & 0xFF;
                
                printf("[USB] SETUP: bmReqType=0x%02x bReq=0x%02x wValue=0x%04x wLen=%d\n",
                       bmRequestType, bRequest, wValue, wLength);
                
                if (bRequest == 0x0A) {
                    // SET_IDLE - just acknowledge
                    printf("[USB] SET_IDLE\n");
                    read(g_ep0_fd, NULL, 0);
                }
                else if (bRequest == 0x01) {
                    // GET_REPORT
                    const char* name = NULL;
                    const uint8_t* data = ds3_get_feature_report(report_id, &name);
                    
                    if (data) {
                        size_t send_len = (DS3_FEATURE_REPORT_SIZE < wLength) ? 
                                          DS3_FEATURE_REPORT_SIZE : wLength;
                        printf("[USB] GET_REPORT 0x%02x (%s) -> %zu bytes\n", 
                               report_id, name, send_len);
                        write(g_ep0_fd, data, send_len);
                    } else {
                        printf("[USB] GET_REPORT 0x%02x unknown, stalling\n", report_id);
                        read(g_ep0_fd, NULL, 0);  // Stall
                    }
                }
                else if (bRequest == 0x09) {
                    // SET_REPORT
                    uint8_t buf[64] = {0};
                    ssize_t r = 0;
                    
                    if (wLength > 0) {
                        r = read(g_ep0_fd, buf, wLength < 64 ? wLength : 64);
                        printf("[USB] SET_REPORT 0x%02x: %zd bytes\n", report_id, r);
                        if (r > 0) {
                            ds3_handle_set_report(report_id, buf, r);
                        }
                    }
                    write(g_ep0_fd, NULL, 0);  // ACK
                }
                else {
                    printf("[USB] Unknown request 0x%02x, stalling\n", bRequest);
                    read(g_ep0_fd, NULL, 0);  // Stall
                }
                break;
            }
            
            case FUNCTIONFS_ENABLE:
                printf("[USB] *** ENABLED - PS3 connected ***\n");
                g_usb_enabled = 1;
                break;
                
            case FUNCTIONFS_DISABLE:
                printf("[USB] *** DISABLED - PS3 disconnected ***\n");
                g_usb_enabled = 0;
                pthread_mutex_lock(&g_rumble_mutex);
                g_rumble_right = 0;
                g_rumble_left = 0;
                pthread_mutex_unlock(&g_rumble_mutex);
                break;
                
            case FUNCTIONFS_UNBIND:
                printf("[USB] UNBIND\n");
                g_running = 0;
                break;
                
            default:
                printf("[USB] Event type=%d\n", event.type);
                break;
        }
        
        fflush(stdout);
    }
    
    return NULL;
}

void* usb_input_thread(void* arg) {
    (void)arg;
    
    g_ep1_fd = usb_open_endpoint(1);
    if (g_ep1_fd < 0) {
        printf("[USB] Failed to open ep1\n");
        return NULL;
    }
    
    printf("[USB] Input thread started (ep1)\n");
    
    uint8_t buf[DS3_INPUT_REPORT_SIZE];
    
    while (g_running) {
        if (g_usb_enabled) {
            ds3_copy_report(buf);
            
            // Debug: print full report every ~1 second (250 reports)
            static int report_count = 0;
            if (report_count >= 250) {
                report_count = 0;
                printf("[DS3 Report] 49 bytes to PS3:\n");
                printf("00 01 02 03 04 05 06 07 08 09 0a 0b 0c 0d 0e 0f\n");
                printf("-----------------------------------------------\n");
                for (int i = 0; i < DS3_INPUT_REPORT_SIZE; i++) {
                    if (i % 16 == 0 && i != 0) printf("%02X: ", i);
                    printf("%02x ", buf[i]);
                    if (i % 16 == 15 || i == DS3_INPUT_REPORT_SIZE - 1) printf("\n");
                }
                printf("Battery: Plugged=%02x Charge=%02x Conn=%02x\n",
                       buf[29], buf[30], buf[31]);
                printf("Motion: AccelX=%02x%02x AccelY=%02x%02x AccelZ=%02x%02x GyroZ=%02x%02x\n",
                       buf[41], buf[40], buf[43], buf[42],
                       buf[45], buf[44], buf[47], buf[46]);
                printf("\n");
                fflush(stdout);
            }
            report_count++;

            write(g_ep1_fd, buf, DS3_INPUT_REPORT_SIZE);
        }
        usleep(4000);  // ~250Hz
    }
    
    return NULL;
}

void* usb_output_thread(void* arg) {
    (void)arg;
    
    g_ep2_fd = usb_open_endpoint(2);
    if (g_ep2_fd < 0) {
        printf("[USB] Failed to open ep2\n");
        return NULL;
    }
    
    printf("[USB] Output thread started (ep2)\n");
    
    uint8_t buf[EP_MAX_PACKET];
    
    while (g_running) {
        ssize_t n = read(g_ep2_fd, buf, sizeof(buf));
        
        if (n <= 0) {
            if (errno == EAGAIN) {
                usleep(1000);
                continue;
            }
            continue;
        }
        
        // PS3 output report format:
        // buf[0] = report ID (usually 0x01)
        // buf[1] = duration for weak motor
        // buf[2] = power for weak motor (0 or 1)
        // buf[3] = duration for strong motor
        // buf[4] = power for strong motor (0-255)
        
        if (n >= 6) {
            uint8_t right_power = buf[3];  // Weak motor (on/off)
            uint8_t left_power = buf[5];   // Strong motor (variable)
            
            // Convert DS3 rumble to DualSense
            // DS3 weak motor: 0 or 1 -> DS full strength if on
            // DS3 strong motor: 0-255 -> DS 0-255
            uint8_t ds_right = right_power ? 0xFF : 0x00;
            uint8_t ds_left = left_power;
            
            pthread_mutex_lock(&g_rumble_mutex);
            g_rumble_right = ds_right;
            g_rumble_left = ds_left;
            pthread_mutex_unlock(&g_rumble_mutex);
        }
    }
    
    return NULL;
}