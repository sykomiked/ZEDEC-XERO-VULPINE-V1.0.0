/* Copyright (c) 2024-2026 Michael Laurence Curzi and 36N9 Genetics, LLC */
/* SPDX-License-Identifier: Apache-2.0 */
/* aham.c — the amateur-radio frame: build, sign, open. See aham.h. */
#include "aham.h"

int32_t aham_build(aham_msg_t *m, const vna_identity_t *self, const uint8_t rnd[32], uint8_t *out,
                   uint32_t cap)
{
    if (!m || !self || !rnd || !out) return VNA_ERR_ARG;
    if (m->call_len > HAM_CALL_MAX || m->text_len > HAM_TEXT_MAX) return VNA_ERR_PARSE;
    m->signer = self->id;
    uint32_t n = 6u + m->call_len + 8u + 4u + 32u + 2u + m->text_len;
    if (cap < n + 2u + HAM_SIG_LEN) return VNA_ERR_SPACE;
    out[0] = HAM_MAGIC0;
    out[1] = HAM_MAGIC1;
    out[2] = HAM_MAGIC2;
    out[3] = HAM_MAGIC3;
    out[4] = m->precedence;
    out[5] = m->call_len;
    uint32_t off = 6u;
    vna_copy(out + off, m->call, m->call_len);
    off += m->call_len;
    vna_put64(out + off, m->time);
    vna_put32(out + off + 8u, m->seq);
    vna_copy(out + off + 12u, m->signer.b, 32);
    off += 44u;
    vna_put16(out + off, m->text_len);
    vna_copy(out + off + 2u, m->text, m->text_len);
    off += 2u + m->text_len;
    vna_put16(out + off, (uint16_t) HAM_SIG_LEN);
    vna_sign(self->sk, AHAM_CTX, out, off, rnd, out + off + 2u);
    uint32_t len = off + 2u + HAM_SIG_LEN;
    uint32_t signed_len = 0;
    /* The gate is the single definition of a valid frame: build only what it passes. */
    if (!ham_frame_ok(out, len, &signed_len) || signed_len != off) return VNA_ERR_PARSE;
    return (int32_t) len;
}

vna_status_t aham_open(const uint8_t *in, uint32_t len, const uint8_t pk[VNA_PK_LEN], aham_msg_t *m)
{
    uint32_t sl = 0;
    if (!in || !pk || !m) return VNA_ERR_ARG;
    if (!ham_frame_ok(in, len, &sl)) return VNA_ERR_PARSE;
    vna_zero(m, sizeof *m);
    m->precedence = in[4];
    m->call_len = in[5];
    uint32_t off = 6u;
    vna_copy(m->call, in + off, m->call_len);
    off += m->call_len;
    m->time = vna_get64(in + off);
    m->seq = vna_get32(in + off + 8u);
    vna_copy(m->signer.b, in + off + 12u, 32);
    off += 44u;
    m->text_len = vna_get16(in + off);
    vna_copy(m->text, in + off + 2u, m->text_len);
    vna_id_t id;
    vna_node_id(pk, &id);
    if (!vna_id_eq(&id, &m->signer)) return VNA_ERR_BINDING;
    if (!vna_verify(pk, AHAM_CTX, in, sl, in + sl + 2u)) return VNA_ERR_SIG;
    return VNA_OK;
}
