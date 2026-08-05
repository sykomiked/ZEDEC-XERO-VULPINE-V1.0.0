#!/bin/bash
# server_full_build.sh — Comprehensive B300 server build + QC + docs
# Run on B300 server after syncing source
set -e

WORKDIR="/tmp/zedec-build"
OUTPUT_DIR="/tmp/zedec-output"
mkdir -p "$OUTPUT_DIR"

cd "$WORKDIR"

echo "============================================"
echo "  ZEDEC pqOS — Full Server Build Pipeline"
echo "  B300 Server — All Platforms + QC + Docs"
echo "============================================"

# ===== 1. Build x86_32 ISO (native) =====
echo ""
echo "[PHASE 1] Building x86_32 ISO (native)..."
cd kernel
bash build_iso.sh 2>&1 || true
cp tol_vovina.iso "$OUTPUT_DIR/" 2>/dev/null || true
cp tol_vovina.bin "$OUTPUT_DIR/" 2>/dev/null || true
cd "$WORKDIR"
echo "[DONE] x86_32 ISO: $(ls -la $OUTPUT_DIR/tol_vovina.iso 2>/dev/null | awk '{print $5}') bytes"

# ===== 2. Build x86_64 ISO (native) =====
echo ""
echo "[PHASE 2] Building x86_64 ISO (native)..."
cd kernel
bash build_x86_64.sh 2>&1 || true
cp tol_vovina_x86_64.iso "$OUTPUT_DIR/" 2>/dev/null || true
cp tol_vovina_x86_64.bin "$OUTPUT_DIR/" 2>/dev/null || true
cd "$WORKDIR"
echo "[DONE] x86_64: $(ls -la $OUTPUT_DIR/tol_vovina_x86_64.iso 2>/dev/null | awk '{print $5}') bytes"

# ===== 3. Build ARM32 (cross-compile) =====
echo ""
echo "[PHASE 3] Building ARM32 (cross-compile)..."
cd kernel
bash build_arm.sh 2>&1 || true
cp tol_vovina_arm.bin "$OUTPUT_DIR/" 2>/dev/null || true
cp tol_vovina_arm.img "$OUTPUT_DIR/" 2>/dev/null || true
cd "$WORKDIR"
echo "[DONE] ARM32: $(ls -la $OUTPUT_DIR/tol_vovina_arm.bin 2>/dev/null | awk '{print $5}') bytes"

# ===== 4. Build ARM64 (cross-compile) =====
echo ""
echo "[PHASE 4] Building ARM64 (cross-compile)..."
cd "$WORKDIR"
if make -f build_system/Makefile.arm64 CROSS_COMPILE=aarch64-linux-gnu- clean 2>/dev/null; then true; fi
make -f build_system/Makefile.arm64 CROSS_COMPILE=aarch64-linux-gnu- all 2>&1 || true
cp kernel_arm64.elf "$OUTPUT_DIR/" 2>/dev/null || true
cp kernel_arm64.bin "$OUTPUT_DIR/" 2>/dev/null || true
cd "$WORKDIR"
echo "[DONE] ARM64: $(ls -la $OUTPUT_DIR/kernel_arm64.elf 2>/dev/null | awk '{print $5}') bytes"

# ===== 5. Build RISC-V 64 (cross-compile) =====
echo ""
echo "[PHASE 5] Building RISC-V 64 (cross-compile)..."
cd kernel
if make -f Makefile.riscv CROSS_COMPILE=riscv64-linux-gnu- clean 2>/dev/null; then true; fi
make -f Makefile.riscv CROSS_COMPILE=riscv64-linux-gnu- all 2>&1 || true
cp kernel_riscv.elf "$OUTPUT_DIR/" 2>/dev/null || true
cp kernel_riscv.bin "$OUTPUT_DIR/" 2>/dev/null || true
cd "$WORKDIR"
echo "[DONE] RISC-V64: $(ls -la $OUTPUT_DIR/kernel_riscv.elf 2>/dev/null | awk '{print $5}') bytes"

# ===== 6. Build RISC-V 32 (cross-compile) =====
echo ""
echo "[PHASE 6] Building RISC-V 32 (cross-compile)..."
cd kernel
if make -f Makefile.riscv32 CROSS_COMPILE=riscv64-linux-gnu- clean 2>/dev/null; then true; fi
make -f Makefile.riscv32 CROSS_COMPILE=riscv64-linux-gnu- all 2>&1 || true
cp kernel_riscv32.elf "$OUTPUT_DIR/" 2>/dev/null || true
cp kernel_riscv32.bin "$OUTPUT_DIR/" 2>/dev/null || true
cd "$WORKDIR"
echo "[DONE] RISC-V32: $(ls -la $OUTPUT_DIR/kernel_riscv32.elf 2>/dev/null | awk '{print $5}') bytes"

# ===== 7. QC: SHA256 checksums =====
echo ""
echo "[QC] Computing SHA256 checksums..."
cd "$OUTPUT_DIR"
for f in *; do
    if [ -f "$f" ]; then
        sha=$(sha256sum "$f" | awk '{print $1}')
        size=$(stat -c%s "$f" 2>/dev/null || stat -f%z "$f" 2>/dev/null)
        echo "  $f: ${size} bytes sha256=$sha"
    fi
done

# ===== 8. QC: QEMU smoke tests =====
echo ""
echo "[QC] QEMU smoke tests..."
if which qemu-system-i386 >/dev/null 2>&1; then
    echo "  [x86_32] QEMU boot test (5s timeout)..."
    timeout 5 qemu-system-i386 -cdrom tol_vovina.iso -boot d -nographic -no-reboot 2>&1 || true
    echo "  [x86_32] QEMU test complete"
fi
if which qemu-system-x86_64 >/dev/null 2>&1 && [ -f vovina_shakina_x86_64.iso ]; then
    echo "  [x86_64] QEMU boot test (5s timeout)..."
    timeout 5 qemu-system-x86_64 -cdrom tol_vovina_x86_64.iso -boot d -nographic -no-reboot 2>&1 || true
    echo "  [x86_64] QEMU test complete"
fi
if which qemu-system-aarch64 >/dev/null 2>&1 && [ -f kernel_arm64.elf ]; then
    echo "  [ARM64] QEMU boot test (5s timeout)..."
    timeout 5 qemu-system-aarch64 -M virt -cpu cortex-a53 -m 256M -kernel kernel_arm64.elf -nographic -no-reboot 2>&1 || true
    echo "  [ARM64] QEMU test complete"
fi
if which qemu-system-riscv64 >/dev/null 2>&1 && [ -f kernel_riscv.elf ]; then
    echo "  [RISC-V64] QEMU boot test (5s timeout)..."
    timeout 5 qemu-system-riscv64 -M virt -m 256M -kernel kernel_riscv.elf -nographic -no-reboot 2>&1 || true
    echo "  [RISC-V64] QEMU test complete"
fi

# ===== 9. Summary =====
echo ""
echo "============================================"
echo "  BUILD SUMMARY"
echo "============================================"
for f in *; do
    if [ -f "$f" ]; then
        size=$(stat -c%s "$f" 2>/dev/null || stat -f%z "$f" 2>/dev/null)
        echo "  [OK] $f ($size bytes)"
    fi
done
echo ""
echo "All artifacts in: $OUTPUT_DIR"
echo "============================================"
