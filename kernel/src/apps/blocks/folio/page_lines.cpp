// Copyright (c) 2024-2026 Michael Laurence Curzi and 36N9 Genetics, LLC
// SPDX-License-Identifier: Apache-2.0
//
// Lines per page (folio_rules.h FR1), in C++ compiled to WebAssembly
// (build: tools/apps/build_wasm_blocks.sh): floor((height - top - bottom)
// / leading), every length in twips. The domain is a height and margins
// of 0 .. 31680 twips (22 inches), a leading of 1 .. 31680 and margins
// that leave the page some room (top + bottom at most height); anything
// else traps, which the runtime turns into a refusal (zxapp_wasm.c W3),
// never an answer. page_lines.kron answers past the domain (a negative
// count for margins past the page): there the two disagree, held.
// Arithmetic and branches only: Z-Wasm's validator refuses wasm's untyped
// select, which a conditional value would compile to.
extern "C" long long run(long long height, long long top, long long bottom, long long leading)
{
    if (height < 0) __builtin_trap();
    if (height > 31680) __builtin_trap();
    if (top < 0) __builtin_trap();
    if (top > 31680) __builtin_trap();
    if (bottom < 0) __builtin_trap();
    if (bottom > 31680) __builtin_trap();
    if (leading < 1) __builtin_trap();
    if (leading > 31680) __builtin_trap();
    if (top + bottom > height) __builtin_trap();
    return (height - top - bottom) / leading;
}
