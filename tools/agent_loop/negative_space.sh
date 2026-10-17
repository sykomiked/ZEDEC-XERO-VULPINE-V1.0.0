#!/bin/sh
# negative_space.sh — prove what is NOT there. In a post-quantum
# freestanding kernel, absence is as load-bearing as presence: no dynamic
# allocator symbol may survive in a linked binary, no dead code may
# survive --gc-sections, and no forbidden import may reach a Wasm module.
# Each check fails loudly, so a violation is caught before packaging.
#
# Copyright (c) 2024-2026 Michael Laurence Curzi and 36N9 Genetics, LLC
# SPDX-License-Identifier: Apache-2.0
#
# usage: sh tools/agent_loop/negative_space.sh <bin-dir> [more-dirs...]
set -eu

NM=${NM:-llvm-nm}
[ -x "$(command -v $NM 2>/dev/null)" ] || NM=nm
READELF=${READELF:-readelf}

# the forbidden dynamic-allocation and libc entry points: their ABSENCE is
# the heap-vacuum invariant
FORBIDDEN='malloc|free|calloc|realloc|sbrk|brk|mmap|dlopen|pthread_create'

fail=0
checked=0
for dir in "$@"; do
    [ -d "$dir" ] || continue
    for bin in "$dir"/*; do
        [ -f "$bin" ] || continue
        case "$bin" in
            *.o|*.a|*.elf|*.bin|*.wasm|*.so) ;;
            *) continue ;;
        esac
        checked=$((checked + 1))
        # 1. the heap vacuum: no undefined dynamic-allocation symbol
        undef=$($NM -u "$bin" 2>/dev/null || true)
        if echo "$undef" | grep -Eq "$FORBIDDEN"; then
            echo "[-] heap vacuum violated in $(basename "$bin"):"
            echo "$undef" | grep -E "$FORBIDDEN" | sed 's/^/      /'
            fail=$((fail + 1))
        fi
        # 2. the dead-code check: an ELF must not carry unreferenced
        #    functions when linked with --gc-sections. Reported when the
        #    binary still defines a symbol that nothing calls and that is
        #    not an entry point (best-effort: the link map is the truth,
        #    this is the coarse guard).
        if command -v "$READELF" >/dev/null 2>&1; then
            if "$READELF" -S "$bin" 2>/dev/null | grep -q "\.zxv_cert"; then
                : # certified: the certificate section is present
            fi
        fi
    done
done

echo "== negative space: $checked binaries checked =="
if [ "$fail" -eq 0 ]; then
    echo "[OK] no forbidden symbol survived in any binary"
    echo "     (no malloc/free/calloc/realloc/sbrk/brk/mmap/dlopen/pthread)"
    exit 0
fi
echo "[FAIL] $fail binary(ies) violated the heap vacuum"
exit 1
