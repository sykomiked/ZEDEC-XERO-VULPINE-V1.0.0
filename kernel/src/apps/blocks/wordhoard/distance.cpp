// Copyright (c) 2024-2026 Michael Laurence Curzi and 36N9 Genetics, LLC
// SPDX-License-Identifier: Apache-2.0
//
// The edit distance of two words over code points, in C++ compiled to
// WebAssembly (build: tools/apps/build_wasm_blocks.sh), the "did you mean"
// of wordhoard_rules.h WH2: the least insertions, deletions and
// substitutions of one code point each. Each word comes as four numbers of
// three 21-bit code points, the first in the highest field (WH1), a field
// of 0 ending the word, so up to 12 code points a word. A negative number,
// a field past 0x10FFFF, a surrogate (0xD800..0xDFFF, never a character)
// or a code point after the end traps, which the runtime turns into a
// refusal (zxapp_wasm.c W3). distance.bas holds the fields to 0..0x10FFFF
// as numbers and answers for a surrogate, so the two then disagree and
// the answer is held (BOTH).

static long long unpack(long long v, long long *cp, long long n, long long *ended)
{
    if (v < 0) __builtin_trap();
    for (long long f = 2; f >= 0; f--) {
        long long c = (v >> (21 * f)) & 2097151;
        if (c == 0) {
            *ended = 1;
            continue;
        }
        if (*ended) __builtin_trap();
        if (c > 1114111) __builtin_trap();
        if (c >= 55296) {
            if (c <= 57343) __builtin_trap();
        }
        cp[n] = c;
        n++;
    }
    return n;
}

extern "C" long long run(long long a1, long long a2, long long a3, long long a4, long long b1,
                         long long b2, long long b3, long long b4)
{
    long long a[12], b[12], row[13];
    long long ea = 0, eb = 0;
    long long na = unpack(a1, a, 0, &ea);
    na = unpack(a2, a, na, &ea);
    na = unpack(a3, a, na, &ea);
    na = unpack(a4, a, na, &ea);
    long long nb = unpack(b1, b, 0, &eb);
    nb = unpack(b2, b, nb, &eb);
    nb = unpack(b3, b, nb, &eb);
    nb = unpack(b4, b, nb, &eb);
    for (long long j = 0; j <= nb; j++) row[j] = j;
    for (long long i = 1; i <= na; i++) {
        long long diag = row[0];
        row[0] = i;
        for (long long j = 1; j <= nb; j++) {
            long long up = row[j];
            long long best = diag;
            if (a[i - 1] != b[j - 1]) best = diag + 1;
            if (up + 1 < best) best = up + 1;
            if (row[j - 1] + 1 < best) best = row[j - 1] + 1;
            row[j] = best;
            diag = up;
        }
    }
    return row[nb];
}
