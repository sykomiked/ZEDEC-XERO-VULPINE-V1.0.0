# platform_layer.mk — the arch-neutral platform layer, as one includable list.
#
# These 11 modules sit on top of the M5 core and are pure, freestanding, integer
# C with no architecture dependency. They are already wired into Makefile.arm64
# (which boots them — see the [FEAT] lines in the boot log). To bring the same
# platform layer up on x86_64 / riscv / arm32, an arch Makefile does two things:
#
#   1. include this fragment and append the vars:
#        include build_system/platform_layer.mk
#        KERNEL_SRCS += $(PLATFORM_LAYER_SRCS)
#        CFLAGS      += $(PLATFORM_LAYER_INC)
#
#   2. call the single entry point from that arch's kernel_main, just before the
#      "entering event cycle" / BOOT_OK milestone:
#        #include "boot_features.h"
#        boot_features_init(<arch_uart_puts>, <cpu_cores_or_0>, <mem_mb_or_0>);
#
# That is the entire integration. Verified: all 11 compile freestanding for
# x86_64-elf, aarch64-none-elf and armv7-none-eabi under clang. (riscv compiles
# too on a clang that has the RISCV backend built in.)

PLATFORM_LAYER_SRCS := \
    kernel/src/deploy/deploy.c \
    kernel/src/theme/theme.c \
    kernel/src/icon/icon.c \
    kernel/src/font/script.c \
    kernel/src/font/font.c \
    kernel/src/font/truetype.c \
    kernel/src/bridge/bridge.c \
    kernel/src/update/update.c \
    kernel/src/mage/mage.c \
    kernel/src/reality/reality.c \
    kernel/src/bootfeat/boot_features.c

PLATFORM_LAYER_INC := \
    -Ikernel/src/deploy -Ikernel/src/theme -Ikernel/src/icon -Ikernel/src/font \
    -Ikernel/src/bridge -Ikernel/src/update -Ikernel/src/mage -Ikernel/src/reality \
    -Ikernel/src/bootfeat -Ikernel/src/cards -Ikernel/src/surplus -Ikernel/src/edp_risk
