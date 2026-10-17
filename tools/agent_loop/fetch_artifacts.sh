#!/bin/sh
# fetch_artifacts.sh — pull the completed artifacts from the build host to
# this machine, incrementally (each completed one as it appears), and —
# with --all — the complete primitive set: every source, criteria, test,
# icon and manifest that was used to create them, so the build is
# reproducible here without the H100.
#
# Copyright (c) 2024-2026 Michael Laurence Curzi and 36N9 Genetics, LLC
# SPDX-License-Identifier: Apache-2.0
#
# usage: sh tools/agent_loop/fetch_artifacts.sh [--artifacts] [--primitives] [--all]
set -eu

HOST="${ZXV_BUILD_HOST:-ionet@78.232.6.191}"
KEY="${ZXV_BUILD_KEY:-$HOME/.ssh/keys/key1}"
REMOTE="${ZXV_REMOTE_ROOT:-/opt/zxv}"
LOCAL="${ZXV_LOCAL_ROOT:-$HOME/zxv-ipfs-build/fetched}"
SSH="ssh -i $KEY -o BatchMode=yes -o StrictHostKeyChecking=no"
mkdir -p "$LOCAL"

log() { printf '[fetch] %s\n' "$1"; }

# ---- the artifacts: the built things, incrementally --------------------
fetch_artifacts() {
    log "the artifacts -> $LOCAL/artifacts"
    mkdir -p "$LOCAL/artifacts"
    # the kernels, the wasm, the volumes, the desktop packages: whatever
    # exists, brought down (rsync skips what is already here and unchanged)
    for sub in build/platforms build/volumes dist/desks dist/discs; do
        if $SSH "$HOST" "test -d $REMOTE/$sub" 2>/dev/null; then
            log "  $sub"
            rsync -a --partial -e "$SSH" "$HOST:$REMOTE/$sub/" "$LOCAL/artifacts/$(basename "$sub")/" 2>/dev/null || true
        fi
    done
    # the single-file artifacts at the root
    for f in kernel_x86_64.elf kernel_arm64.elf kernel_x86_64.bin; do
        if $SSH "$HOST" "test -f $REMOTE/$f" 2>/dev/null; then
            rsync -a --partial -e "$SSH" "$HOST:$REMOTE/$f" "$LOCAL/artifacts/" 2>/dev/null || true
        fi
    done
    log "  done: $(find "$LOCAL/artifacts" -type f 2>/dev/null | wc -l) files"
}

# ---- the primitives: everything used to create them --------------------
fetch_primitives() {
    log "the primitives -> $LOCAL/primitives"
    mkdir -p "$LOCAL/primitives"
    # every source, criteria, test, icon, manifest: the build inputs, so
    # the artifacts are reproducible on this machine
    for sub in kernel sdk tools docs web zxv-brand build_system tests fuzz api assets packages; do
        if $SSH "$HOST" "test -d $REMOTE/$sub" 2>/dev/null; then
            log "  $sub"
            rsync -a --partial --exclude='*.o' --exclude='*.elf' --exclude='*.a' \
                -e "$SSH" "$HOST:$REMOTE/$sub/" "$LOCAL/primitives/$sub/" 2>/dev/null || true
        fi
    done
    # the root documents
    rsync -a --partial --include='*.md' --include='*.txt' --include='LICENSE*' \
        --exclude='*' -e "$SSH" "$HOST:$REMOTE/" "$LOCAL/primitives/" 2>/dev/null || true
    log "  done: $(find "$LOCAL/primitives" -type f 2>/dev/null | wc -l) files"
    # the manifest: what was fetched, so the set is verifiable
    find "$LOCAL/primitives" -type f -exec shasum -a 256 {} \; \
        > "$LOCAL/primitives/MANIFEST.sha256" 2>/dev/null || true
    log "  the manifest: primitives/MANIFEST.sha256"
}

case "${1:---all}" in
    --artifacts)  fetch_artifacts ;;
    --primitives) fetch_primitives ;;
    --all|*)      fetch_artifacts; fetch_primitives ;;
esac
log "the fetched set: $LOCAL"
ls -la "$LOCAL" 2>/dev/null | awk 'NR>1 {printf "  %s\n", $9}'
