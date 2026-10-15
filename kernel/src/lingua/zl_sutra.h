/* Copyright (c) 2024-2026 Michael Laurence Curzi and 36N9 Genetics, LLC */
/* SPDX-License-Identifier: Apache-2.0 */
/* zl_sutra.h — the Dhātu view of the Lingua IR (docs/spec/ALPHABET.md).
 *
 * Every Lingua IR operation (the alphabet of booklet 000) has a name in
 * Pāṇini's terms and a 64-bit sutra cell:
 *   dhātu      the verbal root naming the action (√grah take, √dhṛ hold, √gam go)
 *   upasarga   one of the 22 Pāṇinian prefixes, directing the root
 *   pratyaya   the suffix: which variant (slot or frame, floor or truncate)
 *   kāraka     the six roles and where each operand comes from
 *   belnap     the four-valued evidence tag (lpres: N, T, F, B)
 * The view is a naming of the IR, one cell per operation and one operation
 * per cell (tested both ways). It adds no operation and changes no
 * semantics: what an operation does is still exactly what zl_run does.
 *
 * Cell layout, low bit first (shifts and masks only, never bitfields, so
 * the layout is the same on every compiler and platform):
 *   bits  0..11  dhātu      (12)  the root (zl_sutra_root); 0 none, 1..4095
 *   bits 12..16  upasarga   (5)   0 none, 1..22 (zl_sutra_upasarga)
 *   bits 17..24  pratyaya   (8)   the variant (zl_sutra_pratyaya)
 *   bits 25..48  kāraka     (24)  six 4-bit operand codes, role r at 25 + 4r
 *   bits 49..59  sandhi     (11)  how two cells join; 0 (reserved) for now
 *   bits 60..61  belnap     (2)   lpres state: 0 N, 1 T, 2 F, 3 B
 *   bits 62..63  svara      (2)   0 (reserved) for now
 *
 * Freestanding C11: integers only, no allocation. */
#ifndef ZXV_ZL_SUTRA_H
#define ZXV_ZL_SUTRA_H

#include <stdint.h>

/* the six kāraka roles, in cell order */
enum {
    ZL_KARTR = 0,      /* agent: who acts */
    ZL_KARMAN = 1,     /* object: what the action reaches or makes */
    ZL_KARANA = 2,     /* instrument: what it acts with */
    ZL_SAMPRADANA = 3, /* recipient: who receives */
    ZL_APADANA = 4,    /* source: what it comes from */
    ZL_ADHIKARANA = 5  /* locus: where */
};

/* operand codes in a kāraka slot */
enum {
    ZL_K_NONE = 0,  /* the role is absent */
    ZL_K_S1 = 1,    /* the top of the stack */
    ZL_K_S2 = 2,    /* the second */
    ZL_K_S3 = 3,    /* the third */
    ZL_K_SN = 4,    /* stack cells, how many depends on the operands */
    ZL_K_TOP = 5,   /* the new top of the stack (a result) */
    ZL_K_CONST = 6, /* the instruction's constant */
    ZL_K_SLOT = 7,  /* a named slot */
    ZL_K_ARRAY = 8, /* an array cell */
    ZL_K_ADDR = 9,  /* a cell through an address */
    ZL_K_FRAME = 10,/* a frame cell */
    ZL_K_CODE = 11, /* a place in the program */
    ZL_K_IN = 12,   /* the input */
    ZL_K_OUT = 13,  /* the output */
    ZL_K_HEAP = 14, /* a pair, vector or string */
    ZL_K_HANDLER = 15 /* an exception handler */
};

#define ZL_SUTRA_UPASARGAS 22u

typedef struct {
    uint32_t dhatu, upasarga, pratyaya, sandhi, belnap, svara;
    uint8_t karaka[6];
} zl_sutra_t;

/* pack and unpack; pack returns 0 for a field out of range */
uint64_t zl_sutra_pack(const zl_sutra_t *s);
void zl_sutra_unpack(uint64_t cell, zl_sutra_t *s);

/* The canonical cell of IR operation op with an evidence tag (0..3), or 0
 * for an unknown op. */
uint64_t zl_sutra_cell(uint32_t op, uint32_t belnap);
/* The IR operation a cell names, or -1: the dhātu, upasarga, pratyaya and
 * kāraka must be exactly one operation's; sandhi and svara must be 0. The
 * belnap tag is carried, not part of the operation. */
int32_t zl_sutra_op(uint64_t cell);

/* names, for documents and tools (IAST, ASCII-safe where noted) */
const char *zl_sutra_root(uint32_t op);     /* the root, e.g. "grah" */
const char *zl_sutra_gloss(uint32_t op);    /* its sense, e.g. "take" */
const char *zl_sutra_upasarga(uint32_t u);  /* "pra", ...; "" for 0 */
const char *zl_sutra_pratyaya(uint32_t p);  /* the variant's name; "" for 0 */
char zl_sutra_kchar(uint32_t code);         /* one letter per operand code */
uint32_t zl_sutra_count(void);              /* the number of operations */

#endif
