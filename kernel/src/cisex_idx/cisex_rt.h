/* Copyright (c) 2024-2026 Michael Laurence Curzi and 36N9 Genetics, LLC */
/* SPDX-License-Identifier: Apache-2.0 */
/* cisex_rt.h — the CISEX delegated-routing datagram responder (see
 * cisex_rt.c for the wire format). */
#ifndef CISEX_RT_H
#define CISEX_RT_H

#include "cisex_idx.h"

/* Request ops (in[0]). */
typedef enum {
    CISEX_OP_WANT = 0x01,   /* [op][region][digest 32B]  passive wantlist    */
    CISEX_OP_KQ = 0x02,     /* [op][region][kw_len][kw]  keyword query       */
    CISEX_OP_DQ = 0x03,     /* [op][region][digest 32B]  exact CID lookup    */
    CISEX_OP_IDX = 0x04,    /* [op][region][env 168B]    armada index sync   */
    CISEX_OP_BOUNTY = 0x05  /* [op][region]              top bounty list     */
} cisex_op_t;

/* Reply tags (out[0]). */
enum {
    CISEX_ACK_OK = 0xA1,     /* accepted                                     */
    CISEX_ACK_BAD = 0xA3,    /* envelope failed verification                 */
    CISEX_ACK_FULL = 0xA4,   /* table full: the want could not be recorded   */
    CISEX_RSP_KQ = 0xB0,     /* [tag][n][env...] keyword results             */
    CISEX_RSP_DQ = 0xB1,     /* [tag][1][env] hit, [tag][0] miss             */
    CISEX_RSP_BOUNTY = 0xB2  /* [tag][n][env...] missing-block bounties      */
};

/* Index envelopes per reply; 4 * 168 = 672 B stays inside one UDP payload. */
#define CISEX_RT_MAX_ENV 4u

/* Handle one CISEX datagram against the local index. Writes the reply into
 * `out` (always at least a 1-byte ACK on a well-formed request); *out_len is
 * the reply length. IPFSN_OK when a reply was produced; a negative status
 * means drop the datagram (malformed, or out buffer too small). Signing,
 * sequencing and transport are the caller's (vna_session). */
int cisex_rt_handle(cisex_t *x, const uint8_t *in, uint32_t len, uint8_t self_region,
                    uint8_t *out, uint32_t cap, uint32_t *out_len);

#endif /* CISEX_RT_H */
