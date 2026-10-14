/* Copyright (c) 2024-2026 Michael Laurence Curzi and 36N9 Genetics, LLC */
/* SPDX-License-Identifier: Apache-2.0 */
/* br_ham.h — signed, cleartext emergency messages over amateur radio
 * (the HAM_SDR bearer, roadmap R2.6).
 *
 * WHAT IT IS FOR
 *   When every other link is gone, a licensed amateur operator can send a
 *   short emergency message ("water needed at the school, 40 people") that
 *   anyone can read and anyone can check came from that station's key.
 *
 * WHAT GOES ON AIR
 *   body  = text "\n-- sig " base64(signature)
 *   The body is cut into chunks of BR_HAM_CHUNK bytes; each chunk is the
 *   information field of one AX.25 UI frame (br_ax25.h), PID 0xF0:
 *     "ZXV1 " <msg id, 4 hex digits> " " <i>/<n> " " <chunk>
 *   Everything is printable ASCII. The signature is appended in the clear:
 *   it proves who sent the text and that it was not changed, and it hides
 *   nothing, so the text keeps its plain meaning.
 *   The signed bytes are "ZXV-EMRG-1\n" <source call-SSID> "\n" <text>, so a
 *   signature cannot be replayed under another call sign.
 *
 * RULES (fail closed; designed to follow 47 CFR 97.113 and 97.309)
 *   H1  Transmitting is OFF unless cfg.licensed_operator is set by the host
 *       after the user has entered their own amateur call sign and
 *       confirmed they hold a licence (BR_ERR_DISABLED otherwise). The
 *       source must have the shape of an amateur call sign (br_ax25.h X1).
 *   H2  br_policy_check(BR_HAM_SDR, ...) must pass: class EMERGENCY only,
 *       BR_F_SIGNED required, BR_F_ENCRYPTED / BR_F_AEAD / BR_F_PAYMENT
 *       refused, text printable ASCII (tab, CR, LF allowed).
 *   H3  The signature must verify (cfg.verify) before anything is
 *       transmitted, and again on receipt; no verifier = refused.
 *   H4  Text at most BR_HAM_TEXT_MAX bytes, signature at most
 *       BR_HAM_SIG_MAX bytes, at most BR_HAM_MAX_FRAMES frames.
 *   H5  The receiver keeps one message at a time per source; a frame for a
 *       different message id or source restarts reassembly. A chunk whose
 *       size, index or header is wrong is refused.
 *
 * HONEST LIMITS
 *   - The gates cannot read meaning: a licensee could still type a
 *     business message as plain text. H1 puts that responsibility where
 *     the rules put it, on the licensed control operator.
 *   - Station identification (97.119: call sign every 10 minutes and at the
 *     end) is satisfied by the AX.25 source address of every frame; that
 *     is the common packet-radio practice, not legal advice.
 *   - Rules outside the US differ; a host must not enable H1 where the
 *     local rules forbid this traffic.
 *   - An ML-DSA-87 signature is ~4.6 KB (about 6.2 KB in base64, ~31
 *     frames): at 1200 bit/s that is close to a minute of air time.
 *   - No radio driver: frames go to a KISS TNC or an SDR modem via the host.
 */
#ifndef ZXV_BR_HAM_H
#define ZXV_BR_HAM_H

#include "br_ax25.h"

#define BR_HAM_CHUNK      200u
#define BR_HAM_MAX_FRAMES 48u
#define BR_HAM_TEXT_MAX   1024u
#define BR_HAM_SIG_MAX    4700u
#define BR_HAM_BODY_MAX   (BR_HAM_MAX_FRAMES * BR_HAM_CHUNK)
#define BR_HAM_SIGNED_MAX (11u + 10u + 1u + BR_HAM_TEXT_MAX)
#define BR_HAM_HDR_MAX    16u /* "ZXV1 ABCD 48/48 " */

/* 1 = signature valid for this call sign. */
typedef int (*br_ham_verify_fn)(void *ctx, const char *callsign, const uint8_t *msg, uint32_t len,
                                const uint8_t *sig, uint32_t sig_len);

typedef struct {
    uint8_t licensed_operator; /* H1: 0 by default */
    br_ax25_addr_t src;        /* the operator's own station */
    br_ax25_addr_t dst;        /* e.g. "EMERG" or "CQ" */
    br_ham_verify_fn verify;
    void *verify_ctx;
} br_ham_cfg_t;

/* RFC 4648 base64 with padding. */
int32_t br_b64_encode(const uint8_t *in, uint32_t n, char *out, uint32_t cap);
int32_t br_b64_decode(const char *in, uint32_t n, uint8_t *out, uint32_t cap);

/* The bytes a station signs for `text`. */
int32_t br_ham_signed_bytes(const br_ax25_addr_t *src, const uint8_t *text, uint32_t n,
                            uint8_t *out, uint32_t cap);

typedef struct {
    br_ham_cfg_t cfg;
    uint16_t msg_id;
    uint32_t body_len, idx, count;
    char body[BR_HAM_BODY_MAX];
    uint8_t scratch[BR_HAM_SIGNED_MAX];
} br_ham_tx_t;

/* H1..H4. On BR_OK call br_ham_tx_next() until it returns 0. */
br_status_t br_ham_tx_begin(br_ham_tx_t *tx, const br_ham_cfg_t *cfg, uint16_t msg_id, uint8_t cls,
                            uint8_t flags, const uint8_t *text, uint32_t n, const uint8_t *sig,
                            uint32_t sig_len);
/* Next AX.25 frame (no flags, FCS included): length, 0 when done, <0 error. */
int32_t br_ham_tx_next(br_ham_tx_t *tx, uint8_t *out, uint32_t cap);

typedef struct {
    br_ham_verify_fn verify;
    void *verify_ctx;
    uint8_t active;
    uint16_t msg_id;
    br_ax25_addr_t src;
    uint32_t count;
    uint64_t have;
    uint32_t last_len;
    char body[BR_HAM_BODY_MAX];
    uint8_t sig[BR_HAM_SIG_MAX];
    uint8_t scratch[BR_HAM_SIGNED_MAX];
    uint32_t refused;
} br_ham_rx_t;

br_status_t br_ham_rx_init(br_ham_rx_t *rx, br_ham_verify_fn verify, void *ctx);
/* Feed one AX.25 frame. 1 = a whole message verified: *text (inside rx)
 * and *text_len, *from = source station. 0 = more frames needed. <0 =
 * refused (bad frame, bad chunk, policy, or a signature that fails). */
int br_ham_rx_push(br_ham_rx_t *rx, const uint8_t *frame, uint32_t n, const uint8_t **text,
                   uint32_t *text_len, br_ax25_addr_t *from);

#endif /* ZXV_BR_HAM_H */
