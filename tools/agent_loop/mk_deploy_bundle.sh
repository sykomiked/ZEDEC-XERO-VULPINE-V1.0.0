#!/bin/sh
# mk_deploy_bundle.sh — pack the source a worker needs (the kernel, the
# tools, the criteria, the docs, the web) without the build artifacts:
# the 358 MB generated blob and the build/ tree stay behind.
#
# Copyright (c) 2024-2026 Michael Laurence Curzi and 36N9 Genetics, LLC
# SPDX-License-Identifier: Apache-2.0
#
# usage: sh tools/agent_loop/mk_deploy_bundle.sh [out.tgz]
set -eu

ROOT="$(cd "$(dirname "$0")/../.." && pwd)"
OUT="${1:-$ROOT/build/zxv-deploy-bundle.tgz}"
cd "$ROOT"
mkdir -p "$(dirname "$OUT")"

tar czf "$OUT" \
    --exclude='build' \
    --exclude='.git' \
    --exclude='kernel/src/seedbank/seedbank_blob.c' \
    --exclude='*.o' --exclude='*.a' --exclude='*.elf' \
    --exclude='dist' \
    --exclude='tools/agent_loop/results' \
    --exclude='tools/agent_loop/*.state' \
    --exclude='tools/agent_loop/pipeline*.out' \
    kernel tools docs web zxv-brand build_system tests fuzz sdk \
    2>/dev/null || true

ls -la "$OUT" | awk '{printf "bundle: %s (%.1f MB)\n", $9, $5/1048576}'
echo "the worker unpacks this and runs tools/agent_loop/deploy_worker.sh <bundle> [model]"
