#!/bin/sh
# Copyright (c) 2024-2026 Michael Laurence Curzi and 36N9 Genetics, LLC
# SPDX-License-Identifier: Apache-2.0
#
# install.sh — install an unpacked zxv-edge tarball. Run it from the
# unpacked tree as root:
#   tar -xzf zxv-edge-rpi5-*.tar.gz -C /tmp/zxv-edge && sudo /tmp/zxv-edge/opt/zxv-edge/install.sh
#
# Rules:
#   1. Every file is checked against MANIFEST.sha256 before anything is copied.
#   2. Files go under ${DESTDIR:-/}: /opt/zxv-edge, /etc/zxv-edge,
#      /lib/systemd/system/zxv-edge.service. Nothing else is touched.
#   3. An existing /etc/zxv-edge/edge.conf is kept; the new one is written
#      beside it as edge.conf.new for you to compare.
#   4. The service is NOT enabled or started: you do that after putting the
#      models in place (see EDGE_NODES.md).
set -eu
SRC="$(cd "$(dirname "$0")/../.." && pwd)"
DEST="${DESTDIR:-}"
cd "$SRC"
sha256sum -c --quiet opt/zxv-edge/MANIFEST.sha256 || { echo "install.sh: manifest check failed" >&2; exit 1; }

mkdir -p "$DEST/opt/zxv-edge/bin" "$DEST/opt/zxv-edge/models" "$DEST/etc/zxv-edge" \
    "$DEST/lib/systemd/system"
for f in $(cd opt/zxv-edge && find . -type f | sed 's|^\./||'); do
    mkdir -p "$DEST/opt/zxv-edge/$(dirname "$f")"
    cp -p "opt/zxv-edge/$f" "$DEST/opt/zxv-edge/$f"
done
cp -p lib/systemd/system/zxv-edge.service "$DEST/lib/systemd/system/zxv-edge.service"
if [ -f "$DEST/etc/zxv-edge/edge.conf" ]; then
    cp -p etc/zxv-edge/edge.conf "$DEST/etc/zxv-edge/edge.conf.new"
    echo "kept your /etc/zxv-edge/edge.conf; the packaged one is edge.conf.new"
else
    cp -p etc/zxv-edge/edge.conf "$DEST/etc/zxv-edge/edge.conf"
fi
"$DEST/opt/zxv-edge/bin/zxv-edge-run" --check "$DEST/etc/zxv-edge/edge.conf"
if [ -z "$DEST" ] && command -v systemctl >/dev/null 2>&1; then systemctl daemon-reload; fi
echo "installed. Next: copy the GGUF models into /opt/zxv-edge/models, then"
echo "  systemctl enable --now zxv-edge && journalctl -u zxv-edge -f"
