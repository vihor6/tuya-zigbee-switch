# Silicon Labs Tools Download Makefile
# This makefile provides targets to download Silicon Labs development tools

PROJECT_ROOT := ../..
TOOLS_DIR := $(PROJECT_ROOT)/silabs_tools
DOWNLOAD_DIR := $(TOOLS_DIR)/downloads
BOARD_MAKE := $(PROJECT_ROOT)/board.mk

COMMANDER_URL := https://www.silabs.com/documents/public/software/SimplicityCommander-Linux.zip
SLC_CLI_URL := https://www.silabs.com/documents/public/software/slc_cli_linux.zip

ZAP_VERSION := 2025.10.23
ZAP_ARCHIVE := zap-linux-x64.zip
ZAP_URL := https://github.com/project-chip/zap/releases/download/v$(ZAP_VERSION)/$(ZAP_ARCHIVE)

.PHONY: all sdk-all clean clean-downloads help status verify trust commander slc-cli zap

all: sdk-all commander slc-cli zap trust verify
	@echo "All Silicon Labs tools have been downloaded and installed to $(TOOLS_DIR)"

help:
	@echo "Silicon Labs Tools Download Makefile"
	@echo ""
	@echo "Available targets:"
	@echo "  all              - Download commander, slc-cli, zap, and all SDK roots from board.mk"
	@echo "  sdk-all          - Download and install all SDK roots from the authoritative selector manifest"
	@echo "  commander        - Download and install Simplicity Commander"
	@echo "  slc-cli          - Download and install SLC CLI"
	@echo "  zap              - Download and install ZAP"
	@echo "  trust            - Trust all installed SDK roots"
	@echo "  verify           - Verify installed tools and requirements"
	@echo "  clean            - Remove installed tools (keep downloads)"
	@echo "  clean-downloads  - Remove downloaded archives"
	@echo "  status           - Show status summary"

$(TOOLS_DIR):
	mkdir -p $(TOOLS_DIR)

$(DOWNLOAD_DIR): | $(TOOLS_DIR)
	mkdir -p $(DOWNLOAD_DIR)

sdk-all: | $(DOWNLOAD_DIR)
	@set -e; \
	$(MAKE) --no-print-directory -s -f $(BOARD_MAKE) print-silabs-sdk-install-manifest | while IFS='|' read -r line version archive url; do \
		echo "Downloading $$line v$$version..."; \
		if [ ! -f "$(DOWNLOAD_DIR)/$$archive" ]; then \
			echo "Downloading $$url"; \
			curl -L "$$url" -o "$(DOWNLOAD_DIR)/$$archive" --fail --show-error; \
		fi; \
		echo "Extracting $$line..."; \
		rm -rf "$(TOOLS_DIR)/$$line"; \
		mkdir -p "$(TOOLS_DIR)/$$line"; \
		unzip -q "$(DOWNLOAD_DIR)/$$archive" -d "$(TOOLS_DIR)/$$line"; \
		SDK_SUBDIR=$$(find "$(TOOLS_DIR)/$$line" -mindepth 1 -maxdepth 1 -type d | head -1); \
		ENTRY_COUNT=$$(find "$(TOOLS_DIR)/$$line" -mindepth 1 -maxdepth 1 | wc -l); \
		if [ "$$ENTRY_COUNT" = "1" ] && [ -n "$$SDK_SUBDIR" ]; then \
			cp -a "$$SDK_SUBDIR"/. "$(TOOLS_DIR)/$$line/"; \
			rm -rf "$$SDK_SUBDIR"; \
		fi; \
	done; \
	rm -f "$(TOOLS_DIR)/spiflash_extension"; \
	ln -s ../src/silabs/spiflash_extension "$(TOOLS_DIR)/spiflash_extension"; \
	echo "Installed Silicon Labs SDK roots from board.mk selector manifest"

trust: $(TOOLS_DIR)/slc-cli sdk-all
	@set -e; \
	$(MAKE) --no-print-directory -s -f $(BOARD_MAKE) print-silabs-sdk-install-manifest | while IFS='|' read -r line version archive url; do \
		echo "Trusting Silicon Labs SDK signature for $$line..."; \
		$(TOOLS_DIR)/slc-cli/slc signature trust --sdk "$(TOOLS_DIR)/$$line"; \
		$(TOOLS_DIR)/slc-cli/slc signature trust --sdk "$(TOOLS_DIR)/$$line" -extpath $(abspath $(TOOLS_DIR)/spiflash_extension); \
	done

commander: $(TOOLS_DIR)/commander
	@echo "Simplicity Commander installed successfully"

$(TOOLS_DIR)/commander: | $(DOWNLOAD_DIR)
	@echo "Downloading Simplicity Commander..."
	@if [ ! -f "$(DOWNLOAD_DIR)/SimplicityCommander-Linux.zip" ]; then \
		echo "Attempting to download from: $(COMMANDER_URL)"; \
		if ! curl -L "$(COMMANDER_URL)" -o "$(DOWNLOAD_DIR)/SimplicityCommander-Linux.zip" --fail --show-error --connect-timeout 30; then \
			echo "Download failed. Please manually download SimplicityCommander-Linux.zip"; \
			exit 1; \
		fi; \
	fi
	@rm -rf $(TOOLS_DIR)/commander
	@mkdir -p $(TOOLS_DIR)/commander_temp
	@unzip -q "$(DOWNLOAD_DIR)/SimplicityCommander-Linux.zip" -d $(TOOLS_DIR)/commander_temp
	@ARCH=$$(uname -m); \
	if [ "$$ARCH" = "x86_64" ]; then \
		COMMANDER_FILE=$$(find $(TOOLS_DIR)/commander_temp -name "*cli*linux_x86_64*.tar.bz" | head -1); \
	elif [ "$$ARCH" = "aarch64" ]; then \
		COMMANDER_FILE=$$(find $(TOOLS_DIR)/commander_temp -name "*cli*linux_aarch64*.tar.bz" | head -1); \
	elif [ "$$ARCH" = "armv7l" ]; then \
		COMMANDER_FILE=$$(find $(TOOLS_DIR)/commander_temp -name "*cli*linux_aarch32*.tar.bz" | head -1); \
	else \
		COMMANDER_FILE=$$(find $(TOOLS_DIR)/commander_temp -name "*cli*linux_x86_64*.tar.bz" | head -1); \
	fi; \
	if [ -n "$$COMMANDER_FILE" ]; then \
		mkdir -p $(TOOLS_DIR)/commander; \
		tar -xjf "$$COMMANDER_FILE" -C $(TOOLS_DIR)/commander --strip-components=1; \
	else \
		echo "No suitable Commander archive found for architecture $$ARCH"; \
		exit 1; \
	fi
	@rm -rf $(TOOLS_DIR)/commander_temp
	@chmod +x $(TOOLS_DIR)/commander/commander* 2>/dev/null || true

slc-cli: $(TOOLS_DIR)/slc-cli
	@echo "SLC CLI installed successfully"

$(TOOLS_DIR)/slc-cli: | $(DOWNLOAD_DIR)
	@echo "Downloading SLC CLI..."
	@if [ ! -f "$(DOWNLOAD_DIR)/slc_cli_linux.zip" ]; then \
		echo "Attempting to download from: $(SLC_CLI_URL)"; \
		if ! curl -L "$(SLC_CLI_URL)" -o "$(DOWNLOAD_DIR)/slc_cli_linux.zip" --fail --show-error --connect-timeout 30; then \
			echo "Download failed. Please manually download slc_cli_linux.zip"; \
			exit 1; \
		fi; \
	fi
	@rm -rf $(TOOLS_DIR)/slc-cli
	@mkdir -p $(TOOLS_DIR)/slc-cli
	@unzip -q "$(DOWNLOAD_DIR)/slc_cli_linux.zip" -d $(TOOLS_DIR)/slc-cli
	@if [ -d "$(TOOLS_DIR)/slc-cli/slc_cli" ]; then \
		cp -a $(TOOLS_DIR)/slc-cli/slc_cli/. $(TOOLS_DIR)/slc-cli/; \
		rm -rf $(TOOLS_DIR)/slc-cli/slc_cli; \
	fi
	@chmod +x $(TOOLS_DIR)/slc-cli/slc $(TOOLS_DIR)/slc-cli/bin/slc-cli 2>/dev/null || true

zap: $(TOOLS_DIR)/zap
	@echo "ZAP installed successfully"

$(TOOLS_DIR)/zap: | $(DOWNLOAD_DIR)
	@echo "Downloading ZAP v$(ZAP_VERSION)..."
	@if [ ! -f "$(DOWNLOAD_DIR)/$(ZAP_ARCHIVE)" ]; then \
		echo "Downloading $(ZAP_URL)"; \
		curl -L "$(ZAP_URL)" -o "$(DOWNLOAD_DIR)/$(ZAP_ARCHIVE)" --fail --show-error; \
	fi
	@rm -rf $(TOOLS_DIR)/zap
	@mkdir -p $(TOOLS_DIR)/zap
	@unzip -q "$(DOWNLOAD_DIR)/$(ZAP_ARCHIVE)" -d $(TOOLS_DIR)/zap
	@chmod +x $(TOOLS_DIR)/zap/zap* 2>/dev/null || true

clean:
	@echo "Removing installed tools from $(TOOLS_DIR)..."
	@rm -rf $(TOOLS_DIR)/simplicity_sdk $(TOOLS_DIR)/gecko_sdk $(TOOLS_DIR)/commander $(TOOLS_DIR)/slc-cli $(TOOLS_DIR)/zap $(TOOLS_DIR)/spiflash_extension
	@echo "Tools removed (downloads preserved)"

clean-downloads:
	@echo "Removing downloaded archives from $(DOWNLOAD_DIR)..."
	@rm -rf $(DOWNLOAD_DIR)
	@echo "Downloads removed"

verify:
	@echo "Verifying installed tools..."
	@set -e; \
	$(MAKE) --no-print-directory -s -f $(BOARD_MAKE) print-silabs-sdk-install-manifest | while IFS='|' read -r line version archive url; do \
		if [ -d "$(TOOLS_DIR)/$$line" ]; then \
			echo "✓ Silicon Labs SDK ($$line): $(TOOLS_DIR)/$$line"; \
			SDK_METADATA=$$(find "$(TOOLS_DIR)/$$line" -maxdepth 1 -name '*.slcs' | head -1); \
			echo "  Version: $$(grep '^sdk_version:' "$$SDK_METADATA" 2>/dev/null | sed 's/sdk_version: "\(.*\)"/\1/' || echo 'Unknown')"; \
		else \
			echo "✗ Silicon Labs SDK ($$line): Not installed"; \
			exit 1; \
		fi; \
	done
	@if [ -f "$(TOOLS_DIR)/commander/commander-cli" ]; then \
		echo "✓ Simplicity Commander: $(TOOLS_DIR)/commander/commander-cli"; \
	else \
		echo "✗ Simplicity Commander: Not installed"; \
		exit 1; \
	fi
	@if [ -f "$(TOOLS_DIR)/slc-cli/slc" ]; then \
		echo "✓ SLC CLI: $(TOOLS_DIR)/slc-cli/slc"; \
	else \
		echo "✗ SLC CLI: Not installed"; \
		exit 1; \
	fi
	@ZAP_EXEC=$$(find $(TOOLS_DIR)/zap -name "zap" -type f -executable 2>/dev/null | head -1); \
	if [ -n "$$ZAP_EXEC" ]; then \
		echo "✓ ZAP: $$ZAP_EXEC"; \
	else \
		echo "✗ ZAP: Not installed"; \
		exit 1; \
	fi

status:
	@echo "Silicon Labs Tools Status:"
	@echo "Tools directory: $(TOOLS_DIR)"
	@echo "Download directory: $(DOWNLOAD_DIR)"
	@echo ""
	@$(MAKE) -f $(lastword $(MAKEFILE_LIST)) verify
