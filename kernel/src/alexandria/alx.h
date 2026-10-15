/* Copyright (c) 2024-2026 Michael Laurence Curzi and 36N9 Genetics, LLC */
/* SPDX-License-Identifier: Apache-2.0 */
/* alx.h — the Library of Alexandria's index: shorthand slots for verified
 * building blocks (docs/ALEXANDRIA.md).
 *
 * A slot is a fixed u32 that names one block entry for good: "@num.gcd" is
 * slot 1, number_classics.gcd, whose manifest has one content id. People and
 * models write the shorthand ("@num.gcd" or "@S:0001"); code holds the
 * slot; the table turns either into the block, its version, its contract
 * and its CID with no hashing and no search beyond the name lookup.
 *
 * ALEXANDRIA.TBL (written by tools/alexandria/build_index.py), big-endian:
 *   header, 32 bytes
 *     0  "ALXT"            4  version (1), then 3 zero bytes
 *     8  u32 count        12  u32 entry size (128)   16  u32 base end (0x10000)
 *     20 12 zero bytes
 *   count entries, 128 bytes each, entry i holding slot i + 1
 *     0  u32 slot          4  u8 answer kind (1 num, 2 truth; 0 retired)
 *     5  u8 inputs         6  u8 flags (1 retired, 2 witnessed)   7 u8 languages
 *     8  u16 major, minor, patch, then u16 zero
 *     16 shorthand, 32 bytes, NUL-padded ("num.gcd")
 *     48 block.entry, 44 bytes, NUL-padded ("number_classics.gcd")
 *     92 CID, 36 bytes: CIDv1 raw sha2-256 of the block's manifest
 *   SHA-256 of everything above, 32 bytes
 * alx_open checks every byte of that and refuses the table otherwise.
 *
 * Composing: a block's answer can feed the next block when the next takes
 * exactly one input (alx_dock). Inputs are integers today; the Babel value
 * kinds will type them later. A retired slot never docks and never resolves
 * to another block.
 *
 * Freestanding C11: integers only, no allocation, no libc; uses sha256(). */
#ifndef ZXV_ALX_H
#define ZXV_ALX_H

#include <stdbool.h>
#include <stdint.h>

#define ALX_HEAD     32u
#define ALX_ENTRY    128u
#define ALX_BASE_END 0x10000u
#define ALX_MAX      4096u /* base slots a version-1 table may hold */

enum {
    ALX_OK = 0,
    ALX_E_FORM = -1,    /* the table is not a well-formed version-1 table */
    ALX_E_HASH = -2,    /* the table's bytes do not match its hash        */
    ALX_E_NAME = -3,    /* no slot has that shorthand                     */
    ALX_E_SLOT = -4,    /* no such slot                                   */
    ALX_E_RETIRED = -5, /* the slot was withdrawn                         */
    ALX_E_DOCK = -6,    /* the answer cannot feed that block              */
    ALX_E_LIMIT = -7    /* an output buffer is too small                  */
};

enum { ALX_NUM = 1, ALX_TRUTH = 2 };
enum { ALX_F_RETIRED = 1, ALX_F_WITNESSED = 2 };

typedef struct {
    const uint8_t *tbl;
    uint32_t count;
} alx_t;

typedef struct {
    uint32_t slot;
    uint8_t kind, inputs, flags, languages;
    uint16_t major, minor, patch;
    char shorthand[33]; /* NUL-terminated */
    char target[45];    /* "block.entry" */
    uint8_t cid[36];
} alx_entry_t;

/* Check a table; t keeps pointing into tbl, which must outlive it. */
int alx_open(alx_t *t, const uint8_t *tbl, uint32_t len);
/* Slot to entry, by position. A retired slot is returned with its flag set. */
int alx_get(const alx_t *t, uint32_t slot, alx_entry_t *e);
/* "@num.gcd", "num.gcd" or "@S:0001" to a slot (> 0), or a status. */
int32_t alx_find(const alx_t *t, const char *s, uint32_t n);
/* "@num.gcd": the length written (no NUL), or a status. */
int32_t alx_shorthand(const alx_t *t, uint32_t slot, char *out, uint32_t cap);
/* Whether block a's answer can feed block b: ALX_OK or a status. */
int alx_dock(const alx_t *t, uint32_t a, uint32_t b);
/* A pipeline of n slots: ALX_OK, or the status at the first link that fails,
 * with *at set to its position. */
int alx_pipeline(const alx_t *t, const uint32_t *slots, uint32_t n, uint32_t *at);

const char *alx_status_name(int s);

#endif
