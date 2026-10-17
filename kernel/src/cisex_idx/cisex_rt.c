/* Copyright (c) 2024-2026 Michael Laurence Curzi and 36N9 Genetics, LLC */
/* SPDX-License-Identifier: Apache-2.0 */
/* cisex_rt.c — the CISEX delegated-routing datagram responder: the bounded
 * packet handler an armada host binds to Vinea :8724 (or the door's HTTP
 * POST) to serve the global index.
 *
 * Wire format (one datagram = one op, replies cap at 4 index envelopes so a
 * response stays under a typical UDP payload):
 *
 *   WANT    [0x01][region][digest 32B]
 *           passive wantlist sniffing -> cisex_want; reply ACK [0xA1].
 *   KQ      [0x02][region][kw_len u8][kw]
 *           keyword query -> top-4 EpistemicRank index envelopes [0xB0][n][env...].
 *   DQ      [0x03][region][digest 32B]
 *           exact CID lookup -> [0xB1][1][env] or [0xB1][0] (unknown: and a
 *           cisex_want is recorded so the index learns demand).
 *   IDX     [0x04][region][envelope 168B]
 *           armada index sync -> cisex_merge; reply ACK [0xA1] or [0xA3] when
 *           the record failed verification (peer may retry with clean data).
 *   BOUNTY  [0x05][region]
 *           top-4 missing-block bounty envelopes [0xB2][n][env...].
 *
 * The handler only touches bounded tables: no allocation, no parsing state
 * carried between datagrams. Signing/encryption is the transport's job
 * (vna_session wraps the datagram in ML-KEM + AEAD + ML-DSA).
 */
#include "cisex_rt.h"

/* Pack up to cap-ranked entries as envelopes at *out; returns count. */
static uint32_t pack_top(cisex_t *x, const uint32_t *idx, uint32_t n, uint8_t region, uint8_t *out,
                         uint32_t cap, uint32_t *used)
{
    uint32_t packed = 0, off = 0;
    for (uint32_t i = 0; i < n && packed < CISEX_RT_MAX_ENV; i++) {
        const cisex_ent_t *e = cisex_at(x, idx[i]);
        if (!e) continue;
        if (off + CISEX_ENV_BYTES > cap) break;
        if (cisex_pack_idx(e, region, out + off) != IPFSN_OK) continue;
        off += CISEX_ENV_BYTES;
        packed++;
    }
    *used = off;
    return packed;
}

int cisex_rt_handle(cisex_t *x, const uint8_t *in, uint32_t len, uint8_t self_region,
                    uint8_t *out, uint32_t cap, uint32_t *out_len)
{
    uint32_t idx[CISEX_RT_MAX_ENV], n, used;
    uint8_t region;

    if (!x || !in || !out || !out_len || cap == 0 || self_region >= CISEX_REGIONS)
        return IPFSN_ERR_ARG;
    *out_len = 0;
    if (len < 2) return IPFSN_ERR_MALFORMED;
    region = in[1];
    if (region >= CISEX_REGIONS) return IPFSN_ERR_MALFORMED;

    switch (in[0]) {
    case CISEX_OP_WANT:
        if (len != 2 + 32) return IPFSN_ERR_MALFORMED;
        if (cisex_want(x, in + 2, region) == IPFSN_ERR_FULL) {
            out[0] = CISEX_ACK_FULL;
            *out_len = 1;
            return IPFSN_ERR_FULL;
        }
        out[0] = CISEX_ACK_OK;
        *out_len = 1;
        return IPFSN_OK;

    case CISEX_OP_KQ:
        if (len < 3 || len != 3 + in[2]) return IPFSN_ERR_MALFORMED;
        n = cisex_search(x, (const char *) in + 3, in[2], idx, CISEX_RT_MAX_ENV);
        if (cap < 2 || (n && cap < 2 + CISEX_ENV_BYTES)) return IPFSN_ERR_SPACE;
        used = 0;
        out[0] = CISEX_RSP_KQ;
        out[1] = (uint8_t) pack_top(x, idx, n, self_region, out + 2, cap - 2, &used);
        *out_len = 2 + used;
        return IPFSN_OK;

    case CISEX_OP_DQ:
        if (len != 2 + 32) return IPFSN_ERR_MALFORMED;
        if (cap < 2) return IPFSN_ERR_SPACE;
        {
            const cisex_ent_t *e = cisex_find(x, in + 2);
            out[0] = CISEX_RSP_DQ;
            if (!e) {
                /* unknown: record the demand so the index learns it */
                (void) cisex_want(x, in + 2, region);
                out[1] = 0;
                *out_len = 2;
                return IPFSN_OK;
            }
            if (cap < 2 + CISEX_ENV_BYTES) return IPFSN_ERR_SPACE;
            if (cisex_pack_idx(e, self_region, out + 2) != IPFSN_OK) return IPFSN_ERR_MALFORMED;
            out[1] = 1;
            *out_len = 2 + CISEX_ENV_BYTES;
        }
        return IPFSN_OK;

    case CISEX_OP_IDX:
        if (len != 2 + CISEX_ENV_BYTES) return IPFSN_ERR_MALFORMED;
        if (cisex_merge(x, in + 2) == IPFSN_OK) {
            out[0] = CISEX_ACK_OK;
            *out_len = 1;
            return IPFSN_OK;
        }
        out[0] = CISEX_ACK_BAD;
        *out_len = 1;
        return IPFSN_ERR_MALFORMED;

    case CISEX_OP_BOUNTY:
        n = cisex_bounty(x, idx, CISEX_RT_MAX_ENV);
        if (cap < 2 || (n && cap < 2 + CISEX_ENV_BYTES)) return IPFSN_ERR_SPACE;
        used = 0;
        out[0] = CISEX_RSP_BOUNTY;
        out[1] = (uint8_t) pack_top(x, idx, n, self_region, out + 2, cap - 2, &used);
        *out_len = 2 + used;
        return IPFSN_OK;

    default:
        return IPFSN_ERR_MALFORMED;
    }
}
