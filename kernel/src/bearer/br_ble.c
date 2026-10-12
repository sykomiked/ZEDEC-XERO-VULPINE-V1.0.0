/* Copyright (c) 2024-2026 Michael Laurence Curzi and 36N9 Genetics, LLC */
/* SPDX-License-Identifier: Apache-2.0 */
/* br_ble.c — ATT payload sizes and the length-prefixed stream (br_ble.h). */
#include "br_ble.h"

int32_t br_ble_payload_mtu(uint32_t att_mtu)
{
    if (att_mtu < BR_ATT_MTU_MIN || att_mtu > BR_ATT_MTU_MAX) return BR_ERR_ARG;
    uint32_t p = att_mtu - 3u;
    return (int32_t) (p > BR_ATT_VAL_MAX ? BR_ATT_VAL_MAX : p);
}

int32_t br_lps_encode_hdr(uint32_t len, uint8_t hdr[BR_LPS_HDR])
{
    if (!hdr || len == 0) return BR_ERR_ARG;
    hdr[0] = (uint8_t) (len >> 24);
    hdr[1] = (uint8_t) (len >> 16);
    hdr[2] = (uint8_t) (len >> 8);
    hdr[3] = (uint8_t) len;
    return (int32_t) BR_LPS_HDR;
}

br_status_t br_lps_rx_init(br_lps_rx_t *rx, uint8_t *buf, uint32_t cap)
{
    if (!rx || !buf || cap == 0) return BR_ERR_ARG;
    br_mset(rx, 0, sizeof *rx);
    rx->buf = buf;
    rx->cap = cap;
    return BR_OK;
}

void br_lps_rx_reset(br_lps_rx_t *rx)
{
    if (!rx) return;
    rx->hn = 0;
    rx->want = 0;
    rx->have = 0;
    rx->dead = 0;
}

int br_lps_rx_push(br_lps_rx_t *rx, const uint8_t *p, uint32_t n, uint32_t *used, uint32_t *len)
{
    if (!rx || !rx->buf || (!p && n) || !used || !len) return BR_ERR_ARG;
    *used = 0;
    if (rx->dead) return BR_ERR_SIZE;
    uint32_t i = 0;
    while (i < n) {
        if (rx->hn < BR_LPS_HDR) {
            rx->hdr[rx->hn++] = p[i++];
            if (rx->hn == BR_LPS_HDR) {
                uint32_t w = ((uint32_t) rx->hdr[0] << 24) | ((uint32_t) rx->hdr[1] << 16) |
                             ((uint32_t) rx->hdr[2] << 8) | rx->hdr[3];
                if (w == 0 || w > rx->cap) {
                    rx->dead = 1;
                    *used = i;
                    return BR_ERR_SIZE;
                }
                rx->want = w;
                rx->have = 0;
            }
            continue;
        }
        uint32_t take = n - i;
        if (take > rx->want - rx->have) take = rx->want - rx->have;
        br_mcpy(rx->buf + rx->have, p + i, take);
        rx->have += take;
        i += take;
        if (rx->have == rx->want) {
            *len = rx->want;
            *used = i;
            rx->hn = 0;
            rx->want = 0;
            rx->have = 0;
            return 1;
        }
    }
    *used = i;
    return 0;
}
