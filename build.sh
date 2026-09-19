#!/bin/bash
# ZXV Build Wrapper Script
# Usage: ./build.sh [arch] [clean|run]
#   arch: arm64 (default), x86_64, riscv, riscv32, arm32
#   clean: clean before build
#   run: run in QEMU after build

set -e

ARCH="${1:-arm64}"
ACTION="${2:-build}"

# Map architecture names to Makefiles
case "$ARCH" in
    arm64|aarch64)
        MAKEFILE="build_system/Makefile.arm64"
        ;;
    x86_64|x64|amd64)
        MAKEFILE="build_system/Makefile.x86_64"
        ;;
    riscv|riscv64|rv64)
        MAKEFILE="build_system/Makefile.riscv"
        ;;
    riscv32|rv32)
        MAKEFILE="build_system/Makefile.riscv32"
        ;;
    arm32|arm)
        MAKEFILE="build_system/Makefile.arm32"
        ;;
    *)
        echo "Error: Unknown architecture '$ARCH'"
        echo "Supported: arm64, x86_64, riscv, riscv32, arm32"
        exit 1
        ;;
esac

if [ ! -f "$MAKEFILE" ]; then
    echo "Error: Makefile not found: $MAKEFILE"
    exit 1
fi

echo "Building ZXV for $ARCH using $MAKEFILE"

if [ "$ACTION" = "clean" ]; then
    make -f "$MAKEFILE" clean
    echo "Clean complete"
elif [ "$ACTION" = "run" ]; then
    make -f "$MAKEFILE" clean
    make -f "$MAKEFILE" all
    echo "Running in QEMU..."
    make -f "$MAKEFILE" run
else
    make -f "$MAKEFILE" clean
    make -f "$MAKEFILE" all
    echo "Build complete for $ARCH"
    echo "Run with: ./build.sh $ARCH run"
fi
