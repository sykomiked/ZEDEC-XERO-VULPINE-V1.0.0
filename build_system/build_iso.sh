#!/bin/bash
# build_iso.sh — Build TOL VOVINA Kernel bootable ISO
# Runs inside Docker or locally if tools are available
# Delegates to the Makefile, which is the authoritative build system.
set -e

echo "============================================"
echo "  TOL VOVINA Kernel — Bootable ISO Builder"
echo "  M5 Axiomatic Kernel"
echo "============================================"
echo ""

BUILD_DIR="${BUILD_DIR:-kernel}"
cd /zedec-build/$BUILD_DIR

echo "[1/3] Building kernel binary + ISO via Makefile..."
make clean 2>/dev/null || true
make iso 2>&1

echo ""
echo "  [OK] tol_vovina.bin ($(wc -c < tol_vovina.bin 2>/dev/null || echo '?') bytes)"
echo "  [OK] tol_vovina.iso ($(wc -c < tol_vovina.iso 2>/dev/null || echo '?') bytes)"

echo ""
echo "============================================"
echo "  BUILD COMPLETE"
echo "  ISO: $BUILD_DIR/tol_vovina.iso"
echo "============================================"
echo ""

# If QEMU is available, offer to test-boot
if [ "$1" = "--boot" ]; then
    if command -v qemu-system-i386 &> /dev/null; then
        echo "[QEMU] Booting TOL VOVINA Kernel..."
        qemu-system-i386 -cdrom tol_vovina.iso -m 256M -boot d -nographic
    else
        echo "QEMU not available for boot test"
    fi
fi
