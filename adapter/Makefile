# RosettaPad Makefile
# ====================

CC = gcc
CFLAGS = -Wall -Wextra -O2 -pthread
LDFLAGS = -pthread -lbluetooth

# Directories
SRC_DIR = src
INC_DIR = include
BUILD_DIR = build

INCLUDES = -I$(INC_DIR)

# =============================================================================
# SOURCE FILES - Add new files here
# =============================================================================

SRCS = \
    $(SRC_DIR)/core/common.c \
    $(SRC_DIR)/controllers/controller_registry.c \
    $(SRC_DIR)/controllers/dualsense/dualsense.c \
    $(SRC_DIR)/console/ps3/ds3_emulation.c \
    $(SRC_DIR)/console/ps3/usb_gadget.c \
    $(SRC_DIR)/console/ps3/bt_hid.c \
    $(SRC_DIR)/main.c

# Object files (automatically derived from sources)
OBJS = $(SRCS:$(SRC_DIR)/%.c=$(BUILD_DIR)/%.o)

# =============================================================================
# TARGETS
# =============================================================================

.PHONY: all clean debug info help

all: rosettapad

rosettapad: $(OBJS)
	$(CC) -o $@ $^ $(LDFLAGS)
	@echo ""
	@echo "Build complete: rosettapad"
	@echo ""

# Pattern rule: compile any .c to .o
$(BUILD_DIR)/%.o: $(SRC_DIR)/%.c
	@mkdir -p $(dir $@)
	$(CC) $(CFLAGS) $(INCLUDES) -c $< -o $@

clean:
	rm -rf $(BUILD_DIR) rosettapad

debug: CFLAGS += -g -DDEBUG -O0
debug: all

info:
	@echo "Sources: $(SRCS)"
	@echo "Objects: $(OBJS)"

help:
	@echo "make        - Build rosettapad"
	@echo "make clean  - Remove build files"
	@echo "make debug  - Build with debug symbols"