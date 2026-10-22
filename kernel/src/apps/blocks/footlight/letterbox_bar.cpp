// Copyright (c) 2024-2026 Michael Laurence Curzi and 36N9 Genetics, LLC
// SPDX-License-Identifier: Apache-2.0
//
// The letterbox bar before a fitted side (footlight_rules.h FL3), in C++
// compiled to WebAssembly (build: tools/apps/build_wasm_blocks.sh):
// floor((span - fit) / 2), the bar left of a fitted width or above a
// fitted height, the odd pixel going after the slide. The domain is a span
// of 0 .. 65535 pixels and a fit of 0 .. span; anything else traps, which
// the runtime turns into a refusal (zxapp_wasm.c W3), never an answer.
// letterbox_bar.kron answers a fit past its span (a negative bar): there
// the two disagree, held.
extern "C" long long run(long long span, long long fit)
{
    if (span < 0) __builtin_trap();
    if (span > 65535) __builtin_trap();
    if (fit < 0) __builtin_trap();
    if (fit > span) __builtin_trap();
    return (span - fit) / 2;
}
