# ==============================================================================
# Configuration Variables
# ==============================================================================

# Help target
help:
	@echo "Board-Specific Build System"
	@echo "==========================="
	@echo ""
	@echo "Device Database Build:"
	@echo "  build              - Build firmware for specified BOARD (from device_db.yaml)"
	@echo ""
	@echo "Configuration:"
	@echo "  BOARD              - Device name from device_db.yaml (default: $(BOARD))"
	@echo "  DEVICE_TYPE        - Extracted from database (current: $(DEVICE_TYPE))"
	@echo ""
	@echo "Generated Files:"
	@echo "  OTA Files          - Standard, Tuya migration, and force upgrade variants"
	@echo "  Z2M Indexes        - Zigbee2MQTT OTA index updates"
	@echo ""
	@echo "Example Usage:"
	@echo "  BOARD=TUYA_TS0012 make build"
	@echo "  BOARD=MOES_3_GANG_SWITCH DEVICE_TYPE=router make build"
	@echo ""

# Parse version from VERSION file
VERSION_FILE_CONTENT := $(shell cat VERSION 2>/dev/null || echo "1.0.0")

# Split semantic version
VERSION_PARTS := $(subst ., ,$(VERSION_FILE_CONTENT))
VERSION_MAJOR := $(word 1,$(VERSION_PARTS))
VERSION_MINOR := $(word 2,$(VERSION_PARTS))
VERSION_PATCH := $(word 3,$(VERSION_PARTS))

# Get git info
GIT_HASH   := $(shell git rev-parse --short HEAD 2>/dev/null || echo "")

# Limites to 16 characters
VERSION_STR := $(shell echo "$(VERSION_FILE_CONTENT)-$(GIT_HASH)" | cut -c1-16)

# Zigbee file version 4 bytes are:
# APP_RELEASE, APP_BUILD, STACK_RELEASE, STACK_BUILD
# We encode MAJOR.MINOR -> APP_RELEASE (in BCD format)
# PATCH -> APP_BUILD
# and STACK_RELEASE fixed to 0x30,
# STACK_BUILD is depth from main branch, to allow easy updating during feature development
STACK_BUILD := $(shell git rev-list --count origin/main..HEAD 2>/dev/null || echo "0")
VERSION_PATCH_HEX := $(shell printf "%02d" $(VERSION_PATCH))
STACK_BUILD_HEX := $(shell printf "%02X" $(STACK_BUILD))
FILE_VERSION = 0x$(VERSION_MAJOR)$(VERSION_MINOR)$(VERSION_PATCH_HEX)30$(STACK_BUILD_HEX)

NVM_MIGRATIONS_VERSION := $(shell cat NVM_MIGRATIONS_VERSION 2>/dev/null || echo "1")

PROJECT_NAME := tlc_switch
BOARD ?= TUYA_TS0012
DEVICE_DB_FILE := device_db.yaml

# ==============================================================================
# Database-Derived Variables
# ==============================================================================
DEVICE_TYPE ?= $(shell yq -r .$(BOARD).device_type $(DEVICE_DB_FILE))
MCU ?= $(shell yq -r .$(BOARD).mcu $(DEVICE_DB_FILE))
MCU_FAMILY := $(shell yq -r .$(BOARD).mcu_family $(DEVICE_DB_FILE))
CONFIG_STR := $(shell yq -r .$(BOARD).config_str $(DEVICE_DB_FILE))
FROM_STOCK_MANUFACTURER_ID := $(shell yq -r .$(BOARD).stock_manufacturer_id $(DEVICE_DB_FILE))
FROM_STOCK_IMAGE_TYPE := $(shell yq -r .$(BOARD).stock_image_type $(DEVICE_DB_FILE))
FIRMWARE_IMAGE_TYPE := $(shell yq -r .$(BOARD).firmware_image_type $(DEVICE_DB_FILE))

# ==============================================================================
# Platform Configuration
# ==============================================================================
# TODO: make MCU_FAMILY lowercase in device_db.yaml and remove this line
PLATFORM_PREFIX := $(shell echo $(MCU_FAMILY) | tr A-Z a-z)

# ==============================================================================
# Silicon Labs SDK Selector Contract
# ==============================================================================
SILABS_SDK_CATALOG := \
	gecko_sdk|4.5.0|gecko-sdk.zip|https://github.com/SiliconLabs/gecko_sdk/releases/download/v4.5.0/gecko-sdk.zip \
	simplicity_sdk|2025.6.2|simplicity-sdk.zip|https://github.com/SiliconLabs/simplicity_sdk/releases/download/v2025.6.2/simplicity-sdk.zip

SILABS_SDK_SELECTOR := \
	EFR32MG13P732F512GM48|gecko_sdk \
	EFR32MG21A020F768IM32|simplicity_sdk \
	EFR32MG22C224F512GN32|simplicity_sdk

SILABS_MG13_TARGET_BOARDS := \
	SWITCH_TUYA_VQJOB26P_TS0011 \
	SWITCH_TUYA_VQJOB26P_TS0012 \
	SWITCH_TUYA_VQJOB26P_TS0013

SILABS_NON_MG13_REGRESSION_BOARDS := \
	MODULE_MOES_TS0011 \
	MODULE_TUYA_ZS2S_TS0001 \
	REMOTE_MOES_SWITCH_A_TS0042 \
	REMOTE_MOES_SWITCH_A_TS0043 \
	REMOTE_MOES_SWITCH_A_TS0044 \
	REMOTE_TUYA_TS004F \
	SWITCH_MANHOT_B_TS0011 \
	SWITCH_MANHOT_B_TS0012 \
	SWITCH_MANHOT_B_TS0013 \
	SWITCH_MOES_ALL_TS0014 \
	SWITCH_PSMART_SL_TS0001 \
	SWITCH_PSMART_SL_TS0002 \
	SWITCH_PSMART_SL_TS0003 \
	SWITCH_ZEMISMART_2_TS0011 \
	SWITCH_ZEMISMART_2_TS0012 \
	MODULE_SONOFF_ZBMINIL2

SILABS_ARCHITECTURE_GATE_BOARDS := \
	SWITCH_TUYA_VQJOB26P_TS0011 \
	MODULE_MOES_TS0011 \
	MODULE_SONOFF_ZBMINIL2

SILABS_REGRESSION_GATE_BOARDS := \
	$(SILABS_MG13_TARGET_BOARDS) \
	$(SILABS_NON_MG13_REGRESSION_BOARDS)

silabs_selector_record = $(strip $(firstword $(filter $(1)|%,$(SILABS_SDK_SELECTOR))))
silabs_catalog_record = $(strip $(firstword $(filter $(1)|%,$(SILABS_SDK_CATALOG))))
silabs_selector_field = $(word $(2),$(subst |, ,$(call silabs_selector_record,$(1))))
silabs_catalog_field = $(word $(2),$(subst |, ,$(call silabs_catalog_record,$(1))))

SILABS_SDK_LINES := $(foreach record,$(SILABS_SDK_CATALOG),$(word 1,$(subst |, ,$(record))))
SILABS_SDK_LINE := $(call silabs_selector_field,$(MCU),2)
SILABS_SDK_VERSION := $(call silabs_catalog_field,$(SILABS_SDK_LINE),2)
SILABS_SDK_ARCHIVE := $(call silabs_catalog_field,$(SILABS_SDK_LINE),3)
SILABS_SDK_URL := $(call silabs_catalog_field,$(SILABS_SDK_LINE),4)
SILABS_SDK_DIR := silabs_tools/$(SILABS_SDK_LINE)

REQUESTED_SILABS_SDK_LINE := $(if $(strip $(SDK_LINE)),$(SDK_LINE),$(SILABS_SDK_LINE))
REQUESTED_SILABS_SDK_VERSION := $(call silabs_catalog_field,$(REQUESTED_SILABS_SDK_LINE),2)
REQUESTED_SILABS_SDK_ARCHIVE := $(call silabs_catalog_field,$(REQUESTED_SILABS_SDK_LINE),3)
REQUESTED_SILABS_SDK_URL := $(call silabs_catalog_field,$(REQUESTED_SILABS_SDK_LINE),4)

# ==============================================================================
# Path Variables
# ==============================================================================
BOARD_DIR := $(BOARD)$(if $(filter end_device,$(DEVICE_TYPE)),_END_DEVICE)
BIN_PATH := bin/$(DEVICE_TYPE)/$(BOARD_DIR)
HELPERS_PATH := ./helper_scripts

# OTA Files
ifeq ($(PLATFORM_PREFIX),silabs)
BIN_FILE := $(BIN_PATH)/$(PROJECT_NAME)-$(VERSION_STR).s37
else
BIN_FILE := $(BIN_PATH)/$(PROJECT_NAME)-$(VERSION_STR).bin
endif
OTA_FILE := $(BIN_PATH)/$(PROJECT_NAME)-$(VERSION_STR).zigbee
FROM_TUYA_OTA_FILE := $(BIN_PATH)/$(PROJECT_NAME)-$(VERSION_STR)-from_tuya.zigbee
FORCE_OTA_FILE := $(BIN_PATH)/$(PROJECT_NAME)-$(VERSION_STR)-forced.zigbee

# Index Files
Z2M_INDEX_FILE := zigbee2mqtt/ota/index_$(DEVICE_TYPE).json
Z2M_FORCE_INDEX_FILE := zigbee2mqtt/ota/index_$(DEVICE_TYPE)-FORCE.json

SILABS_BUILD_PREREQS :=
ifeq ($(PLATFORM_PREFIX),silabs)
SILABS_BUILD_PREREQS += assert-silabs-sdk-selector
endif

# Main target - builds firmware and generates all OTA files
build: drop-old-files build-firmware generate-ota-files update-indexes

# Build the firmware for the specified board
build-firmware: $(SILABS_BUILD_PREREQS)
ifeq ($(PLATFORM_PREFIX),silabs)
	$(MAKE) silabs/gen \
		BOARD=$(BOARD) \
		VERSION_STR=$(VERSION_STR) \
		NVM_MIGRATIONS_VERSION=$(NVM_MIGRATIONS_VERSION) \
		FILE_VERSION=$(FILE_VERSION) \
		DEVICE_TYPE=$(DEVICE_TYPE) \
		CONFIG_STR="$(CONFIG_STR)" \
		IMAGE_TYPE=$(FIRMWARE_IMAGE_TYPE) \
		BIN_FILE=../../$(BIN_FILE) \
		MCU=$(MCU) \
		SILABS_SDK_LINE=$(SILABS_SDK_LINE) \
		SILABS_SDK_VERSION=$(SILABS_SDK_VERSION) \
		SILABS_SDK_DIR=../../$(SILABS_SDK_DIR)
endif
ifeq ($(PLATFORM_PREFIX),telink)
	$(MAKE) -C src/telink clean
endif
	$(MAKE) -C src/$(PLATFORM_PREFIX) build \
		BOARD=$(BOARD) \
		VERSION_STR=$(VERSION_STR) \
		NVM_MIGRATIONS_VERSION=$(NVM_MIGRATIONS_VERSION) \
		FILE_VERSION=$(FILE_VERSION) \
		DEVICE_TYPE=$(DEVICE_TYPE) \
		CONFIG_STR="$(CONFIG_STR)" \
		IMAGE_TYPE=$(FIRMWARE_IMAGE_TYPE) \
		BIN_FILE=../../$(BIN_FILE) \
		MCU=$(MCU) \
		SILABS_SDK_LINE=$(SILABS_SDK_LINE) \
		SILABS_SDK_VERSION=$(SILABS_SDK_VERSION) \
		SILABS_SDK_DIR=../../$(SILABS_SDK_DIR) \
		 -j32

drop-old-files:
	rm -f $(BIN_PATH)/*.bin
	rm -f $(BIN_PATH)/*.s37
	rm -f $(BIN_PATH)/*.zigbee

# Generate all three types of OTA files
generate-ota-files: generate-normal-ota generate-tuya-ota generate-force-ota

generate-normal-ota:
	$(MAKE) $(PLATFORM_PREFIX)/ota \
		DEVICE_TYPE=$(DEVICE_TYPE) \
		FILE_VERSION=$(FILE_VERSION) \
		OTA_IMAGE_TYPE=$(FIRMWARE_IMAGE_TYPE) \
		OTA_FILE=../../$(OTA_FILE)

generate-tuya-ota:
ifneq ($(PLATFORM_PREFIX),silabs)  # Silabs platform does not support Tuya migration OTAs
	$(MAKE) $(PLATFORM_PREFIX)/ota \
		OTA_VERSION=0xFFFFFFFF \
		DEVICE_TYPE=$(DEVICE_TYPE) \
		OTA_IMAGE_TYPE=$(FROM_STOCK_IMAGE_TYPE) \
		OTA_MANUFACTURER_ID=$(FROM_STOCK_MANUFACTURER_ID) \
		OTA_FILE=../../$(FROM_TUYA_OTA_FILE)
endif

generate-force-ota:
	$(MAKE) $(PLATFORM_PREFIX)/ota \
		OTA_VERSION=0xFFFFFFFF \
		DEVICE_TYPE=$(DEVICE_TYPE) \
		OTA_IMAGE_TYPE=$(FIRMWARE_IMAGE_TYPE) \
		OTA_FILE=../../$(FORCE_OTA_FILE)

# Update Zigbee2MQTT index files
update-indexes:
	@python3 $(HELPERS_PATH)/make_z2m_ota_index.py --db_file $(DEVICE_DB_FILE) $(OTA_FILE) $(Z2M_INDEX_FILE) --board $(BOARD)
ifneq ($(PLATFORM_PREFIX),silabs)  # Silabs platform does not support Tuya migration OTAs
ifneq ($(FROM_STOCK_MANUFACTURER_ID),null)
ifneq ($(FROM_STOCK_IMAGE_TYPE),null)
	@python3 $(HELPERS_PATH)/make_z2m_ota_index.py --db_file $(DEVICE_DB_FILE) $(FROM_TUYA_OTA_FILE) $(Z2M_INDEX_FILE) --board $(BOARD)
endif
endif
endif
	@python3 $(HELPERS_PATH)/make_z2m_ota_index.py --db_file $(DEVICE_DB_FILE) $(FORCE_OTA_FILE) $(Z2M_FORCE_INDEX_FILE) --board $(BOARD)


flash_telink: build-firmware
	@echo "Flashing $(BIN_FILE) to device via $(TLSRPGM_TTY)"
	$(MAKE) telink/flasher ARGS="-t25 -a 20 --mrst we 0 ../../$(BIN_FILE)"

assert-silabs-sdk-selector:
	@if [ -z "$(SILABS_SDK_LINE)" ] || [ -z "$(SILABS_SDK_VERSION)" ]; then \
		echo "Unsupported Silabs MCU '$(MCU)' for BOARD '$(BOARD)'. Update SILABS_SDK_SELECTOR in board.mk." >&2; \
		exit 1; \
	fi

assert-requested-silabs-sdk-line:
	@if [ -z "$(REQUESTED_SILABS_SDK_LINE)" ] || [ -z "$(REQUESTED_SILABS_SDK_VERSION)" ]; then \
		echo "Unsupported Silabs SDK line '$(SDK_LINE)'." >&2; \
		exit 1; \
	fi

print-silabs-sdk-record: assert-requested-silabs-sdk-line
	@printf '%s|%s|%s|%s\n' \
		"$(REQUESTED_SILABS_SDK_LINE)" \
		"$(REQUESTED_SILABS_SDK_VERSION)" \
		"$(REQUESTED_SILABS_SDK_ARCHIVE)" \
		"$(REQUESTED_SILABS_SDK_URL)"

print-silabs-sdk-install-manifest:
	@$(foreach line,$(SILABS_SDK_LINES),printf '%s|%s|%s|%s\n' "$(line)" "$(call silabs_catalog_field,$(line),2)" "$(call silabs_catalog_field,$(line),3)" "$(call silabs_catalog_field,$(line),4)";)

print-silabs-sdk-cache-key: assert-requested-silabs-sdk-line
	@printf '%s-%s\n' "$(REQUESTED_SILABS_SDK_LINE)" "$(REQUESTED_SILABS_SDK_VERSION)"

print-silabs-sdk-provenance: assert-silabs-sdk-selector
	@printf 'BOARD=%s MCU=%s SDK_LINE=%s SDK_VERSION=%s\n' \
		"$(BOARD)" "$(MCU)" "$(SILABS_SDK_LINE)" "$(SILABS_SDK_VERSION)"

print-silabs-architecture-gate-boards:
	@printf '%s\n' "$(SILABS_ARCHITECTURE_GATE_BOARDS)"

print-silabs-mg13-target-boards:
	@printf '%s\n' "$(SILABS_MG13_TARGET_BOARDS)"

print-silabs-non-mg13-regression-boards:
	@printf '%s\n' "$(SILABS_NON_MG13_REGRESSION_BOARDS)"

print-silabs-regression-gate-boards:
	@printf '%s\n' "$(SILABS_REGRESSION_GATE_BOARDS)"

.PHONY: help build build-firmware drop-old-files generate-ota-files generate-normal-ota generate-tuya-ota generate-force-ota update-indexes clean_z2m_index update_converters update_zha_quirk update_homed_extension update_supported_devices freeze_ota_links assert-silabs-sdk-selector assert-requested-silabs-sdk-line print-silabs-sdk-record print-silabs-sdk-install-manifest print-silabs-sdk-cache-key print-silabs-sdk-provenance print-silabs-architecture-gate-boards print-silabs-mg13-target-boards print-silabs-non-mg13-regression-boards print-silabs-regression-gate-boards
