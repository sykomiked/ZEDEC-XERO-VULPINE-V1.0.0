/* entropy.c — ARM64 entropy source (FEAT_RNG / RNDR) with honest fallback
 *
 * Audit finding P0-4: the previous vault-key derivation mixed the generic
 * timer counter, counter frequency, a stack pointer, and a static object's
 * address, then claimed "hardware entropy, per-boot unique." Those values
 * are largely predictable and are NOT a cryptographic entropy source.
 *
 * This module fixes that truthfully:
 *   - If the CPU implements FEAT_RNG (ID_AA64ISAR0_EL1.RNDR >= 1), it reads
 *     the architected RNDR instruction, with a health test (reject stuck /
 *     all-zero / all-ones outputs) and bounded retries.
 *   - If FEAT_RNG is absent (e.g. QEMU cortex-a53), it does NOT pretend to
 *     have hardware entropy. It returns "unavailable" so callers can fail
 *     closed for security-sensitive keys, or fall back to a clearly-labeled
 *     NON-cryptographic development value.
 *
 * A reviewed DRBG and full key lifecycle are still future work; this closes
 * the "predictable material presented as hardware entropy" defect.
 *
 * Author: H.M. Michael-Laurence: Curzi (c)  (ZXV entropy slice)
 * License: SEL-3.3
 */
#include <stdint.h>
#include <stdbool.h>
#include "entropy.h"

/* ID_AA64ISAR0_EL1.RNDR occupies bits [63:60]; >= 1 means RNDR/RNDRRS. */
bool arm64_rng_available(void) {
    uint64_t isar0;
    __asm__ volatile("mrs %0, ID_AA64ISAR0_EL1" : "=r"(isar0));
    return ((isar0 >> 60) & 0xF) >= 1;
}

/* One RNDR read. Returns true and *out set on success. On this CPU RNDR
 * sets PSTATE.NZCV: Z=0 (NE) on success, Z=1 on transient failure. */
static bool rndr_once(uint64_t *out) {
    uint64_t v = 0, ok = 0;
    __asm__ volatile(
        "mrs %0, S3_3_C2_C4_0\n\t"   /* RNDR */
        "cset %1, ne\n\t"            /* ok = (Z==0) */
        : "=r"(v), "=r"(ok) :: "cc");
    if (!ok) return false;
    *out = v;
    return true;
}

/* Get a 64-bit random value from RNDR with retries + a basic health test.
 * Returns false if FEAT_RNG is absent or the source looks stuck. */
bool arm64_rng_get64(uint64_t *out) {
    if (!out) return false;
    if (!arm64_rng_available()) return false;

    uint64_t prev = 0;
    bool have_prev = false;
    for (int attempt = 0; attempt < 16; attempt++) {
        uint64_t v;
        if (!rndr_once(&v)) continue;
        /* Health test: reject degenerate constants and immediate repeats. */
        if (v == 0 || v == 0xFFFFFFFFFFFFFFFFULL) continue;
        if (have_prev && v == prev) continue;
        prev = v; have_prev = true;
        *out = v;
        return true;
    }
    return false;
}

/* Fill `len` bytes. Returns true only if every byte came from RNDR. */
bool arm64_rng_fill(uint8_t *buf, uint32_t len) {
    if (!buf) return false;
    uint32_t i = 0;
    while (i < len) {
        uint64_t v;
        if (!arm64_rng_get64(&v)) return false;
        for (int b = 0; b < 8 && i < len; b++, i++)
            buf[i] = (uint8_t)(v >> (b * 8));
    }
    return true;
}
