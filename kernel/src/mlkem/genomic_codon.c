/* genomic_codon.c — Genomic codon domain-separation layer
 * See genomic_codon.h for design rationale and grounding notes.
 *
 * Author: Michael Laurence Curzi (c)
 * 36N9 Genetics, LLC — All Rights Reserved
 * Copyright (c) 2024-2026 Michael Laurence Curzi and 36N9 Genetics, LLC
 * SPDX-License-Identifier: LicenseRef-OPL-1.1 AND CC-BY-SA-4.0
 * Licensed under OPL-1.1, SEL-3.3, the Royal Writ of the Sicilian Crown,
 * and CC BY-SA 4.0. See LICENSE at the repository root.
 */
#include "genomic_codon.h"
#include "keccak.h"

uint8_t genomic_base_encode(genomic_base_t b) {
    return (uint8_t)(b & 0x3);
}

genomic_base_t genomic_base_decode(uint8_t bits2) {
    return (genomic_base_t)(bits2 & 0x3);
}

uint8_t codon_to_index(codon_t c) {
    return (uint8_t)((genomic_base_encode(c.bases[0]) << 4) |
                      (genomic_base_encode(c.bases[1]) << 2) |
                      (genomic_base_encode(c.bases[2])));
}

codon_t codon_from_index(uint8_t index) {
    codon_t c;
    c.bases[0] = genomic_base_decode((uint8_t)((index >> 4) & 0x3));
    c.bases[1] = genomic_base_decode((uint8_t)((index >> 2) & 0x3));
    c.bases[2] = genomic_base_decode((uint8_t)(index & 0x3));
    return c;
}

uint32_t genomic_base_freq_mhz(genomic_base_t b) {
    switch (b) {
        case BASE_A: return BASE_FREQ_MHZ_A;
        case BASE_C: return BASE_FREQ_MHZ_C;
        case BASE_G: return BASE_FREQ_MHZ_G;
        case BASE_T: return BASE_FREQ_MHZ_T;
        default:     return 0;
    }
}

uint32_t fib_dimension(uint32_t index) {
    /* 0:5:M5  1:8:M8  2:13:M13  3:21  4:34  5:55  6:89  7:144 ...
     * matches this codebase's existing M5->M8->M13 isometry chain,
     * extended along the same Fibonacci sequence. */
    uint32_t a = 5, b = 8;
    if (index == 0) return a;
    if (index == 1) return b;
    for (uint32_t i = 2; i <= index; i++) {
        uint32_t c = a + b;
        a = b;
        b = c;
    }
    return b;
}

void genomic_domain_tag(const codon_t *codons, size_t codon_count,
                         uint32_t fib_dimension_value,
                         uint8_t *out, size_t out_len) {
    /* Serialize: ["ZXV-GENOMIC-DOMAIN-V1"] || dim(4B BE) ||
     * for each codon: [index(1B), freq0(4B BE), freq1(4B BE), freq2(4B BE)]
     * then SHAKE256 the whole buffer to `out_len` bytes. This is pure
     * domain separation / context binding -- the resulting tag carries
     * no secret entropy of its own and must never be used as key
     * material by itself. */
    uint8_t buf[16 + 4 + 64 * 13]; /* bounded: codon_count is small in practice */
    size_t pos = 0;
    const char *label = "ZXV-GENOMIC-DOMAIN-V1";
    for (int i = 0; label[i] != '\0' && pos < sizeof(buf); i++) buf[pos++] = (uint8_t)label[i];

    buf[pos++] = (uint8_t)(fib_dimension_value >> 24);
    buf[pos++] = (uint8_t)(fib_dimension_value >> 16);
    buf[pos++] = (uint8_t)(fib_dimension_value >> 8);
    buf[pos++] = (uint8_t)(fib_dimension_value);

    for (size_t i = 0; i < codon_count && pos + 13 <= sizeof(buf); i++) {
        buf[pos++] = codon_to_index(codons[i]);
        for (int b = 0; b < 3; b++) {
            uint32_t f = genomic_base_freq_mhz(codons[i].bases[b]);
            buf[pos++] = (uint8_t)(f >> 24);
            buf[pos++] = (uint8_t)(f >> 16);
            buf[pos++] = (uint8_t)(f >> 8);
            buf[pos++] = (uint8_t)(f);
        }
    }

    shake256(buf, pos, out, out_len);
}
