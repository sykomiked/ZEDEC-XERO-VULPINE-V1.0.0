// Copyright (c) 2024-2026 Michael Laurence Curzi and 36N9 Genetics, LLC
// SPDX-License-Identifier: Apache-2.0
//
// What a sale is worth (vintner_model.h VN1), in C++ compiled to
// WebAssembly (build: tools/apps/build_wasm_blocks.sh): bottles * price,
// the price per bottle in integer minor units. A negative count or price,
// or a product past 64 bits, is no sale: the module traps, which the
// runtime turns into a refusal (zxapp_wasm.c W3), never a wrapped answer.
// sale.sutra holds nine digits each; past them the two disagree (held).
// Arithmetic only: Z-Wasm's validator refuses wasm's untyped select, which
// a conditional would compile to.
extern "C" long long run(long long bottles, long long price)
{
    if (bottles < 0) __builtin_trap();
    if (price < 0) __builtin_trap();
    if (price > 0) {
        if (bottles > 9223372036854775807LL / price) __builtin_trap();
    }
    return bottles * price;
}
