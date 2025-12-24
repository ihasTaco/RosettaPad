# RosettaPad Makefile
# Directory structure: include/ for headers, src/ for sources

CC = gcc
CFLAGS = -Wall -Wextra -O2 -pthread -I./include
LDFLAGS = -pthread

# Directories
SRC_DIR = src
INC_DIR = include
BUILD_DIR = build

# Source files
COMMON_SRCS = $(SRC_DIR)/common.c \
              $(SRC_DIR)/ds3.c \
              $(SRC_DIR)/dualsense.c \
              $(SRC_DIR)/usb_gadget.c

HYBRID_SRCS = $(COMMON_SRCS) \
              $(SRC_DIR)/bt_hid.c \
              $(SRC_DIR)/main.c

# Object files
HYBRID_OBJS = $(HYBRID_SRCS:$(SRC_DIR)/%.c=$(BUILD_DIR)/%.o)

# Bluetooth libraries
BT_CFLAGS = $(shell pkg-config --cflags bluez 2>/dev/null)
BT_LDFLAGS = $(shell pkg-config --libs bluez 2>/dev/null || echo "-lbluetooth")

# Default target - build with Bluetooth support
.PHONY: all clean install uninstall debug

all: CFLAGS += $(BT_CFLAGS)
all: LDFLAGS += $(BT_LDFLAGS)
all: $(BUILD_DIR) rosettapad

rosettapad: $(HYBRID_OBJS)
	$(CC) -o $@ $^ $(LDFLAGS)
	@echo "Built rosettapad (hybrid USB + Bluetooth)"

# Create build directory
$(BUILD_DIR):
	mkdir -p $(BUILD_DIR)

# Compile source files
$(BUILD_DIR)/%.o: $(SRC_DIR)/%.c | $(BUILD_DIR)
	$(CC) $(CFLAGS) $(BT_CFLAGS) -c $< -o $@

# Header dependencies
$(BUILD_DIR)/common.o: $(INC_DIR)/common.h
$(BUILD_DIR)/ds3.o: $(INC_DIR)/ds3.h $(INC_DIR)/common.h
$(BUILD_DIR)/dualsense.o: $(INC_DIR)/dualsense.h $(INC_DIR)/common.h $(INC_DIR)/ds3.h
$(BUILD_DIR)/usb_gadget.o: $(INC_DIR)/usb_gadget.h $(INC_DIR)/common.h $(INC_DIR)/ds3.h
$(BUILD_DIR)/bt_hid.o: $(INC_DIR)/bt_hid.h $(INC_DIR)/common.h $(INC_DIR)/ds3.h
$(BUILD_DIR)/main.o: $(INC_DIR)/common.h $(INC_DIR)/ds3.h $(INC_DIR)/dualsense.h $(INC_DIR)/usb_gadget.h $(INC_DIR)/bt_hid.h

clean:
	rm -rf $(BUILD_DIR) rosettapad

install: rosettapad
	install -m 755 rosettapad /usr/local/bin/
	mkdir -p /etc/rosettapad
	@if [ ! -f /etc/rosettapad/config ]; then \
		echo "# RosettaPad Configuration" > /etc/rosettapad/config; \
		echo "enable_bluetooth=1" >> /etc/rosettapad/config; \
		echo "enable_motion=1" >> /etc/rosettapad/config; \
		echo "enable_wake=1" >> /etc/rosettapad/config; \
		echo "debug_level=0" >> /etc/rosettapad/config; \
	fi
	@echo "Installed rosettapad to /usr/local/bin/"

uninstall:
	rm -f /usr/local/bin/rosettapad
	@echo "Uninstalled rosettapad"

# Debug build
debug: CFLAGS += -g -DDEBUG -O0
debug: all

# USB-only build (no Bluetooth)
usb-only: CFLAGS += -DNO_BLUETOOTH
usb-only: $(BUILD_DIR) rosettapad-usb

USBONLY_SRCS = $(COMMON_SRCS) $(SRC_DIR)/main.c
USBONLY_OBJS = $(USBONLY_SRCS:$(SRC_DIR)/%.c=$(BUILD_DIR)/%.o)

rosettapad-usb: $(USBONLY_OBJS)
	$(CC) -o $@ $^ $(LDFLAGS)
	@echo "Built rosettapad-usb (USB only, no Bluetooth)"

# Cross-compile for Pi Zero 2W
CROSS_CC = arm-linux-gnueabihf-gcc
cross: CC = $(CROSS_CC)
cross: all

# Show config
info:
	@echo "CC: $(CC)"
	@echo "CFLAGS: $(CFLAGS) $(BT_CFLAGS)"
	@echo "LDFLAGS: $(LDFLAGS) $(BT_LDFLAGS)"
	@echo "Sources: $(HYBRID_SRCS)"