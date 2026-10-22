// Copyright (c) 2024-2026 Michael Laurence Curzi and 36N9 Genetics, LLC
// SPDX-License-Identifier: Apache-2.0
//
// The shuffle (warble_rules.h WR1), in Rust compiled to WebAssembly (build:
// tools/apps/build_wasm_blocks.sh): the track (0..count - 1) played at
// place step of the shuffle of count tracks from seed. Six rounds of
// swap-or-not over x, from x = step: round r takes the key
// k = (s + 104729 * r) mod count (s = seed mod 999983), the partner
// p = (k + count - x) mod count, and swaps x for p when the square of
// h = (s + 65537 * r + 31337 * max(x, p)) mod 999983 is odd mod 999983.
// x and its partner share their pair's max, so both swap or neither: every
// round is a permutation, and every track is played once. A seed outside
// 0..999999999, a count outside 1..999999 or a step outside 0..count - 1
// traps, which the runtime turns into a refusal (zxapp_wasm.c W3), never an
// answer. Inside those, no product passes 64 bits.
#![no_std]

#[panic_handler]
fn panic(_: &core::panic::PanicInfo) -> ! {
    core::arch::wasm32::unreachable()
}

const MIX: i64 = 999983;

#[no_mangle]
pub extern "C" fn run(seed: i64, count: i64, step: i64) -> i64 {
    if seed < 0 || seed > 999999999 || count < 1 || count > 999999 || step < 0 || step >= count {
        core::arch::wasm32::unreachable();
    }
    let s = seed % MIX;
    let mut x = step;
    let mut r: i64 = 0;
    while r < 6 {
        let k = (s + 104729 * r) % count;
        let p = (k + count - x) % count;
        let m = if x > p { x } else { p };
        let h = (s + 65537 * r + 31337 * m) % MIX;
        if (h * h % MIX) % 2 == 1 {
            x = p;
        }
        r += 1;
    }
    x
}
