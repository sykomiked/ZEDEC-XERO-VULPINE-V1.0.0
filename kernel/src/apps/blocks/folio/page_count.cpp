// Copyright (c) 2024-2026 Michael Laurence Curzi and 36N9 Genetics, LLC
// SPDX-License-Identifier: Apache-2.0
//
// Pages for a count of lines (folio_rules.h FR2), in C++ compiled to
// WebAssembly (build: tools/apps/build_wasm_blocks.sh): ceil(lines /
// perpage), 0 for no lines. The domain is 0 .. 999999999 lines and
// 1 .. 9999 lines a page; anything else traps, which the runtime turns
// into a refusal (zxapp_wasm.c W3). page_count.kron answers a negative
// count of lines (rounded up toward 0): there the two disagree, held.
// Arithmetic and branches only: Z-Wasm's validator refuses wasm's untyped
// select.
extern "C" long long run(long long lines, long long perpage)
{
    if (lines < 0) __builtin_trap();
    if (lines > 999999999) __builtin_trap();
    if (perpage < 1) __builtin_trap();
    if (perpage > 9999) __builtin_trap();
    return (lines + perpage - 1) / perpage;
}
