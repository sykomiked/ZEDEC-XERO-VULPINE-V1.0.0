/* Copyright (c) 2024-2026 Michael Laurence Curzi and 36N9 Genetics, LLC */
/* SPDX-License-Identifier: Apache-2.0 */
/* aham.h — AHAM, the amateur-radio profile of the protocol twins
 * (docs/P2P_PROTOCOL_TWINS.md rule P13, End-User Agreement section 4.1(e)).
 *
 * Builds and verifies the one frame that may be sent on an amateur band
 * (shape rules H1-H7 in src/net/ham_gate.h). The text and callsign travel in
 * the clear; the ML-DSA-87 signature, also in the clear, proves who sent it.
 * There is no encryption, no session, no payment, no settlement and no
 * attachment: the frame has no field for any of them, and the router refuses
 * anything else on an amateur bearer.
 */
#ifndef ZXV_AHAM_H
#define ZXV_AHAM_H

#include "../net/ham_gate.h"
#include "../vinea/vna_id.h"

#define AHAM_CTX "aproto/v1/aham"

_Static_assert(HAM_SIG_LEN == VNA_SIG_LEN, "the gate's signature length is ML-DSA-87's");

typedef struct {
    uint8_t precedence; /* HAM_PREC_* */
    uint8_t call_len;
    uint8_t call[HAM_CALL_MAX];
    uint64_t time;
    uint32_t seq;
    vna_id_t signer;
    uint16_t text_len;
    uint8_t text[HAM_TEXT_MAX];
} aham_msg_t;

/* Build and sign one frame (sets m->signer to self). Returns its length, or
 * VNA_ERR_PARSE if the message breaks H2-H5, VNA_ERR_SPACE if cap is short. */
int32_t aham_build(aham_msg_t *m, const vna_identity_t *self, const uint8_t rnd[32], uint8_t *out,
                   uint32_t cap);

/* Check the shape (H1-H7), the key binding (signer == SHA3-256(pk)) and the
 * signature, then decode into m. VNA_OK, VNA_ERR_PARSE, VNA_ERR_BINDING or
 * VNA_ERR_SIG. */
vna_status_t aham_open(const uint8_t *in, uint32_t len, const uint8_t pk[VNA_PK_LEN],
                       aham_msg_t *m);

#endif /* ZXV_AHAM_H */
