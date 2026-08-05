#!/bin/bash
# docker_build_server.sh — Build ZEDEC pqOS kernel_server ISO via Docker
# Usage: ./docker_build_server.sh [--boot]
set -e

IMAGE_NAME="zedec-pqos-builder"
CONTAINER_NAME="zedec-pqos-server-build"

echo "============================================"
echo "  ZEDEC pqOS — Docker Server ISO Build"
echo "  kernel_server (full parity with kernel)"
echo "============================================"
echo ""

# Build Docker image
echo "[1/3] Building Docker image..."
docker build -t $IMAGE_NAME . 2>&1

# Run build inside container, output ISO to host
echo ""
echo "[2/3] Building kernel_server ISO in container..."
docker run --rm \
    -v "$(pwd)/kernel_server:/zedec-build/kernel_server" \
    -v "$(pwd)/init:/zedec-build/init" \
    -v "$(pwd)/gui:/zedec-build/gui" \
    -v "$(pwd)/tests:/zedec-build/tests" \
    -e BUILD_DIR=kernel_server \
    --name $CONTAINER_NAME \
    $IMAGE_NAME \
    bash /zedec-build/build_iso.sh "$@" 2>&1

echo ""
echo "[3/3] Server ISO built successfully!"
echo "  Output: kernel_server/vovina_shakina.iso"
echo ""

if [ "$1" = "--boot" ]; then
    echo "To boot locally with QEMU:"
    echo "  qemu-system-i386 -cdrom kernel_server/vovina_shakina.iso -m 256M -boot d"
fi
