/* dnx.h — duplex digital DNA: a strand that reads the same both ways.
 *
 * The audio-genomics build plan (audiogenomics_pro/digital_dna) maps binary
 * to bases two bits at a time (00 A, 01 C, 10 G, 11 T or U) and reads the
 * result in one direction only. dnx keeps that mapping and makes every strand
 * readable start to finish AND finish to start, with both reads checked and
 * joined in lpres.
 *
 * THE ONE FACT EVERYTHING RESTS ON
 * --------------------------------
 * Under 00 A, 01 C, 10 G, 11 T the Watson-Crick complement (A-T, C-G) is a
 * bitwise NOT of the two bits: complement(b) == b ^ 3. So the reverse
 * complement of a strand (the partner strand of a double helix, read in its
 * own 5'->3' direction) is "walk the bases backwards, flip both bits".
 * Reading backwards is therefore not a second format: it is the same reader
 * fed through that one transform.
 *
 * D1  CONVERSION. dnx_bytes_to_bases / dnx_bases_to_bytes, most significant
 *     pair first (0xE3 = 11 10 00 11 -> T G A T, the plan's example).
 *     Text form uses T (DNA) or U (RNA); both parse back to code 3.
 *
 * D2  METRICS (integers only): per-base counts, GC content in permille
 *     (floor), longest homopolymer run, start (ATG) and stop codons in frame
 *     0, and whether the strand is its own reverse complement.
 *
 * D3  DUPLEX FRAME. One frame F carries a payload:
 *         F = MAGIC(8) | LEN(16) | BODY(4*len) | CRC32(16)      (in bases)
 *     and the strand is
 *         W = F | PIVOT | revcomp(F)       PIVOT = GAATTC (EcoRI site)
 *     W is its own reverse complement, so the forward reader (index 0 up)
 *     and the reverse reader (index n-1 down, complemented) each see a whole
 *     copy of F. Each read checks magic, length against the strand, pivot
 *     and CRC on its own, and yields an lpres state:
 *         TRUE     the read verified
 *         FALSE    the strand claims a frame and this read failed a check
 *         NEITHER  the strand is too short to hold any frame
 *     The verdict is lpres_kjoin(forward, reverse): agreement keeps the
 *     value, one TRUE with one FALSE is BOTH. Two reads that each verify but
 *     carry different payloads (a spliced strand) are BOTH too. Only TRUE
 *     hands back a payload as THE payload; under BOTH the caller holds both
 *     candidates and decides nothing from them.
 *
 * D4  REVERSIBLE CODON PROGRAM. A strand of 3m bases is a program of m
 *     codons over four 32-bit registers. A codon c never equals its own
 *     reverse complement rc(c) (the middle base would have to be its own
 *     complement), so the 64 codons fall into 32 pairs {c, rc(c)}. Pair k is
 *     op k: the smaller codon runs f_k, the larger runs f_k's exact inverse.
 *     Reading the program finish to start, complemented, yields
 *     rc(c_m) ... rc(c_1), which is the inverse program, so for every
 *     program P and state x:
 *         run(revcomp(P), run(P, x)) == x
 *     dnx_prog_bidi checks a claimed (x, y) both ways: forward (run(P, x)
 *     == y) and backward (run(revcomp(P), y) == x), joined with kjoin. A
 *     correct machine never splits them; a split (BOTH) means the op table
 *     or the machine is broken and is reported, never resolved.
 *
 * D5  GENETIC SYMBOLS. dnx_codon_amino gives the standard genetic code's
 *     one-letter amino acid ('*' stop) for a codon, so a strand can be shown
 *     as protein letters as well as bases.
 *
 * Bounds: every writer takes a capacity and checks it before writing
 * anything (DNX_E_CAP). Payloads are capped at DNX_MAX_PAYLOAD bytes so no
 * length arithmetic can overflow 32 bits.
 *
 * Copyright (c) 2024-2026 Michael Laurence Curzi and 36N9 Genetics, LLC
 * SPDX-License-Identifier: Apache-2.0
 * Licensed under the Apache License, Version 2.0. See LICENSE at
 * the repository root.
 */

#ifndef DNX_H
#define DNX_H

#include <stdbool.h>
#include <stdint.h>

#include "lpres.h"

/* Base codes. complement(b) == b ^ DNX_COMP. */
#define DNX_A    0u
#define DNX_C    1u
#define DNX_G    2u
#define DNX_T    3u
#define DNX_COMP 3u

#define DNX_MAX_PAYLOAD (1u << 20) /* bytes; 4 MiB bases per frame half */
#define DNX_MAGIC_BASES 8u
#define DNX_LEN_BASES   16u /* 32-bit big-endian length */
#define DNX_CRC_BASES   16u /* CRC-32 (IEEE) of the payload */
#define DNX_PIVOT_BASES 6u
#define DNX_FRAME_FIXED (DNX_MAGIC_BASES + DNX_LEN_BASES + DNX_CRC_BASES)

/* Bases a duplex strand of a len-byte payload needs. */
#define DNX_DUPLEX_BASES(len) (2u * (DNX_FRAME_FIXED + 4u * (uint32_t) (len)) + DNX_PIVOT_BASES)

#define DNX_REGS 4u
#define DNX_OPS  32u

typedef enum {
    DNX_OK = 0,
    DNX_E_ARG = -1,   /* NULL pointer or a length the operation cannot take */
    DNX_E_CAP = -2,   /* output capacity too small; nothing was written */
    DNX_E_BASE = -3,  /* a base code above 3 or a character not in ACGTU */
    DNX_E_SHORT = -4, /* strand too short to hold a frame */
    DNX_E_MAGIC = -5,
    DNX_E_LEN = -6, /* length field disagrees with the strand */
    DNX_E_PIVOT = -7,
    DNX_E_CRC = -8,
    DNX_E_SPLIT = -9, /* both reads verified but carry different payloads */
} dnx_status_t;

/* ---- D1 conversion ---- */

/* 4 * n bases into bases[0 .. 4n). Returns bases written or DNX_E_*. */
int32_t dnx_bytes_to_bases(const uint8_t *in, uint32_t n, uint8_t *bases, uint32_t cap);
/* n must be a multiple of 4; n / 4 bytes out. */
int32_t dnx_bases_to_bytes(const uint8_t *bases, uint32_t n, uint8_t *out, uint32_t cap);
/* Reverse complement; out may equal in (done in place). */
int32_t dnx_revcomp(const uint8_t *in, uint32_t n, uint8_t *out, uint32_t cap);
/* Letters A C G and T (rna false) or U (rna true); no terminator written. */
int32_t dnx_to_text(const uint8_t *bases, uint32_t n, bool rna, char *out, uint32_t cap);
/* Parses ACGTU in either case; anything else is DNX_E_BASE. */
int32_t dnx_from_text(const char *text, uint32_t n, uint8_t *bases, uint32_t cap);

/* ---- D2 metrics ---- */

typedef struct {
    uint32_t bases;
    uint32_t count[4];     /* A C G T */
    uint32_t gc_permille;  /* floor(1000 * (C + G) / bases); 0 for empty */
    uint32_t longest_run;  /* longest run of one base */
    uint32_t start_codons; /* ATG in frame 0 */
    uint32_t stop_codons;  /* TAA TAG TGA in frame 0 */
    bool revcomp_palindrome;
} dnx_metrics_t;

int32_t dnx_metrics(const uint8_t *bases, uint32_t n, dnx_metrics_t *m);

/* ---- D3 duplex frame ---- */

typedef struct {
    lpres_state_t state;
    dnx_status_t status; /* DNX_OK when state is TRUE, else the first check failed */
    uint32_t len;        /* payload bytes this read found (0 if unreadable) */
    uint32_t crc;        /* CRC this read carried */
} dnx_read_t;

typedef struct {
    dnx_read_t fwd;
    dnx_read_t rev;
    bool payloads_equal; /* both verified and byte-identical */
    lpres_state_t state; /* kjoin(fwd, rev), forced to BOTH when both verify but differ */
} dnx_verdict_t;

/* Returns bases written (DNX_DUPLEX_BASES(len)) or DNX_E_*. */
int32_t dnx_duplex_encode(const uint8_t *payload, uint32_t len, uint8_t *strand, uint32_t cap);
/* Reads both ways. Each read's payload goes to fwd_out / rev_out (cap bytes
 * each) when it verifies. Returns DNX_OK when the verdict is TRUE, otherwise
 * the first failing read's status (DNX_E_SPLIT when two verified reads
 * differ). Either out may be NULL. DNX_E_CAP when a verified payload exceeds
 * cap: v is filled, nothing is written. */
int32_t dnx_duplex_decode(const uint8_t *strand, uint32_t n, uint8_t *fwd_out, uint8_t *rev_out,
                          uint32_t cap, dnx_verdict_t *v);

uint32_t dnx_crc32(const uint8_t *p, uint32_t n);

/* ---- D4 reversible codon program ---- */

typedef struct {
    uint32_t r[DNX_REGS];
} dnx_state_t;

/* Codon value 16*b0 + 4*b1 + b2 and its reverse complement. */
uint8_t dnx_codon_value(const uint8_t *three);
uint8_t dnx_codon_revcomp(uint8_t c);
/* Op entry for a codon: (k << 1) | inverse, k in 0..31. */
uint8_t dnx_codon_op(uint8_t c);
/* Runs n bases (n % 3 == 0) forward over s. */
int32_t dnx_prog_run(const uint8_t *bases, uint32_t n, dnx_state_t *s);
/* Runs the same strand read finish to start, complemented (the inverse). */
int32_t dnx_prog_run_reverse(const uint8_t *bases, uint32_t n, dnx_state_t *s);
/* Checks the claim run(P, *x) == *y both ways. Returns kjoin(fwd, back). */
lpres_state_t dnx_prog_bidi(const uint8_t *bases, uint32_t n, const dnx_state_t *x,
                            const dnx_state_t *y, lpres_state_t *fwd, lpres_state_t *back);

/* ---- D5 genetic symbols ---- */

char dnx_codon_amino(uint8_t c);

#endif /* DNX_H */
