// Copyright (c) 2024-2026 Michael Laurence Curzi and 36N9 Genetics, LLC
// SPDX-License-Identifier: Apache-2.0
//
// Gain and clip of one 16-bit PCM sample, in C++ compiled to WebAssembly:
// floor(sample * level / 10000) (level 10000 is unity, in hundredths of a
// percent; the floor is toward minus infinity, as KRON's _ is), then held
// inside -32768 .. 32767, so a loud sample saturates instead of wrapping.
// The domain is a 16-bit sample and a level of 0 .. 1000000 (100 times);
// anything outside it is refused with -32769, one below the 16-bit floor,
// which no clipped sample can be (birdsong_model.h BIRDSONG_REFUSED).
// Written with branches only: Z-Wasm's validator refuses wasm's untyped
// select, which a conditional assignment would compile to.
extern "C" long long run(long long sample, long long level)
{
    if (sample < -32768) return -32769;
    if (sample > 32767) return -32769;
    if (level < 0) return -32769;
    if (level > 1000000) return -32769;
    long long p = sample * level;
    if (p >= 0) {
        if (p / 10000 > 32767) return 32767;
        return p / 10000;
    }
    if ((p - 9999) / 10000 < -32768) return -32768;
    return (p - 9999) / 10000;
}
