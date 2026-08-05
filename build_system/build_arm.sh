#!/bin/bash
# build_arm.sh — Build VOVINA SHAKINA kernel for ARM (QEMU virt machine)
# Runs inside Docker with ARM cross-compiler
# Author: H.M. Michael-Laurence: Curzi (c)
set -e

echo "============================================"
echo "  ZEDEC pqOS — ARM Kernel Builder"
echo "  VOVINA SHAKINA M5 Axiomatic Kernel [ARM]"
echo "============================================"
echo ""

cd /zedec-build/kernel

echo "[1/4] Compiling ARM boot assembly..."
arm-none-eabi-as -mcpu=cortex-a15 -o arch/arm/boot.o arch/arm/boot.s
echo "  [OK] boot.o"

echo "[2/4] Compiling ARM architecture sources..."
ARM_CFLAGS="-mcpu=cortex-a15 -marm -std=c11 -Wall -Wextra -ffreestanding -nostdlib -O2 \
    -include include/freestanding.h \
    -include arch/arm/arch_compat.h \
    -Dmemset=fs_memset -Dmemcpy=fs_memcpy -Dstrlen=fs_strlen \
    -Dstrcmp=fs_strcmp -Dstrcpy=fs_strcpy \
    -Dmalloc=fs_malloc -Dcalloc=fs_calloc \
    -D__ARM_ARCH__ -DTEST_HOST=0 \
    -Iinclude -Iarch/arm -Iboot \
    -Isrc/oseq -Isrc/rmag -Isrc/lpres -Isrc/iphase \
    -Isrc/choice -Isrc/phase_coord -Isrc/telemetry \
    -Isrc/axiom_matrix -Isrc/crit168 \
    -Isrc/mm -Isrc/sched -Isrc/syscall -Isrc/vfs \
    -Isrc/net -Isrc/vino -Isrc/vena -Isrc/apps -Isrc/holographic \
    -Isrc/idt -Isrc/timer -Isrc/keyboard -Isrc/mouse \
    -Isrc/pic -Isrc/gdt -Isrc/pci -Isrc/acpi -Isrc/vbe -Isrc/ata -Isrc/fat32 \
    -Isrc/crypto_wallet -Isrc/nlb -Isrc/pterm -Isrc/xedit \
    -Isrc/lattice -Isrc/pungent -Isrc/ascent \
    -Isrc/plnp -Isrc/smap -Isrc/decent -Isrc/recon \
    -Isrc/hdcm -Isrc/gematria \
    -Isrc/dualtrack -Isrc/superpos \
    -Isrc/audiogenomics_pro \
    -Isrc/synthesis -Isrc/license -Isrc/surplus -Isrc/edp_risk \
    -Isrc/predictive -Isrc/situation -Isrc/finance -Isrc/identity \
    -Isrc/quantum -Isrc/hardware -I../compat"

ARM_SRCS="arch/arm/kernel_main_arm.c arch/arm/uart.c arch/arm/gic.c arch/arm/timer_arm.c \
    src/oseq/oseq_core.c src/rmag/rmag_core.c src/lpres/lpres_core.c \
    src/iphase/iphase_core.c src/choice/choice_core.c \
    src/phase_coord/phase_coordinator.c src/telemetry/telemetry_core.c \
    src/axiom_matrix/axiom_matrix_core.c src/crit168/crit_168_word.c \
    src/mm/mm.c src/sched/sched.c src/syscall/syscall.c \
    src/vfs/vfs.c src/net/net.c src/net/m5route.c \
    src/net/dtmf.c src/net/radio.c \
    src/vino/vino.c src/vena/vena.c src/holographic/holo.c \
    src/license/license.c src/surplus/surplus.c src/edp_risk/edp_risk.c \
    src/predictive/predictive_model.c src/situation/situation_model.c \
    src/finance/triple_ledger.c src/finance/financial.c \
    src/finance/rails.c src/finance/crypto_bridge.c \
    src/identity/identity.c src/quantum/quantum_device.c \
    src/hardware/rtl_device.c src/net/jdr_piratenet.c \
    src/synthesis/synthesis_engine.c \
    src/crypto_wallet/crypto_wallet.c src/nlb/nlb.c \
    src/pterm/pterm.c src/xedit/xedit.c \
    src/lattice/lattice.c src/pungent/pungent.c src/ascent/ascent.c \
    src/plnp/plnp.c src/smap/smap.c src/decent/decent.c \
    src/recon/recon.c src/hdcm/hdcm.c src/gematria/gematria.c \
    src/dualtrack/dualtrack.c src/superpos/superpos.c \
    src/audiogenomics_pro/audiogenomics_pro.c \
    src/audiogenomics_pro/digital_dna.c \
    src/audiogenomics_pro/architectural_directives.c"

ALL_OBJS="arch/arm/boot.o"
FAILURES=0
for src in $ARM_SRCS; do
    obj="${src%.c}.o"
    if arm-none-eabi-gcc $ARM_CFLAGS -c "$src" -o "$obj" 2>/dev/null; then
        echo "  [OK] $src"
    else
        echo "  [!] $src — retrying with -Wno-error..."
        if arm-none-eabi-gcc $ARM_CFLAGS -Wno-error -c "$src" -o "$obj" 2>&1; then
            echo "  [OK] $src (with warnings)"
        else
            echo "  [FAIL] $src"
            FAILURES=$((FAILURES + 1))
        fi
    fi
    ALL_OBJS="$ALL_OBJS $obj"
done

if [ $FAILURES -gt 0 ]; then
    echo "  [!] $FAILURES file(s) failed to compile"
    exit 1
fi

echo "[3/4] Linking ARM kernel binary..."
arm-none-eabi-ld -T arch/arm/linker.ld -o vovina_shakina_arm.bin $ALL_OBJS 2>&1
echo "  [OK] vovina_shakina_arm.bin ($(wc -c < vovina_shakina_arm.bin) bytes)"

echo "[4/4] Creating raw kernel image for QEMU..."
arm-none-eabi-objcopy -O binary vovina_shakina_arm.bin vovina_shakina_arm.img
echo "  [OK] vovina_shakina_arm.img ($(wc -c < vovina_shakina_arm.img) bytes)"

echo ""
echo "============================================"
echo "  ARM BUILD COMPLETE"
echo "  Kernel: kernel/vovina_shakina_arm.img"
echo ""
echo "  To boot in QEMU:"
echo "    qemu-system-arm -M virt -cpu cortex-a15 -m 256M"
echo "    -kernel vovina_shakina_arm.img -nographic"
echo "============================================"
