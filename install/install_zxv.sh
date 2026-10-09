#!/usr/bin/env sh
# install_zxv.sh — POSIX wrapper for install_zxv.py
#
# Falls back to a minimal detection-only mode when Python is unavailable.
#
# Author: H.M. Michael-Laurence: Curzi (c)
# License: Apache-2.0

set -e

SCRIPT_DIR="$(CDPATH='' cd -- "$(dirname -- "$0")" && pwd)"
BUNDLE_DIR="${1:-.}"
TARGET_DIR="${2:-./zxv_staged}"

if command -v python3 >/dev/null 2>&1; then
    exec python3 "${SCRIPT_DIR}/install_zxv.py" \
        --bundle-dir "$BUNDLE_DIR" \
        --target-dir "$TARGET_DIR"
fi

# Fallback: report basic detection and a manual selection reminder.
ARCH="unknown"
FW="unknown"

if [ -d /sys/firmware/efi ]; then
    FW="uefi"
elif [ -d /sys/firmware/devicetree ] || [ -d /proc/device-tree ]; then
    FW="devicetree"
else
    FW="bios"
fi

ARCH="$(uname -m 2>/dev/null || echo unknown)"
OS="$(uname -s 2>/dev/null || echo unknown)"

cat <<EOF
ZXV platform detection (shell fallback)
=======================================
  architecture : $ARCH
  firmware     : $FW
  os           : $OS

Python 3 is required for the full installer (payload selection, digest
verification, and staging). Please install Python 3 and re-run:

  python3 install/install_zxv.py --bundle-dir "$BUNDLE_DIR" --target-dir "$TARGET_DIR"
EOF
