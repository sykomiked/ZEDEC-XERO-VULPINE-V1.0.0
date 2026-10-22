// Copyright (c) 2024-2026 Michael Laurence Curzi and 36N9 Genetics, LLC
// SPDX-License-Identifier: Apache-2.0
//
// What stays of the owner's gift ceiling after a gift (hamlet_rules.h
// HR1), in C++ compiled to WebAssembly (build:
// tools/apps/build_wasm_blocks.sh): ceiling - amount, in integer minor
// units. A gift below one minor unit, or past the ceiling, is no gift: the
// module traps, which the runtime turns into a refusal (zxapp_wasm.c W3),
// never a wrapped answer. gift.sutra holds eighteen digits each; past them
// the two disagree (held). Arithmetic only: Z-Wasm's validator refuses
// wasm's untyped select, which a conditional would compile to.
extern "C" long long run(long long amount, long long ceiling)
{
    if (amount < 1) __builtin_trap();
    if (amount > ceiling) __builtin_trap();
    return ceiling - amount;
}
