/* sha256.h — Freestanding SHA-256 (FIPS 180-4)
 *
 * Self-contained, dependency-free, single-shot SHA-256 digest.
 * No libc dependency beyond stdint.h/stddef.h.
 *
 * Author: Michael Laurence Curzi (c)
 * 36N9 Genetics, LLC — All Rights Reserved
 * License: SEL-3.3 (kernel component)
 */
#ifndef SHA256_H
#define SHA256_H

#include <stdint.h>
#include <stddef.h>

#define SHA256_DIGEST_LEN 32

/* One-shot digest: hashes `len` bytes of `data`, writes 32-byte digest to out. */
void sha256(const uint8_t *data, size_t len, uint8_t out[SHA256_DIGEST_LEN]);

#endif /* SHA256_H */
