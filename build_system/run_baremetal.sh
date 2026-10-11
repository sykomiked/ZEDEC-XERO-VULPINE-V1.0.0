#!/usr/bin/env bash
# run_baremetal.sh — boot a ZXV bare-metal UEFI disc in QEMU inside Docker.
#
#   bash build_system/run_baremetal.sh [x86_64|arm64] [--screen]
#
# Works the same on macOS, Linux and Windows (Git Bash/WSL) with only Docker
# installed: the toolchain image, firmware and QEMU all live in the container.
# Builds the image and the disc on first use. The serial console is this
# terminal (Ctrl-A then X quits QEMU). --screen also serves the virtual
# monitor over VNC on 127.0.0.1:5900, so you see what a real PC's screen would.
# QEMU opens the disc with snapshot=on: the image file is never modified, and
# nothing here touches a physical disk.
set -euo pipefail
cd "$(dirname "$0")/.."

ARCH=${1:-x86_64}; SCREEN=${2:-}
IMAGE=zxv-baremetal
DISC=dist/discs/zxv-${ARCH}-uefi.img
case "$ARCH" in
  x86_64) QEMU="qemu-system-x86_64 -machine q35 -m 1G -bios /usr/share/ovmf/OVMF.fd" ;;
  arm64)  QEMU="qemu-system-aarch64 -M virt,gic-version=3 -cpu cortex-a57 -m 1G -bios /usr/share/qemu-efi-aarch64/QEMU_EFI.fd -device ramfb -device qemu-xhci -device usb-kbd" ;;
  *) echo "usage: run_baremetal.sh [x86_64|arm64] [--screen]"; exit 2 ;;
esac

docker image inspect "$IMAGE" >/dev/null 2>&1 || \
  docker build -f build_system/Dockerfile.baremetal -t "$IMAGE" .
[ -f "$DISC" ] || \
  docker run --rm -v "$PWD:/src" -w /src "$IMAGE" bash build_system/baremetal_images.sh "$ARCH"

PORTS=(); DISPLAY_ARGS="-display none"
if [ "$SCREEN" = "--screen" ]; then
  PORTS=(-p 127.0.0.1:5900:5900); DISPLAY_ARGS="-display vnc=0.0.0.0:0"
  echo "Screen: open vnc://127.0.0.1:5900 (macOS Screen Sharing, or any VNC viewer)"
fi
echo "Booting $DISC under UEFI. Ctrl-A then X quits."
exec docker run --rm -it ${PORTS[@]+"${PORTS[@]}"} -v "$PWD:/src:ro" -w /src "$IMAGE" \
  bash -c "$QEMU $DISPLAY_ARGS -net none \
    -drive if=none,id=disc,format=raw,snapshot=on,file=$DISC \
    -device virtio-blk-pci,drive=disc,bootindex=0 -serial mon:stdio"
