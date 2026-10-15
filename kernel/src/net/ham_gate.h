/* Copyright (c) 2024-2026 Michael Laurence Curzi and 36N9 Genetics, LLC */
/* SPDX-License-Identifier: Apache-2.0 */
/* ham_gate.h — the amateur-radio gate (docs/P2P_PROTOCOL_TWINS.md rule P13,
 * End-User Agreement section 4.1(e)).
 *
 * When the router sends on an amateur band (M5_PROTO_HAM_VHF: AX.25 / APRS;
 * M5_PROTO_HAM_HF: PSK31, RTTY and the like), the only bytes that may leave
 * are one AHAM frame: a cleartext, signed, non-commercial message. This is not
 * a setting. Selecting an amateur bearer turns encryption and every
 * value-bearing record off, because the frame has no place to carry either.
 *
 *   H1  magic "AHM1".
 *   H2  precedence: EMERGENCY, PRIORITY (life or safety), WELFARE (health and
 *       welfare), or DRILL (an exercise, marked as one). Nothing else.
 *   H3  the sending station's callsign (station identification): 3 to 10
 *       characters from A-Z, 0-9 and '/', with at least one letter and one
 *       digit.
 *   H4  time (u64 LE, ms), seq (u32 LE), signer (32 bytes: the NodeID).
 *   H5  text: 1 to HAM_TEXT_MAX bytes of printable ASCII (0x20..0x7E) or
 *       newline. Plain language only: no UTF-8, no control bytes, no binary.
 *   H6  signature: u16 length that must equal HAM_SIG_LEN, then the ML-DSA-87
 *       signature over every byte before the length (context
 *       "aproto/v1/aham"). It is sent in the clear: it authenticates the
 *       message and does not hide its meaning.
 *   H7  the frame is consumed exactly; nothing may follow.
 *
 * This header only checks the frame's shape, so the router (stratum
 * "transport") can enforce it without the post-quantum code. Building and
 * verifying the signature is src/aproto/aham.h.
 *
 * HONEST LIMIT: the gate cannot tell whether a person typed text that is
 * itself a code. It guarantees that the system never puts ciphertext, a
 * session record, a payment or a settlement on an amateur band; what a
 * licensed operator types is their responsibility under their licence.
 */
#ifndef ZXV_HAM_GATE_H
#define ZXV_HAM_GATE_H

#include <stdbool.h>
#include <stdint.h>

#define HAM_MAGIC0 'A'
#define HAM_MAGIC1 'H'
#define HAM_MAGIC2 'M'
#define HAM_MAGIC3 '1'

#define HAM_CALL_MIN 3u
#define HAM_CALL_MAX 10u
#define HAM_TEXT_MAX 1024u
#define HAM_SIG_LEN  4627u /* ML-DSA-87 (FIPS 204) */

enum { HAM_PREC_EMERGENCY = 1, HAM_PREC_PRIORITY = 2, HAM_PREC_WELFARE = 3, HAM_PREC_DRILL = 4 };

/* Fixed layout: magic 4, precedence 1, call_len 1, call, time 8, seq 4,
 * signer 32, text_len 2, text, sig_len 2, sig. */
#define HAM_FRAME_MAX                                                                              \
    (4u + 1u + 1u + HAM_CALL_MAX + 8u + 4u + 32u + 2u + HAM_TEXT_MAX + 2u + HAM_SIG_LEN)

static inline bool ham_call_ok(const uint8_t *c, uint32_t n)
{
    if (n < HAM_CALL_MIN || n > HAM_CALL_MAX) return false;
    bool letter = false, digit = false;
    for (uint32_t i = 0; i < n; i++) {
        uint8_t ch = c[i];
        if (ch >= 'A' && ch <= 'Z')
            letter = true;
        else if (ch >= '0' && ch <= '9')
            digit = true;
        else if (ch != '/')
            return false;
    }
    return letter && digit;
}

/* H1-H7. On success, *signed_len (if not NULL) is the length of the signed
 * region (every byte before the signature length). */
static inline bool ham_frame_ok(const void *data, uint32_t len, uint32_t *signed_len)
{
    const uint8_t *p = (const uint8_t *) data;
    if (!p || len < 6u) return false;
    if (p[0] != HAM_MAGIC0 || p[1] != HAM_MAGIC1 || p[2] != HAM_MAGIC2 || p[3] != HAM_MAGIC3)
        return false;
    if (p[4] < HAM_PREC_EMERGENCY || p[4] > HAM_PREC_DRILL) return false;
    uint32_t cl = p[5], off = 6u;
    if (cl > HAM_CALL_MAX || len - off < cl || !ham_call_ok(p + off, cl)) return false;
    off += cl;
    if (len - off < 8u + 4u + 32u + 2u) return false;
    off += 8u + 4u + 32u;
    uint32_t tl = (uint32_t) p[off] | ((uint32_t) p[off + 1] << 8);
    off += 2u;
    if (tl == 0 || tl > HAM_TEXT_MAX || len - off < tl) return false;
    for (uint32_t i = 0; i < tl; i++) {
        uint8_t ch = p[off + i];
        if (!(ch == '\n' || (ch >= 0x20 && ch <= 0x7e))) return false;
    }
    off += tl;
    if (len - off < 2u) return false;
    uint32_t sl = (uint32_t) p[off] | ((uint32_t) p[off + 1] << 8);
    if (sl != HAM_SIG_LEN || len - off - 2u != sl) return false; /* H6, H7 */
    if (signed_len) *signed_len = off;
    return true;
}

#endif /* ZXV_HAM_GATE_H */
