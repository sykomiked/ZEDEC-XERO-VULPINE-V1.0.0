// Copyright (c) 2024-2026 Michael Laurence Curzi and 36N9 Genetics, LLC
// SPDX-License-Identifier: Apache-2.0
//
// Whole days of uptime, in C++ compiled to WebAssembly (build:
// tools/apps/build_wasm_blocks.sh). KRON says the same in uptime_days.kron.
extern "C" long long run(long long seconds)
{
    return seconds < 0 ? -1 : seconds / 86400;
}
