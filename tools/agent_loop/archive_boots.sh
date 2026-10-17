#!/bin/sh
# archive_boots.sh — capture and archive the boot logs from every platform
# we can reach. The boot log is the ground truth: it says exactly how far
# the kernel got, which driver answered, which feature banner printed, and
# where it stopped. Archiving them (with their hashes and timestamps)
# gives the stabilization work a record to reason from — and lets the
# image be reconstructed from the lattice's evidence rather than guessed.
#
# Copyright (c) 2024-2026 Michael Laurence Curzi and 36N9 Genetics, LLC
# SPDX-License-Identifier: Apache-2.0
#
# usage: sh tools/agent_loop/archive_boots.sh [--capture] [--collect]
set -eu

HOST="${ZXV_BUILD_HOST:-ionet@78.232.6.191}"
KEY="${ZXV_BUILD_KEY:-$HOME/.ssh/keys/key1}"
REMOTE="${ZXV_REMOTE_ROOT:-/opt/zxv}"
LOCAL="${ZXV_LOCAL_ROOT:-$HOME/zxv-ipfs-build/fetched/boots}"
SSH="ssh -i $KEY -o BatchMode=yes -o StrictHostKeyChecking=no"
mkdir -p "$LOCAL"
STAMP=$(date -u +%Y%m%dT%H%M%SZ)

log() { printf '[boots] %s\n' "$1"; }

# ---- capture: boot each platform and record the serial output ----------
capture() {
    log "capturing the boot logs on $HOST"
    # the QEMU boot checks the repo already provides, for x86_64 and arm64;
    # each writes the serial console to a log we then archive
    $SSH "$HOST" "cd $REMOTE && \
        if [ -x build_system/ci_boot_check.sh ] && [ -f kernel_x86_64.elf ]; then \
            sh build_system/ci_boot_check.sh x86_64 build_system/Makefile.x86_64 30 '[AI_OK]' \
                > /tmp/boot-x86_64.log 2>&1 || true; \
        fi; \
        if [ -x build_system/ci_boot_check.sh ] && [ -f kernel_arm64.elf ]; then \
            sh build_system/ci_boot_check.sh arm64 build_system/Makefile.arm64 30 '[AI_OK]' \
                > /tmp/boot-arm64.log 2>&1 || true; \
        fi; \
        for a in x86_64 arm64; do \
            [ -f /tmp/boot-\$a.log ] && cp /tmp/boot-\$a.log /tmp/boot-\$a-$STAMP.log; \
        done; \
        ls /tmp/boot-*.log 2>/dev/null | wc -l" \
        | xargs -I{} log "  the logs captured: {}"
}

# ---- collect: bring every boot log (and any prior archive) here -------
collect() {
    log "collecting the boot logs -> $LOCAL"
    # the freshly captured logs
    rsync -a --partial -e "$SSH" \
        --include='boot-*.log' --exclude='*' \
        "$HOST:/tmp/" "$LOCAL/" 2>/dev/null || true
    # any boot logs the repo or the fleet keeps
    for d in /tmp "$REMOTE/build/platforms" "$REMOTE/build/boots" "$REMOTE/var/log"; do
        $SSH "$HOST" "test -d $d" 2>/dev/null || continue
        rsync -a --partial -e "$SSH" \
            --include='*boot*.log' --include='*serial*.log' --exclude='*' \
            "$HOST:$d/" "$LOCAL/$STAMP/" 2>/dev/null || true
    done
    # the manifest: every log, its size and hash, so the record is
    # verifiable and a later boot can be compared to it
    ( cd "$LOCAL" && find . -name '*.log' -type f 2>/dev/null | while read -r f; do
        printf '%s  %s  %s\n' "$(wc -c < "$f" | tr -d ' ')" "$(shasum -a 256 "$f" 2>/dev/null | cut -c1-16)" "$f"
    done > "MANIFEST-$STAMP.txt" ) || true
    log "  the manifest: $LOCAL/MANIFEST-$STAMP.txt"
    log "  the logs collected: $(find "$LOCAL" -name '*.log' -type f 2>/dev/null | wc -l)"
}

case "${1:---all}" in
    --capture) capture ;;
    --collect) collect ;;
    --all|*)   capture; collect ;;
esac
log "the archive: $LOCAL"
