/* mlkem_encode.h — ML-KEM-768 (FIPS 203) CBD sampling, compression,
 * and byte encode/decode.
 *
 * ML-KEM-768 parameters (FIPS 203 Table 2): k=3, eta1=2, eta2=2,
 * du=10, dv=4. Cross-checked against the Go standard library's
 * FIPS-140 ML-KEM-768 implementation and the pq-code-package/
 * mlkem-native reference compression routines.
 *
 * Author: Michael Laurence Curzi (c)
 * 36N9 Genetics, LLC — All Rights Reserved
 * License: SEL-3.3 (kernel component)
 */
#ifndef MLKEM_ENCODE_H
#define MLKEM_ENCODE_H

#include "mlkem_ntt.h"
#include <stdint.h>
#include <stddef.h>

#define MLKEM768_K    3
#define MLKEM768_ETA1 2
#define MLKEM768_ETA2 2
#define MLKEM768_DU   10
#define MLKEM768_DV   4

/* FIPS 203 Algorithm 8, SamplePolyCBD (eta=2 case, used for both eta1
 * and eta2 in ML-KEM-768): consumes 2*eta*N/8 = 128 bytes for eta=2,
 * produces a polynomial with centered-binomial-distributed noise. */
void poly_cbd_eta2(const uint8_t buf[128], poly_t *out);

/* FIPS 203 Algorithms 5/6, ByteEncode_d / ByteDecode_d: pack/unpack
 * N=256 coefficients, each d bits wide, into ceil(256*d/8) bytes.
 * Supports d in {4, 10, 12} (the only values ML-KEM-768 uses). */
void byte_encode(const poly_t *p, int d, uint8_t *out);
void byte_decode(const uint8_t *in, int d, poly_t *out);

/* FIPS 203 compression: round each coefficient to the nearest
 * multiple of Q/2^d, represented as a d-bit integer (Compress_d),
 * and its inverse (Decompress_d). Compress reduces precision (lossy);
 * Decompress maps back into the full [0,Q) range approximately. */
uint16_t scalar_compress(int16_t x, int d);
int16_t scalar_decompress(uint16_t x, int d);
void poly_compress(const poly_t *p, int d, uint8_t *out);
void poly_decompress(const uint8_t *in, int d, poly_t *out);

#endif /* MLKEM_ENCODE_H */
