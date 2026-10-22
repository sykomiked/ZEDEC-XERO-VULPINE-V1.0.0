// Copyright (c) 2024-2026 Michael Laurence Curzi and 36N9 Genetics, LLC
// SPDX-License-Identifier: Apache-2.0
//
// One byte of a published space's CID text, in Rust compiled to
// WebAssembly (build: tools/apps/build_wasm_blocks.sh), as seedbank_rules.h
// SR1 says: the text is "b" then base32 lower case, 59 bytes in all (zxvd's
// ZXS_CID_LEN). 1 when the byte code at pos fits, 0 when it does not. An
// input outside the domain (len 1..255, pos 0..len-1, code 0..255) is no
// byte of a text: the module traps, which the runtime turns into a refusal
// (zxapp_wasm.c W3), never an answer. Comparisons only, so nothing can
// overflow.
#![no_std]

#[panic_handler]
fn panic(_: &core::panic::PanicInfo) -> ! {
    core::arch::wasm32::unreachable()
}

#[no_mangle]
pub extern "C" fn run(pos: i64, code: i64, len: i64) -> i64 {
    if len < 1 || len > 255 || pos < 0 || pos >= len || code < 0 || code > 255 {
        core::arch::wasm32::unreachable();
    }
    if len != 59 {
        return 0;
    }
    let fits = if pos == 0 {
        code == 98
    } else {
        (code >= 97 && code <= 122) || (code >= 50 && code <= 55)
    };
    if fits {
        1
    } else {
        0
    }
}
