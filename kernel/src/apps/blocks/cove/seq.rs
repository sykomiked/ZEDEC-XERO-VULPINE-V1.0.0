// Copyright (c) 2024-2026 Michael Laurence Curzi and 36N9 Genetics, LLC
// SPDX-License-Identifier: Apache-2.0
//
// The nonce discipline of one tunnel channel (cove_rules.h CR2), in Rust
// compiled to WebAssembly (build: tools/apps/build_wasm_blocks.sh): a
// frame's seq must rise past the channel's last seq, and the answer is
// how far, seq - last (1 for the very next frame). A seq at or below last
// (a reuse or a replay) or a last below 0 is refused, and so is a seq of
// i64::MAX, after which no seq could follow (overflow): the module traps,
// which the runtime turns into a refusal (zxapp_wasm.c W3), never an
// answer. It answers to 64 bits, past seq.sutra's 9(18): there the two
// disagree and the frame is held.
#![no_std]

#[panic_handler]
fn panic(_: &core::panic::PanicInfo) -> ! {
    core::arch::wasm32::unreachable()
}

#[no_mangle]
pub extern "C" fn run(last: i64, seq: i64) -> i64 {
    if last < 0 || seq <= last || seq == i64::MAX {
        core::arch::wasm32::unreachable();
    }
    seq - last
}
