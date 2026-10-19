/* Copyright (c) 2024-2026 Michael Laurence Curzi and 36N9 Genetics, LLC */
/* SPDX-License-Identifier: Apache-2.0 */
/* tvl_rom.h — TOL VOVINA UPAAH LOT: the ROM container's parser / validator.
 *
 * WHAT THIS IS, AND WHAT IT DELIBERATELY IS NOT
 * ---------------------------------------------
 * This is the reader for ONE role payload of a TVUL world (see
 * PROVENANCE/TVUL_ROM_FORMAT.md). It invents no storage format: the OUTER layer
 * is a zxvfs_tri triad (<world>.zxvc / .cedez / .cedec / .tri, descriptor
 * written last) and the INNER codec is dimfold — whose channel polarities
 * already mirror trispace.h's tri_role_t exactly. What this file adds is the
 * one thing neither of those provides: a SECTION DIRECTORY over the payload,
 * and the derivation of the working set that directory implies.
 *
 * It is not a loader. Nothing here allocates, decompresses, renders, or boots a
 * world. It reads a byte image that is already in memory, refuses it if it is
 * not exactly well-formed, and answers two questions about it: what is in it,
 * and how much RAM does its content imply.
 *
 * THE RAM REQUIREMENT IS DERIVED, NEVER DECLARED
 * ----------------------------------------------
 * There is no ram_bytes field in this header and there never will be. The ROM
 * is the hard instruction set; RAM is scratch that the instruction set implies.
 * A declared RAM number is a SECOND instruction set that can silently disagree
 * with the first — unverifiable at load (a loader can check a digest, it cannot
 * check a promise), stale on the next authoring edit, and, for a hostile ROM,
 * an under-declaration that makes the loader under-allocate and the streamer
 * overrun. Derivation is fail-closed; a declaration is trust-by-default.
 *
 * So every quantity this file works with is RECOVERED FROM CONTENT:
 *   - a section's stored extent is cross-checked against the image bounds and
 *     against a SHA-256d digest of the very bytes it names;
 *   - a section's RESIDENT (expanded) size is read out of the codec container's
 *     own leading length — dimfold_compress writes the original length as its
 *     first varint, and dimfold_elevate writes 'ZXVE' | identity[32] |
 *     varint(total). We never ask the author how big it expands to; we read it
 *     from the thing that will do the expanding, and dimfold_descend then
 *     re-verifies the reconstruction against its own SHA-256d before returning.
 *   - the maximum live set is a GRAPH property of the hoard, computed from the
 *     WORLD section's adjacency in one pass.
 * The header's only nod to authoring is `iws_witness`, which is a WITNESS and
 * not an authority: the loader always uses its own derived number, and a
 * mismatch flags build-pipeline drift rather than overriding anything.
 *
 * FAIL CLOSED
 * -----------
 * Assume the image is hostile. Every predicate in this file starts at false and
 * has to be earned; there is no path that returns "valid" by falling off the
 * end of a check. Truncation, offsets outside the image, sections that overlap
 * each other or the header, and offset+size integer overflow are each rejected
 * explicitly and each has a dedicated rejection test in tvl_rom_selfcheck().
 *
 * Author: H.M. Michael-Laurence: Curzi (c)  (ZXV TVUL container slice)
 */
#ifndef ZXV_TVL_ROM_H
#define ZXV_TVL_ROM_H

#include <stdint.h>
#include <stdbool.h>

#include "trispace.h"      /* tri_role_t — the container's role vocabulary   */

/* ---- container constants ------------------------------------------------ */

#define TVL_MAGIC0 'T'
#define TVL_MAGIC1 'V'
#define TVL_MAGIC2 'U'
#define TVL_MAGIC3 'L'

#define TVL_VERSION      1u
#define TVL_HDR_SIZE     60u    /* fixed header, section table follows       */
#define TVL_SEC_SIZE     48u    /* one section-table entry                   */
#define TVL_MAX_SECTIONS 32u    /* directory ceiling (bounded, freestanding) */
#define TVL_WITNESS_LEN  32u    /* iws_witness digest length                 */
#define TVL_DIGEST_LEN   32u    /* per-section digest length                 */

/* The hoard (the world graph) lives inside the WORLD section. */
#define TVL_HOARD_MAGIC0 'H'
#define TVL_HOARD_MAGIC1 'O'
#define TVL_HOARD_MAGIC2 'R'
#define TVL_HOARD_MAGIC3 'D'
#define TVL_HOARD_HDR    12u    /* 'HORD' | hold_count | gate_count          */
#define TVL_HOLD_SIZE     8u
#define TVL_GATE_SIZE     8u
#define TVL_MAX_HOLDS   128u    /* holds per world (bounded)                 */
#define TVL_MAX_GATES   512u    /* gates per world (bounded)                 */
#define TVL_NO_SECTION 0xFFFFu  /* a hold with no streamed body              */

/* dimfold's block codec refuses next_pow2(n) > 1024, so a chunk exponent above
 * 10 names a chunk the codec would never accept. The bound is the CODEC's, read
 * off dimfold.c's own limit — not a number chosen here. */
#define TVL_CHUNK_LOG2_MIN 1u
#define TVL_CHUNK_LOG2_MAX 10u

/* ---- section vocabulary -------------------------------------------------- */

typedef enum {
    TVL_SEC_WORLD  = 0,   /* the hoard: holds (enterable places) + gates    */
    TVL_SEC_MECH   = 1,   /* the mechanic palette (game_universe vectors)   */
    TVL_SEC_SUTRA  = 2,   /* the story = the AI's instruction set           */
    TVL_SEC_ASSET  = 3,   /* geometry-oriented content                      */
    TVL_SEC_ORIENT = 4,   /* the E8/Z[phi] orientation frame                */
    TVL_SEC_BIND   = 5,   /* hold -> capability declarations                */
    TVL_SEC_KIND_COUNT
} tvl_sec_kind_t;

/* How a section's bytes are stored. Each form's RESIDENT size is recoverable
 * from the stored bytes themselves — that is the whole point of the list. */
typedef enum {
    TVL_FORM_RAW  = 0,    /* verbatim; resident == stored                   */
    TVL_FORM_FOLD = 1,    /* dimfold_compress; resident = leading varint    */
    TVL_FORM_ELEV = 2,    /* dimfold_elevate;  resident = container total   */
    TVL_FORM_PACK = 3,    /* dimfold_pack;     resident = sum of channels   */
    TVL_FORM_COUNT
} tvl_form_t;

typedef enum {
    TVL_RES_PERSISTENT = 0,  /* resident for the whole session (W_core)     */
    TVL_RES_STREAMED   = 1,  /* paged by the hold frontier    (W_stream)    */
    TVL_RES_COUNT
} tvl_residency_t;

/* ---- parsed views -------------------------------------------------------- */

/* One directory entry, as parsed. `stored`/`offset` are the on-image extent;
 * `resident` is RECOVERED from the section's own codec container, never read
 * from an author-supplied field. */
typedef struct {
    uint16_t kind;        /* tvl_sec_kind_t                                 */
    uint8_t  form;        /* tvl_form_t                                     */
    uint8_t  residency;   /* tvl_residency_t                                */
    uint16_t channel;     /* index into the enclosing dimfold_pack manifest */
    uint32_t offset;      /* byte offset of the stored bytes in the image   */
    uint32_t stored;      /* length of those stored bytes                   */
    uint32_t resident;    /* DERIVED expanded size                          */
    uint8_t  digest[TVL_DIGEST_LEN];   /* SHA-256d over the STORED bytes    */
} tvl_section_t;

/* A hold IS an application: an enterable place in the continuous world. Its
 * address is a Morton code at the hoard's level, so the loader's resident
 * frontier is a prefix test (zo_contains) rather than a distance search. */
typedef struct {
    uint32_t morton;      /* zo_encode2(x,y)                                */
    uint8_t  level;       /* zo_addr_t.level                                */
    uint8_t  role;        /* tri_role_t: protrudes / recedes / screen plane */
    uint16_t section;     /* section holding this hold's body, or TVL_NO_SECTION */
} tvl_hold_t;

/* A gate is a directed edge. `visible_only` is a hold you can SEE but not
 * enter — which is exactly what an S0 (TRI_NEUTRAL) hold renders as. */
typedef struct {
    uint16_t from;
    uint16_t to;
    uint16_t cost;
    uint8_t  visible_only;
} tvl_gate_t;

/* The parsed container. `valid` is the only thing callers should branch on and
 * it is false until every check has passed. */
typedef struct {
    const uint8_t *image;
    uint32_t       image_len;

    uint16_t version;
    uint16_t role;                 /* tri_role_t of the enclosing triad file */
    uint32_t section_count;
    uint32_t orient_root;          /* index into e8_roots(): the world frame */
    uint32_t hoard_level;          /* zo_addr_t level of the hold space      */
    uint32_t chunk_log2;           /* dimfold chunking exponent              */
    uint8_t  iws_witness[TVL_WITNESS_LEN];

    tvl_section_t sec[TVL_MAX_SECTIONS];

    uint32_t world_index;          /* which section is the WORLD section     */
    uint32_t hold_count;
    uint32_t gate_count;
    uint32_t hold_off;             /* image offset of the hold array         */
    uint32_t gate_off;             /* image offset of the gate array         */

    bool     valid;
} tvl_rom_t;

/* Why a parse was refused. Reported so a refusal names the term that broke it
 * rather than collapsing every failure into "bad ROM". */
typedef enum {
    TVL_OK = 0,
    TVL_E_NULL,            /* no image / zero length                        */
    TVL_E_SHORT,           /* image shorter than its own header + table     */
    TVL_E_MAGIC,
    TVL_E_VERSION,
    TVL_E_ROLE,
    TVL_E_COUNT,           /* section_count 0 or above TVL_MAX_SECTIONS     */
    TVL_E_SEAL,            /* header seal mismatch                          */
    TVL_E_FIELD,           /* a header field outside its derived bound      */
    TVL_E_SEC_FIELD,       /* a section field outside its enum / reserved   */
    TVL_E_OVERFLOW,        /* offset + size wrapped                         */
    TVL_E_BOUNDS,          /* a section extent leaves the image             */
    TVL_E_OVERLAP,         /* two sections overlap, or one hits the header  */
    TVL_E_DIGEST,          /* stored bytes do not match the entry's digest  */
    TVL_E_WORLD,           /* WORLD section missing / duplicated / wrong form */
    TVL_E_HOARD,           /* the hoard graph is malformed                  */
    TVL_E_RESIDENT,        /* a codec container's own length is unreadable  */
    TVL_E_REASON_COUNT
} tvl_error_t;

/* ---- parse / validate ---------------------------------------------------- */

/* Parse and FULLY validate `len` bytes at `img` as one TVUL role payload.
 * `expect_role` cross-checks the header against the triad file it came out of;
 * pass a value >= TRI_NEUTRAL+1 to skip that one cross-check.
 * Returns TVL_OK and sets out->valid only if every check passed. On any other
 * return out->valid is false and nothing else in *out may be relied upon.
 * The image is BORROWED, not copied: it must outlive *out. */
tvl_error_t tvl_rom_parse(const uint8_t *img, uint32_t len,
                          uint16_t expect_role, tvl_rom_t *out);

/* Read hold / gate `i` out of the validated hoard. Returns false out of range
 * or if the ROM did not validate. */
bool tvl_rom_hold(const tvl_rom_t *r, uint32_t i, tvl_hold_t *out);
bool tvl_rom_gate(const tvl_rom_t *r, uint32_t i, tvl_gate_t *out);

/* ---- the DERIVED working set --------------------------------------------- */

/* The terms of the implied working set, kept separate so a refusal can name the
 * term that broke it, and so no runtime term is ever attributed to the ROM. */
typedef struct {
    uint32_t core;        /* sum of resident sizes, residency == PERSISTENT  */
    uint32_t stream;      /* MAX over holds of (r(v) + sum r(u) for u in gates(v)) */
    uint32_t work;        /* one decompression chunk + its worst-case coding */
    uint32_t frame;       /* display_w * display_h * bpp * buffers — ASKED   */
    uint32_t ai;          /* runtime expert set — a property of the RUNTIME  */
    uint32_t total;       /* the IWS                                         */
    bool     overflow;    /* a term or the sum did not fit; total is not usable */
} tvl_iws_t;

/* The two terms that are NOT functions of ROM content, supplied by the caller
 * from the hardware actually present. There are no defaults here on purpose:
 * a default display size is exactly the fixed value this project forbids. */
typedef struct {
    uint32_t display_w;   /* the display's REAL width, asked of the display  */
    uint32_t display_h;   /* the display's REAL height                       */
    uint32_t bytes_per_px;
    uint32_t buffers;     /* present + parallax. Complementary-CHANNEL stereo
                           * needs ONE ordinary framebuffer plus one parallax
                           * buffer — not two eyes. (The Virtual Boy's dual LED
                           * arrays and oscillating mirrors are a different
                           * mechanism; do not conflate them.) */
    uint32_t ai_bytes;    /* the inference runtime's expert set              */
} tvl_iws_env_t;

/* THE ROM-DERIVED WORKING SET, IN BYTES.
 *
 * Returns W_core + W_stream + W_work — every term COMPUTED from this image's
 * own content: expanded sizes recovered from each section's codec container,
 * the maximum live set computed from the hoard adjacency, and the decompression
 * scratch computed from the chunk exponent and dimfold_bound's real expansion.
 *
 * It is computed and not stated because a stated number cannot be checked at
 * the only moment that matters. A loader can verify a digest against bytes; it
 * cannot verify a promise about memory. See the header comment above for the
 * full argument. Returns 0 for a ROM that did not validate (fail closed: no
 * working set is claimed for an image we refused).
 *
 * This deliberately EXCLUDES W_frame and W_ai. Those are properties of the
 * hardware and the runtime, not of the cartridge, and attributing them to the
 * container is how a container starts making claims about a machine it has
 * never seen. Use tvl_rom_iws() for the whole figure. */
uint32_t tvl_rom_ram_required(const tvl_rom_t *r);

/* The full IWS = the ROM-derived terms plus the measured-hardware terms.
 * Returns false (and sets out->overflow) if any term or the sum did not fit,
 * which is a refusal, not a clamp. */
bool tvl_rom_iws(const tvl_rom_t *r, const tvl_iws_env_t *env, tvl_iws_t *out);

/* Compare the loader's own derived working set against the witness the
 * AUTHORING TOOL left in the header. The loader's number ALWAYS wins; a false
 * here means the ROM was edited by something that did not re-derive, i.e.
 * build-pipeline drift. It is the exact inverse of trusting a ram_bytes field:
 * the witness can flag a discrepancy, it can never grant one. */
bool tvl_rom_witness_agrees(const tvl_rom_t *r, const tvl_iws_env_t *env);

/* Compute the witness an authoring tool should write for this content+env.
 * Exposed so the tool and the loader share ONE derivation rather than two. */
void tvl_rom_witness_of(const tvl_rom_t *r, const tvl_iws_env_t *env,
                        uint8_t out[TVL_WITNESS_LEN]);

/* ---- registration through the EXISTING console registry ------------------ */

/* Register this world as ONE MR_KIND_GAME cartridge in the megarom registry.
 * ONE registration for the whole world, never one per hold: the registry is a
 * static 16-slot array (MEGAROM_MAX) with seven slots already consumed at boot,
 * so a thirty-hold town registering per hold would blow it six times over. The
 * hold table lives inside the container, where it belongs.
 * `name` and `tagline` are BORROWED by megarom_t (it stores the pointers), so
 * they must have static storage duration.
 * Returns the megarom slot index, or -1 if the ROM did not validate, the
 * registry is full, or a TVUL world is already registered. */
int  tvl_rom_register(const tvl_rom_t *r, const char *name, const char *tagline);

/* The ROM the registered cartridge boots, or NULL. */
const tvl_rom_t *tvl_rom_active(void);

/* On-target self-check: builds a small valid ROM in memory, verifies it parses
 * and that the derived RAM figure equals a hand-computed expectation, then
 * verifies that a battery of malformed ROMs is REFUSED — truncation, bad magic,
 * bad section count, offset+size overflow, out-of-image offsets, overlapping
 * sections, a tampered payload, a tampered header, a missing/duplicated/
 * wrong-form WORLD section, and a malformed hoard. Returns 1 on pass. */
int  tvl_rom_selfcheck(void);

/* ---- the world array: one profile per machine, sealed at install ----------
 *
 * The installer image carries the WHOLE array of MegaROMs — every profile a
 * machine might be configured as. Choosing "which one you need" happens once,
 * at configuration: the installer writes a BOOTSEL.TVL record that commits
 * the chosen world's image digest. The loader then refuses ANY cartridge
 * whose digest differs from the committed one, so a machine cannot casually
 * flip between server and desktop worlds by renaming a file — re-selecting
 * means re-sealing, which is the installer's act, not a boot-time toggle.
 *
 * The existing build_probe() stays byte-identical as TVL_WORLD_BOOT (the
 * embedded reference cartridge). The other profiles are authored by the same
 * spec-driven builder below, so every cartridge in the array shares one
 * derivation and the same fail-closed validator. */

typedef enum {
    TVL_WORLD_BOOT    = 0,  /* the canonical embedded reference cartridge    */
    TVL_WORLD_SERVER  = 1,  /* headless: mesh/ledger/storage/gateway holds   */
    TVL_WORLD_USER    = 2,  /* desktop: GUI world with screen-plane holds    */
    TVL_WORLD_FOUNDRY = 3,  /* dev toolchain: user world + workshop holds    */
    TVL_WORLD_COUNT
} tvl_world_id_t;

/* Author the named world profile into `b` (>= TVL_HDR_SIZE + 32*TVL_SEC_SIZE
 * + 1024 to be safe; the largest profile is well under that). Returns its
 * length, or 0 if `cap` is too small or the profile is unknown. TVL_WORLD_BOOT
 * returns byte-for-byte what tvl_rom_build_boot() returns. */
uint32_t tvl_rom_build_world(uint32_t world, uint8_t *b, uint32_t cap);

/* The stable cartridge file name for a profile ("MEGAROM-SERVER.TVL" etc.);
 * NULL for an unknown id. */
const char *tvl_rom_world_name(uint32_t world);

/* ---- the selection seal (BOOTSEL.TVL) ------------------------------------ */

#define TVL_SEL_MAGIC0 'B'
#define TVL_SEL_MAGIC1 'S'
#define TVL_SEL_MAGIC2 'E'
#define TVL_SEL_MAGIC3 'L'
#define TVL_SEL_VERSION 1u
#define TVL_SEL_HDR     12u   /* magic | version | world_id                  */
#define TVL_SEL_SEAL_LEN 4u
#define TVL_SEL_SIZE    (TVL_SEL_HDR + TVL_DIGEST_LEN + TVL_SEL_SEAL_LEN)

/* A committed selection: which world this machine boots, bound to that
 * cartridge image's SHA-256d so the loader can refuse a swapped cartridge.
 * The seal is the first 4 bytes of SHA-256d over bytes 0..(size-5); a
 * tampered world_id or digest fails the seal before any digest comparison. */
typedef struct {
    uint8_t  magic[4];
    uint16_t version;
    uint32_t world_id;
    uint8_t  rom_digest[TVL_DIGEST_LEN];
    uint8_t  seal[TVL_SEL_SEAL_LEN];
    bool     valid;
} tvl_sel_t;

/* Build a selection record committing `world` + the image digest of the
 * `rom_len` bytes at `rom`. Returns the record length (TVL_SEL_SIZE) or 0 on
 * a null/too-small buffer or an unknown world id. */
uint32_t tvl_sel_build(uint32_t world, const uint8_t *rom, uint32_t rom_len,
                       uint8_t *b, uint32_t cap);

/* Parse + fully validate a BOOTSEL.TVL record (magic, version, world range,
 * seal). Fail-closed: out->valid stays false unless every check passed. */
tvl_error_t tvl_sel_parse(const uint8_t *img, uint32_t len, tvl_sel_t *out);

/* Does this ROM satisfy the committed selection? Requires a VALID selection
 * and a byte-exact SHA-256d match against the record's rom_digest — the only
 * cartridge the loader may run. */
bool tvl_sel_admits(const tvl_sel_t *sel, const uint8_t *rom, uint32_t rom_len);

/* Author the canonical boot MegaROM image into `b` (>= a few hundred bytes;
 * pass TVL_HDR_SIZE + 32*TVL_SEC_SIZE + 512 to be safe). Returns its length,
 * or 0 if `cap` is too small. It is exactly what tvl_rom_parse accepts and is
 * identical on host and target. Same bytes as tvl_rom_build_world(TVL_WORLD_BOOT). */
uint32_t tvl_rom_build_boot(uint8_t *b, uint32_t cap);

/* Build the boot MegaROM into static storage, validate it, and register it in
 * the megarom console registry under its shipped name (MR_KIND_GAME). The
 * image and its name/tagline are static, so the registry's borrowed pointers
 * outlive the call. Returns the megarom slot index, or -1 if the build/parse
 * failed or a TVUL world is already registered. Idempotent: a second call
 * returns the same slot rather than taking a second one. State plainly what
 * this proves: the container is embedded and registered at boot; it is not yet
 * READ off the disc filesystem (the identical bytes are also on the ESP as
 * MEGAROM.TVL for that follow-on). */
int tvl_rom_register_boot(void);

#endif /* ZXV_TVL_ROM_H */
