# SwitchLM-Studio Root Makefile
# Builds studio (.nro), sysmodule (.nsp), overlay (.ovl)
# Auto-detects devkitPro: C:/devkitPro on Windows, /opt/devkitpro on Linux/Docker

ifeq ($(OS),Windows_NT)
    DEVKITPRO ?= C:/devkitPro
else
    DEVKITPRO ?= /opt/devkitpro
endif

DEVKITA64 := $(DEVKITPRO)/devkitA64

include $(DEVKITA64)/base_rules

# Directories
ROOT := .
COMMON_DIR := $(ROOT)/common
STUDIO_DIR := $(ROOT)/studio
SYSMOD_DIR := $(ROOT)/sysmodule
OVERLAY_DIR := $(ROOT)/overlay

# Architecture flags for ARMv8 Cortex-A57 (Tegra X1)
ARCH   := -march=armv8-a+crc+crypto -mtune=cortex-a57
OPT    := -O3 -ffast-math -ftree-vectorize -fno-rtti -fno-exceptions
WARN   := -Wall -Wextra -Wno-unused-parameter
CFLAGS := $(ARCH) $(OPT) $(WARN) -std=c11
CXXFLAGS := $(ARCH) $(OPT) $(WARN) -std=c++20

# Shared libraries
LIBS := -lnx

.PHONY: all studio sysmodule overlay package clean

all: studio sysmodule overlay

studio:
	@echo "================================================"
	@echo " Building SwitchLM-Studio.nro (Main App)"
	@echo "================================================"
	@$(MAKE) -C $(STUDIO_DIR)

sysmodule:
	@echo "================================================"
	@echo " Building SwitchLM-Sysmodule.nsp"
	@echo "================================================"
	@$(MAKE) -C $(SYSMOD_DIR)

overlay:
	@echo "================================================"
	@echo " Building SwitchLM-Overlay.ovl"
	@echo "================================================"
	@$(MAKE) -C $(OVERLAY_DIR)

package: all
	@echo "================================================"
	@echo " Packaging release into sdout/"
	@echo "================================================"
	@mkdir -p sdout/atmosphere/contents/0100000000004C4D/exefs
	@mkdir -p sdout/switch/.overlays
	@mkdir -p sdout/switch/SwitchLM-Studio
	@mkdir -p sdout/switch/SwitchLM/wikis
	@mkdir -p sdout/switch/SwitchLM/models
	@mkdir -p sdout/switch/SwitchLM/micro
	@cp -f sysmodule/build/exefs_dir/* sdout/atmosphere/contents/0100000000004C4D/exefs/ || true
	@cp -f sysmodule/toolbox.json sdout/atmosphere/contents/0100000000004C4D/
	@cp -f overlay/SwitchLM-Overlay.ovl sdout/switch/.overlays/
	@cp -f studio/SwitchLM-Studio.nro sdout/switch/SwitchLM-Studio/
	@python3 tools/build_wiki_idx.py
	@echo "[OK] All artifacts packaged in sdout/"

clean:
	@$(MAKE) -C $(STUDIO_DIR) clean
	@$(MAKE) -C $(SYSMOD_DIR) clean
	@$(MAKE) -C $(OVERLAY_DIR) clean
