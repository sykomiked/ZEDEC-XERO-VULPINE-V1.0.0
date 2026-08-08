/* Copyright (c) 2024-2026 Michael Laurence Curzi and 36N9 Genetics, LLC */
/* SPDX-License-Identifier: LicenseRef-OPL-1.1 AND CC-BY-SA-4.0 AND LicenseRef-Royal-Writ-Sicilian-Crown-1.0 AND LicenseRef-SEL-3.3 */
/* dimfold.h — Dimensional Fold: the flatten/unflatten mechanic as a general tool.
 *
 * The same mechanic that lifts a 2D ROM into 3D volume (see rom_dimensions.h) is
 * not only for graphics. Its essence is a TWO-WAY, KEYED, DIMENSION-CHANGING map:
 *
 *   FLATTEN  (fold down): a thing -> its IDENTITY, a compact lower-dimensional
 *            form (a flattened matrix / a hash).
 *   UNFLATTEN (fold up): the identity -> the full higher-dimensional thing, with
 *            the interior FILLED IN, keyed so nothing is lost.
 *
 * The keys are the same everywhere in this system:
 *   - PRIME      = the forward LOCK           (here: SHA-256d, double SHA-256)
 *   - FIBONACCI  = the RECOVERY key           (the nested φ-shell decomposition)
 *   - φ (PHI)    = the axiom of ALIGNMENT      (consecutive Fibonacci ratios -> φ)
 *
 * From this one core come many faces, all lossless / exactly invertible:
 *   1. TRANSFORM  — a reversible nested multi-resolution decomposition (an integer
 *                   Haar / S-transform): the Russian-doll matrix, coarse identity
 *                   plus nested detail shells. `dimfold_fold` / `dimfold_unfold`.
 *   2. COMPRESS   — that transform makes smooth/self-similar data sparse; encode
 *                   the coarse identity + sparse shells small. `dimfold_compress`
 *                   / `dimfold_expand`. Lossless.
 *   3. ENCRYPT    — the prime LOCK (SHA-256d keystream) seals; re-applying opens.
 *                   `dimfold_seal` / `dimfold_open`. Involutive.
 *   4. IDENTITY   — `dimfold_identity` = the SHA-256d hash: the thing's identity.
 *
 * All three round-trip exactly; `dimfold_selfcheck` proves it on-target. */
#ifndef ZXV_DIMFOLD_H
#define ZXV_DIMFOLD_H

#include <stdint.h>
#include <stddef.h>

/* ---- 1. TRANSFORM: the reversible nested φ-shell decomposition -------------- */
/* In place over `n` ints (n must be a power of two, n>=2). fold: coarse identity
 * lands at a[0], nested detail shells fill the rest. unfold is its exact inverse. */
void dimfold_fold(int32_t *a, int n);
void dimfold_unfold(int32_t *a, int n);

/* ---- 2. COMPRESS / EXPAND (lossless) --------------------------------------- */
/* Compress `n` bytes of `in` into `out` (capacity must be >= dimfold_bound(n));
 * returns the compressed length. Expand reverses it, writing up to `out_cap`
 * bytes and returning the original length (0 on error). */
int  dimfold_compress(const uint8_t *in, int n, uint8_t *out, int out_cap);
int  dimfold_expand(const uint8_t *in, int n, uint8_t *out, int out_cap);
int  dimfold_bound(int n);           /* worst-case compressed size for n bytes   */

/* ---- 3. SEAL / OPEN (symmetric encryption, SHA-256d keystream) -------------- */
/* XOR `data[0..n)` with a SHA-256d(key||counter) keystream. `dimfold_open` is the
 * same operation (involutive): seal then open with the same key restores data. */
void dimfold_seal(uint8_t *data, int n, const uint8_t *key, int keylen);
void dimfold_open(uint8_t *data, int n, const uint8_t *key, int keylen);

/* ---- 4. IDENTITY ----------------------------------------------------------- */
/* The thing's identity = SHA-256d (the prime lock) over its bytes. NOTE: a hash
 * is ONE-WAY by design and CANNOT be reversed to the file (infinitely many files
 * share any digest). Here it is used only as an INTEGRITY fingerprint. */
void dimfold_identity(const uint8_t *data, int n, uint8_t out[32]);

/* ---- 5. DIMENSIONAL ELEVATOR (reversible codec for ANY filetype) ------------ *
 * The honest "reversible hash": NOT a crypto hash (those can't be reversed), but a
 * reversible transform that RAISES a whole file up through the φ/prime/Fibonacci
 * fold and LOWERS it back, KEEPING every shell so nothing is lost. Works on
 * arbitrary-length data of any type (graphics, text, binaries) by chunking. The
 * container carries the SHA-256d identity so `descend` VERIFIES the reconstruction
 * bit-for-bit (returns 0 if the integrity fingerprint does not match). */
int  dimfold_elevate(const uint8_t *in, int n, uint8_t *out, int out_cap);
int  dimfold_descend(const uint8_t *in, int n, uint8_t *out, int out_cap);
int  dimfold_elevate_bound(int n);   /* safe out_cap for elevating n bytes       */

/* ---- 6. BANDS + CHANNELS (frequency subbands; multiplexed data types) ------- *
 * The fold IS a frequency decomposition: after dimfold_fold, the coefficients are
 * BANDS — a[0] is the DC (lowest) band, then successive detail bands, each twice
 * the width of the last (higher and higher frequency). Band `b` occupies the
 * coefficient index range [lo,hi). */
int  dimfold_band_count(int n);                       /* = log2(n) + 1            */
void dimfold_band_range(int n, int band, int *lo, int *hi);

/* A CHANNEL is one data stream of a given type. dimfold_pack multiplexes several
 * channels — each independently elevated into its own bands — into one container
 * with a MANIFEST (type + length per channel: the "matrix assembly instructions").
 * dimfold_unpack follows the manifest to reassemble every channel, each integrity-
 * verified, and fills `slots` with where each landed. Different data types can ride
 * different channels/bands and each is stored folded or raw, whichever is smaller. */
/* A channel's `type` carries the OS's SPACE POLARITY — the positive / neutral /
 * negative space file types. Values mirror trispace.h tri_role_t exactly, so a
 * channel IS a tri-space member: S+ provides (the plug), S- needs (the socket),
 * S0 glue/metadata. A packed container multiplexes all three polarities and the
 * manifest records each channel's polarity for exact reassembly. */
#define DIMFOLD_POSITIVE 0   /* S+  positive space — provides / the plug   */
#define DIMFOLD_NEGATIVE 1   /* S-  negative space — needs / the socket    */
#define DIMFOLD_NEUTRAL  2   /* S0  neutral space  — glue / metadata       */

typedef struct { const uint8_t *data; int len; uint8_t type; } dimfold_channel_t;
typedef struct { uint8_t type; int offset; int len; }          dimfold_slot_t;
int  dimfold_pack(const dimfold_channel_t *ch, int nch, uint8_t *out, int out_cap);
int  dimfold_unpack(const uint8_t *in, int n, uint8_t *out, int out_cap,
                    dimfold_slot_t *slots, int max_slots);   /* returns #channels */

/* On-target self-check: fold/unfold, compress/expand, and seal/open all round-trip
 * losslessly; the Fibonacci recovery key aligns to φ. Returns 1 on pass and writes
 * the achieved compression ratio (permille of original) for a self-similar buffer. */
int  dimfold_selfcheck(uint32_t *ratio_permille_out);

#endif /* ZXV_DIMFOLD_H */
