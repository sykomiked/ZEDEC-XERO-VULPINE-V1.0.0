# Makefile.app — M5 Application Build System
#
# Usage:
#   make APP=myapp                    # Build myapp.c
#   make APP=myapp clean              # Clean myapp build
#   make APP=myapp install            # Install to kernel image
#
# The application must include m5_api.h and define main()
#
# Author: 36N9 Genetics, LLC
# Copyright (c) 2024-2026 Michael Laurence Curzi and 36N9 Genetics, LLC
# SPDX-License-Identifier: Apache-2.0
# Licensed under the Apache License, Version 2.0. See LICENSE at
# the repository root.

# Application name (override on command line: make APP=myapp)
APP ?= myapp

# Kernel source root
KERNEL_ROOT := ../../../..

# Architecture
ARCH ?= arm64

# Cross-compiler
ifeq ($(ARCH),arm64)
    CROSS_COMPILE ?= aarch64-linux-gnu-
    CC = $(CROSS_COMPILE)gcc
    LD = $(CROSS_COMPILE)ld
    OBJCOPY = $(CROSS_COMPILE)objcopy
    OBJDUMP = $(CROSS_COMPILE)objdump
    QEMU = qemu-system-aarch64
    QEMU_FLAGS = -M virt,gic-version=3 -cpu cortex-a53 -m 256M
else ifeq ($(ARCH),x86_64)
    CC = gcc
    LD = ld
    OBJCOPY = objcopy
    OBJDUMP = objdump
    QEMU = qemu-system-x86_64
    QEMU_FLAGS = -M q35 -m 256M
else ifeq ($(ARCH),riscv64)
    CROSS_COMPILE ?= riscv64-linux-gnu-
    CC = $(CROSS_COMPILE)gcc
    LD = $(CROSS_COMPILE)ld
    OBJCOPY = $(CROSS_COMPILE)objcopy
    OBJDUMP = $(CROSS_COMPILE)objdump
    QEMU = qemu-system-riscv64
    QEMU_FLAGS = -M virt -m 256M
else
    $(error Unsupported ARCH: $(ARCH))
endif

# Build directory
BUILD_DIR := build/$(ARCH)/apps/$(APP)

# Compiler flags (match kernel exactly)
CFLAGS = -std=c11 -Wall -Wextra -Werror -O2 -ffreestanding -nostdlib \
         -fno-stack-protector -fno-pie -no-pie -fno-builtin \
         -ffunction-sections -fdata-sections \
         -fno-tree-vectorize -fno-tree-slp-vectorize \
         -fno-math-errno \
         -Wno-misleading-indentation \
         -MMD -MP \
         -DKERNEL_SIM_DEVICES=0 \
         -DENABLE_EL0_USERSPACE=1 \
         -DENABLE_TICK_IRQ=1 \
         -DENABLE_EVENT_LOOP=1 \
         -Dmemset=fs_memset -Dmemcpy=fs_memcpy -Dstrlen=fs_strlen \
         -Dstrcmp=fs_strcmp -Dstrcpy=fs_strcpy \
         -Dmalloc=fs_malloc -Dcalloc=fs_calloc \
         -Dmemcmp=fs_memcmp -Dpow=fs_pow -Dlog=fs_log \
         -include $(KERNEL_ROOT)/kernel/include/freestanding.h \
         -I$(KERNEL_ROOT)/kernel/include \
         -I$(KERNEL_ROOT)/kernel/src \
         -I$(KERNEL_ROOT)/kernel/src/sdk \
         -I$(KERNEL_ROOT)/kernel/src/rational \
         -I$(KERNEL_ROOT)/kernel/src/oseq \
         -I$(KERNEL_ROOT)/kernel/src/rmag \
         -I$(KERNEL_ROOT)/kernel/src/lpres \
         -I$(KERNEL_ROOT)/kernel/src/iphase \
         -I$(KERNEL_ROOT)/kernel/src/choice \
         -I$(KERNEL_ROOT)/kernel/src/phase_coord \
         -I$(KERNEL_ROOT)/kernel/src/crit168 \
         -I$(KERNEL_ROOT)/kernel/include/freestanding_stubs

# Linker flags
LDFLAGS = -nostdlib -static -Wl,--gc-sections -Wl,--build-id=none \
          -T $(KERNEL_ROOT)/kernel/arch/$(ARCH)/linker.ld

# Source files
APP_SRC := $(APP).c
APP_OBJ := $(BUILD_DIR)/$(APP).o
APP_ELF := $(BUILD_DIR)/$(APP).elf
APP_BIN := $(BUILD_DIR)/$(APP).bin

# Default target
.PHONY: all clean install run test

all: $(APP_ELF) $(APP_BIN)

# Compile
$(APP_OBJ): $(APP_SRC)
	@mkdir -p $(BUILD_DIR)
	$(CC) $(CFLAGS) -c -o $@ $<

# Link
$(APP_ELF): $(APP_OBJ)
	$(CC) $(LDFLAGS) -o $@ $^ $(shell $(CC) -print-libgcc-file-name 2>/dev/null || echo /usr/lib/gcc-cross/aarch64-linux-gnu/13/libgcc.a)

# Binary
$(APP_BIN): $(APP_ELF)
	$(OBJCOPY) -O binary $< $@

# Clean
clean:
	rm -rf $(BUILD_DIR)

# Install to kernel image (requires kernel rebuild)
install: $(APP_BIN)
	@echo "Installing $(APP) to kernel..."
	@echo "Add to kernel/src/apps/ and rebuild kernel"

# Run in QEMU (requires kernel image)
run: $(APP_BIN)
	@echo "Running $(APP) in QEMU..."
	@echo "Note: App must be linked into kernel image first"
	@echo "Use 'make -C $(KERNEL_ROOT)/kernel -f build_system/Makefile.$(ARCH) run'"

# Test compile (host)
test: $(APP_SRC)
	@echo "Testing $(APP) on host..."
	@gcc -std=c11 -Wall -Wextra -Werror -DTEST_HOST -I$(KERNEL_ROOT)/kernel/include -I$(KERNEL_ROOT)/kernel/src -I$(KERNEL_ROOT)/kernel/src/sdk -I$(KERNEL_ROOT)/kernel/src/rational -o /tmp/$(APP)_test $(APP_SRC) && echo "Host compile OK"

# Show size
size: $(APP_ELF)
	@$(OBJDUMP) -h $(APP_ELF) | grep -E '\.text|\.data|\.bss|\.rodata'
	@$(CROSS_COMPILE)size $(APP_ELF)

# Disassembly
disasm: $(APP_ELF)
	@$(OBJDUMP) -d $(APP_ELF) | head -100

# Help
help:
	@echo "M5 Application Build System"
	@echo ""
	@echo "Targets:"
	@echo "  all       - Build application (default)"
	@echo "  clean     - Remove build artifacts"
	@echo "  install   - Install to kernel (manual step)"
	@echo "  run       - Run in QEMU (requires kernel rebuild)"
	@echo "  test      - Test compile on host"
	@echo "  size      - Show binary size"
	@echo "  disasm    - Show disassembly"
	@echo "  help      - Show this help"
	@echo ""
	@echo "Variables:"
	@echo "  APP=myapp     - Application name (default: myapp)"
	@echo "  ARCH=arm64    - Target architecture (arm64, x86_64, riscv64)"
	@echo ""
	@echo "Example:"
	@echo "  make APP=wallet ARCH=arm64"
	@echo "  make APP=myapp test"
	@echo "  make APP=myapp clean"

# Include dependencies
-include $(APP_OBJ:.o=.d)
