/* Copyright (c) 2024-2026 Michael Laurence Curzi and 36N9 Genetics, LLC */
/* SPDX-License-Identifier: Apache-2.0 */
/* zxvs.h — ZXVS: the vector shape of any file's data.
 *
 * Every file has a shape, down to the data. ZXVS describes it as vectors,
 * so the shape can be drawn at any size, compared, indexed and filed:
 *   - numeric series (CSV/TSV columns, one number per line) become piecewise
 *     cubic Bezier curves under a stated integer error bound, with the
 *     measured max and RMS error;
 *   - structured data (JSON, XML, HTML, CSV/TSV headers, zxcfg, C source
 *     blocks) becomes a structure graph with an integer tree layout;
 *   - any bytes get an exact byte histogram, a windowed entropy curve (fitted
 *     like a series) and a Hilbert-curve layout of the file with the outlines
 *     of its regions (zero, text, high-entropy, other);
 *   - images and audio are referenced, by CID, to their ZXVG / ZXVA vector
 *     rendition (zui_vec.h / zxva.h), with that rendition's fidelity.
 * Every descriptor carries the source's CID (CIDv1 raw sha2-256), size,
 * kind, per-element fidelity and a 64-number Q16 shape vector.
 *
 * What the vector form buys, stated plainly: the shape SCALES CLEANLY. A
 * curve or outline redraws sharp at any size and the same descriptor serves
 * a thumbnail and a wall display. It does NOT add detail: a fitted curve is
 * within its stated bound of the samples and says nothing between or beyond
 * them, and the original bytes stay the reference, addressed by the CID
 * carried in every descriptor.
 *
 * Rules:
 *   S1 Integer only, freestanding, no allocation: the caller gives a work
 *      buffer (zxvs_work_size) and an output buffer (zxvs_out_bound).
 *   S2 Deterministic: same bytes, same config -> same descriptor everywhere.
 *   S3 zxvs_check fails closed: every read bounds checked, every count and
 *      value range checked, the descriptor must end exactly at END.
 *   S4 Fidelity is measured, never assumed: each curve records the max and
 *      RMS error of its fit against the samples it was fitted to; a curve's
 *      max error never exceeds its bound (the fitter splits until it holds).
 *
 * ---- ZXVS v1 -------------------------------------------------------------
 * uv = unsigned LEB128 varint (at most 10 bytes, value < 2^64); zz = zigzag
 * signed varint. All integers little endian where fixed width.
 *
 *   file    := "ZXVS" u8 version(=1) u8 kind u8 fmt u8 flags(=0)
 *              cid[36]            01 55 12 20 || SHA-256 of the source bytes
 *              uv src_bytes       < 2^32
 *              uv shape[64]       each 0..65536 (Q16), each written in
 *                                 exactly 3 bytes (so it can be patched)
 *              section*  u8 END(=0)        and nothing after END
 *   section := u8 tag  u32 len (little endian)  body[len]  (the body must
 *              parse to exactly len bytes)
 *              tags appear in increasing order; CURVE may repeat (at most
 *              ZXVS_MAX_CURVES), every other tag at most once.
 *
 *   kind: 1 BYTES, 2 SERIES, 3 STRUCT, 4 MEDIA (ZXVS_K_*). fmt: ZXVS_FMT_*.
 *   Every kind has HIST, ENTROPY and HILBERT; SERIES adds CURVE (and STRUCT
 *   for a table's columns); STRUCT adds STRUCT; MEDIA adds REF.
 *
 *   HIST (1):    uv count[256]; they sum to src_bytes (exact).
 *   ENTROPY (2): uv win (bytes per window, >= 1), curvebody of the windows'
 *                Shannon entropy in 1/256 bit (0..2048) against window index.
 *   HILBERT (3): u8 order k (0..6), uv bpc (bytes per cell),
 *                uv nrun, nrun x {u8 class (0..3), uv run >= 1}: the classes
 *                of cells 0 .. ceil(src_bytes / bpc) - 1 in Hilbert order on
 *                a 2^k x 2^k grid (class 0 zero bytes >= 90 %, 1 text >= 90 %,
 *                2 entropy >= 7 bits/byte, 3 other); the runs sum to the
 *                cell count, which is <= 4^k;
 *                uv nedge, nedge x {u8 dir (0 horizontal, 1 vertical), uv x,
 *                uv y, uv len >= 1}: the outline, every lattice segment where
 *                two neighbouring cells (or a cell and the outside) differ in
 *                class, merged into maximal runs; horizontal ones first, by
 *                y then x, then vertical ones by x then y. Lattice 0..2^k.
 *   CURVE (4):   u8 column, curvebody.
 *   STRUCT (5):  u8 grammar (a ZXVS_FMT_*), uv total (nodes in the source),
 *                uv nkept (<= ZXVS_MAX_NODES), then per kept node in
 *                pre-order: uv up (index - parent's index; 0 only for node
 *                0), u8 type (ZXVS_N_*), uv nchild (children in the source,
 *                dropped ones included), uv x, uv y (the layout: y = depth,
 *                x = 2 x leaf rank for leaves, the mean of the first and last
 *                kept child's x for inner nodes, floor), u8 nlabel (<= 15),
 *                label bytes (1..126 printable ASCII).
 *   REF (6):     u8 media (ZXVS_FMT_*), u8 has_vec, [cid[36] u8 vec_kind
 *                (1 ZXVG, 2 ZXVA)], uv a, uv b, uv frames (image: width,
 *                height, 0; audio: rate, channels, frames; 0 = unknown),
 *                u8 fid_known, zz fid_cdb (the rendition's fidelity: SNR in
 *                1/100 dB for ZXVA).
 *
 *   curvebody := u8 nname (<= 31) name bytes, u8 dec (source value =
 *                stored / 10^dec, dec <= 18), zz vmin, uv vrange (vmax -
 *                vmin), uv npts >= 1, uv tol, uv maxerr (<= tol), uv rmserr
 *                (<= maxerr), uv nseg, zz y0, nseg x {uv dx >= 1, zz d1,
 *                zz d2, zz d3}.
 *     Samples are normalised: yn = floor((v - vmin) * 2^24 / vrange + 1/2)
 *     (0 when vrange = 0), 0..2^24. Segment s spans sample x0 .. x0 + dx
 *     (x0 = 0 for the first, the sum of the dx before it after that; they
 *     sum to npts - 1), with control values P0 = the running y (y0 at the
 *     start), P1 = P0 + d1, P2 = P0 + d2, P3 = P0 + d3 (the next P0). The
 *     control points sit at x0, x0 + dx/3, x0 + 2dx/3, x0 + dx, so x is
 *     linear in the curve parameter and the value at sample x is
 *         t = floor((x - x0) * 65536 / dx),
 *         de Casteljau with lerp(a, b) = a + floor((b - a) * t / 65536)
 *     (zxvs_curve_eval). tol, maxerr and rmserr are in the same 2^-24 units
 *     of vrange: maxerr = max |eval - yn|, rmserr = floor(sqrt(mean of
 *     (eval - yn)^2)), over every sample.
 *
 * Grid Chain: zxvs_gc_word packs the 64-bit Grid Chain word
 *     domain(8) place(16) item(8) ext(16) format(8) lang(8)   (high to low)
 * and zxvs_register hands a descriptor to an index (Alexandria) through a
 * callback. Domains as Alexandria assigns them (GRID_CHAIN.md): 0x20 data
 * (the descriptor's curves and graphs) and 0x60 embeddings (its shape
 * vector). A derived word is the source file's own word with the domain
 * byte replaced, keeping place, item, extension, format and language.
 *
 * WHAT IS AND IS NOT IMPLEMENTED
 *   - Detection is by content (magic numbers, then a text test, then the
 *     grammars in order: zxcfg, JSON, HTML, XML, TSV, CSV, one number per
 *     line, C source); a file that fails its grammar falls back to BYTES.
 *   - Series: the first ZXVS_MAX_CURVES numeric columns with >= 2 values;
 *     numbers are decimal (sign, digits, '.', exponent); a column is numeric
 *     when at most 1 % of its non-empty cells are not numbers (those and
 *     empty cells are skipped, not interpolated); more than 18 significant
 *     digits, or a scaled value of 2^62 or more, makes a cell not a number;
 *     values keep at most 12 decimals (rounded half away from zero). The x
 *     axis is the order of the kept values (the row order).
 *   - The fit: Ramer-Douglas-Peucker breakpoints at the bound, then, from
 *     each segment start, the farthest breakpoint that one cubic reaches
 *     within the bound (exponential then binary search; tangents estimated
 *     from the samples h = span/4 (1..64) either side of each end, falling
 *     back to the chord; when not even the next breakpoint is reached, the
 *     farthest sample that is). At most 65536 samples per segment. Every
 *     accepted segment is measured with the exact evaluator, so maxerr <=
 *     tol always. Greedy, not least-squares: segment counts are higher than
 *     an optimal fitter's, and noisy columns need about one segment per two
 *     samples (no curve can be smoother than the data within the bound).
 *   - Structure: at most ZXVS_MAX_NODES nodes are kept, in source order
 *     (the rest are counted in their parent's nchild and in total); JSON and
 *     XML nesting deeper than 64 is folded into the 64th level. CSV/TSV
 *     structure is the table and its columns, not every cell.
 *   - Media: the header is read for size/rate only; the vector rendition is
 *     made elsewhere (zxva_analyse.h for audio, the ZXVG tools for images)
 *     and its CID passed in zxvs_cfg_t. No decoding here.
 *   - Files of 4 GiB and more are refused (ZXVS_ECAP). */
#ifndef ZXV_ZXVS_H
#define ZXV_ZXVS_H

#include <stdbool.h>
#include <stdint.h>

#define ZXVS_CID_LEN    36
#define ZXVS_DIMS       64
#define ZXVS_MAX_CURVES 8
#define ZXVS_MAX_NODES  4096
#define ZXVS_MAX_DEPTH  64

enum { ZXVS_K_BYTES = 1, ZXVS_K_SERIES = 2, ZXVS_K_STRUCT = 3, ZXVS_K_MEDIA = 4 };

enum {
    ZXVS_FMT_UNKNOWN = 0,
    ZXVS_FMT_BIN = 1,
    ZXVS_FMT_TEXT = 2,
    ZXVS_FMT_CSV = 3,
    ZXVS_FMT_TSV = 4,
    ZXVS_FMT_JSON = 5,
    ZXVS_FMT_XML = 6,
    ZXVS_FMT_HTML = 7,
    ZXVS_FMT_ZXCFG = 8,
    ZXVS_FMT_CSRC = 9,
    ZXVS_FMT_NUMTEXT = 10,
    ZXVS_FMT_PNG = 16,
    ZXVS_FMT_JPEG = 17,
    ZXVS_FMT_GIF = 18,
    ZXVS_FMT_BMP = 19,
    ZXVS_FMT_ZXVG = 20,
    ZXVS_FMT_WAV = 24,
    ZXVS_FMT_AIFF = 25,
    ZXVS_FMT_FLAC = 26,
    ZXVS_FMT_ZXVA = 27,
    ZXVS_FMT_MP3 = 28,
    ZXVS_FMT_OGG = 29,
    ZXVS_FMT_MP4 = 30,
    ZXVS_FMT_MAX = 30
};

enum { /* sections */
       ZXVS_END = 0,
       ZXVS_HIST = 1,
       ZXVS_ENTROPY = 2,
       ZXVS_HILBERT = 3,
       ZXVS_CURVE = 4,
       ZXVS_STRUCT = 5,
       ZXVS_REF = 6
};

enum { /* structure node types */
       ZXVS_N_ROOT = 0,
       ZXVS_N_OBJECT = 1,
       ZXVS_N_ARRAY = 2,
       ZXVS_N_STRING = 3,
       ZXVS_N_NUMBER = 4,
       ZXVS_N_BOOL = 5,
       ZXVS_N_NULL = 6,
       ZXVS_N_ELEMENT = 7,
       ZXVS_N_ATTR = 8,
       ZXVS_N_TEXT = 9,
       ZXVS_N_TABLE = 10,
       ZXVS_N_COLUMN = 11,
       ZXVS_N_SECTION = 12,
       ZXVS_N_KEY = 13,
       ZXVS_N_BLOCK = 14,
       ZXVS_N_MAX = 14
};

#define ZXVS_EFORMAT (-1) /* malformed descriptor                           */
#define ZXVS_EARG    (-2) /* bad argument                                   */
#define ZXVS_EWORK   (-3) /* work buffer too small                          */
#define ZXVS_EOUT    (-4) /* output buffer too small                        */
#define ZXVS_ECAP    (-5) /* source too large (>= 4 GiB)                    */

/* ---- Grid Chain (see above) ----------------------------------------------- */
#define ZXVS_GC_DOMAIN_DATA  0x20u /* data: the descriptor's curves and graphs */
#define ZXVS_GC_DOMAIN_EMBED 0x60u /* embeddings: its 64-number shape vector  */

static inline uint64_t zxvs_gc_word(uint32_t domain, uint32_t place, uint32_t item, uint32_t ext,
                                    uint32_t format, uint32_t lang)
{
    return ((uint64_t) (domain & 0xFFu) << 56) | ((uint64_t) (place & 0xFFFFu) << 40) |
           ((uint64_t) (item & 0xFFu) << 32) | ((uint64_t) (ext & 0xFFFFu) << 16) |
           ((uint64_t) (format & 0xFFu) << 8) | (uint64_t) (lang & 0xFFu);
}

/* Alexandria (or any index) registration: called once per word. desc/len is
 * the descriptor; cid its source's CID. Return 0 to go on, nonzero to stop. */
typedef int (*zxvs_register_fn)(void *ctx, uint64_t gc_word, const uint8_t cid[ZXVS_CID_LEN],
                                const uint8_t *desc, uint32_t len);

/* `word` with its domain byte replaced: the derived word Alexandria files a
 * source's descriptor or vector under. */
static inline uint64_t zxvs_gc_rebase(uint64_t word, uint32_t domain)
{
    return (word & 0x00FFFFFFFFFFFFFFull) | ((uint64_t) (domain & 0xFFu) << 56);
}

/* Register a valid descriptor under two words derived from its source file's
 * Grid Chain word: DATA (0x20) and EMBED (0x60). The record also needs the
 * extractor's CID so Alexandria can report it stale when either changes;
 * that is the caller's to supply in ctx. Returns 0, the callback's nonzero
 * value, or ZXVS_EFORMAT. */
int zxvs_register(const uint8_t *desc, uint32_t len, uint64_t source_word, zxvs_register_fn fn,
                  void *ctx);

/* The shape vector as one ALXV row (alx_vec.h): 64 int8 values and a Q16
 * scale with value[i] = row[i] * scale (in Q16 units of the shape), scale =
 * ceil(max / 127), at least 1. 0, or ZXVS_EFORMAT. */
int zxvs_alxv_row(const uint8_t *desc, uint32_t len, int8_t row[ZXVS_DIMS], uint32_t *scale_q16);

/* ---- describing ------------------------------------------------------------ */
typedef struct {
    uint32_t tol_q16;   /* curve error bound, a fraction of each series' range
                           (Q16: 65536 = the whole range); 0 = 64 (~0.1 %)   */
    uint32_t max_nodes; /* structure nodes kept (0 = ZXVS_MAX_NODES)         */
    /* media: the vector rendition made by the caller (optional) */
    const uint8_t *ref_cid; /* 36 bytes or NULL                              */
    uint32_t ref_kind;      /* 1 ZXVG, 2 ZXVA                                */
    bool ref_fid_known;
    int32_t ref_fid_cdb;
} zxvs_cfg_t;

typedef struct {
    uint32_t kind, fmt;
    uint32_t ncurves, nseg, npts;      /* over all curves                         */
    uint32_t curve_max_q24;            /* worst curve max error (2^-24 of range)  */
    uint32_t curve_rms_q24;            /* worst curve RMS error                   */
    uint32_t ent_max_q24, ent_rms_q24; /* the entropy curve's fit             */
    uint32_t nodes, total_nodes;
    uint32_t hilbert_order, hilbert_edges;
    uint32_t bytes;
} zxvs_report_t;

uint64_t zxvs_work_size(uint64_t len, const zxvs_cfg_t *cfg);
uint64_t zxvs_out_bound(uint64_t len, const zxvs_cfg_t *cfg);

/* Describe d[0..len). cfg may be NULL (defaults). Returns the descriptor's
 * size, or < 0. */
int64_t zxvs_describe(const uint8_t *d, uint64_t len, void *work, uint64_t work_size, uint8_t *out,
                      uint64_t out_cap, const zxvs_cfg_t *cfg, zxvs_report_t *rep);

/* ---- reading --------------------------------------------------------------- */
typedef struct {
    uint32_t kind, fmt;
    uint8_t cid[ZXVS_CID_LEN];
    uint32_t src_bytes;
    uint32_t shape[ZXVS_DIMS];
    uint32_t ncurves;
    uint32_t nnodes, total_nodes;
    bool has_ref;
} zxvs_info_t;

/* 0 when well formed (info filled when not NULL), ZXVS_EFORMAT otherwise. */
int zxvs_check(const uint8_t *d, uint32_t n, zxvs_info_t *info);

/* A curve of a valid descriptor: which = 0 .. ncurves - 1 for the series,
 * ZXVS_CURVE_ENTROPY for the entropy curve. */
#define ZXVS_CURVE_ENTROPY 0xFFFFFFFFu
typedef struct {
    const uint8_t *seg; /* first segment's bytes (after y0)                */
    const uint8_t *end;
    uint32_t nseg, npts;
    int64_t vmin;
    uint64_t vrange;
    uint32_t dec;
    uint32_t tol, maxerr, rmserr;
    int64_t y0;
    uint32_t column;
    char name[32];
} zxvs_curve_t;
int zxvs_curve(const uint8_t *d, uint32_t n, uint32_t which, zxvs_curve_t *c);
/* the curve's normalised value (0..2^24, may overshoot slightly) at sample x */
int zxvs_curve_eval(const zxvs_curve_t *c, uint32_t x, int64_t *yn);

/* The body of section `tag` (the index-th one, for CURVE) of a VALID
 * descriptor (check it first): 0 and *body / *len, or ZXVS_EARG if absent. */
int zxvs_section(const uint8_t *d, uint32_t n, uint32_t tag, uint32_t index, const uint8_t **body,
                 uint32_t *len);

/* Similarity of two descriptors' shape vectors, Q16: 65536 = identical,
 * 65536 - (mean absolute difference of the 64 numbers). < 0 when either is
 * malformed. */
int32_t zxvs_similarity(const uint8_t *a, uint32_t na, const uint8_t *b, uint32_t nb);
uint32_t zxvs_similarity_vec(const uint32_t a[ZXVS_DIMS], const uint32_t b[ZXVS_DIMS]);

/* names for tools and the UI */
const char *zxvs_kind_name(uint32_t kind);
const char *zxvs_fmt_name(uint32_t fmt);

#endif
