// Copyright (c) 2024-2026 Michael Laurence Curzi and 36N9 Genetics, LLC
// SPDX-License-Identifier: Apache-2.0
//
// How many whole character cells fit across a span of pixels, in C++
// compiled to WebAssembly (build: tools/apps/build_wasm_blocks.sh):
// floor(pixels / cell), so a part cell at the edge is never drawn. Flint
// asks it once for the columns (the window's width over the cell's width)
// and once for the rows (its height over the cell's height). The domain is
// a span of 0 .. 65535 pixels and a cell of 1 .. 256 pixels; anything
// outside it is refused with -1, which no count of cells can be
// (flint_model.h FLINT_REFUSED). KRON answers past the domain, so the two
// disagree there and the answer is held (BOTH). Branches only: Z-Wasm's
// validator refuses wasm's untyped select.
extern "C" long long run(long long pixels, long long cell)
{
    if (pixels < 0) return -1;
    if (pixels > 65535) return -1;
    if (cell < 1) return -1;
    if (cell > 256) return -1;
    return pixels / cell;
}
