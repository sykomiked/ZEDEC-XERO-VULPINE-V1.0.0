// Copyright (c) 2024-2026 Michael Laurence Curzi and 36N9 Genetics, LLC
// SPDX-License-Identifier: Apache-2.0
//
// One entry's share of a folder in the Hoard, x100 (1234 = 12.34%), in C++
// compiled to WebAssembly. total is the folder's size (at least 1, as the
// KRON does); a negative size or one bigger than the total is -1.
// Written with branches only: Z-Wasm's validator refuses wasm's untyped
// select, which a conditional assignment would compile to.
extern "C" long long run(long long size, long long total)
{
    if (size < 0) return -1;
    if (total < 1) {
        if (size > 1) return -1;
        return size * 10000;
    }
    if (size > total) return -1;
    return size * 10000 / total;
}
