/* invproof.h — ZXI: the Inverse Witness. Makes TRI_INV_EXACT checkable.
 *
 * THE CLAIM THIS REPLACES
 * -----------------------
 * Tri-Space lets an artifact declare `inverse_kind = TRI_INV_EXACT` and
 * `claims_proven_inverse = true`. Both are booleans the author sets. Nothing
 * ever checked that S- actually inverts S+, so "this undo is proven" was an
 * assertion with no content — precisely the shape of claim that requirement 5
 * ("an auto-derived undo is a draft, not a proof") exists to be suspicious of.
 *
 * An inverse witness turns that assertion into something a machine can check by
 * DOING it: apply the undo and see whether you land back on the recorded prior
 * state, byte for byte.
 *
 * THE MECHANISM
 * -------------
 * For a state mutation, the exact inverse is XOR itself. If S+ takes state
 * `before` to `after`, then with delta = before XOR after:
 *
 *     after XOR delta  ==  after XOR before XOR after  ==  before
 *
 * so the delta IS the undo, and applying it is the proof. The witness records
 * the delta plus SHA-256 of both endpoints, so verification is:
 *
 *   1. digest(after)          == witness.digest_after   (this witness is about
 *                                                        THIS state, not another)
 *   2. digest(after XOR delta) == witness.digest_before  (the undo really does
 *                                                        restore the prior state)
 *
 * Both must hold. Step 1 is what stops a witness being lifted from a different
 * transition, which is the same substitution attack the triad seal defends
 * against one level up.
 *
 * WHAT THIS PROVES, AND WHAT IT DOES NOT (read this before quoting it)
 * --------------------------------------------------------------------
 * A witness proves the inverse FOR THE ONE RECORDED TRANSITION. It is a
 * witness, not a theorem: it does not prove the artifact inverts every possible
 * input, and it cannot, because that is a statement about all executions rather
 * than about one. Claiming otherwise would be exactly the kind of overclaim
 * requirement 5 was written to reject.
 *
 * What it does buy, which is not small: an undo that does not actually undo is
 * now caught mechanically at bind time instead of being discovered during an
 * incident. "Proven" stops meaning "the author typed true".
 *
 * SPARSE BY CONSTRUCTION
 * ----------------------
 * The XOR of two states that differ in a few bytes is almost entirely zeros, so
 * the delta is stored as RUNS of changed bytes rather than densely. A three-byte
 * edit to an 8 KB state costs ~15 bytes, not 8 KB. This matters because S- is a
 * stored artifact subject to ZXVFS's per-file extent cap.
 *
 * HOSTILE INPUT
 * -------------
 * A witness arrives as untrusted data (it may have been authored by whoever
 * wrote the S- we are suspicious of). Every run is bounds-checked against the
 * declared state length, runs must be strictly ordered and non-overlapping, and
 * the whole body is sealed. A witness that cannot be parsed is REFUSED, never
 * partially applied.
 *
 * Freestanding: integer only, no libc, no allocation, no float.
 *
 * Author: H.M. Michael-Laurence: Curzi (c)  (ZXV inverse-witness slice)
 * Copyright (c) 2024-2026 Michael Laurence Curzi and 36N9 Genetics, LLC
 * SPDX-License-Identifier: LicenseRef-OPL-1.1 AND CC-BY-SA-4.0
 * Licensed under OPL-1.1, SEL-3.3, the Royal Writ of the Sicilian Crown,
 * and CC BY-SA 4.0. See LICENSE at the repository root.
 */
#ifndef ZXV_INVPROOF_H
#define ZXV_INVPROOF_H

#include <stdint.h>
#include <stdbool.h>

#define ZXI_MAGIC     0x5A584931u   /* 'ZXI1' */
#define ZXI_VERSION   1u
#define ZXI_DIGEST    32u
#define ZXI_MAX_RUNS  1024u
#define ZXI_HDR_BYTES 88u   /* magic4 ver2 kind2 state_len4 runs4 d_before32
                             * d_after32 seal4 = 88 */
#define ZXI_RUN_HDR   6u    /* offset4 + len2 */

typedef enum {
    ZXI_OK = 0,
    ZXI_ERR_NOT_WITNESS  = -1,  /* no ZXI magic                              */
    ZXI_ERR_TRUNCATED    = -2,
    ZXI_ERR_VERSION      = -3,
    ZXI_ERR_TOO_MANY     = -4,  /* run_count > ZXI_MAX_RUNS                  */
    ZXI_ERR_RUN_BOUNDS   = -5,  /* a run falls outside the declared state    */
    ZXI_ERR_RUN_ORDER    = -6,  /* runs overlap or are out of order          */
    ZXI_ERR_SEAL         = -7,
    ZXI_ERR_STATE_LEN    = -8,  /* the state given is not the length recorded */
    ZXI_ERR_AFTER_DIGEST = -9,  /* witness is about a DIFFERENT state        */
    ZXI_ERR_NOT_INVERSE  = -10  /* applying the undo did NOT restore `before`*/
} zxi_result_t;

/* Build a witness for the transition before -> after (both `len` bytes).
 * Returns the number of bytes written to `out`, or <0 on error (including
 * ZXI_ERR_TOO_MANY if the states differ in more than ZXI_MAX_RUNS regions). */
int zxi_build(const uint8_t *before, const uint8_t *after, uint32_t len,
              uint8_t *out, uint32_t outmax);

/* THE CHECK. Verify that `witness` really is an exact inverse for `after`:
 * that it is about this state, and that applying it restores the recorded
 * prior state. Returns ZXI_OK only if both hold. `scratch` must be at least
 * `after_len` bytes; the recovered prior state is left there on success. */
zxi_result_t zxi_verify(const uint8_t *witness, uint32_t wlen,
                        const uint8_t *after, uint32_t after_len,
                        uint8_t *scratch, uint32_t scratch_len);

/* Is this buffer a witness at all (magic only)? Cheap probe — a true result
 * says nothing about validity. */
bool zxi_is_witness(const uint8_t *buf, uint32_t len);

const char *zxi_result_name(zxi_result_t r);

#endif /* ZXV_INVPROOF_H */
