CC = gcc
CFLAGS = -Wall -Wextra -O2 `pkg-config --cflags gtk+-3.0` -lm
LDFLAGS = `pkg-config --libs gtk+-3.0` -lm

ifeq ($(shell uname), Darwin)
    MACOSX_DEPLOYMENT_TARGET ?= 26.0
    export MACOSX_DEPLOYMENT_TARGET
    CFLAGS += -x objective-c -mmacosx-version-min=$(MACOSX_DEPLOYMENT_TARGET)
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
ENTITLEMENTS = packaging/macos/Splashy.entitlements
DEVELOPER_ID ?=
APP_STORE_IDENTITY ?=
INSTALLER_IDENTITY ?=

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

macos-bundle: macos
	chmod +x packaging/macos/bundle-gtk.sh
	packaging/macos/bundle-gtk.sh $(APP_BUNDLE)

macos-sign: macos-bundle
	@if [ -z "$(DEVELOPER_ID)" ]; then echo "Set DEVELOPER_ID to your signing identity."; exit 1; fi
	find $(APP_BUNDLE)/Contents/Frameworks $(APP_BUNDLE)/Contents/Resources/lib -type f \( -name '*.dylib' -o -name '*.so' \) -exec codesign --force --options runtime --timestamp --sign "$(DEVELOPER_ID)" {} \;
	codesign --force --options runtime --timestamp --entitlements $(ENTITLEMENTS) --sign "$(DEVELOPER_ID)" $(APP_BUNDLE)
	codesign --verify --strict --verbose=2 $(APP_BUNDLE)

macos-appstore-sign: macos-bundle
	@if [ -z "$(APP_STORE_IDENTITY)" ]; then echo "Set APP_STORE_IDENTITY to your Mac App Store application signing identity."; exit 1; fi
	find $(APP_BUNDLE)/Contents/Frameworks $(APP_BUNDLE)/Contents/Resources/lib -type f \( -name '*.dylib' -o -name '*.so' \) -exec codesign --force --options runtime --timestamp --sign "$(APP_STORE_IDENTITY)" {} \;
	codesign --force --options runtime --timestamp --entitlements $(ENTITLEMENTS) --sign "$(APP_STORE_IDENTITY)" $(APP_BUNDLE)
	codesign --verify --strict --verbose=2 $(APP_BUNDLE)

macos-pkg: macos-appstore-sign
	@if [ -z "$(INSTALLER_IDENTITY)" ]; then echo "Set INSTALLER_IDENTITY to your Mac App Store installer signing identity."; exit 1; fi
	productbuild --component $(APP_BUNDLE) /Applications --sign "$(INSTALLER_IDENTITY)" $(BUILD_DIR)/$(APP_NAME).pkg
	@echo "Built $(BUILD_DIR)/$(APP_NAME).pkg"

directories:
	mkdir -p $(BUILD_DIR)

clean:
	rm -rf $(BUILD_DIR) AppIcon.icns

.PHONY: all clean directories macos macos-bundle macos-sign macos-appstore-sign macos-pkg
