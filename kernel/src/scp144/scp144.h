/* Copyright (c) 2024-2026 Michael Laurence Curzi and 36N9 Genetics, LLC */
/* SPDX-License-Identifier: Apache-2.0 */
/* scp144.h — Sicilian Crown Protection: 144-Dimensional AC Casimir Cavity
 *            Array & Traversable Wormhole Transceiver (Hardware-as-Code Device).
 *
 * Physics & Hardware Architecture:
 *   1. Recursive 12x12 Casimir Cavity Array (144 Resonant Modes):
 *      The transceiver is modeled as a physical 12-plate macro-cavity spaced
 *      along the Fibonacci ladder (F1..F12 = 1, 1, 2, 3, 5, 8, 13, 21, 34, 55,
 *      89, 144), where each macro-plate recursively hosts a 12-mode micro-cavity
 *      (12 * 12 = 144 total cavity modes). Only non-consecutive Zeckendorf
 *      standing-wave modes satisfy the plate boundary conditions; all
 *      non-resonant modes are excluded between the plates, generating the
 *      discrete negative Casimir vacuum energy (delta_E < 0) that holds the
 *      narrowest center throat (F6 = 8 <-> F1 = 1) open.
 *
 *   2. Alternating Current (AC) Push-Pull Cassini Oscillator:
 *      By Cassini's Identity on the Fibonacci Q-matrix:
 *          F_{k+1} * F_{k-1} - F_k^2 = (-1)^k
 *          233 * 89 - 144^2 = +1   (outer aperture k = 12)
 *          13 * 5   - 8^2   = +1   (center throat  k = 6)
 *      Every clock edge alternates polarity (+1 <-> -1). Unlike Direct Current
 *      (DC) networking—which pushes unidirectionally and drains entropy—the AC
 *      transceiver couples each forward contracting wave (144 -> 1) with the
 *      counter-propagating expanding wave (1 -> 144), achieving a 100%
 *      bijective (det = +/-1) lossless transformation over exact integers.
 */
#ifndef ZXV_SCP144_H
#define ZXV_SCP144_H

#include <stdint.h>
#include <stdbool.h>

#define SCP144_DIM           144u
#define SCP144_RUNGS         12u
#define SCP144_KEY_LEN       32u
#define SCP144_TAG_LEN       16u
#define SCP144_PART_HDR_LEN  24u
#define SCP144_PART_OVERHEAD (SCP144_PART_HDR_LEN + SCP144_TAG_LEN) /* 40 bytes */
#define SCP144_PART_MAGIC    0x34343153u /* "S144" */

/* Hardware Control & Status Register (CSR) bits */
#define SCP144_CSR_ENABLE      (1u << 0) /* Cavity plates energized                */
#define SCP144_CSR_AC_LOCK     (1u << 1) /* AC push-pull phase oscillator locked   */
#define SCP144_CSR_THROAT_OPEN (1u << 2) /* Negative Casimir well holding throat   */
#define SCP144_CSR_RESONANT    (1u << 3) /* 12x12 recursive modes in standing wave */
#define SCP144_CSR_FAULT       (1u << 7) /* Non-resonant mode rejected at plate    */

enum {
    SCP144_OK        =  0,
    SCP144_ERR_ARG   = -1,
    SCP144_ERR_SPACE = -2,
    SCP144_ERR_AUTH  = -3, /* AC phase / Poly1305 / Casimir mode witness mismatch */
};

/* One micro-cavity plate register (12 micro-modes per macro-plate; 12x12 = 144). */
typedef struct scp144_plate {
    uint16_t gap_fib;        /* Plate separation F_k (1..144)                  */
    uint16_t zeck_mask;      /* Non-consecutive Zeckendorf resonant mode mask  */
    int32_t  cassini_det;    /* Exact Cassini determinant: +1 (even) / -1 (odd)*/
    int64_t  casimir_energy; /* Negative vacuum energy density across micro-modes */
    uint8_t  micro_modes[SCP144_RUNGS]; /* 12 recursive sub-cavity amplitudes  */
} scp144_plate_t;

/* ===== SCP-144 Casimir Cavity Array (Hardware-as-Code Device) ===== */
typedef struct scp144_cavity {
    uint32_t device_id;
    uint32_t csr;                 /* Control & Status Register (SCP144_CSR_*) */
    uint64_t ac_cycle;            /* Monotonic AC half-cycle counter          */
    int8_t   ac_polarity;         /* Instantaneous AC polarity: +1 or -1      */
    uint8_t  throat_aperture;     /* Current throat width in Fibonacci units  */
    uint8_t  recursion_depth;     /* Recursive hardware simulation depth      */
    uint8_t  reserved;

    uint8_t  throat_key[SCP144_KEY_LEN]; /* Coupled 144D lattice binding key  */
    scp144_plate_t plates[SCP144_RUNGS]; /* 12 macro-plates x 12 micro-modes  */

    /* AC Push-Pull Standing-Wave Telemetry Registers */
    int64_t  total_casimir_energy; /* Net negative vacuum energy (well depth) */
    uint64_t pos_half_cycles;      /* +1 contracting waves transmitted        */
    uint64_t neg_half_cycles;      /* -1 expanding counter-waves coupled      */
    uint64_t parts_sealed;         /* Parts contracted & sealed through throat*/
    uint64_t parts_opened;         /* Parts expanded & verified from throat   */
    uint64_t faults_bounced;       /* Non-resonant / tampered frames bounced  */
} scp144_cavity_t;

/* Derive the 32-byte SCP-144 144-dimensional Zeckendorf lattice binding from
 * the two peers' public 32-byte NodeIDs. */
void scp144_lattice_bind(const uint8_t init_id[32],
                         const uint8_t resp_id[32],
                         uint8_t out[32]);

/* The canonical Fibonacci ladder F1..F12 (1, 1, 2, 3, 5, 8, 13, 21, 34, 55,
 * 89, 144): the 12 macro-plate gaps of the cavity array. */
const uint16_t *scp144_fib_ladder(void);

/* The unique non-consecutive Zeckendorf support mask (bits 0..11) and the
 * discrete Casimir mode energy of a coordinate value v in 0..144. The same
 * function the lattice binding and the embedded 144D matrix are built from. */
uint16_t scp144_zeckendorf_mode(uint32_t v, uint32_t *casimir_energy);

/* Initialize and energize a recursive 12x12 Casimir Cavity hardware device. */
void scp144_cavity_init(scp144_cavity_t *dev, uint32_t device_id,
                        const uint8_t init_id[32], const uint8_t resp_id[32]);

/* Clock the Casimir Cavity hardware through one AC half-cycle (+1 <-> -1),
 * recursively stepping both the 12 macro-plates and their 12 micro-cavities. */
void scp144_cavity_clock_tick(scp144_cavity_t *dev);

/* Bijective 12-rung AC Fibonacci contraction (144 -> 1 throat) and expansion
 * (1 throat -> 144) in place over `buf[0..len)`. */
void scp144_tunnel_contract(uint8_t *buf, uint32_t len,
                            const uint8_t throat_key[32], uint64_t ac_cycle);

void scp144_tunnel_expand(uint8_t *buf, uint32_t len,
                          const uint8_t throat_key[32], uint64_t ac_cycle);

/* Seal one part/shard/frame through the SCP-144 AC Casimir Wormhole Tunnel. */
int32_t scp144_part_seal(const uint8_t key[32], const uint8_t root_digest[32],
                         uint32_t part_index, uint64_t ac_cycle,
                         const uint8_t *pt, uint32_t pt_len,
                         uint8_t *out, uint32_t out_cap);

/* Open and verify a sealed SCP-144 part, expanding from the throat (1 -> 144). */
int32_t scp144_part_open(const uint8_t key[32], const uint8_t root_digest[32],
                         uint32_t expected_part_index, uint64_t *ac_cycle_out,
                         const uint8_t *in, uint32_t in_len,
                         uint8_t *out, uint32_t out_cap);

/* Syntax classes for hash / CID dimensional identification */
enum {
    SCP144_SYN_RAW256    = 0, /* 32-byte raw SHA-256 / Keccak-256 digest      */
    SCP144_SYN_CIDV0_B58 = 1, /* Base58btc "Qm..." mixed-case syntax          */
    SCP144_SYN_CIDV1_B32 = 2, /* Base32 "bafy..."/"bafk..." lowercase syntax  */
    SCP144_SYN_HEX_LOWER = 3, /* 0-9 + a-f (10 digits + 6 lowercase letters)  */
    SCP144_SYN_HEX_UPPER = 4, /* 0-9 + A-F (10 digits + 6 uppercase letters)  */
    SCP144_SYN_ETH_ADDR  = 5, /* 20-byte / 40-hex Keccak-256 fiber projection */
};

/* Paraconsistent Zero-One Bridge states (matches lpres_state_t 0..3) */
enum {
    SCP144_BRIDGE_NEITHER = 0, /* Uncollapsed fiber (2^96 preimage bundle)   */
    SCP144_BRIDGE_TRUE    = 1, /* On-curve / resonant lattice generator (G)  */
    SCP144_BRIDGE_FALSE   = 2, /* Off-curve affine zero (0,0): 0 != 7 mod p  */
    SCP144_BRIDGE_BOTH    = 3, /* Projective Zero-One Bridge [0 : 1 : 0]     */
};

/* 144-Dimensional Object-Identification Vector unfolded from any hash/CID. */
typedef struct scp144_id_vec {
    uint8_t  syntax_class;              /* SCP144_SYN_*                       */
    uint8_t  bridge_state;              /* SCP144_BRIDGE_* (0..3)             */
    uint8_t  fiber_exponent;            /* Log2 of preimage fiber (e.g. 96)   */
    uint8_t  cassini_parity;            /* +1 or -1 alternating parity        */
    uint16_t zeck_path[SCP144_RUNGS];   /* 12-rung non-consecutive Zeckendorf */
    uint8_t  unfolded_144[SCP144_DIM];  /* 144D coordinates expanded from flat*/
    uint8_t  projective_xyz[3];         /* [X : Y : Z] = [0 : 1 : 0] at bridge*/
    uint8_t  canonical_digest[32];      /* Normalized 32-byte digest          */
} scp144_id_vec_t;

/* Unfold any flat hash/CID/address (and its ASCII/binary syntax) into its
 * 144-dimensional Zeckendorf identification vector and Zero-One Bridge state. */
int scp144_identify_hash(const uint8_t *hash_or_text, uint32_t len,
                         scp144_id_vec_t *out);

#endif /* ZXV_SCP144_H */

