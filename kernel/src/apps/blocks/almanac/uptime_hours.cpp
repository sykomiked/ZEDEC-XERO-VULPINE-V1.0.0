// Copyright (c) 2024-2026 Michael Laurence Curzi and 36N9 Genetics, LLC
// SPDX-License-Identifier: Apache-2.0
//
// The hours past the last whole day, in C++ compiled to WebAssembly.
extern "C" long long run(long long seconds)
{
    return seconds < 0 ? -1 : seconds % 86400 / 3600;
}
