#!/usr/bin/env bash
# Copyright (c) 2024-2026 Michael Laurence Curzi and 36N9 Genetics, LLC
# SPDX-License-Identifier: Apache-2.0
#
# Build every app block written in C++ or Rust to the .wasm beside it
# (kernel/src/apps/blocks/*/*.{cpp,rs} -> *.wasm). The .wasm files are
# committed so the apps and their tests run with no toolchain; --check
# rebuilds into a scratch folder and fails if any committed module differs
# from its source, so a .wasm can never drift from the code it claims to be.
#
# Needs clang with the wasm32 target and wasm-ld (C++), and rustc with
# wasm32-unknown-unknown (Rust).
set -euo pipefail
cd "$(dirname "$0")/../.."
check=0
[[ "${1:-}" == "--check" ]] && check=1
out=$(mktemp -d)
trap 'rm -rf "$out"' EXIT
bad=0
shopt -s nullglob
for src in kernel/src/apps/blocks/*/*.cpp kernel/src/apps/blocks/*/*.rs; do
    wasm="${src%.*}.wasm"
    tmp="$out/$(echo "$src" | tr / _).wasm"
    case "$src" in
    *.cpp)
        clang++ --target=wasm32 -std=c++17 -O2 -nostdlib -fno-exceptions -fno-rtti \
            -Wl,--no-entry -Wl,--export=run -Wl,--strip-all -Wl,--initial-memory=65536 -Wl,--max-memory=65536 -Wl,-z,stack-size=8192 -o "$tmp" "$src"
        ;;
    *.rs)
        rustc --target wasm32-unknown-unknown --crate-type cdylib -C opt-level=2 \
            -C panic=abort -C strip=symbols -C link-arg=--initial-memory=65536 -C link-arg=--max-memory=65536 -C link-arg=-zstack-size=8192 -o "$tmp" "$src"
        ;;
    esac
    if ((check)); then
        if ! cmp -s "$tmp" "$wasm"; then
            echo "DIFFERS: $wasm is not what $src builds to" >&2
            bad=1
        fi
    else
        cp "$tmp" "$wasm"
        echo "built $wasm ($(wc -c <"$wasm") bytes)"
    fi
done
exit $bad
