#!/bin/sh
# Copyright (c) 2024-2026 Michael Laurence Curzi and 36N9 Genetics, LLC
# SPDX-License-Identifier: Apache-2.0
# probe.sh MAIN "SRC..." [CFLAGS...]: build one differential probe (classify.py)
# with ASan + UBSan, no recovery, and run it; its trace goes to stdout.
# Paths are relative to kernel/. Runs from the tree root.
set -e
main=$1; srcs=$2; shift 2
cd kernel
export CPATH=$PWD/include:$PWD/src/modbind:$PWD/src/e8:$PWD/src/event_space:$PWD/src/surplus
T=${ZXV_TMP:-/tmp}/probe_$(basename "$main" .c)_$$
gcc -std=c11 -O1 -g -fno-omit-frame-pointer -fsanitize=address,undefined \
    -fno-sanitize-recover=all -w "$@" -o "$T" "$main" $srcs -lm
ASAN_OPTIONS=detect_leaks=0 "$T"
rc=$?
rm -f "$T"
exit $rc
