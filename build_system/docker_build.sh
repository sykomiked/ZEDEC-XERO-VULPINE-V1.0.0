#!/bin/bash
# docker_build.sh — Build ZEDEC pqOS ISO via Docker
# Usage: ./docker_build.sh [--boot]
set -e

IMAGE_NAME="zedec-pqos-builder"
CONTAINER_NAME="zedec-pqos-build"

echo "============================================"
echo "  ZEDEC pqOS — Docker ISO Build"
echo "============================================"
echo ""

# Build Docker image
echo "[1/3] Building Docker image..."
docker build -t $IMAGE_NAME . 2>&1

# Run build inside container, output ISO to host
echo ""
echo "[2/3] Building ISO in container..."
docker run --rm \
    -v "$(pwd)/kernel:/zedec-build/kernel" \
    -v "$(pwd)/init:/zedec-build/init" \
    -v "$(pwd)/gui:/zedec-build/gui" \
    -v "$(pwd)/tests:/zedec-build/tests" \
    --name $CONTAINER_NAME \
    $IMAGE_NAME \
    bash /zedec-build/build_iso.sh "$@" 2>&1

echo ""
echo "[3/3] ISO built successfully!"
echo "  Output: kernel/vovina_shakina.iso"
echo ""

if [ "$1" = "--boot" ]; then
    echo "To boot locally with QEMU:"
    echo "  qemu-system-i386 -cdrom kernel/vovina_shakina.iso -m 256M -boot d"
fi
