// Copyright (c) 2024-2026 Michael Laurence Curzi and 36N9 Genetics, LLC
// SPDX-License-Identifier: Apache-2.0
//
// One line of an invoice or report table at a fixed scale (folio_rules.h
// FR3), in C++ compiled to WebAssembly (build:
// tools/apps/build_wasm_blocks.sh): (qty * price + 500) / 1000, a quantity
// in thousandths of a unit at a unit price in minor units, rounded half up
// to a whole minor unit. A negative quantity or price, or a product past
// 64 bits, traps: the runtime turns it into a refusal (zxapp_wasm.c W3),
// never a wrapped answer. amount.sutra holds nine digits each; past them
// the two disagree (held). Arithmetic and branches only: Z-Wasm's
// validator refuses wasm's untyped select.
extern "C" long long run(long long qty, long long price)
{
    if (qty < 0) __builtin_trap();
    if (price < 0) __builtin_trap();
    if (price > 0) {
        if (qty > (9223372036854775807LL - 500) / price) __builtin_trap();
    }
    return (qty * price + 500) / 1000;
}
