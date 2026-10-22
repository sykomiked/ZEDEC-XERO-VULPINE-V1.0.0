/* Copyright (c) 2024-2026 Michael Laurence Curzi and 36N9 Genetics, LLC
 * SPDX-License-Identifier: Apache-2.0 */
/* zxv_receipt.h — one sealed receipt per interaction (R24, hosted app).
 *
 *   E1 RECORD. Each time a person is answered, one JSON line is appended:
 *        {"seq":N,"prev":HEX,"kind":K,"q_sha":HEX,"shown_sha":HEX,
 *         "lead":S,"witness":S,"verdict":S,"cycle":N,"tokens":N,
 *         "paused":S,"hash":HEX[,"sig":HEX,"signer":HEX]}
 *      q_sha and shown_sha are the SHA-256 of the question and of what the
 *      person was shown (the texts themselves stay out of the file);
 *      paused names the idle task the interaction interrupted (zxv_idle.h).
 *   E2 CHAIN. hash is the SHA-256 of the line up to (not including) ',"hash"'
 *      with '}' appended: the body. prev is the previous receipt's hash
 *      (32 zero bytes for the first), seq counts from 1. Changing, removing
 *      or reordering a receipt breaks every later one.
 *   E3 SIGN. With a signer (zxvd: the owner's ML-DSA-65 key), sig signs the
 *      hash and signer is the SHA-256 of the public key. Without one
 *      (zxv-host) the receipt is sealed by the chain alone.
 *   E4 REPLAY. zxv_receipt_verify reads a receipt file and checks every
 *      seq, prev and hash; zxv_receipt_resume continues a file's chain after
 *      a restart (and refuses a file that does not verify).
 */
#ifndef ZXV_RECEIPT_H
#define ZXV_RECEIPT_H

#include <stddef.h>
#include <stdint.h>
#include <stdio.h>

typedef struct {
    uint64_t seq;     /* the last receipt's seq; 0 before the first */
    uint8_t prev[32]; /* the last receipt's hash */
} zxv_receipt_chain_t;

typedef struct {
    const char *kind; /* "ask", "check" */
    const char *question;
    const char *shown;
    const char *lead, *witness, *verdict; /* "" when not applicable */
    uint64_t cycle, tokens;
    const char *paused; /* zxv_idle_describe */
} zxv_receipt_t;

/* E3: write the signature of hash as hex into sig (cap bytes) and the signer
 * id (SHA-256 of the public key, 64 hex) into signer. true on success. */
typedef int (*zxv_receipt_sign_fn)(void *ctx, const uint8_t hash[32], char *sig, size_t sig_cap,
                                   char signer[65]);

void zxv_receipt_init(zxv_receipt_chain_t *ch);

/* E1-E3. Append the receipt to f (flushed) and advance the chain. 0, or -1
 * when it cannot be written or signed (the chain is then not advanced).
 * hash (may be NULL) gets the receipt's hash. */
int zxv_receipt_seal(zxv_receipt_chain_t *ch, const zxv_receipt_t *r, zxv_receipt_sign_fn sign,
                     void *sign_ctx, FILE *f, uint8_t hash[32]);

/* E4. 0 with *n receipts checked (and *ch at the file's end, when not NULL),
 * or -1 with "receipt N: why" in err. A missing file is 0 receipts. */
int zxv_receipt_verify(const char *path, uint64_t *n, zxv_receipt_chain_t *ch, char *err,
                       size_t err_cap);
int zxv_receipt_resume(const char *path, zxv_receipt_chain_t *ch, char *err, size_t err_cap);

#endif
