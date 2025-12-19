CC = gcc
CFLAGS = -Wall -Wextra -O2 -pthread
LDFLAGS = -pthread

# Source files
SRCS = common.c debug.c ds3.c dualsense.c usb_gadget.c bt_hid.c main.c
TARGET = rosettapad

# Default: full build with USB + Bluetooth
full: CFLAGS += -DENABLE_USB -DENABLE_BLUETOOTH
full: LDFLAGS += -lbluetooth
full: $(TARGET)
	@echo "Built full version (USB + Bluetooth)"

# USB only (no Bluetooth dependency)
usb: CFLAGS += -DENABLE_USB
usb: $(TARGET)
	@echo "Built USB-only version"

# Bluetooth only
bt: CFLAGS += -DENABLE_BLUETOOTH
bt: LDFLAGS += -lbluetooth
bt: $(TARGET)
	@echo "Built Bluetooth-only version"

$(TARGET): $(SRCS)
	$(CC) $(CFLAGS) -o $@ $^ $(LDFLAGS)

clean:
	rm -f $(TARGET)

install: full
	install -D -m 755 $(TARGET) /opt/rosettapad/$(TARGET)
	ln -sf /opt/rosettapad/$(TARGET) /usr/local/bin/$(TARGET)

.PHONY: full usb bt clean install