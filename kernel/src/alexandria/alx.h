/* Copyright (c) 2024-2026 Michael Laurence Curzi and 36N9 Genetics, LLC */
/* SPDX-License-Identifier: Apache-2.0 */
/* alx.h — the Library of Alexandria: an index of verified building blocks,
 * the booklets that hold them, and three small languages over them
 * (docs/ALEXANDRIA.md).
 *
 * A SLOT is a fixed u32 that names one block entry for good. A block entry
 * also has a PLACE in the Alexandria classification, "CCC.II": class,
 * division and section digits, then an item ("512.01" is the greatest
 * common divisor). Each of its languages has a three-digit LANGUAGE number:
 * 1..99 are Lingua's own (zl_langs.def), 101..199 the other runtimes and
 * targets (c 101, sutra 102, eve 103, kron 104, ... zlir 120, x86-64
 * assembly 121, wire 122, then in the order they arrive). So
 * "512.01-001" is gcd in FORTRAN.
 *
 * Every division's blocks are packed into one BOOKLET: a 64-byte header
 * ("ALXB", version 1, the division, the file count and length) and then
 * each manifest and logic file at a 64-byte boundary. A booklet is at most
 * 1 MiB, one IPFS block, so its CID is the CIDv1 raw sha2-256 of its bytes;
 * the index names it once and gives offsets into it.
 *
 * ALEXANDRIA.TBL (tools/alexandria/build_index.py), big-endian:
 *   header, 32 bytes: "ALXT", version 1, three zero bytes, u32 slots,
 *     u32 booklets, u32 targets, u32 base end (0x10000), 8 zero bytes
 *   booklets, 48 bytes each, divisions rising:
 *     0 u16 division (100..990, a multiple of 10)  2 u16 zero
 *     4 u32 length (64..1 MiB, a multiple of 64)   8 u32 zero
 *     12 CID, 36 bytes (01 55 12 20, then the SHA-256 of the booklet)
 *   slots, 144 bytes each, entry i holding slot i + 1:
 *     0 u32 slot   4 u8 answer kind (1 num, 2 truth)   5 u8 inputs
 *     6 u8 flags (1 retired, 2 witnessed)   7 u8 languages
 *     8 u16 major, minor, patch, then u16 zero
 *     16 shorthand, 32 bytes NUL-padded ("num.gcd")
 *     48 block.entry, 44 bytes NUL-padded ("number_classics.gcd")
 *     92 u16 class-division-section (512)   94 u16 item (1..99)
 *     96 u16 booklet index   98 u16 zero   100 u32 first target
 *     104 u32 manifest offset   108 u32 manifest length
 *     112 the manifest's SHA-256 (its content id, zxapp.h C3)
 *     A retired slot keeps its slot, names and place; booklet 0xFFFF and
 *     every other field zero.
 *   targets, 16 bytes each, in slot order, languages rising within a slot:
 *     0 u32 slot   4 u16 language   6 u16 zero   8 u32 offset   12 u32 length
 *   SHA-256 of everything above, 32 bytes
 * alx_open checks every byte of that and refuses the index otherwise.
 *
 * Freestanding C11: integers only, no allocation, no libc; uses sha256(). */
#ifndef ZXV_ALX_H
#define ZXV_ALX_H

#include <stdbool.h>
#include <stdint.h>

#define ALX_HEAD      32u
#define ALX_BOOK      48u
#define ALX_SLOTREC   144u
#define ALX_TARGET    16u
#define ALX_BASE_END  0x10000u
#define ALX_MAX       4096u /* slots a version-1 index may hold */
#define ALX_MAX_BOOKS 90u   /* one per division */
#define ALX_BOOK_MAX  1048576u
#define ALX_ALIGN     64u

enum {
    ALX_OK = 0,
    ALX_E_FORM = -1,     /* not a well-formed version-1 index or booklet        */
    ALX_E_HASH = -2,     /* bytes do not match their hash or CID                */
    ALX_E_NAME = -3,     /* no slot has that name or place                      */
    ALX_E_SLOT = -4,     /* no such slot                                        */
    ALX_E_RETIRED = -5,  /* the slot was withdrawn                              */
    ALX_E_DOCK = -6,     /* an answer cannot feed that block                    */
    ALX_E_LIMIT = -7,    /* an output buffer or a fixed bound is too small      */
    ALX_E_LANG = -8,     /* the block has no logic in that language             */
    ALX_E_SYNTAX = -9,   /* TECTON or AXION text that does not parse            */
    ALX_E_LINEAGE = -10, /* an addendum whose parent is not in the active lineage */
    ALX_E_OVERLAY = -11, /* an addendum in the lineage that is not mounted here  */
    ALX_E_DOMAIN = -12   /* a grid word in a domain that holds nothing yet       */
};

enum { ALX_NUM = 1, ALX_TRUTH = 2 };
enum { ALX_F_RETIRED = 1, ALX_F_WITNESSED = 2 };

typedef struct {
    const uint8_t *tbl;
    uint32_t slots, books, targets;
    const uint8_t (*roots)[36]; /* the CIDs addenda may extend (alx_set_roots) */
    uint32_t nroots;
} alx_t;

typedef struct {
    uint32_t slot;
    uint8_t kind, inputs, flags, languages;
    uint16_t major, minor, patch;
    uint16_t place, item; /* 512, 1: "512.01" */
    uint16_t booklet;     /* index into the booklets; 0xFFFF when retired */
    uint32_t first_target, manifest_off, manifest_len;
    uint8_t content[32]; /* SHA-256 of the manifest */
    char shorthand[33];  /* NUL-terminated */
    char target[45];     /* "block.entry" */
} alx_entry_t;

typedef struct {
    uint16_t division;
    uint32_t length;
    uint8_t cid[36];
} alx_booklet_t;

/* ---- the index (alx.c) ---- */

/* Check an index; t keeps pointing into tbl, which must outlive it. */
int alx_open(alx_t *t, const uint8_t *tbl, uint32_t len);
/* Slot to entry, by position. A retired slot is returned with its flag set. */
int alx_get(const alx_t *t, uint32_t slot, alx_entry_t *e);
int alx_booklet(const alx_t *t, uint32_t index, alx_booklet_t *b);
/* Where a slot's logic in one language lies in its booklet. */
int alx_target(const alx_t *t, uint32_t slot, uint32_t language, uint32_t *off, uint32_t *len);
/* Whether these bytes are booklet `index`: ALX_OK, ALX_E_HASH or ALX_E_FORM. */
int alx_booklet_check(const alx_t *t, uint32_t index, const uint8_t *b, uint32_t len);
/* "@num.gcd": the length written (no NUL), or a status. */
int32_t alx_shorthand(const alx_t *t, uint32_t slot, char *out, uint32_t cap);
/* Whether block a's answer can feed block b: b takes exactly one input. */
int alx_dock(const alx_t *t, uint32_t a, uint32_t b);
/* A pipeline of n slots: ALX_OK, or the status at the first link that
 * fails, with *at set to its position. */
int alx_pipeline(const alx_t *t, const uint32_t *slots, uint32_t n, uint32_t *at);

/* ---- references: LEXIS's resolution step (alx_ref.c) ----
 *
 * A reference names a block, optionally in one language:
 *   @num.gcd   num.gcd   @S:0001   @512.01   512.01
 * each optionally followed by "-NNN" or ":NNN", a language number. A
 * community addendum is named by the CID it extends, then a place followed
 * by the addendum's own parts (extension, revision, fork marks: 1 to 8 of
 * a-z and 0-9 each), then its language:
 *   <parent CID>/512.01.42.01:001
 * An addendum is refused with LINEAGE unless its parent CID is one of the
 * active roots, and with OVERLAY while no addendum is mounted, which in
 * version 1 is always. */
typedef struct {
    uint32_t slot;     /* the base slot, or 0 for an addendum                */
    uint32_t language; /* 0: any                                             */
    uint8_t parent[36];
    char path[48]; /* an addendum's place and parts, NUL-terminated      */
} alx_ref_t;

int alx_resolve(const alx_t *t, const char *s, uint32_t n, alx_ref_t *r);
/* The slot a name or place gives (> 0), or a status; no language allowed. */
int32_t alx_find(const alx_t *t, const char *s, uint32_t n);
/* A CIDv1 "b..." string (base32, sha2-256, raw or dag-pb) to its 36 bytes. */
int alx_cid_parse(const char *s, uint32_t n, uint8_t out[36]);
/* The roots an addendum may extend: the sealed base's CIDs. Kept by
 * pointer, so they must outlive t. alx_open clears them. */
void alx_set_roots(alx_t *t, const uint8_t (*roots)[36], uint32_t n);

/* ---- TECTON: composing blocks (alx_tecton.c) ----
 *
 *   BIND telemetry {
 *       SOURCE : @num.gcd
 *       PIPE   : @931.01-104
 *       SINK   : @swarm.busiest
 *   }
 *
 * One SOURCE, then any PIPEs, then at most one SINK, each a reference.
 * Every link must dock and every language asked for must exist. Comments
 * run from a slash-star to a star-slash, as in C. The name is lower case,
 * digits, "_" and "."; at most 31 bytes. */
#define ALX_TECTON_MAX 16u
typedef struct {
    char name[32];
    uint32_t n;
    uint32_t slot[ALX_TECTON_MAX];
    uint32_t language[ALX_TECTON_MAX]; /* 0: any of the block's languages */
} alx_plan_t;

/* ALX_OK, or a status with *at set to the byte where checking stopped. */
int alx_tecton(const alx_t *t, const char *s, uint32_t n, alx_plan_t *p, uint32_t *at);

/* ---- AXION: loop-free truth checks (alx_axion.c) ----
 *
 * A program is a list of words, run once from first to last: there are no
 * jumps, so it takes exactly as many steps as it has words. Values are
 * integers (readings) and truths (0 NEITHER, 1 TRUE, 2 FALSE, 3 BOTH):
 *   in K        push reading K (a missing reading is pushed as missing)
 *   int V       push the integer V
 *   true false both neither   push a truth
 *   lt le eq    pop b, a: compare a with b; a missing reading gives NEITHER
 *   not and or  the paraconsistent gates, the same tables as lpres
 *               (and is the least, or the greatest, on FALSE < NEITHER <
 *               BOTH < TRUE)
 *   join        the witness join: both sides' evidence together
 *               (TRUE join FALSE is BOTH, NEITHER adds nothing)
 * A program must end with exactly one truth on the stack. The caller acts
 * on TRUE or FALSE; BOTH and NEITHER take its fallback path. */
#define ALX_AXION_WORDS 256u
#define ALX_AXION_STACK 32u
typedef struct {
    uint8_t op[ALX_AXION_WORDS];
    int64_t arg[ALX_AXION_WORDS];
    uint32_t n;
} alx_axion_t;

int alx_axion_parse(const char *s, uint32_t n, alx_axion_t *p, uint32_t *at);
/* The truth (0..3), or a status. Reading i is missing when bit i of
 * `present` is clear. */
int alx_axion_run(const alx_axion_t *p, const int64_t *readings, uint32_t nreadings,
                  uint64_t present);

/* ---- the grid: 64-bit coordinate words (alx_grid.c) ----
 *
 * A grid word names one stored thing by number, for code that dispatches
 * on a fixed-width key. It is a key into the sealed index, never an
 * address and never code: resolving it gives where verified bytes lie in a
 * booklet, and running them still goes through zxapp's checked call.
 *   bits 63..56  domain    ALX_G_CODE 0x10 (the only one that resolves);
 *                          0x20 data, 0x50 sensor streams, 0x60
 *                          embeddings, 0x70 attestations are reserved
 *   bits 55..40  place     100..999 ("512")
 *   bits 39..32  item      1..99 ("01")
 *   bits 31..16  extension 0 for the sealed base; else an addendum
 *   bits 15..8   format    ALX_G_LOGIC 0 or ALX_G_MANIFEST 1
 *   bits 7..0    language  1..120 for logic; 0 for the manifest
 * docs/spec/GRID_CHAIN.md. */
enum {
    ALX_G_CODE = 0x10,
    ALX_G_DATA = 0x20,
    ALX_G_SENSOR = 0x50,
    ALX_G_EMBED = 0x60,
    ALX_G_ATTEST = 0x70
};
enum { ALX_G_LOGIC = 0, ALX_G_MANIFEST = 1 };

/* The radix table: place, then item, to slot. Built once from an open
 * index; it points at t, which must outlive it. */
typedef struct {
    const alx_t *t;
    uint16_t first[900]; /* place - 100: first position in order */
    uint8_t count[900];  /* how many entries share that place */
    uint16_t order[ALX_MAX];
} alx_grid_t;

typedef struct {
    uint32_t slot, language, format;
    uint32_t booklet;  /* index into the booklets */
    uint32_t off, len; /* the bytes inside that booklet */
} alx_grid_hit_t;

int alx_grid_build(alx_grid_t *g, const alx_t *t);
/* The word for a slot's logic in one language, or its manifest. */
int alx_grid_word(const alx_t *t, uint32_t slot, uint32_t format, uint32_t language,
                  uint64_t *word);
/* ALX_OK and *h, or: FORM (a field out of range), DOMAIN, OVERLAY (an
 * extension: addenda are not mounted), NAME (no entry at that place),
 * RETIRED, LANG. */
int alx_grid_resolve(const alx_grid_t *g, uint64_t word, alx_grid_hit_t *h);

/* ---- layer lineage (alx_grid.c) ----
 *
 * The release is stacked in layers, each named by the CID of its manifest
 * and each manifest naming its parent's CID: L0 hardware spec, L1 Disc 1
 * core, L2 swarm discs, L3 the library, L4 community overlays (an overlay
 * may sit on another overlay). A chain is given root first. Each manifest
 * is at most 1 MiB, so its CID is the CIDv1 raw sha2-256 of its bytes. */
typedef struct {
    uint8_t level; /* 0..4 */
    const uint8_t *manifest;
    uint32_t len;
    uint8_t parent[36]; /* the parent CID the manifest names; zero at L0 */
} alx_layer_t;

/* ALX_OK with the top layer's CID in leaf (may be NULL), or LINEAGE or FORM
 * with *at set to the first layer that fails. */
int alx_lineage(const alx_layer_t *l, uint32_t n, uint8_t leaf[36], uint32_t *at);

const char *alx_status_name(int s);

#endif
