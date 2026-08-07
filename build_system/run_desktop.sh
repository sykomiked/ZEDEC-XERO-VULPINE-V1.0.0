#!/usr/bin/env bash
# Copyright (c) 2024-2026 Michael Laurence Curzi and 36N9 Genetics, LLC
# SPDX-License-Identifier: LicenseRef-OPL-1.1 AND CC-BY-SA-4.0 AND LicenseRef-Royal-Writ-Sicilian-Crown-1.0 AND LicenseRef-SEL-3.3
#
# run_desktop.sh — boot the ZEDEC pqOS GRAPHICAL DESKTOP in a window.
# Mouse + keyboard work. This is the "run my OS and click it" launcher.
#
#   bash build_system/run_desktop.sh            # boot the desktop (build if needed)
#   BUILD=1 bash build_system/run_desktop.sh    # force a rebuild first
#   MEM=1024 bash build_system/run_desktop.sh   # more RAM
#
# The launch flags are load-bearing:
#   -device ramfb                      -> the universal framebuffer we scan out
#   -device virtio-tablet/keyboard     -> the pointer + keyboard
#   -global virtio-mmio.force-legacy=false  -> modern virtio (our drivers are v2)
set -uo pipefail
cd "$(dirname "$0")/.." || exit 1

QEMU=${QEMU:-qemu-system-aarch64}
MEM=${MEM:-512}
KERNEL=kernel_arm64.bin

command -v "$QEMU" >/dev/null 2>&1 || {
  echo "!! $QEMU not found. Install QEMU (macOS: brew install qemu)."; exit 1; }

if [ "${BUILD:-0}" = "1" ] || [ ! -f "$KERNEL" ]; then
  echo "==> building the kernel ($KERNEL)..."
  make -f build_system/Makefile.arm64 all >/tmp/zxv_desktop_build.log 2>&1 || {
    echo "!! build failed — see /tmp/zxv_desktop_build.log"; tail -20 /tmp/zxv_desktop_build.log; exit 1; }
fi

# pick a graphical display backend for this host
case "$(uname -s)" in
  Darwin) DISPLAY_ARG="-display cocoa" ;;
  Linux)  if "$QEMU" -display help 2>/dev/null | grep -q gtk; then DISPLAY_ARG="-display gtk";
          else DISPLAY_ARG="-display sdl"; fi ;;
  *)      DISPLAY_ARG="" ;;
esac

# pick an audio backend so the boot chime (virtio-snd) is audible (NO_AUDIO=1 to mute)
AUDIO_ARG=""
if [ "${NO_AUDIO:-0}" != "1" ]; then
  case "$(uname -s)" in
    Darwin) AUDIO_ARG="-audiodev coreaudio,id=snd0 -device virtio-sound-device,audiodev=snd0" ;;
    Linux)  if "$QEMU" -audiodev help 2>/dev/null | grep -q pipewire; then AB=pipewire;
            elif "$QEMU" -audiodev help 2>/dev/null | grep -q pa; then AB=pa; else AB=sdl; fi
            AUDIO_ARG="-audiodev ${AB},id=snd0 -device virtio-sound-device,audiodev=snd0" ;;
  esac
fi

echo "==> booting the ZEDEC pqOS desktop"
echo "    (a window opens with the desktop; move the mouse, click a dock app, type in the terminal)"
echo "    a boot chime plays through virtio-snd; serial log + monitor are on this terminal."
exec "$QEMU" -M virt,gic-version=3 -cpu cortex-a53 -m "${MEM}" \
  -global virtio-mmio.force-legacy=false \
  -device ramfb \
  -device virtio-tablet-device -device virtio-keyboard-device \
  $AUDIO_ARG \
  $DISPLAY_ARG -serial mon:stdio \
  -kernel "$KERNEL"
