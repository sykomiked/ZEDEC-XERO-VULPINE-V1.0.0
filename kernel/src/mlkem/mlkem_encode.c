/* mlkem_encode.c — ML-KEM-768 CBD sampling, compression, byte encode/decode
 * See mlkem_encode.h for design notes.
 *
 * Author: Michael Laurence Curzi (c)
 * 36N9 Genetics, LLC — All Rights Reserved
 * License: SEL-3.3 (kernel component)
 */
#include "mlkem_encode.h"

static int get_bit(const uint8_t *buf, size_t pos) {
    return (buf[pos / 8] >> (pos % 8)) & 1;
}

/* FIPS 203 Algorithm 8, SamplePolyCBD_2 (eta=2): for each of the 256
 * output coefficients, sum 2 input bits into x, the next 2 into y,
 * output (x - y) mod q. Consumes 4*256 = 1024 bits = 128 bytes. */
void poly_cbd_eta2(const uint8_t buf[128], poly_t *out) {
    for (int i = 0; i < MLKEM_N; i++) {
        int x = get_bit(buf, (size_t)4 * i) + get_bit(buf, (size_t)4 * i + 1);
        int y = get_bit(buf, (size_t)4 * i + 2) + get_bit(buf, (size_t)4 * i + 3);
        out->coeffs[i] = mlkem_mod_reduce(x - y);
    }
}

/* FIPS 203 Algorithms 5/6: generic d-bit pack/unpack, LSB-first within
 * each coefficient and within each byte (BitsToBytes convention).
 * Implemented bit-by-bit for auditability; produces byte-identical
 * output to any correct optimized (e.g. nibble/5-byte-group) packer
 * for the same d, since both implement the same bit-level definition. */
void byte_encode(const poly_t *p, int d, uint8_t *out) {
    size_t total_bytes = ((size_t)MLKEM_N * (size_t)d + 7) / 8;
    for (size_t i = 0; i < total_bytes; i++) out[i] = 0;

    for (int c = 0; c < MLKEM_N; c++) {
        uint16_t val = (uint16_t)p->coeffs[c];
        for (int bit = 0; bit < d; bit++) {
            int bitval = (val >> bit) & 1;
            size_t global_bit = (size_t)c * (size_t)d + (size_t)bit;
            out[global_bit / 8] = (uint8_t)(out[global_bit / 8] | (uint8_t)(bitval << (global_bit % 8)));
        }
    }
}

void byte_decode(const uint8_t *in, int d, poly_t *out) {
    for (int c = 0; c < MLKEM_N; c++) {
        uint16_t val = 0;
        for (int bit = 0; bit < d; bit++) {
            size_t global_bit = (size_t)c * (size_t)d + (size_t)bit;
            int bitval = get_bit(in, global_bit);
            val = (uint16_t)(val | (uint16_t)(bitval << bit));
        }
        out->coeffs[c] = (int16_t)val;
    }
}

/* FIPS 203 Compress_d(x) = round(x * 2^d / q) mod 2^d, computed with
 * integer-only rounding (add q/2 before dividing) -- the same
 * technique used throughout every reference ML-KEM implementation. */
uint16_t scalar_compress(int16_t x, int d) {
    uint32_t xu = (uint32_t)((int32_t)x % MLKEM_Q + MLKEM_Q) % MLKEM_Q; /* canonical [0,Q) */
    uint32_t numerator = xu << d;
    uint32_t val = (numerator + (uint32_t)(MLKEM_Q / 2)) / (uint32_t)MLKEM_Q;
    return (uint16_t)(val & ((1u << d) - 1u));
}

/* FIPS 203 Decompress_d(y) = round(y * q / 2^d) */
int16_t scalar_decompress(uint16_t x, int d) {
    uint32_t numerator = (uint32_t)x * (uint32_t)MLKEM_Q;
    uint32_t half = 1u << (d - 1);
    uint32_t val = (numerator + half) >> d;
    return (int16_t)val;
}

void poly_compress(const poly_t *p, int d, uint8_t *out) {
    poly_t tmp;
    for (int i = 0; i < MLKEM_N; i++) tmp.coeffs[i] = (int16_t)scalar_compress(p->coeffs[i], d);
    byte_encode(&tmp, d, out);
}

void poly_decompress(const uint8_t *in, int d, poly_t *out) {
    poly_t tmp;
    byte_decode(in, d, &tmp);
    for (int i = 0; i < MLKEM_N; i++) out->coeffs[i] = scalar_decompress((uint16_t)tmp.coeffs[i], d);
}
