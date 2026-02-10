CC = gcc
CFLAGS = -Wall -Wextra -O2 `pkg-config --cflags gtk+-3.0` -lm
LDFLAGS = `pkg-config --libs gtk+-3.0` -lm

TARGET = splashy
SRC = src/splashy.c
BUILD_DIR = build

all: directories $(BUILD_DIR)/$(TARGET)

$(BUILD_DIR)/$(TARGET): $(SRC)
	$(CC) $(CFLAGS) -o $@ $< $(LDFLAGS)

directories:
	mkdir -p $(BUILD_DIR)

clean:
	rm -rf $(BUILD_DIR)

.PHONY: all clean directories