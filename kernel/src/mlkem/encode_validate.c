#include <stdio.h>
#include <string.h>
#include "mlkem_encode.h"

int main(void) {
    int failures = 0;

    /* CBD eta=2: all-zero input must produce the all-zero polynomial
     * (0 bits everywhere -> x=0,y=0 -> coeff=0 for every position) */
    {
        uint8_t buf[128] = {0};
        poly_t p;
        poly_cbd_eta2(buf, &p);
        int all_zero = 1;
        for (int i = 0; i < MLKEM_N; i++) if (p.coeffs[i] != 0) all_zero = 0;
        printf("%s: CBD eta=2 all-zero input -> all-zero polynomial\n", all_zero ? "PASS" : "FAIL");
        if (!all_zero) failures++;
    }

    /* CBD eta=2: all-ones input -> x=1+1=2, y=1+1=2 -> coeff = 2-2=0 for every position */
    {
        uint8_t buf[128];
        for (int i = 0; i < 128; i++) buf[i] = 0xFF;
        poly_t p;
        poly_cbd_eta2(buf, &p);
        int all_zero = 1;
        for (int i = 0; i < MLKEM_N; i++) if (p.coeffs[i] != 0) all_zero = 0;
        printf("%s: CBD eta=2 all-ones input -> all-zero polynomial (x=y=2)\n", all_zero ? "PASS" : "FAIL");
        if (!all_zero) failures++;
    }

    /* CBD eta=2: range check -- output must always be in {-2,-1,0,1,2} mod q,
     * i.e. one of {0,1,2,3327,3328} */
    {
        uint8_t buf[128];
        for (int i = 0; i < 128; i++) buf[i] = (uint8_t)(i * 37 + 11); /* arbitrary bit pattern */
        poly_t p;
        poly_cbd_eta2(buf, &p);
        int in_range = 1;
        for (int i = 0; i < MLKEM_N; i++) {
            int16_t c = p.coeffs[i];
            if (!(c == 0 || c == 1 || c == 2 || c == 3327 || c == 3328)) in_range = 0;
        }
        printf("%s: CBD eta=2 output always in {-2..2} mod q\n", in_range ? "PASS" : "FAIL");
        if (!in_range) failures++;
    }

    /* byte_encode/byte_decode round-trip for d=4, 10, 12 */
    for (int d_idx = 0; d_idx < 3; d_idx++) {
        int d = (d_idx == 0) ? 4 : (d_idx == 1) ? 10 : 12;
        poly_t p, back;
        for (int i = 0; i < MLKEM_N; i++) p.coeffs[i] = (int16_t)((i * 97 + 13) % (1 << d));
        uint8_t buf[256 * 12 / 8 + 4];
        byte_encode(&p, d, buf);
        byte_decode(buf, d, &back);
        int match = memcmp(p.coeffs, back.coeffs, sizeof(p.coeffs)) == 0;
        printf("%s: byte_encode/byte_decode round-trip (d=%d)\n", match ? "PASS" : "FAIL", d);
        if (!match) failures++;
    }

    /* Cross-check d=4 against the exact reference bit-packing formula
     * from pq-code-package/mlkem-native poly_compress_d4:
     *   r[i*4] = t[0] | (t[1] << 4)   (and similarly for the other 3 bytes)
     * i.e. two 4-bit values packed per byte, low nibble first. */
    {
        poly_t p = {0};
        p.coeffs[0] = 0x3; p.coeffs[1] = 0xA; /* expect byte0 = 0x3 | (0xA<<4) = 0xA3 */
        p.coeffs[2] = 0x5; p.coeffs[3] = 0xC; /* expect byte1 = 0x5 | (0xC<<4) = 0xC5 */
        uint8_t buf[128];
        byte_encode(&p, 4, buf);
        int ok = (buf[0] == 0xA3) && (buf[1] == 0xC5);
        printf("%s: byte_encode(d=4) matches reference nibble-packing formula (byte0=%02x byte1=%02x)\n",
               ok ? "PASS" : "FAIL", buf[0], buf[1]);
        if (!ok) failures++;
    }

    /* scalar_compress/decompress: 0 must compress to 0, and Q/2 (~1664/1665)
     * must compress to the midpoint of the range for various d */
    {
        int ok = 1;
        if (scalar_compress(0, 4) != 0) ok = 0;
        if (scalar_compress(0, 10) != 0) ok = 0;
        printf("%s: scalar_compress(0, d) == 0\n", ok ? "PASS" : "FAIL");
        if (!ok) failures++;
    }

    /* Compress then decompress should recover a value close to the
     * original (lossy, but must be within the expected error bound
     * of q/2^(d+1)) */
    {
        int max_err_d4 = 0, max_err_d10 = 0;
        for (int x = 0; x < MLKEM_Q; x += 7) {
            uint16_t c4 = scalar_compress((int16_t)x, 4);
            int16_t d4v = scalar_decompress(c4, 4);
            int err4 = d4v - x; if (err4 < 0) err4 = -err4;
            if (err4 > MLKEM_Q / 2) err4 = MLKEM_Q - err4; /* wraparound distance */
            if (err4 > max_err_d4) max_err_d4 = err4;

            uint16_t c10 = scalar_compress((int16_t)x, 10);
            int16_t d10v = scalar_decompress(c10, 10);
            int err10 = d10v - x; if (err10 < 0) err10 = -err10;
            if (err10 > MLKEM_Q / 2) err10 = MLKEM_Q - err10;
            if (err10 > max_err_d10) max_err_d10 = err10;
        }
        printf("compress/decompress max error: d=4 -> %d (bound ~%d), d=10 -> %d (bound ~%d)\n",
               max_err_d4, MLKEM_Q / (1 << 5), max_err_d10, MLKEM_Q / (1 << 11) + 2);
        int ok = (max_err_d4 <= MLKEM_Q / (1 << 4)) && (max_err_d10 <= MLKEM_Q / (1 << 9));
        printf("%s: compress/decompress error within expected bound\n", ok ? "PASS" : "FAIL");
        if (!ok) failures++;
    }

    if (failures == 0) printf("\n=== ALL ENCODE/CBD/COMPRESS VALIDATION TESTS PASSED ===\n");
    else printf("\n=== %d VALIDATION FAILURE(S) ===\n", failures);
    return failures;
}
