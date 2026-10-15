/* Copyright (c) 2024-2026 Michael Laurence Curzi and 36N9 Genetics, LLC */
/* SPDX-License-Identifier: Apache-2.0 */
/* br_frag.c — fragmentation and reassembly (rules F1..F4 in br_frag.h). */
#include "br_frag.h"

uint32_t br_frag_count(uint32_t msg_len, uint32_t mtu)
{
    if (mtu < BR_FRAG_MIN_MTU || msg_len == 0 || msg_len > BR_FRAG_MAX_MSG) return 0;
    uint32_t first = mtu - BR_FRAG_HDR1;
    if (msg_len <= first) return 1;
    uint32_t rest = msg_len - first, per = mtu - BR_FRAG_HDRN;
    return 1u + (rest + per - 1u) / per;
}

br_status_t br_frag_tx_init(br_frag_tx_t *tx, const uint8_t *msg, uint32_t len, uint32_t mtu,
                            uint8_t ctr)
{
    if (!tx || !msg || br_frag_count(len, mtu) == 0) return BR_ERR_ARG;
    tx->msg = msg;
    tx->len = len;
    tx->off = 0;
    tx->idx = 0;
    tx->mtu = mtu;
    tx->ctr = (uint8_t) (ctr & 0x7Fu);
    return BR_OK;
}

int32_t br_frag_tx_next(br_frag_tx_t *tx, uint8_t *out, uint32_t cap)
{
    if (!tx || !out || !tx->msg) return BR_ERR_ARG;
    if (tx->off >= tx->len) return 0;
    uint32_t mtu = tx->mtu < cap ? tx->mtu : cap;
    uint32_t h = tx->idx == 0 ? BR_FRAG_HDR1 : BR_FRAG_HDRN;
    if (mtu <= h) return BR_ERR_SIZE;
    uint32_t take = tx->len - tx->off;
    if (take > mtu - h) take = mtu - h;
    if (tx->idx == 0) {
        out[0] = (uint8_t) (BR_FRAG_FIRST | tx->ctr);
        out[1] = 0;
        out[2] = (uint8_t) (tx->len >> 16);
        out[3] = (uint8_t) (tx->len >> 8);
        out[4] = (uint8_t) tx->len;
    } else {
        out[0] = tx->ctr;
        out[1] = (uint8_t) tx->idx;
    }
    br_mcpy(out + h, tx->msg + tx->off, take);
    tx->off += take;
    tx->idx++;
    return (int32_t) (h + take);
}

br_status_t br_frag_rx_init(br_frag_rx_t *rx, uint8_t *buf, uint32_t cap)
{
    if (!rx || !buf || cap == 0) return BR_ERR_ARG;
    br_mset(rx, 0, sizeof *rx);
    rx->buf = buf;
    rx->cap = cap;
    return BR_OK;
}

static int drop(br_frag_rx_t *rx, int rc)
{
    rx->active = 0;
    rx->drops++;
    return rc;
}

int br_frag_rx_push(br_frag_rx_t *rx, const uint8_t *frag, uint32_t n, uint32_t *len)
{
    if (!rx || !rx->buf || !frag || !len) return BR_ERR_ARG;
    if (n < BR_FRAG_HDRN) return drop(rx, BR_ERR_FORMAT);
    if (frag[0] & BR_FRAG_FIRST) {
        /* F2: a first fragment always restarts. */
        if (rx->active) rx->drops++;
        rx->active = 0;
        if (n < BR_FRAG_HDR1 + 1u || frag[1] != 0) return drop(rx, BR_ERR_FORMAT);
        uint32_t want = ((uint32_t) frag[2] << 16) | ((uint32_t) frag[3] << 8) | frag[4];
        if (want == 0) return drop(rx, BR_ERR_FORMAT);
        if (want > rx->cap) return drop(rx, BR_ERR_SIZE); /* F3 */
        uint32_t d = n - BR_FRAG_HDR1;
        if (d > want) return drop(rx, BR_ERR_FORMAT);
        rx->ctr = (uint8_t) (frag[0] & 0x7Fu);
        rx->want = want;
        rx->have = d;
        rx->idx = 1;
        br_mcpy(rx->buf, frag + BR_FRAG_HDR1, d);
        rx->active = 1;
    } else {
        if (!rx->active) return drop(rx, BR_ERR_STATE);
        if (frag[0] != rx->ctr || frag[1] != (uint8_t) rx->idx) return drop(rx, BR_ERR_STATE);
        uint32_t d = n - BR_FRAG_HDRN;
        if (d == 0 || d > rx->want - rx->have) return drop(rx, BR_ERR_FORMAT);
        br_mcpy(rx->buf + rx->have, frag + BR_FRAG_HDRN, d);
        rx->have += d;
        rx->idx++;
    }
    if (rx->have == rx->want) {
        rx->active = 0;
        *len = rx->want;
        return 1;
    }
    return 0;
}
