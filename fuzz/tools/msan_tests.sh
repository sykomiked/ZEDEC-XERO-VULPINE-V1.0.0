#!/bin/bash
# Copyright (c) 2024-2026 Michael Laurence Curzi and 36N9 Genetics, LLC
# SPDX-License-Identifier: Apache-2.0
#
# msan_tests.sh — the host test subset that runs under MemorySanitizer.
#
# MSan reports a read of memory that no instrumented code wrote. libc is not
# instrumented here. Tests are therefore limited to modules whose libc use
# MSan intercepts (memcpy/memset, stdio, string functions). Tests that read
# sockets, run child processes or call un-intercepted libc are left out. The
# whole of `make -C kernel verify-all` still runs. Only commands that build a
# test named by MSAN_MATCH get -fsanitize=memory (via tools/san-cc). The rest
# build as usual.
#
#   fuzz/tools/msan_tests.sh
#   MSAN_MATCH='src/legacy/test_' fuzz/tools/msan_tests.sh
set -euo pipefail
FUZZ="$(cd "$(dirname "$0")/.." && pwd)"
ROOT="$(dirname "$FUZZ")"
DEFAULT='src/(legacy|freight|ipfs_node|rational|rmag|vino|count_house|finance|cbank|zab|invproof|syscall|bombsquad|codec|megarom|onepolicy|tensor|pay|zxvfs|display)/test_'
B="$(mktemp -d)"
trap 'rm -rf "$B"' EXIT
ln -s "$FUZZ/tools/san-cc" "$B/gcc"
ln -s "$FUZZ/tools/san-cc" "$B/cc"
export PATH="$B:$PATH"
export SANCC_CC="${MSAN_CC:-clang}"
export SANCC_MATCH="${MSAN_MATCH:-$DEFAULT}"
export SANCC_STRIP_SAN=1
export SANCC_FLAGS="-fsanitize=memory -fsanitize-memory-track-origins=2 -fno-omit-frame-pointer -fno-sanitize-recover=all -Wno-error -Wno-unknown-warning-option -Wno-unused-command-line-argument"
echo "MSan subset: $SANCC_MATCH"
make -C "$ROOT/kernel" verify-all
