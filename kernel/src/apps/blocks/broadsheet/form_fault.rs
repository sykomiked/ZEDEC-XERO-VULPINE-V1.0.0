// Copyright (c) 2024-2026 Michael Laurence Curzi and 36N9 Genetics, LLC
// SPDX-License-Identifier: Apache-2.0
//
// Why one cross-reference offset of a document is not well formed
// (broadsheet_rules.h BR2), in Rust compiled to WebAssembly (build:
// tools/apps/build_wasm_blocks.sh). header is 1 when the file starts with
// its header and 0 when not, off the offset, room the file's size less the
// offset. The first fault that applies: 1 no header, 2 the offset outside
// the file (below 0, or no byte left after it), 0 none. A header that is no
// flag, an offset past zxvd's largest file (256 MiB, 268435456 bytes) or a
// room past it is no measurement: the module traps, which the runtime turns
// into a refusal (zxapp_wasm.c W3), never an answer. Comparisons only, so
// no input can overflow.
#![no_std]

#[panic_handler]
fn panic(_: &core::panic::PanicInfo) -> ! {
    core::arch::wasm32::unreachable()
}

#[no_mangle]
pub extern "C" fn run(header: i64, off: i64, room: i64) -> i64 {
    if header < 0 || header > 1 || off > 268435455 || room > 268435456 {
        core::arch::wasm32::unreachable();
    }
    if header == 0 {
        1
    } else if off < 0 || room < 1 {
        2
    } else {
        0
    }
}
