CC = gcc
CFLAGS = -Wall -Wextra -O2 `pkg-config --cflags gtk+-3.0` -lm
LDFLAGS = `pkg-config --libs gtk+-3.0` -lm

ifeq ($(shell uname), Darwin)
    CFLAGS += -x objective-c
    LDFLAGS += -framework AppKit
endif

TARGET = splashy
SRC = src/splashy.c
BUILD_DIR = build
APP_NAME = Splashy
APP_BUNDLE = $(BUILD_DIR)/$(APP_NAME).app
CONTENTS = $(APP_BUNDLE)/Contents
MACOS = $(CONTENTS)/MacOS
RESOURCES = $(CONTENTS)/Resources

all: directories $(BUILD_DIR)/$(TARGET)

$(BUILD_DIR)/$(TARGET): $(SRC)
	$(CC) $(CFLAGS) -o $@ $< $(LDFLAGS)

AppIcon.icns: logo.png
	mkdir -p AppIcon.iconset
	sips -z 16 16     $< --out AppIcon.iconset/icon_16x16.png
	sips -z 32 32     $< --out AppIcon.iconset/icon_16x16@2x.png
	sips -z 32 32     $< --out AppIcon.iconset/icon_32x32.png
	sips -z 64 64     $< --out AppIcon.iconset/icon_32x32@2x.png
	sips -z 128 128   $< --out AppIcon.iconset/icon_128x128.png
	sips -z 256 256   $< --out AppIcon.iconset/icon_128x128@2x.png
	sips -z 256 256   $< --out AppIcon.iconset/icon_256x256.png
	sips -z 512 512   $< --out AppIcon.iconset/icon_256x256@2x.png
	sips -z 512 512   $< --out AppIcon.iconset/icon_512x512.png
	cp $< AppIcon.iconset/icon_512x512@2x.png
	iconutil -c icns AppIcon.iconset
	rm -rf AppIcon.iconset

macos: all AppIcon.icns
	mkdir -p $(MACOS) $(RESOURCES)
	cp $(BUILD_DIR)/$(TARGET) $(MACOS)/
	cp Info.plist $(CONTENTS)/
	cp AppIcon.icns $(RESOURCES)/
	@echo "Built $(APP_BUNDLE)"

directories:
	mkdir -p $(BUILD_DIR)

clean:
	rm -rf $(BUILD_DIR) AppIcon.icns

.PHONY: all clean directories macos