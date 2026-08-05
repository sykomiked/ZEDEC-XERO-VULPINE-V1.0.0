/* genomic_codon.h — Genomic codon domain-separation layer
 *
 * Maps the 4-base genetic alphabet (A/C/G/T) onto the 2-bit encoding
 * convention standard in bioinformatics (e.g. the .2bit file format),
 * groups bases into codon triplets (4^3 = 64 combinations, matching
 * the biological codon table), and uses this structure purely as a
 * DOMAIN-SEPARATION / CONTEXT-BINDING input to key derivation --
 * NOT as a replacement for the underlying Module-LWE hardness
 * assumption that actually secures ML-KEM. This keeps the scheme's
 * security resting entirely on the vetted lattice problem while still
 * giving the system a distinct, deterministic, auditable identity
 * label rooted in this codebase's existing conventions.
 *
 * GROUNDING NOTES (what is standard vs. system-specific):
 *   - 2-bit base encoding (A=00,C=01,G=10,T/U=11) and the 64-codon
 *     count (4^3) are standard, uncontested facts of molecular
 *     biology and bioinformatics file formats.
 *   - The specific per-base frequency values below are THIS SYSTEM'S
 *     chosen design constants (audiogenomics acoustic mapping),
 *     not claimed as an independently peer-reviewed physical
 *     resonance measurement. They are used only as opaque bytes fed
 *     into the domain-separation hash, so their numerical origin has
 *     no bearing on cryptographic soundness either way.
 *   - The Fibonacci dimension ladder (5, 8, 13, 21, 34, 55, 89, 144)
 *     mirrors this codebase's existing M5 -> M8 -> M13 isometry lift
 *     chain already declared in m5_types.h (ISOMETRY_LIFT_M8,
 *     ISOMETRY_LIFT_M13). Extending an ML-KEM module rank k to a
 *     larger value (e.g. k=13 instead of the FIPS 203 standard k=4)
 *     is mathematically sound -- Module-LWE remains hard, arguably
 *     more conservatively so, at larger ranks -- but it departs from
 *     the NIST-certified parameter sets and is therefore NOT
 *     interoperable with standard ML-KEM implementations elsewhere.
 *     Non-standard ranks must be explicitly and separately labeled
 *     (see mlkem_params_t) and never silently substituted for the
 *     default FIPS 203 ML-KEM-768 mode used for interoperability.
 *
 * Author: Michael Laurence Curzi (c)
 * 36N9 Genetics, LLC — All Rights Reserved
 * License: SEL-3.3 (kernel component)
 */
#ifndef GENOMIC_CODON_H
#define GENOMIC_CODON_H

#include <stdint.h>
#include <stddef.h>

typedef enum {
    BASE_A = 0,  /* 00 */
    BASE_C = 1,  /* 01 */
    BASE_G = 2,  /* 10 */
    BASE_T = 3   /* 11, T (DNA) or U (RNA) */
} genomic_base_t;

/* Fixed-point millihertz to avoid floating point in the freestanding
 * kernel build (545.6 Hz -> 545600 mHz). Values as specified for this
 * system's audiogenomics acoustic mapping. */
#define BASE_FREQ_MHZ_A 545600u
#define BASE_FREQ_MHZ_C 537800u
#define BASE_FREQ_MHZ_G 550000u
#define BASE_FREQ_MHZ_T 543400u

/* One codon = 3 bases = 6 bits = one of 64 combinations, matching the
 * biological codon table (4 bases ^ 3 positions = 64). */
typedef struct {
    genomic_base_t bases[3];
} codon_t;

#define CODON_COUNT 64

/* 2-bit encode/decode */
uint8_t genomic_base_encode(genomic_base_t b);
genomic_base_t genomic_base_decode(uint8_t bits2);

/* Codon <-> 6-bit index (0..63) */
uint8_t codon_to_index(codon_t c);
codon_t codon_from_index(uint8_t index);

/* Millihertz for a base, per this system's acoustic mapping table. */
uint32_t genomic_base_freq_mhz(genomic_base_t b);

/* Fibonacci dimension ladder used by this codebase's M5 isometry lift
 * chain (M5 -> M8 -> M13 -> ...). Index 0 = 5 (M5 baseline). */
uint32_t fib_dimension(uint32_t index);

/* Derive a fixed-length domain-separation tag from a codon sequence
 * and a target Fibonacci dimension, for use as HKDF/SHAKE "info"
 * context binding -- NOT as key material and NOT as a substitute for
 * the lattice-based KEM's own security. `out_len` bytes are written
 * via SHAKE256 (see keccak.h), so any length is supported. */
void genomic_domain_tag(const codon_t *codons, size_t codon_count,
                         uint32_t fib_dimension_value,
                         uint8_t *out, size_t out_len);

#endif /* GENOMIC_CODON_H */
