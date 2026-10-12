<!-- Copyright (c) 2024-2026 Michael Laurence Curzi and 36N9 Genetics, LLC -->
<!-- SPDX-License-Identifier: Apache-2.0 -->

# z-genome: exact, reproducible alignment scores

z-genome is a C11 library that aligns two sequences and gives the same
score and the same alignment on every machine and compiler. Every score is
an `int64`. A sum that would leave `int64` is refused, never wrapped or
saturated. The tie-break is fixed and written down, so the traceback is
reproducible too, not only the score. A result can be signed with ML-DSA-87
and checked on another machine, which recomputes it from the inputs. The
API is [`include/zgenome.h`](include/zgenome.h).

**Research use only.** z-genome is not a clinical diagnostic and not a
medical device. See [Limits](#limits).

## What it does

| Part | What it gives you |
|---|---|
| Matrices | BLOSUM62 exactly as NCBI publishes it (alphabet `ARNDCQEGHILKMFPSTWYVBZX*`; the test checks entries and the sum of all 576 entries against the NCBI file), a DNA match/mismatch matrix over `ACGTN` (N never matches), or your own matrix of up to 32 symbols of `[A-Z*]` with any `int64` entries except `INT64_MIN`. `zgn_matrix_digest` is SHA3-256 over the alphabet and every entry; the name is only a label. |
| Alignment | `zgn_align`: Needleman-Wunsch (`ZGN_GLOBAL`) or Smith-Waterman (`ZGN_LOCAL`) with affine gaps (Gotoh). A gap of k columns costs `gap_open + k * gap_extend`. A linear gap is `gap_open = 0`. A gap in one sequence may directly follow a gap in the other; each then pays its own open. |
| Overflow | Every addition and subtraction is checked. If any cell, on the best path or not, would leave `int64`, the call returns `ZGN_E_OVERFLOW` and no result. |
| Self-check | Before it returns, `zgn_align` rescores its own alignment (`zgn_score_rows`) and fails with `ZXV_E_INTERNAL` if the two disagree. |
| Ratios | `zgn_identity` (matches / columns) and `zgn_score_per_column` as exact rationals through `zxv_rat` (`zind_qty`): 61/145, never 0.42068... |
| FASTA | `zgn_fasta_parse`: a strict, bounded parser (rules below). |
| Records | `zgn_record_append` signs a result into a libzind chain (`"z-genome/result"`). `zgn_record_verify` and `zgn_verify_export` re-verify it by recomputing. |

### The tie-break

Two alignments often have the same score. z-genome always returns the same
one, by this rule:

1. The states are M (residue against residue), X (a residue of `a`
   against a gap) and Y (a gap against a residue of `b`).
2. When predecessors tie, prefer M, then X, then Y, in every cell and for
   the last column of a global alignment.
3. Local: a match cell starts a new alignment when the best predecessor
   score is `<= 0`, so a zero-scoring prefix is dropped. The end is the M
   cell with the highest score; on a tie, the first in row-major order
   (smallest i, then smallest j). If nothing scores above 0, the result is
   the empty alignment with score 0.

Integer arithmetic and this rule are all the algorithm depends on, so
the alignment is the same on x86-64 and arm64, with gcc and with clang.
The test pins the SHA3-256 digest of the HBA/HBB alignment. A machine that
disagrees fails the test.

### FASTA rules

Accepted: one or more records, each a header line `>ID` or
`>ID DESCRIPTION` followed by one or more sequence lines.

- ID: 1..64 bytes of printable ASCII without a space or `>` (so
  `sp|P69905|HBA_HUMAN` is fine). Description: up to 200 printable ASCII
  bytes after one space or tab.
- Sequence lines: `[A-Za-z*]` only. Lower case is upper-cased.
- Lines end in `\n` or `\r\n`, and the last newline is optional.

Refused with `ZGN_E_FASTA`, with the line number in `zind_last_error`:
text before the first `>`, any blank line, a gap (`-`, `.`), a digit or
space in a sequence, a NUL or control byte, a lone `\r`, a record with no
residues and a repeated ID. More records, residues or bytes than the
limits allow is `ZGN_E_LIMIT` (defaults: 1,024 records, 2^20 residues per
record, 64 MiB).

### Signed records and re-verification

A record holds these fields:

- the mode;
- the SHA3-256 of the matrix and its name;
- both gap penalties;
- the SHA3-256 of each sequence, with its length and its ID (the ID is a
  label only and is not hashed);
- the score, coordinates and counts;
- SHA3-256 of the alignment rows.

Nothing in it depends on the machine or the clock. The record is signed
with ML-DSA-87 by the machine that computed it.

`zgn_record_append` refuses a result that does not belong to the inputs:
rows that do not spell the sequences, a score that does not rescore, or
counts that do not add up (`ZGN_E_MISMATCH`).

`zgn_record_verify` (or `zgn_verify_export` on the exported bytes) runs
these checks in order:

1. Every link and signature in the chain verifies against your trusted
   keys (`ZIND_E_CHAIN`, `ZIND_E_UNTRUSTED`).
2. The record names your matrix (`ZGN_E_MATRIX`).
3. Your sequences hash to the recorded digests (`ZGN_E_MISMATCH`).
4. Recomputing the alignment gives the same score, coordinates, counts and
   alignment digest (`ZGN_E_MISMATCH`).

Step 4 means a validly signed record of a wrong score is still refused.

## Example

```c
zgn_matrix m;  zgn_matrix_blosum62(&m);
zgn_params p = {ZGN_LOCAL, 11, 1, 0};          /* open 11, extend 1, default cell bound */
zgn_result r;
zgn_align(&m, &p, a.res, a.len, b.res, b.len, &r);   /* r.score, r.a_row, r.b_row ... */

zind_chain_create(ZGN_DOMAIN, 64, &chain);
zgn_record_append(cap, chain, lab_key, &m, &p, &a, &b, &r, NULL);
zind_chain_export(chain, buf, cap_len, &len);          /* send it anywhere */

/* on another machine, with the same FASTA and the lab's public key */
zgn_verify_export(cap, buf, len, 0, &lab_pk, 1, &m, &a, &b, 0, &claim);   /* ZXV_OK */
```

`make demo` runs [`examples/genome_demo.c`](examples/genome_demo.c). It
parses a two-record FASTA file and refuses one with a gap character. It
aligns human hemoglobin alpha and beta (local score 285, global 282 under
BLOSUM62, open 11, extend 1) and prints the alignment and its identity as
an exact fraction. It signs both results and re-verifies them from the
exported bytes. A swapped pair of sequences, the wrong matrix and a
flipped bit are each refused. It ends with the Wikipedia DNA example and a
matrix that overflows.

## Tests

```sh
make -C sdk/libzxv all
make -C sdk/industry/z-genome test     # 5,556 checks, fuzz smoke, demo, no-float check
make -C sdk/industry/z-genome SANITIZE=1 OUT=build/san test
make -C sdk/industry/z-genome fuzz FUZZ_ITERS=20000
```

`test_genome` covers these areas:

- BLOSUM62 entries and their sum against the NCBI file, with the matrix
  digests pinned, and every way a custom matrix is refused;
- parameter, residue, length and cell-bound refusals;
- textbook alignments:
  - Durbin et al. HEAGAWGHEE / PAWHEAE under BLOSUM50 with a linear gap
    of 8: global 1, and local 28 as `AWGHE / AW-HE`;
  - the Wikipedia Smith-Waterman example: 13, `GTT-AC / GTTGAC`;
  - the Wikipedia Needleman-Wunsch example: 0;
  - affine against linear gaps;
  - a protein against itself;
  - HBA / HBB with the score and the alignment digest pinned;
- every rule of the tie-break, with empty inputs;
- overflow refusal through the matrix, through the gap penalties, and off
  the best path;
- 240 property cases against two slow reference implementations in the
  test: an exhaustive enumeration of every alignment (tiny inputs) and a
  full-matrix Gotoh. Each case also checks rescoring, coverage, symmetry
  under symmetric matrices, local >= max(0, global), and an identical
  traceback on a second run;
- the FASTA rules: every refusal and every limit;
- signed records:
  - append refusals;
  - a wrong key, the wrong matrix, swapped or shortened sequences;
  - a validly signed forged score;
  - other record kinds and truncated payloads;
  - 48 flipped bits across an export;
  - a capability without `CRYPTO_SIGN`.

`fuzz_genome` mixes several kinds of operation:

- random alignments checked against the full-matrix reference, each run
  twice to show it is deterministic;
- matrices near 2^62, which must either overflow cleanly or rescore
  exactly;
- residues outside the alphabet;
- generated and mutated FASTA, which must round-trip or be refused;
- signed records, re-verified, with bit flips in exports.

The fuzzer makes one random draw per statement, so gcc and clang print
identical output.

## Limits

- **Research use only.** z-genome is not a clinical diagnostic, a medical
  device or software of a medical device. It makes no IEC 62304, FDA, CE
  or ISO 13485 claim. A score is a number about two strings. It is not a
  finding about a patient.
- **Memory is O(n·m).** It needs (len_a + 1) x (len_b + 1) bytes of
  traceback plus 6 x (len_b + 1) `int64` rows. The default bound is 2^24
  cells (16 MiB). `max_cells` goes up to 2^28 (256 MiB). A sequence holds
  at most 2^20 residues. Larger problems are refused, not truncated.
  There is no linear-space (Hirschberg) mode, no banding and no heuristic
  seeding (BLAST-style). Time is O(n·m) too.
- **Integer scores only.** A matrix in fractional bits (or half bits) must
  be scaled to integers by you. The kit does not compute E-values or bit
  scores, because those need logarithms and floats. Exact ratios are
  given as rationals.
- **Reproducible is not correct.** Two machines agreeing proves they ran
  the same algorithm on the same bytes. It does not prove the sequences,
  the matrix or the parameters were the right ones for your question.
- A signature proves which key signed. Keeping that key safe, and
  deciding whom to trust, is up to you. There is no HSM integration.
- BLOSUM62 is reproduced from the public NCBI matrix file. NCBI and the
  matrix's authors have not endorsed this kit.
