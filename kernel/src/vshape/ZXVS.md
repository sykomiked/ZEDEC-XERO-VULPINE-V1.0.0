<!-- Copyright (c) 2024-2026 Michael Laurence Curzi and 36N9 Genetics, LLC -->
<!-- SPDX-License-Identifier: Apache-2.0 -->
# ZXVS: the vector shape of data

ZXVS does for a file's *data* what ZXVG does for pictures and ZXVA for sound.
It describes the data's shape as vectors, so the shape can be drawn at any
size, compared, indexed and filed:

- numeric series (CSV / TSV columns, one number per line) become piecewise
  cubic Bézier curves under a stated error bound, with the measured max and
  RMS error;
- structured data (JSON, XML, HTML, zxcfg, C source blocks, table columns)
  becomes a structure graph with an integer tree layout;
- any bytes get an exact byte histogram, a windowed entropy curve (fitted like
  a series) and a Hilbert-curve map of the file with the outlines of its
  regions (zero, text, high entropy, other);
- images and audio are referenced by CID to their ZXVG / ZXVA vector
  rendition, together with that rendition's measured fidelity.

Every descriptor carries the source's CID (CIDv1 raw sha2-256: `01 55 12 20 ||
SHA-256`), its size, kind and format, per-element fidelity, and a 64-number
Q16 shape vector for similarity search.

**What the vector form gives, stated plainly.** The shape *scales cleanly*:
a curve or outline redraws sharp at any size, and the same descriptor serves
a thumbnail and a wall display. It **does not add detail**. A fitted curve
stays within its stated bound of the samples it was fitted to and says nothing
between or beyond them. The original bytes remain the reference, and every
descriptor carries the CID that addresses them. A descriptor sits beside the
original; it never replaces it.

| file | role |
|---|---|
| `zxvs.h`, `zxvs.c` | describe, check, read curves and sections, similarity, Grid Chain words, registration. Integer only, freestanding, no allocation |
| `zxvs_draw.h`, `zxvs_draw.c` | draws a descriptor through `zui_vec` (curves in bands, the structure tree, Hilbert map + outline + entropy curve) |
| `test_zxvs.c`, `test/zxvs_fixtures.inc` | the test suite and its fixtures |
| `tools/vshape/gen_zxvs_fixtures.py` | generates the fixtures, including independent float-computed normalised samples for the curve checks |
| `tools/zxva/zxva_bench.c` | corpus benchmark (data mode: one JSON line per file, with curve errors) |

## 1. Rules

- **S1** Integer only, freestanding, no allocation. The caller passes a work
  buffer (`zxvs_work_size`) and an output buffer (`zxvs_out_bound`).
  `zxvs.c` links against nothing but `sha256_*` (x86_64; on i386 also
  libgcc's 64-bit division helpers).
- **S2** Deterministic: the same bytes and config give the same descriptor on
  every machine.
- **S3** `zxvs_check` fails closed. Every read is bounds-checked, every count
  and value range is checked, tags must appear in order, and the descriptor
  must end exactly at END.
- **S4** Fidelity is measured, never assumed. Each curve records the max and
  RMS error of its fit against its samples, and the max never exceeds the
  bound. `zxvs_describe` runs `zxvs_check` on its own output before returning
  it.

## 2. Format (v1)

`zxvs.h` holds the normative byte layout. In short:

```
file    := "ZXVS" u8 version(=1) u8 kind u8 fmt u8 flags(=0)
           cid[36]  uv src_bytes (< 2^32)  uv shape[64] (0..65536, 3 bytes each)
           section*  u8 END(=0)
section := u8 tag  u32le len  body[len]       (tags increasing; CURVE repeats <= 8)

HIST (1)     uv count[256]                     exact; sums to src_bytes
ENTROPY (2)  uv win, curvebody                 window entropy, 1/256 bit
HILBERT (3)  u8 k (<= 6), uv bpc, class runs, outline edges (merged lattice runs)
CURVE (4)    u8 column, curvebody
STRUCT (5)   u8 grammar, uv total, uv nkept (<= 4096), pre-order nodes
             {uv up, u8 type, uv nchild, uv x, uv y, u8 nlabel, label}
REF (6)      u8 media, u8 has_vec, [cid[36] u8 vec_kind (1 ZXVG, 2 ZXVA)],
             uv a, uv b, uv frames, u8 fid_known, zz fid_cdb

curvebody := name, u8 dec, zz vmin, uv vrange, uv npts, uv tol, uv maxerr,
             uv rmserr, uv nseg, zz y0, nseg x {uv dx, zz d1, zz d2, zz d3}
```

kind: 1 BYTES, 2 SERIES, 3 STRUCT, 4 MEDIA. Every kind has HIST, ENTROPY and
HILBERT. SERIES adds CURVE (and STRUCT for the table's columns), STRUCT adds
STRUCT, and MEDIA adds REF.

Curve values are normalised to 2^24 steps over the column's range. The value
at sample x of a segment starting at x0 is defined in integers:
`t = floor((x - x0) * 65536 / dx)`, then de Casteljau with
`lerp(a, b) = a + floor((b - a) * t / 65536)`. `zxvs_curve_eval` implements
it, and `tol`, `maxerr` and `rmserr` are measured in the same units, so
0.1 % of the range is 16,777 units and the default bound (`tol_q16` = 64) is
64/65536 = 0.098 %.

**The fitter.** It places Ramer-Douglas-Peucker breakpoints at the bound.
Then, from each segment start, it takes the farthest breakpoint that one cubic
reaches within the bound (exponential then binary search). Tangents come from
the samples either side of each end, with a chord fallback. When not even the
next breakpoint is reached, it takes the farthest sample that is. Every
accepted segment is measured with the exact evaluator. It is greedy, not
least-squares.

**Shape vector** (64 × Q16):

| dims | content |
|---|---|
| 0–15 | the byte histogram by high nibble |
| 16–31 | the entropy curve at 16 even positions |
| 32–47 | kind-specific: the first curve's profile (series), the depth histogram (structure), dimensions and rate (media), or the Hilbert class mix overall and per quarter (bytes) |
| 48–63 | global: log size, mean entropy, text, zero, digit, letter, punctuation and high-byte fractions, line density, a kind one-hot, node or curve count, segments per point, max depth |

`zxvs_similarity(a, b)` = 65536 − mean |a_i − b_i|.

## 3. API

```
uint64_t zxvs_work_size(uint64_t len, const zxvs_cfg_t *cfg);
uint64_t zxvs_out_bound(uint64_t len, const zxvs_cfg_t *cfg);
int64_t  zxvs_describe(d, len, work, work_size, out, out_cap, cfg, report);
int      zxvs_check(desc, n, info);                       /* fail-closed */
int      zxvs_section(desc, n, tag, index, &body, &len);
int      zxvs_curve(desc, n, which, &curve);  int zxvs_curve_eval(&curve, x, &yn);
int32_t  zxvs_similarity(a, na, b, nb);       uint32_t zxvs_similarity_vec(va, vb);
int      zxvs_register(desc, n, place, item, lang, fn, ctx);     /* Alexandria */
int      zxvs_draw(desc, n, vec, box, opts, err_bits);           /* zxvs_draw.h */
```

`zxvs_cfg_t`: `tol_q16` (the curve bound), `max_nodes`, and for media the
CID, kind and fidelity of the vector rendition made elsewhere (for audio,
`zxva_analyse`, whose SNR goes into `fid_cdb`).

**Grid Chain and Alexandria.** `zxvs_gc_word(domain, place, item, ext, format,
lang)` packs `domain(8) place(16) item(8) ext(16) format(8) lang(8)`, high
byte first (GRID_CHAIN.md). The domains are Alexandria's: 0x20 data and 0x60
embeddings. `zxvs_register` takes the source file's own word and calls the
index callback twice, with the domain byte replaced by 0x20 (the descriptor)
and by 0x60 (its shape vector), keeping place, item, extension, format and
language, the vector-plane rule. Alexandria files the bytes in a booklet by
CID; the record names the source CID and the extractor's CID so it is
reported stale when either changes. `zxvs_alxv_row` gives the shape vector as
one ALXV row (64 int8 values and a Q16 scale) for the Alexandria packer.

## 4. Tests (`test_zxvs.c`, 8 sections)

1. **Fixtures**: 16 real and synthetic inputs (repo TSVs, `.zenodo.json`, a C
   source, XML, HTML, zxcfg, 6000 bytes of a TTF, a PNG, a WAV, deep and
   broken JSON, empty, one byte). Checks kind, format, CID and histogram
   against values the generator computed independently.
2. **Curves vs independent samples**: normalised samples are computed in
   Python floats, and every sample is re-evaluated with `zxvs_curve_eval`.
   The energy TSV column gives 11 segments, max 0.075 %, RMS 0.033 %. A sine
   column gives 28 segments, max 0.096 %. The bound sweep 0.024 / 0.098 /
   1 / 10 % gives 41 / 28 / 17 / 9 segments, each within its bound.
3. **Structure graphs and layout**: node counts and types, deep JSON folded
   at depth 64 (102 of 102 nodes, max y 65), the node cap (kept ≤ cap, total
   unchanged).
4. **Media references, similarity, Grid Chain**: TSV against itself 1.0000,
   an edited copy 1.0000, another CSV 0.9624, JSON 0.7617, a font 0.7469.
5. **The Alexandria registration callback.**
6. **Determinism and error paths.**
7. **Fuzz**: 600 random and mutated inputs described and checked; every
   truncation of every fixture descriptor (20,121) rejected; 4,800 random
   byte flips (2,424 rejected, the rest still valid and re-checked).
   Exact-size mallocs, so ASan sees any overread.
8. **`zxvs_draw`**: draws at 96 and 512 px, deterministic, and a malformed
   descriptor draws nothing.

The suite passes under gcc `-Werror`, clang, gcc-11 and gcc with
ASan + UBSan. `kernel/src/zui/test_zui_toolkit.sh` runs it.

## 5. Measured on real files

`zxva_bench --mode data` on repository files, one run on one machine (x86_64,
gcc -O2). "Max / RMS" is the worst curve's error as a percentage of its
column's range. `bytes` kinds have no data curves; their only curve is the
entropy curve, which stays within the same bound.

Default bound, `tol_q16` = 64 (0.098 %):

| file | kind / format | source | descriptor | curves (points → segments) | max / RMS | other | ms |
|---|---|---|---|---|---|---|---|
| `bench/results/2026-10-11-russell-gating/logs/energy_balance_float64.tsv` | series / tsv | 2,299 B | 2,904 B | 8 (224 → 145) | 0.094 / 0.038 % | | 0.2 |
| `cards/cards.tsv` | series / tsv | 5,404,518 B | 1,189,614 B | 4 (208,380 → 102,624) | 0.098 / 0.032 % | | 471 |
| `.zenodo.json` | struct / json | 1,175 B | 1,038 B | | | 23 / 23 nodes | 0.06 |
| `api/zxvd.openrpc.json` | struct / json | 540,692 B | 74,124 B | | | 4,096 of 13,743 nodes | 9.8 |
| `kernel/src/vaudio/zxva.c` | struct / c | 56,744 B | 12,292 B | | | 233 / 233 blocks | 2.1 |
| `assets/fonts/ascw-golden/ASCW-Golden-Armenian.ttf` | bytes / bin | 40,772 B | 11,491 B | | | Hilbert k = 6, 812 outline edges | 2.1 |
| an x86_64 ELF executable (`zxva_bench`) | bytes / bin | 189,048 B | 20,448 B | | | Hilbert k = 6, 169 edges | 7.3 |
| `piano_44k16.flac` (synthetic, with its ZXVA) | media / flac | 35,887 B | 4,868 B | | | REF → ZXVA, SNR 40.64 dB | 1.6 |

Bound `tol_q16` = 655 (1 %):

| file | descriptor | segments | max / RMS |
|---|---|---|---|
| energy_balance_float64.tsv | 2,358 B | 99 | 0.955 / 0.319 % |
| cards.tsv | 1,103,850 B | 95,040 | 0.996 / 0.241 % |
| .zenodo.json | 1,022 B | | |
| zxvd.openrpc.json | 70,858 B | | |
| zxva.c | 10,969 B | | |
| the TTF | 10,606 B | | |

How to read this:

- The energy TSV is smooth data, so its curves take fewer segments than
  points even at 0.1 %. Its descriptor is larger than the source because it
  also carries the histogram, the entropy curve, the Hilbert map and the
  64-number shape vector.
- The numeric columns of `cards.tsv` are an index, a set number, a gematria
  value and a root. They are close to random from row to row, so no curve
  within 0.1 % (or 1 %) can be much smoother than the data: about one segment
  per two points. The bound holds; the descriptor is the shape, not a
  compressed copy. Use the CID to reach the data.
- Structure descriptors keep at most 4,096 nodes. The OpenRPC file has
  13,743, and the rest are counted in their parents.

Regenerate with:

```
./zxva_bench --mode data --jsonl data.jsonl --summary data_summary.json FILES...
./zxva_bench --mode data --tol 655 --jsonl data_1pct.jsonl FILES...
```

(build line in `tools/zxva/zxva_bench.c` or `kernel/src/vaudio/ZXVA.md`
section 7).

## 6. WHAT IS AND IS NOT IMPLEMENTED

- Detection is by content: media magic numbers, a text test, then the
  grammars in order (zxcfg, JSON, HTML / XML, TSV, CSV, one number per line,
  C source). A file that fails its grammar falls back to BYTES.
- Series: the first 8 numeric columns that have at least 2 values. Decimal
  numbers only, at most 12 decimals kept. The x axis is row order; empty and
  non-numeric cells are skipped, not interpolated.
- Structure: at most 4,096 nodes in source order; nesting deeper than 64 is
  folded into the 64th level. CSV / TSV structure is the table and its
  columns, not every cell. C source is preprocessor lines and brace
  blocks labelled with the identifier before them; it is not a C parser.
- Media: only the header is read (size, rate, channels). The vector rendition
  is made elsewhere and passed in by CID (`zxva_analyse` for audio, the ZXVG
  tools / `kernel/src/vtrace` for pictures). No decoding happens here.
- Not done: dates and times as series, spreadsheets (XLSX / ODS), Parquet,
  compressed archives (described as bytes), a least-squares fitter, files of
  4 GiB or more (`ZXVS_ECAP`), GPU paths (describing is memory-bound; CPU
  only).
- Registration needs Alexandria (`kernel/src/alexandria`), which lands
  separately; until then `zxvs_register` only calls the caller's callback.
