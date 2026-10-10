/* bluetooth.c — ZEDEC XERO pqOS Bluetooth host stack (BR/EDR)
 *
 * The split here is deliberate and matches the virtio-net driver in this tree:
 * everything in this file is portable logic with no MMIO and no timing
 * assumptions, and the single line to the silicon is bt_ops_t. That is why the
 * whole protocol layer can be driven from a test with a loopback controller,
 * and why an unbound stack fails loudly (BT_ENODEV) instead of pretending.
 *
 * Read the LIMITATIONS block in bluetooth.h before trusting any capability
 * name in this file. In particular: no LE, no GATT, no SSP, no SBC encoder,
 * no SDP, no SCO, and no RFCOMM credit flow control.
 *
 * Author: H.M. Michael-Laurence: Curzi (c)
 * Copyright (c) 2024-2026 Michael Laurence Curzi and 36N9 Genetics, LLC
 * SPDX-License-Identifier: Apache-2.0
 * Licensed under the Apache License, Version 2.0. See LICENSE at
 * the repository root.
 */

#ifdef TEST_HOST
#include <string.h>
#include <stdio.h>
#include <stdlib.h>
#else
#include "freestanding.h"
#endif

#include "bluetooth.h"

/* ===================== small local helpers =====================
 * No libc: the kernel builds -ffreestanding -nostdlib. */

static void bts_memset(void *dst, int v, uint32_t n) {
    uint8_t *d = (uint8_t *)dst;
    for (uint32_t i = 0; i < n; i++) d[i] = (uint8_t)v;
}

static void bts_memcpy(void *dst, const void *src, uint32_t n) {
    uint8_t *d = (uint8_t *)dst;
    const uint8_t *s = (const uint8_t *)src;
    for (uint32_t i = 0; i < n; i++) d[i] = s[i];
}

static int bts_memcmp(const void *a, const void *b, uint32_t n) {
    const uint8_t *pa = (const uint8_t *)a;
    const uint8_t *pb = (const uint8_t *)b;
    for (uint32_t i = 0; i < n; i++)
        if (pa[i] != pb[i]) return (int)pa[i] - (int)pb[i];
    return 0;
}

static void bts_strcpy(char *dst, const char *src, uint32_t max) {
    uint32_t i = 0;
    if (max == 0) return;
    for (; i + 1 < max && src && src[i]; i++) dst[i] = src[i];
    dst[i] = '\0';
}

static uint32_t bts_strlen(const char *s) {
    uint32_t n = 0;
    if (!s) return 0;
    while (s[n]) n++;
    return n;
}

static uint16_t bts_le16(const uint8_t *p) {
    return (uint16_t)((uint16_t)p[0] | ((uint16_t)p[1] << 8));
}

static void bts_put16(uint8_t *p, uint16_t v) {
    p[0] = (uint8_t)(v & 0xFF);
    p[1] = (uint8_t)(v >> 8);
}

/* ===================== pure codec: HCI ===================== */

uint16_t bt_hci_opcode(uint8_t ogf, uint16_t ocf) {
    return (uint16_t)(((uint16_t)(ogf & 0x3F) << 10) | (ocf & 0x03FF));
}

uint8_t bt_hci_opcode_ogf(uint16_t opcode) {
    return (uint8_t)((opcode >> 10) & 0x3F);
}

uint16_t bt_hci_opcode_ocf(uint16_t opcode) {
    return (uint16_t)(opcode & 0x03FF);
}

/* Command packet: opcode (2, LE) | parameter total length (1) | parameters. */
int bt_hci_build_cmd(uint8_t *buf, uint32_t cap, uint16_t opcode,
                     const uint8_t *params, uint32_t plen) {
    if (!buf) return BT_EINVAL;
    if (plen > BT_HCI_MAX_PARAM) return BT_EMSGSIZE;
    if (plen > 0 && !params) return BT_EINVAL;
    if (cap < BT_HCI_CMD_HDR_LEN + plen) return BT_ENOSPC;
    bts_put16(buf, opcode);
    buf[2] = (uint8_t)plen;
    if (plen) bts_memcpy(buf + BT_HCI_CMD_HDR_LEN, params, plen);
    return (int)(BT_HCI_CMD_HDR_LEN + plen);
}

int bt_hci_parse_cmd(const uint8_t *buf, uint32_t len, bt_hci_cmd_t *out) {
    if (!buf || !out) return BT_EINVAL;
    if (len < BT_HCI_CMD_HDR_LEN) return BT_EPROTO;
    uint32_t plen = buf[2];
    if (len != BT_HCI_CMD_HDR_LEN + plen) return BT_EPROTO;
    out->opcode = bts_le16(buf);
    out->ogf = bt_hci_opcode_ogf(out->opcode);
    out->ocf = bt_hci_opcode_ocf(out->opcode);
    out->plen = (uint8_t)plen;
    out->params = plen ? (buf + BT_HCI_CMD_HDR_LEN) : (const uint8_t *)0;
    return BT_OK;
}

/* Event packet: event code (1) | parameter total length (1) | parameters. */
int bt_hci_build_event(uint8_t *buf, uint32_t cap, uint8_t code,
                       const uint8_t *params, uint32_t plen) {
    if (!buf) return BT_EINVAL;
    if (plen > BT_HCI_MAX_PARAM) return BT_EMSGSIZE;
    if (plen > 0 && !params) return BT_EINVAL;
    if (cap < BT_HCI_EVT_HDR_LEN + plen) return BT_ENOSPC;
    buf[0] = code;
    buf[1] = (uint8_t)plen;
    if (plen) bts_memcpy(buf + BT_HCI_EVT_HDR_LEN, params, plen);
    return (int)(BT_HCI_EVT_HDR_LEN + plen);
}

int bt_hci_parse_event(const uint8_t *buf, uint32_t len, bt_hci_event_t *out) {
    if (!buf || !out) return BT_EINVAL;
    if (len < BT_HCI_EVT_HDR_LEN) return BT_EPROTO;
    uint32_t plen = buf[1];
    if (len != BT_HCI_EVT_HDR_LEN + plen) return BT_EPROTO;
    out->code = buf[0];
    out->plen = (uint8_t)plen;
    out->params = plen ? (buf + BT_HCI_EVT_HDR_LEN) : (const uint8_t *)0;
    return BT_OK;
}

/* ACL packet: handle:12 | PB:2 | BC:2 (one LE word) | data length (2, LE). */
int bt_hci_build_acl(uint8_t *buf, uint32_t cap, uint16_t handle,
                     uint8_t pb, uint8_t bc, const uint8_t *data, uint32_t dlen) {
    if (!buf) return BT_EINVAL;
    if (handle > 0x0FFF || pb > 3 || bc > 3) return BT_EINVAL;
    if (dlen > 0xFFFF) return BT_EMSGSIZE;
    if (dlen > 0 && !data) return BT_EINVAL;
    if (cap < BT_HCI_ACL_HDR_LEN + dlen) return BT_ENOSPC;
    uint16_t word = (uint16_t)(handle | ((uint16_t)pb << 12) | ((uint16_t)bc << 14));
    bts_put16(buf, word);
    bts_put16(buf + 2, (uint16_t)dlen);
    if (dlen) bts_memcpy(buf + BT_HCI_ACL_HDR_LEN, data, dlen);
    return (int)(BT_HCI_ACL_HDR_LEN + dlen);
}

int bt_hci_parse_acl(const uint8_t *buf, uint32_t len, bt_hci_acl_t *out) {
    if (!buf || !out) return BT_EINVAL;
    if (len < BT_HCI_ACL_HDR_LEN) return BT_EPROTO;
    uint16_t word = bts_le16(buf);
    uint32_t dlen = bts_le16(buf + 2);
    if (len != BT_HCI_ACL_HDR_LEN + dlen) return BT_EPROTO;
    out->handle = (uint16_t)(word & 0x0FFF);
    out->pb = (uint8_t)((word >> 12) & 0x03);
    out->bc = (uint8_t)((word >> 14) & 0x03);
    out->dlen = (uint16_t)dlen;
    out->data = dlen ? (buf + BT_HCI_ACL_HDR_LEN) : (const uint8_t *)0;
    return BT_OK;
}

int bt_h4_wrap(uint8_t *buf, uint32_t cap, uint8_t pkt_type,
               const uint8_t *pkt, uint32_t len) {
    if (!buf || (!pkt && len)) return BT_EINVAL;
    if (pkt_type < BT_H4_CMD || pkt_type > BT_H4_EVT) return BT_EINVAL;
    /* Written as a SUBTRACTION, never `cap < len + 1`: len is caller-supplied
     * and len+1 wraps to 0 at UINT32_MAX, which turns the capacity test into a
     * no-op and the memcpy below into a 4 GiB overrun. Same for every other
     * capacity test in this file. */
    if (cap < 1u || len > cap - 1u) return BT_ENOSPC;
    buf[0] = pkt_type;
    if (len) bts_memcpy(buf + 1, pkt, len);
    return (int)(len + 1);
}

int bt_h4_unwrap(const uint8_t *buf, uint32_t len, uint8_t *pkt_type,
                 const uint8_t **pkt, uint32_t *pkt_len) {
    if (!buf || !pkt_type || !pkt || !pkt_len) return BT_EINVAL;
    if (len < 2) return BT_EPROTO;
    if (buf[0] < BT_H4_CMD || buf[0] > BT_H4_EVT) return BT_EPROTO;
    *pkt_type = buf[0];
    *pkt = buf + 1;
    *pkt_len = len - 1;
    return BT_OK;
}

/* ===================== pure codec: L2CAP ===================== */

int bt_l2cap_build(uint8_t *buf, uint32_t cap, uint16_t cid,
                   const uint8_t *payload, uint32_t n) {
    if (!buf) return BT_EINVAL;
    if (n > 0xFFFF) return BT_EMSGSIZE;
    if (n > 0 && !payload) return BT_EINVAL;
    if (cap < BT_L2CAP_HDR_LEN + n) return BT_ENOSPC;
    bts_put16(buf, (uint16_t)n);
    bts_put16(buf + 2, cid);
    if (n) bts_memcpy(buf + BT_L2CAP_HDR_LEN, payload, n);
    return (int)(BT_L2CAP_HDR_LEN + n);
}

int bt_l2cap_parse(const uint8_t *buf, uint32_t len, bt_l2cap_frame_t *out) {
    if (!buf || !out) return BT_EINVAL;
    if (len < BT_L2CAP_HDR_LEN) return BT_EPROTO;
    uint32_t n = bts_le16(buf);
    if (len != BT_L2CAP_HDR_LEN + n) return BT_EPROTO;
    out->len = (uint16_t)n;
    out->cid = bts_le16(buf + 2);
    out->payload = n ? (buf + BT_L2CAP_HDR_LEN) : (const uint8_t *)0;
    return BT_OK;
}

/* Signalling command: code (1) | identifier (1) | length (2, LE) | data. */
int bt_l2cap_build_sig(uint8_t *buf, uint32_t cap, uint8_t code, uint8_t ident,
                       const uint8_t *data, uint32_t n) {
    if (!buf) return BT_EINVAL;
    if (n > 0xFFFF) return BT_EMSGSIZE;
    if (n > 0 && !data) return BT_EINVAL;
    if (cap < 4 + n) return BT_ENOSPC;
    /* Identifier 0 is reserved by the spec — it can never match a response. */
    if (ident == 0) return BT_EINVAL;
    buf[0] = code;
    buf[1] = ident;
    bts_put16(buf + 2, (uint16_t)n);
    if (n) bts_memcpy(buf + 4, data, n);
    return (int)(4 + n);
}

int bt_l2cap_parse_sig(const uint8_t *buf, uint32_t len, bt_l2cap_sig_t *out) {
    if (!buf || !out) return BT_EINVAL;
    if (len < 4) return BT_EPROTO;
    uint32_t n = bts_le16(buf + 2);
    if (len != 4 + n) return BT_EPROTO;
    out->code = buf[0];
    out->ident = buf[1];
    out->len = (uint16_t)n;
    out->data = n ? (buf + 4) : (const uint8_t *)0;
    return BT_OK;
}

/* ===================== fragmentation / reassembly ===================== */

uint32_t bt_acl_frag_count(uint32_t pdu_len, uint16_t acl_mtu) {
    if (acl_mtu == 0 || pdu_len == 0) return 0;
    /* Divide-then-adjust, not (pdu_len + acl_mtu - 1): the round-up form wraps
     * for pdu_len near UINT32_MAX and would report FEWER fragments than the PDU
     * needs, silently truncating it. */
    uint32_t whole = pdu_len / acl_mtu;
    return (pdu_len % acl_mtu) ? whole + 1u : whole;
}

int bt_acl_fragment(uint8_t *out, uint32_t cap, uint16_t handle, uint16_t acl_mtu,
                    const uint8_t *pdu, uint32_t pdu_len, uint32_t index) {
    if (!out || !pdu) return BT_EINVAL;
    if (acl_mtu == 0 || pdu_len == 0) return BT_EINVAL;
    uint32_t nfrag = bt_acl_frag_count(pdu_len, acl_mtu);
    if (index >= nfrag) return 0;
    uint32_t off = index * (uint32_t)acl_mtu;
    uint32_t n = pdu_len - off;
    if (n > acl_mtu) n = acl_mtu;
    uint8_t pb = (index == 0) ? BT_PB_START_FLUSH : BT_PB_CONTINUE;
    return bt_hci_build_acl(out, cap, handle, pb, BT_BC_POINT_TO_POINT,
                            pdu + off, n);
}

void bt_reasm_init(bt_reasm_t *r) {
    if (!r) return;
    bts_memset(r, 0, (uint32_t)sizeof(*r));
}

int bt_reasm_push(bt_reasm_t *r, const uint8_t *acl_pkt, uint32_t len) {
    if (!r || !acl_pkt) return BT_EINVAL;
    bt_hci_acl_t a;
    int rc = bt_hci_parse_acl(acl_pkt, len, &a);
    if (rc != BT_OK) { r->drops++; return BT_EPROTO; }

    /* PB flag on the CONTROLLER-to-HOST direction is 0b10 (first
     * automatically-flushable) or, on LE, 0b11 (a complete PDU). 0b00 exists
     * but is host-to-controller only, so a controller sending it to us is out
     * of spec and we say so rather than guessing what it meant. */
    if (a.pb == BT_PB_START_FLUSH || a.pb == 0x03) {
        /* A start fragment abandons anything half-assembled. */
        if (r->active) r->drops++;
        r->have = 0;
        r->want = 0;
        r->handle = a.handle;
        r->active = true;
    } else if (a.pb == BT_PB_CONTINUE) {
        if (!r->active) { r->drops++; return BT_EPROTO; }
        if (r->handle != a.handle) { r->drops++; return BT_EPROTO; }
    } else {
        r->drops++;
        return BT_EPROTO;
    }

    if (a.dlen > 0) {
        if (r->have + a.dlen > BT_REASM_MAX) {
            r->drops++;
            r->active = false;
            r->have = 0;
            r->want = 0;
            return BT_ENOSPC;
        }
        bts_memcpy(r->buf + r->have, a.data, a.dlen);
        r->have += a.dlen;
    }

    if (r->want == 0 && r->have >= BT_L2CAP_HDR_LEN)
        r->want = (uint32_t)bts_le16(r->buf) + BT_L2CAP_HDR_LEN;

    if (r->want != 0 && r->have > r->want) {
        /* The peer sent more than its own length field promised. */
        r->drops++;
        r->active = false;
        r->have = 0;
        r->want = 0;
        return BT_EPROTO;
    }
    if (r->want != 0 && r->have == r->want) {
        r->active = false;
        return (int)r->want;
    }
    return 0;
}

const uint8_t *bt_reasm_pdu(const bt_reasm_t *r, uint32_t *len) {
    if (!r || !len) return (const uint8_t *)0;
    if (r->want == 0 || r->have != r->want) { *len = 0; return (const uint8_t *)0; }
    *len = r->want;
    return r->buf;
}

/* ===================== pure codec: RFCOMM =====================
 * GSM 07.10 FCS: CRC-8 with the reflected polynomial 0xE0 (x^8+x^2+x+1),
 * initial value 0xFF, and the final register subtracted from 0xFF. The table
 * is built once at first use so there is no 256-byte literal to mistype; the
 * test cross-checks it against a bit-at-a-time reference AND against the
 * published SABM/UA vectors. */
static uint8_t bts_fcs_tab[256];
static bool bts_fcs_ready;

static void bts_fcs_build(void) {
    for (uint32_t i = 0; i < 256; i++) {
        uint8_t c = (uint8_t)i;
        for (int b = 0; b < 8; b++)
            c = (uint8_t)((c & 1) ? ((c >> 1) ^ 0xE0) : (c >> 1));
        bts_fcs_tab[i] = c;
    }
    bts_fcs_ready = true;
}

uint8_t bt_rfcomm_fcs(const uint8_t *hdr, uint32_t n) {
    if (!bts_fcs_ready) bts_fcs_build();
    uint8_t c = 0xFF;
    if (!hdr) return 0;
    for (uint32_t i = 0; i < n; i++) c = bts_fcs_tab[c ^ hdr[i]];
    return (uint8_t)(0xFF - c);
}

/* Address octet: EA=1 (bit0) | C/R (bit1) | DLCI (bits 2..7). */
static uint8_t bts_rfcomm_addr(uint8_t dlci, uint8_t cr) {
    return (uint8_t)(((dlci & 0x3F) << 2) | ((cr & 1) << 1) | 0x01);
}

int bt_rfcomm_build(uint8_t *buf, uint32_t cap, uint8_t dlci, uint8_t cr,
                    uint8_t ctrl, bool pf, const uint8_t *data, uint32_t n) {
    if (!buf) return BT_EINVAL;
    if (dlci > 0x3F) return BT_EINVAL;
    if (n > 0 && !data) return BT_EINVAL;
    if (n > 0x3FFF) return BT_EMSGSIZE;
    bool uih = ((ctrl & 0xEF) == BT_RFCOMM_UIH);
    uint32_t lenbytes = (n < 128) ? 1u : 2u;
    uint32_t total = 2 + lenbytes + n + 1;
    if (cap < total) return BT_ENOSPC;

    buf[0] = bts_rfcomm_addr(dlci, cr);
    buf[1] = (uint8_t)(pf ? (ctrl | 0x10) : (ctrl & ~0x10));
    if (lenbytes == 1) {
        buf[2] = (uint8_t)((n << 1) | 0x01);          /* EA=1, 7-bit length */
    } else {
        buf[2] = (uint8_t)((n & 0x7F) << 1);          /* EA=0 */
        buf[3] = (uint8_t)(n >> 7);
    }
    if (n) bts_memcpy(buf + 2 + lenbytes, data, n);
    /* FCS covers address+control for UIH, and address+control+length for
     * every other frame type (RFCOMM 5.1.1). */
    buf[2 + lenbytes + n] = bt_rfcomm_fcs(buf, uih ? 2u : 3u);
    return (int)total;
}

int bt_rfcomm_parse(const uint8_t *buf, uint32_t len, bt_rfcomm_frame_t *out) {
    if (!buf || !out) return BT_EINVAL;
    if (len < 4) return BT_EPROTO;             /* addr+ctrl+len+fcs minimum */
    uint8_t addr = buf[0];
    if ((addr & 0x01) == 0) return BT_EPROTO;  /* EA must terminate here */
    uint32_t lenbytes = (buf[2] & 0x01) ? 1u : 2u;
    if (len < 2 + lenbytes + 1) return BT_EPROTO;
    uint32_t n;
    if (lenbytes == 1) n = (uint32_t)(buf[2] >> 1);
    else               n = (uint32_t)(buf[2] >> 1) | ((uint32_t)buf[3] << 7);
    if (len != 2 + lenbytes + n + 1) return BT_EPROTO;

    out->dlci = (uint8_t)(addr >> 2);
    out->cr   = (uint8_t)((addr >> 1) & 0x01);
    out->pf   = (buf[1] & 0x10) != 0;
    out->ctrl = (uint8_t)(buf[1] & ~0x10);
    out->len  = (uint16_t)n;
    out->data = n ? (buf + 2 + lenbytes) : (const uint8_t *)0;
    out->fcs  = buf[2 + lenbytes + n];
    bool uih = (out->ctrl == BT_RFCOMM_UIH);
    out->fcs_ok = (out->fcs == bt_rfcomm_fcs(buf, uih ? 2u : 3u));
    return BT_OK;
}

/* ===================== pure codec: AVDTP / HIDP / RTP ===================== */

int bt_avdtp_build(uint8_t *buf, uint32_t cap, uint8_t label, uint8_t msg_type,
                   uint8_t signal_id, const uint8_t *params, uint32_t n) {
    if (!buf) return BT_EINVAL;
    if (label > 0x0F || msg_type > 0x03) return BT_EINVAL;
    if (signal_id == 0 || signal_id > 0x3F) return BT_EINVAL;
    if (n > 0 && !params) return BT_EINVAL;
    if (cap < 2u || n > cap - 2u) return BT_ENOSPC;   /* subtraction: see bt_h4_wrap */
    /* Header octet 0: label:4 | packet type:2 | message type:2. Single packet
     * only — this stack never fragments a signalling message. */
    buf[0] = (uint8_t)((label << 4) | (BT_AVDTP_PKT_SINGLE << 2) | msg_type);
    buf[1] = (uint8_t)(signal_id & 0x3F);
    if (n) bts_memcpy(buf + 2, params, n);
    return (int)(2 + n);
}

int bt_avdtp_parse(const uint8_t *buf, uint32_t len, bt_avdtp_msg_t *out) {
    if (!buf || !out) return BT_EINVAL;
    if (len < 2) return BT_EPROTO;
    out->label     = (uint8_t)(buf[0] >> 4);
    out->pkt_type  = (uint8_t)((buf[0] >> 2) & 0x03);
    out->msg_type  = (uint8_t)(buf[0] & 0x03);
    if (out->pkt_type != BT_AVDTP_PKT_SINGLE) return BT_ENOTSUP;
    out->signal_id = (uint8_t)(buf[1] & 0x3F);
    out->plen      = (uint16_t)(len - 2);
    out->params    = (len > 2) ? (buf + 2) : (const uint8_t *)0;
    return BT_OK;
}

int bt_hidp_build(uint8_t *buf, uint32_t cap, uint8_t trans_type,
                  uint8_t param, const uint8_t *data, uint32_t n) {
    if (!buf) return BT_EINVAL;
    if (trans_type > 0x0F || param > 0x0F) return BT_EINVAL;
    if (n > 0 && !data) return BT_EINVAL;
    if (cap < 1u || n > cap - 1u) return BT_ENOSPC;   /* subtraction: see bt_h4_wrap */
    buf[0] = (uint8_t)((trans_type << 4) | param);
    if (n) bts_memcpy(buf + 1, data, n);
    return (int)(1 + n);
}

int bt_hidp_parse(const uint8_t *buf, uint32_t len, uint8_t *trans_type,
                  uint8_t *param, const uint8_t **payload, uint32_t *plen) {
    if (!buf || !trans_type || !param || !payload || !plen) return BT_EINVAL;
    if (len < 1) return BT_EPROTO;
    *trans_type = (uint8_t)(buf[0] >> 4);
    *param      = (uint8_t)(buf[0] & 0x0F);
    *payload    = (len > 1) ? (buf + 1) : (const uint8_t *)0;
    *plen       = len - 1;
    return BT_OK;
}

/* RTP fixed header (RFC 3550 §5.1) followed by the A2DP SBC media header
 * (A2DP 4.3.4): fragmentation/starting/last flags clear, frame count in the
 * low 4 bits. Sequence number, timestamp and SSRC are big-endian. */
int bt_a2dp_media_header(uint8_t *buf, uint32_t cap, uint16_t seq, uint32_t ts,
                         uint32_t ssrc, uint8_t frame_count) {
    if (!buf) return BT_EINVAL;
    if (frame_count == 0 || frame_count > 0x0F) return BT_EINVAL;
    if (cap < BT_A2DP_RTP_HDR_LEN + 1u) return BT_ENOSPC;
    buf[0] = 0x80;                                    /* V=2, P=0, X=0, CC=0 */
    buf[1] = (uint8_t)(BT_A2DP_RTP_PAYLOAD_TYPE & 0x7F); /* M=0, PT=96 */
    buf[2] = (uint8_t)(seq >> 8);
    buf[3] = (uint8_t)(seq & 0xFF);
    buf[4] = (uint8_t)(ts >> 24);
    buf[5] = (uint8_t)(ts >> 16);
    buf[6] = (uint8_t)(ts >> 8);
    buf[7] = (uint8_t)(ts);
    buf[8]  = (uint8_t)(ssrc >> 24);
    buf[9]  = (uint8_t)(ssrc >> 16);
    buf[10] = (uint8_t)(ssrc >> 8);
    buf[11] = (uint8_t)(ssrc);
    buf[12] = (uint8_t)(frame_count & 0x0F);
    return (int)(BT_A2DP_RTP_HDR_LEN + 1);
}

/* ===================== Class of Device decoding =====================
 * Assigned Numbers, Baseband: bits 8..12 major device class, bits 2..7 minor.
 * Anything we do not have a bucket for stays BT_CLASS_UNKNOWN rather than
 * being guessed into the nearest enum. */
bt_class_t bt_class_from_cod(uint32_t cod) {
    uint32_t major = (cod >> 8) & 0x1F;
    uint32_t minor = (cod >> 2) & 0x3F;
    switch (major) {
        case 0x01: return BT_CLASS_COMPUTER;
        case 0x02: return BT_CLASS_PHONE;
        case 0x04:                                    /* Audio/Video */
            /* 0x01 wearable headset, 0x02 hands-free, 0x06 headphones. 0x04 is
             * MICROPHONE and 0x07 is portable audio — neither is a headset, and
             * there is no bucket for them, so they stay UNKNOWN rather than
             * being reported as something the user could try to route calls to. */
            if (minor == 0x01 || minor == 0x02 || minor == 0x06)
                return BT_CLASS_HEADSET;
            if (minor == 0x05) return BT_CLASS_SPEAKER;   /* loudspeaker */
            return BT_CLASS_UNKNOWN;
        case 0x05: {                                  /* Peripheral */
            uint32_t kbd_ptr = (cod >> 6) & 0x03;
            if (kbd_ptr == 0x01) return BT_CLASS_KEYBOARD;
            if (kbd_ptr == 0x02) return BT_CLASS_MOUSE;
            if (kbd_ptr == 0x03) return BT_CLASS_KEYBOARD;  /* combo */
            /* Peripheral device type, CoD bits 2..5: 0x01 joystick,
             * 0x02 gamepad. 0x03 is a REMOTE CONTROL, which is not a gamepad,
             * so it is not claimed as one. */
            if ((minor & 0x0F) == 0x01 || (minor & 0x0F) == 0x02)
                return BT_CLASS_GAMEPAD;
            return BT_CLASS_UNKNOWN;
        }
        case 0x07: return BT_CLASS_WEARABLE;
        case 0x09: return BT_CLASS_HEALTH;
        default:   return BT_CLASS_UNKNOWN;
    }
}

/* ===================== connection state machine =====================
 * Only these transitions exist. An incoming link is put in CONNECTING when the
 * Connection Request arrives, so DISCONNECTED -> CONNECTED stays illegal in
 * both directions and a lost event cannot silently promote a link. */
bool bt_state_transition_ok(bt_state_t from, bt_state_t to) {
    if (from == to) return false;
    switch (from) {
        case BT_STATE_DISCONNECTED: return to == BT_STATE_CONNECTING;
        case BT_STATE_CONNECTING:   return to == BT_STATE_CONNECTED ||
                                           to == BT_STATE_DISCONNECTED;
        case BT_STATE_CONNECTED:    return to == BT_STATE_PAIRING ||
                                           to == BT_STATE_DISCONNECTED;
        case BT_STATE_PAIRING:      return to == BT_STATE_PAIRED ||
                                           to == BT_STATE_CONNECTED ||
                                           to == BT_STATE_DISCONNECTED;
        case BT_STATE_PAIRED:       return to == BT_STATE_PAIRING ||
                                           to == BT_STATE_DISCONNECTED;
        default: return false;
    }
}

/* ===================== byte rings ===================== */

static uint32_t ring_used(uint32_t head, uint32_t tail, uint32_t size) {
    return (head >= tail) ? (head - tail) : (size - tail + head);
}

static uint32_t ring_free(uint32_t head, uint32_t tail, uint32_t size) {
    return size - 1u - ring_used(head, tail, size);
}

static void ring_put(uint8_t *buf, uint32_t size, uint32_t *head,
                     const uint8_t *src, uint32_t n) {
    uint32_t h = *head;
    for (uint32_t i = 0; i < n; i++) {
        buf[h] = src[i];
        h = (h + 1u) % size;
    }
    *head = h;
}

static void ring_peek(const uint8_t *buf, uint32_t size, uint32_t tail,
                      uint32_t off, uint8_t *dst, uint32_t n) {
    uint32_t t = (tail + off) % size;
    for (uint32_t i = 0; i < n; i++) {
        dst[i] = buf[t];
        t = (t + 1u) % size;
    }
}

static void ring_drop(uint32_t size, uint32_t *tail, uint32_t n) {
    *tail = (*tail + n) % size;
}

/* ===================== device table ===================== */

static int dev_find_index(const bluetooth_device_t *dev, const uint8_t *addr) {
    for (uint32_t i = 0; i < dev->num_devices && i < BT_MAX_DEVICES; i++)
        if (bts_memcmp(dev->devices[i].bdaddr, addr, 6) == 0) return (int)i;
    return -1;
}

/* Adds an entry for an address we ACTUALLY HEARD FROM. Nothing else in this
 * file creates device records: with no radio the table stays empty. */
static int dev_add(bluetooth_device_t *dev, const uint8_t *addr) {
    int idx = dev_find_index(dev, addr);
    if (idx >= 0) return idx;
    if (dev->num_devices >= BT_MAX_DEVICES) return BT_ENOSPC;
    bt_device_t *d = &dev->devices[dev->num_devices];
    bts_memset(d, 0, (uint32_t)sizeof(*d));
    bts_memcpy(d->bdaddr, addr, 6);
    d->rssi = BT_RSSI_UNKNOWN;
    d->dev_class = BT_CLASS_UNKNOWN;
    d->state = BT_STATE_DISCONNECTED;
    idx = (int)dev->num_devices;
    dev->num_devices++;
    return idx;
}

bt_device_t *bt_find_device(bluetooth_device_t *dev, const uint8_t *bdaddr) {
    if (!dev || !bdaddr) return (bt_device_t *)0;
    int idx = dev_find_index(dev, bdaddr);
    if (idx < 0) return (bt_device_t *)0;
    return &dev->devices[idx];
}

uint32_t bt_get_device_count(bluetooth_device_t *dev) {
    if (!dev) return 0;
    return dev->num_devices;
}

bt_device_t *bt_get_device(bluetooth_device_t *dev, uint32_t index) {
    if (!dev) return (bt_device_t *)0;
    if (index >= dev->num_devices || index >= BT_MAX_DEVICES)
        return (bt_device_t *)0;
    return &dev->devices[index];
}

/* ===================== connection slots ===================== */

static void conn_sync(bluetooth_device_t *dev) {
    uint32_t n = 0;
    for (uint32_t i = 0; i < BT_MAX_CONNECTIONS; i++) {
        dev->connections[i] = dev->conns[i].in_use ? dev->conns[i].handle : 0u;
        if (dev->conns[i].in_use) n++;
    }
    dev->num_connections = n;
}

static bt_conn_t *conn_alloc(bluetooth_device_t *dev, const uint8_t *addr,
                             bool incoming) {
    for (uint32_t i = 0; i < BT_MAX_CONNECTIONS; i++) {
        bt_conn_t *c = &dev->conns[i];
        if (c->in_use) continue;
        bts_memset(c, 0, (uint32_t)sizeof(*c));
        c->in_use = true;
        c->incoming = incoming;
        c->state = BT_STATE_DISCONNECTED;
        c->dev_index = -1;
        bts_memcpy(c->bdaddr, addr, 6);
        bt_reasm_init(&c->reasm);
        /* SSRC is derived from the peer address so two links never collide;
         * it is an identifier, not a random number, and is not security
         * relevant (see LIMITATIONS: no crypto here). */
        c->rtp_ssrc = ((uint32_t)addr[5] << 24) | ((uint32_t)addr[4] << 16) |
                      ((uint32_t)addr[3] << 8)  | (uint32_t)addr[2];
        conn_sync(dev);
        return c;
    }
    return (bt_conn_t *)0;
}

static void conn_free(bluetooth_device_t *dev, bt_conn_t *c) {
    bts_memset(c, 0, (uint32_t)sizeof(*c));
    c->dev_index = -1;
    conn_sync(dev);
}

bt_conn_t *bt_find_conn_handle(bluetooth_device_t *dev, uint16_t handle) {
    if (!dev || handle == 0) return (bt_conn_t *)0;
    for (uint32_t i = 0; i < BT_MAX_CONNECTIONS; i++)
        if (dev->conns[i].in_use && dev->conns[i].handle == handle)
            return &dev->conns[i];
    return (bt_conn_t *)0;
}

bt_conn_t *bt_find_conn_addr(bluetooth_device_t *dev, const uint8_t *bdaddr) {
    if (!dev || !bdaddr) return (bt_conn_t *)0;
    for (uint32_t i = 0; i < BT_MAX_CONNECTIONS; i++)
        if (dev->conns[i].in_use &&
            bts_memcmp(dev->conns[i].bdaddr, bdaddr, 6) == 0)
            return &dev->conns[i];
    return (bt_conn_t *)0;
}

static bool conn_set_state(bluetooth_device_t *dev, bt_conn_t *c, bt_state_t to) {
    if (!bt_state_transition_ok(c->state, to)) return false;
    c->state = to;
    if (c->dev_index >= 0 && (uint32_t)c->dev_index < dev->num_devices)
        dev->devices[c->dev_index].state = to;
    return true;
}

/* ===================== transmit path ===================== */

static int tx_enqueue(bluetooth_device_t *dev, uint8_t pkt_type,
                      const uint8_t *pkt, uint32_t len) {
    if (len == 0 || len > BT_MAX_HCI_PKT) return BT_EINVAL;
    uint32_t need = 3u + len;
    if (ring_free(dev->tx_head, dev->tx_tail, (uint32_t)sizeof(dev->tx_dma)) < need) {
        dev->stats.tx_queue_full++;
        return BT_ENOSPC;
    }
    uint8_t hdr[3];
    bts_put16(hdr, (uint16_t)len);
    hdr[2] = pkt_type;
    ring_put(dev->tx_dma, (uint32_t)sizeof(dev->tx_dma), &dev->tx_head, hdr, 3);
    ring_put(dev->tx_dma, (uint32_t)sizeof(dev->tx_dma), &dev->tx_head, pkt, len);
    return BT_OK;
}

int bt_tx_flush(bluetooth_device_t *dev) {
    if (!dev) return BT_EINVAL;
    if (!dev->ops_bound || !dev->ops.hci_send) return BT_ENODEV;
    uint32_t size = (uint32_t)sizeof(dev->tx_dma);
    int sent = 0;
    while (ring_used(dev->tx_head, dev->tx_tail, size) >= 3u) {
        uint8_t hdr[3];
        ring_peek(dev->tx_dma, size, dev->tx_tail, 0, hdr, 3);
        uint32_t len = bts_le16(hdr);
        uint8_t type = hdr[2];
        if (len == 0 || len > BT_MAX_HCI_PKT) {
            /* Cannot happen through tx_enqueue; if it does the ring is corrupt
             * and draining it further would read garbage. */
            dev->tx_tail = dev->tx_head;
            return BT_EPROTO;
        }
        if (ring_used(dev->tx_head, dev->tx_tail, size) < 3u + len) break;

        /* Controller flow control: commands and ACL data have separate
         * credit pools, and head-of-line blocking is the correct behaviour —
         * reordering HCI packets breaks the protocol. */
        if (type == BT_H4_CMD && dev->cmd_credits == 0) break;
        if (type == BT_H4_ACL && dev->acl_credits == 0) break;

        uint8_t pkt[BT_MAX_HCI_PKT];
        ring_peek(dev->tx_dma, size, dev->tx_tail, 3, pkt, len);
        int rc = dev->ops.hci_send(dev->ops.ctx, type, pkt, len);
        if (rc < 0) {
            if (type == BT_H4_CMD) dev->stats.hci_cmds_failed++;
            break;                       /* stays queued; retry on next flush */
        }
        ring_drop(size, &dev->tx_tail, 3u + len);
        if (type == BT_H4_CMD) {
            dev->stats.hci_cmds_sent++;
            if (dev->cmd_credits > 0) dev->cmd_credits--;
        } else if (type == BT_H4_ACL) {
            dev->stats.acl_tx_pkts++;
            if (dev->acl_credits > 0) dev->acl_credits--;
            dev->stats.bytes_tx += (len >= BT_HCI_ACL_HDR_LEN)
                                 ? (len - BT_HCI_ACL_HDR_LEN) : 0u;
        }
        dev->irq_tx_done = true;
        sent++;
    }
    return sent;
}

static int hci_cmd(bluetooth_device_t *dev, uint16_t opcode,
                   const uint8_t *params, uint32_t plen) {
    if (!dev->ops_bound || !dev->ops.hci_send) {
        dev->stats.hci_cmds_failed++;
        return BT_ENODEV;
    }
    uint8_t buf[BT_HCI_CMD_HDR_LEN + BT_HCI_MAX_PARAM];
    int n = bt_hci_build_cmd(buf, (uint32_t)sizeof(buf), opcode, params, plen);
    if (n < 0) return n;
    int rc = tx_enqueue(dev, BT_H4_CMD, buf, (uint32_t)n);
    if (rc != BT_OK) return rc;
    dev->reg_command = opcode;
    bt_tx_flush(dev);
    return BT_OK;
}

/* Send one L2CAP PDU on an established ACL link, fragmenting to the
 * controller's ACL packet size. All-or-nothing: if the queue cannot hold every
 * fragment nothing is enqueued, because a half-sent PDU is unrecoverable. */
static int l2cap_send(bluetooth_device_t *dev, bt_conn_t *c, uint16_t cid,
                      const uint8_t *payload, uint32_t n) {
    if (!dev->ops_bound) return BT_ENODEV;
    if (c->handle == 0) return BT_ESTATE;
    if (dev->reg_acl_mtu == 0) return BT_ESTATE;   /* Read Buffer Size not done */
    if (n + BT_L2CAP_HDR_LEN > BT_REASM_MAX) return BT_EMSGSIZE;

    uint8_t pdu[BT_REASM_MAX];
    int pdu_len = bt_l2cap_build(pdu, (uint32_t)sizeof(pdu), cid, payload, n);
    if (pdu_len < 0) return pdu_len;

    uint16_t mtu = (uint16_t)((dev->reg_acl_mtu > 0xFFFFu) ? 0xFFFFu : dev->reg_acl_mtu);
    if (mtu > BT_MAX_HCI_PKT - BT_HCI_ACL_HDR_LEN)
        mtu = (uint16_t)(BT_MAX_HCI_PKT - BT_HCI_ACL_HDR_LEN);
    uint32_t nfrag = bt_acl_frag_count((uint32_t)pdu_len, mtu);
    if (nfrag == 0) return BT_EINVAL;

    uint32_t need = nfrag * (3u + BT_HCI_ACL_HDR_LEN) + (uint32_t)pdu_len;
    if (ring_free(dev->tx_head, dev->tx_tail, (uint32_t)sizeof(dev->tx_dma)) < need) {
        dev->stats.tx_queue_full++;
        return BT_ENOSPC;
    }

    for (uint32_t i = 0; i < nfrag; i++) {
        uint8_t frag[BT_MAX_HCI_PKT];
        int fl = bt_acl_fragment(frag, (uint32_t)sizeof(frag), c->handle, mtu,
                                 pdu, (uint32_t)pdu_len, i);
        if (fl <= 0) return BT_EPROTO;
        int rc = tx_enqueue(dev, BT_H4_ACL, frag, (uint32_t)fl);
        if (rc != BT_OK) return rc;      /* space was checked; treat as fatal */
    }
    dev->stats.l2cap_tx_frames++;
    bt_tx_flush(dev);
    return BT_OK;
}

/* ===================== L2CAP channels ===================== */

static bt_l2cap_chan_t *chan_by_scid(bt_conn_t *c, uint16_t scid) {
    for (uint32_t i = 0; i < BT_MAX_CHANNELS; i++)
        if (c->chans[i].state != BT_CHAN_CLOSED && c->chans[i].scid == scid)
            return &c->chans[i];
    return (bt_l2cap_chan_t *)0;
}

static bt_l2cap_chan_t *chan_by_role(bt_conn_t *c, bt_chan_role_t role) {
    for (uint32_t i = 0; i < BT_MAX_CHANNELS; i++)
        if (c->chans[i].state != BT_CHAN_CLOSED && c->chans[i].role == role)
            return &c->chans[i];
    return (bt_l2cap_chan_t *)0;
}

static uint16_t scid_alloc(bluetooth_device_t *dev) {
    for (uint32_t tries = 0; tries < 256; tries++) {
        uint16_t cand = dev->next_scid;
        dev->next_scid = (uint16_t)((dev->next_scid + 1u) & 0xFFFFu);
        if (dev->next_scid < BT_L2CAP_CID_DYN_BASE)
            dev->next_scid = BT_L2CAP_CID_DYN_BASE;
        bool taken = false;
        for (uint32_t i = 0; i < BT_MAX_CONNECTIONS && !taken; i++)
            if (dev->conns[i].in_use && chan_by_scid(&dev->conns[i], cand))
                taken = true;
        if (!taken) return cand;
    }
    return 0;
}

/* Every signalling command this stack originates or answers is built into a
 * buffer this size. All of them are fixed and small (4..8 bytes of data) except
 * an Echo Response, whose data comes from the peer. */
#define BT_SIG_BUF 64

static uint8_t sig_ident(bluetooth_device_t *dev) {
    dev->next_sig_ident++;
    if (dev->next_sig_ident == 0) dev->next_sig_ident = 1;  /* 0 is reserved */
    return dev->next_sig_ident;
}

static int sig_send(bluetooth_device_t *dev, bt_conn_t *c, uint8_t code,
                    uint8_t ident, const uint8_t *data, uint32_t n) {
    uint8_t sig[BT_SIG_BUF];
    int sl = bt_l2cap_build_sig(sig, (uint32_t)sizeof(sig), code, ident, data, n);
    if (sl < 0) return sl;
    return l2cap_send(dev, c, BT_L2CAP_CID_SIGNAL, sig, (uint32_t)sl);
}

static int chan_open(bluetooth_device_t *dev, bt_conn_t *c, uint16_t psm,
                     bt_chan_role_t role) {
    if (chan_by_role(c, role)) return BT_OK;   /* already opening or open */
    bt_l2cap_chan_t *ch = (bt_l2cap_chan_t *)0;
    for (uint32_t i = 0; i < BT_MAX_CHANNELS; i++)
        if (c->chans[i].state == BT_CHAN_CLOSED) { ch = &c->chans[i]; break; }
    if (!ch) return BT_ENOSPC;

    uint16_t scid = scid_alloc(dev);
    if (scid == 0) return BT_ENOSPC;
    bts_memset(ch, 0, (uint32_t)sizeof(*ch));
    ch->psm = psm;
    ch->scid = scid;
    ch->role = role;
    ch->in_mtu = BT_L2CAP_DEFAULT_MTU;
    ch->out_mtu = BT_L2CAP_DEFAULT_MTU;
    ch->state = BT_CHAN_CONN_SENT;
    ch->last_ident = sig_ident(dev);

    uint8_t p[4];
    bts_put16(p, psm);
    bts_put16(p + 2, scid);
    int rc = sig_send(dev, c, BT_L2CAP_CONN_REQ, ch->last_ident, p, 4);
    if (rc != BT_OK) {
        bts_memset(ch, 0, (uint32_t)sizeof(*ch));
        return rc;
    }
    return BT_OK;
}

static int chan_send_config_req(bluetooth_device_t *dev, bt_conn_t *c,
                                bt_l2cap_chan_t *ch) {
    /* dcid + flags + MTU option. The MTU we advertise is what WE can receive. */
    uint8_t p[8];
    bts_put16(p, ch->dcid);
    bts_put16(p + 2, 0x0000);
    p[4] = BT_L2CAP_CONF_OPT_MTU;
    p[5] = 0x02;
    bts_put16(p + 6, ch->in_mtu);
    ch->last_ident = sig_ident(dev);
    return sig_send(dev, c, BT_L2CAP_CONF_REQ, ch->last_ident, p, 8);
}

static int chan_data_send(bluetooth_device_t *dev, bt_conn_t *c,
                          bt_l2cap_chan_t *ch, const uint8_t *p, uint32_t n) {
    if (ch->state != BT_CHAN_OPEN) return BT_ESTATE;
    if (n > ch->out_mtu) return BT_EMSGSIZE;
    return l2cap_send(dev, c, ch->dcid, p, n);
}

/* ===================== profile bring-up ===================== */

static int rfcomm_send_frame(bluetooth_device_t *dev, bt_conn_t *c, uint8_t dlci,
                             uint8_t cr, uint8_t ctrl, bool pf,
                             const uint8_t *data, uint32_t n) {
    bt_l2cap_chan_t *ch = chan_by_role(c, BT_ROLE_RFCOMM);
    if (!ch) return BT_ENOENT;
    uint8_t frame[BT_REASM_MAX];
    int fl = bt_rfcomm_build(frame, (uint32_t)sizeof(frame), dlci, cr, ctrl, pf,
                             data, n);
    if (fl < 0) return fl;
    return chan_data_send(dev, c, ch, frame, (uint32_t)fl);
}

static int avdtp_send_cmd(bluetooth_device_t *dev, bt_conn_t *c,
                          uint8_t signal_id, const uint8_t *params, uint32_t n) {
    bt_l2cap_chan_t *ch = chan_by_role(c, BT_ROLE_AVDTP_SIG);
    if (!ch) return BT_ENOENT;
    uint8_t msg[64];
    c->a2dp_label = (uint8_t)((c->a2dp_label + 1u) & 0x0F);
    int ml = bt_avdtp_build(msg, (uint32_t)sizeof(msg), c->a2dp_label,
                            BT_AVDTP_MSG_COMMAND, signal_id, params, n);
    if (ml < 0) return ml;
    return chan_data_send(dev, c, ch, msg, (uint32_t)ml);
}

/* Called when an L2CAP channel finishes configuration. */
static void chan_became_open(bluetooth_device_t *dev, bt_conn_t *c,
                             bt_l2cap_chan_t *ch) {
    switch (ch->role) {
        case BT_ROLE_RFCOMM:
            if (c->rfcomm_state == BT_RFCOMM_CLOSED) {
                /* SABM on DLCI 0 brings the multiplexer up. C/R = 1: we are
                 * the initiating station, so our commands carry C/R set. */
                if (rfcomm_send_frame(dev, c, 0, 1, BT_RFCOMM_SABM, true,
                                      (const uint8_t *)0, 0) == BT_OK)
                    c->rfcomm_state = BT_RFCOMM_SABM0_SENT;
            }
            break;
        case BT_ROLE_AVDTP_SIG:
            if (c->a2dp_state == BT_A2DP_IDLE) {
                if (avdtp_send_cmd(dev, c, BT_AVDTP_DISCOVER,
                                   (const uint8_t *)0, 0) == BT_OK)
                    c->a2dp_state = BT_A2DP_DISCOVERING;
            }
            break;
        case BT_ROLE_AVDTP_MEDIA:
            if (c->a2dp_state == BT_A2DP_OPEN) {
                if (avdtp_send_cmd(dev, c, BT_AVDTP_START, &c->a2dp_seid, 1) == BT_OK)
                    c->a2dp_state = BT_A2DP_STARTING;
            }
            break;
        case BT_ROLE_HID_CTRL:
        case BT_ROLE_HID_INTR: {
            bt_l2cap_chan_t *ctrl = chan_by_role(c, BT_ROLE_HID_CTRL);
            bt_l2cap_chan_t *intr = chan_by_role(c, BT_ROLE_HID_INTR);
            if (ctrl && intr && ctrl->state == BT_CHAN_OPEN &&
                intr->state == BT_CHAN_OPEN)
                c->active_profiles |= (uint32_t)BT_PROFILE_HID;
            break;
        }
        default:
            break;
    }
}

static int profile_bringup(bluetooth_device_t *dev, bt_conn_t *c, uint32_t profile) {
    switch (profile) {
        case (uint32_t)BT_PROFILE_SPP:
            if (c->rfcomm_server_channel == 0) c->rfcomm_server_channel = 1;
            c->rfcomm_dlci = (uint8_t)(c->rfcomm_server_channel << 1);
            return chan_open(dev, c, BT_PSM_RFCOMM, BT_ROLE_RFCOMM);
        case (uint32_t)BT_PROFILE_HID: {
            int rc = chan_open(dev, c, BT_PSM_HID_CTRL, BT_ROLE_HID_CTRL);
            if (rc != BT_OK) return rc;
            return chan_open(dev, c, BT_PSM_HID_INTR, BT_ROLE_HID_INTR);
        }
        case (uint32_t)BT_PROFILE_A2DP_SRC:
            return chan_open(dev, c, BT_PSM_AVDTP, BT_ROLE_AVDTP_SIG);
        default:
            return BT_ENOTSUP;
    }
}

/* ===================== receive: L2CAP signalling ===================== */

static void handle_sig(bluetooth_device_t *dev, bt_conn_t *c,
                       const uint8_t *p, uint32_t n) {
    bt_l2cap_sig_t s;
    if (bt_l2cap_parse_sig(p, n, &s) != BT_OK) { dev->stats.acl_rx_bad++; return; }

    switch (s.code) {
        case BT_L2CAP_CONN_RSP: {
            if (s.len < 8) { dev->stats.acl_rx_bad++; return; }
            uint16_t dcid   = bts_le16(s.data);
            uint16_t scid   = bts_le16(s.data + 2);
            uint16_t result = bts_le16(s.data + 4);
            bt_l2cap_chan_t *ch = chan_by_scid(c, scid);
            if (!ch) return;
            if (result == 0x0000) {
                ch->dcid = dcid;
                ch->state = BT_CHAN_CONFIG;
                chan_send_config_req(dev, c, ch);
            } else if (result == 0x0001) {
                /* pending — the peer will send another response */
            } else {
                dev->stats.conn_refused++;
                bts_memset(ch, 0, (uint32_t)sizeof(*ch));
            }
            break;
        }
        case BT_L2CAP_CONF_REQ: {
            if (s.len < 4) { dev->stats.acl_rx_bad++; return; }
            uint16_t dcid = bts_le16(s.data);      /* our scid */
            bt_l2cap_chan_t *ch = chan_by_scid(c, dcid);
            if (!ch) return;
            /* Walk the options; the only one we honour is MTU, which tells us
             * what the PEER can receive, i.e. our outgoing limit. */
            uint32_t i = 4;
            while (i + 2u <= s.len) {
                uint8_t type = (uint8_t)(s.data[i] & 0x7F);
                uint8_t olen = s.data[i + 1];
                if (i + 2u + olen > s.len) break;
                if (type == BT_L2CAP_CONF_OPT_MTU && olen == 2)
                    ch->out_mtu = bts_le16(s.data + i + 2);
                i += 2u + olen;
            }
            uint8_t rsp[6];
            bts_put16(rsp, ch->dcid);              /* echo the requester's CID */
            bts_put16(rsp + 2, 0x0000);            /* flags */
            bts_put16(rsp + 4, 0x0000);            /* result: success */
            sig_send(dev, c, BT_L2CAP_CONF_RSP, s.ident, rsp, 6);
            ch->cfg_in_done = true;
            if (ch->cfg_out_done && ch->state != BT_CHAN_OPEN) {
                ch->state = BT_CHAN_OPEN;
                chan_became_open(dev, c, ch);
            }
            break;
        }
        case BT_L2CAP_CONF_RSP: {
            if (s.len < 6) { dev->stats.acl_rx_bad++; return; }
            uint16_t scid   = bts_le16(s.data);    /* our scid */
            uint16_t result = bts_le16(s.data + 4);
            bt_l2cap_chan_t *ch = chan_by_scid(c, scid);
            if (!ch) return;
            if (result != 0x0000) {                /* renegotiation unsupported */
                dev->stats.conn_refused++;
                bts_memset(ch, 0, (uint32_t)sizeof(*ch));
                return;
            }
            ch->cfg_out_done = true;
            if (ch->cfg_in_done && ch->state != BT_CHAN_OPEN) {
                ch->state = BT_CHAN_OPEN;
                chan_became_open(dev, c, ch);
            }
            break;
        }
        case BT_L2CAP_DISCONN_REQ: {
            if (s.len < 4) { dev->stats.acl_rx_bad++; return; }
            uint16_t dcid = bts_le16(s.data);      /* our scid */
            uint16_t scid = bts_le16(s.data + 2);
            uint8_t rsp[4];
            bts_put16(rsp, dcid);
            bts_put16(rsp + 2, scid);
            sig_send(dev, c, BT_L2CAP_DISCONN_RSP, s.ident, rsp, 4);
            bt_l2cap_chan_t *ch = chan_by_scid(c, dcid);
            if (ch) {
                if (ch->role == BT_ROLE_AVDTP_MEDIA) {
                    dev->audio_streaming = false;
                    c->a2dp_state = BT_A2DP_OPEN;
                }
                if (ch->role == BT_ROLE_RFCOMM) c->rfcomm_state = BT_RFCOMM_CLOSED;
                bts_memset(ch, 0, (uint32_t)sizeof(*ch));
            }
            break;
        }
        case BT_L2CAP_ECHO_REQ:
            /* The Echo Response Data field is optional and implementation
             * specific (Vol 3 Part A 4.9). When the request carries more than
             * the signalling buffer holds, answer with an EMPTY response —
             * building into sig[] would fail and the peer would get NO reply
             * at all, which looks like a dead link rather than a live one. */
            if (s.len + 4u <= BT_SIG_BUF)
                sig_send(dev, c, BT_L2CAP_ECHO_RSP, s.ident, s.data, s.len);
            else
                sig_send(dev, c, BT_L2CAP_ECHO_RSP, s.ident, (const uint8_t *)0, 0);
            break;
        default:
            /* Command Reject etc. — nothing to do, and inventing a reply would
             * be worse than silence. */
            break;
    }
}

/* ===================== receive: profiles ===================== */

static void rx_deliver(bluetooth_device_t *dev, bt_conn_t *c,
                       const uint8_t *p, uint32_t n) {
    if (n == 0) return;
    uint32_t size = (uint32_t)sizeof(dev->rx_dma);
    uint32_t need = 2u + 6u + n;
    if (need > size - 1u || ring_free(dev->rx_head, dev->rx_tail, size) < need) {
        dev->stats.rx_ring_drops++;
        return;
    }
    uint8_t hdr[8];
    bts_put16(hdr, (uint16_t)n);
    bts_memcpy(hdr + 2, c->bdaddr, 6);
    ring_put(dev->rx_dma, size, &dev->rx_head, hdr, 8);
    ring_put(dev->rx_dma, size, &dev->rx_head, p, n);
    dev->stats.bytes_rx += n;
    dev->irq_rx_ready = true;
}

static void handle_rfcomm(bluetooth_device_t *dev, bt_conn_t *c,
                          const uint8_t *p, uint32_t n) {
    bt_rfcomm_frame_t f;
    if (bt_rfcomm_parse(p, n, &f) != BT_OK) { dev->stats.acl_rx_bad++; return; }
    if (!f.fcs_ok) { dev->stats.rfcomm_fcs_errors++; return; }

    switch (f.ctrl) {
        case BT_RFCOMM_UA:
            if (f.dlci == 0 && c->rfcomm_state == BT_RFCOMM_SABM0_SENT) {
                c->rfcomm_state = BT_RFCOMM_MUX_UP;
                if (rfcomm_send_frame(dev, c, c->rfcomm_dlci, 1, BT_RFCOMM_SABM,
                                      true, (const uint8_t *)0, 0) == BT_OK)
                    c->rfcomm_state = BT_RFCOMM_SABM_SENT;
            } else if (f.dlci == c->rfcomm_dlci && f.dlci != 0 &&
                       c->rfcomm_state == BT_RFCOMM_SABM_SENT) {
                c->rfcomm_state = BT_RFCOMM_OPEN;
                c->active_profiles |= (uint32_t)BT_PROFILE_SPP;
            }
            break;
        case BT_RFCOMM_DM:
        case BT_RFCOMM_DISC:
            if (f.dlci == c->rfcomm_dlci || f.dlci == 0) {
                c->rfcomm_state = BT_RFCOMM_CLOSED;
                c->active_profiles &= ~(uint32_t)BT_PROFILE_SPP;
            }
            break;
        case BT_RFCOMM_UIH:
            /* DLCI 0 carries multiplexer control messages (PN, MSC, ...),
             * which this stack does not implement — see LIMITATIONS. Data on
             * our own DLCI is delivered. */
            if (f.dlci == c->rfcomm_dlci && c->rfcomm_state == BT_RFCOMM_OPEN)
                rx_deliver(dev, c, f.data, f.len);
            break;
        default:
            break;
    }
}

static void handle_avdtp(bluetooth_device_t *dev, bt_conn_t *c,
                         const uint8_t *p, uint32_t n) {
    bt_avdtp_msg_t m;
    if (bt_avdtp_parse(p, n, &m) != BT_OK) { dev->stats.acl_rx_bad++; return; }
    if (m.msg_type == BT_AVDTP_MSG_REJECT || m.msg_type == BT_AVDTP_MSG_GEN_REJ) {
        /* Fall back to the last stable state rather than pretending. */
        if (c->a2dp_state == BT_A2DP_STARTING) c->a2dp_state = BT_A2DP_OPEN;
        else if (c->a2dp_state == BT_A2DP_CONFIGURING) c->a2dp_state = BT_A2DP_DISCOVERED;
        else if (c->a2dp_state == BT_A2DP_OPENING) c->a2dp_state = BT_A2DP_CONFIGURED;
        else if (c->a2dp_state == BT_A2DP_SUSPENDING) c->a2dp_state = BT_A2DP_STREAMING;
        else if (c->a2dp_state == BT_A2DP_DISCOVERING) c->a2dp_state = BT_A2DP_IDLE;
        return;
    }
    if (m.msg_type != BT_AVDTP_MSG_ACCEPT) return;   /* peer-originated command */

    switch (m.signal_id) {
        case BT_AVDTP_DISCOVER:
            if (c->a2dp_state != BT_A2DP_DISCOVERING) return;
            /* SEP entries are 2 bytes: [SEID:6|inuse:1|rfa:1]
             *                          [media type:4|TSEP:1|rfa:3]
             * We are a source, so we want a free SINK (TSEP bit set). */
            for (uint32_t i = 0; i + 1 < m.plen; i += 2) {
                bool in_use = (m.params[i] & 0x02) != 0;
                bool is_sink = (m.params[i + 1] & 0x08) != 0;
                if (!in_use && is_sink) {
                    c->a2dp_seid = m.params[i];      /* keep the shifted form */
                    c->a2dp_state = BT_A2DP_DISCOVERED;
                    return;
                }
            }
            /* No usable endpoint: stay honest, go back to IDLE. */
            c->a2dp_state = BT_A2DP_IDLE;
            break;
        case BT_AVDTP_SET_CONFIG:
            if (c->a2dp_state == BT_A2DP_CONFIGURING) {
                c->a2dp_state = BT_A2DP_CONFIGURED;
                if (avdtp_send_cmd(dev, c, BT_AVDTP_OPEN, &c->a2dp_seid, 1) == BT_OK)
                    c->a2dp_state = BT_A2DP_OPENING;
            }
            break;
        case BT_AVDTP_OPEN:
            if (c->a2dp_state == BT_A2DP_OPENING) {
                c->a2dp_state = BT_A2DP_OPEN;
                chan_open(dev, c, BT_PSM_AVDTP, BT_ROLE_AVDTP_MEDIA);
            }
            break;
        case BT_AVDTP_START:
            if (c->a2dp_state == BT_A2DP_STARTING) {
                c->a2dp_state = BT_A2DP_STREAMING;
                dev->audio_streaming = true;
                dev->audio_handle = c->handle;
                c->active_profiles |= (uint32_t)BT_PROFILE_A2DP_SRC;
            }
            break;
        case BT_AVDTP_SUSPEND:
            if (c->a2dp_state == BT_A2DP_SUSPENDING) {
                c->a2dp_state = BT_A2DP_OPEN;
                dev->audio_streaming = false;
            }
            break;
        case BT_AVDTP_CLOSE:
            c->a2dp_state = BT_A2DP_CONFIGURED;
            dev->audio_streaming = false;
            c->active_profiles &= ~(uint32_t)BT_PROFILE_A2DP_SRC;
            break;
        default:
            break;
    }
}

static void handle_hid(bluetooth_device_t *dev, bt_conn_t *c,
                       const uint8_t *p, uint32_t n) {
    uint8_t tt = 0, param = 0;
    const uint8_t *pl = (const uint8_t *)0;
    uint32_t pn = 0;
    if (bt_hidp_parse(p, n, &tt, &param, &pl, &pn) != BT_OK) {
        dev->stats.acl_rx_bad++;
        return;
    }
    /* Input reports are the only HID traffic a host consumes here. */
    if (tt == BT_HIDP_DATA && param == BT_HIDP_RTYPE_INPUT && pn > 0)
        rx_deliver(dev, c, pl, pn);
}

static void handle_l2cap_pdu(bluetooth_device_t *dev, bt_conn_t *c,
                             const uint8_t *pdu, uint32_t len) {
    bt_l2cap_frame_t f;
    if (bt_l2cap_parse(pdu, len, &f) != BT_OK) { dev->stats.acl_rx_bad++; return; }
    dev->stats.l2cap_rx_frames++;

    if (f.cid == BT_L2CAP_CID_SIGNAL) {
        if (f.len) handle_sig(dev, c, f.payload, f.len);
        return;
    }
    bt_l2cap_chan_t *ch = chan_by_scid(c, f.cid);
    if (!ch) return;                    /* unknown CID: drop, do not guess */
    if (f.len == 0) return;

    switch (ch->role) {
        case BT_ROLE_RFCOMM:      handle_rfcomm(dev, c, f.payload, f.len); break;
        case BT_ROLE_AVDTP_SIG:   handle_avdtp(dev, c, f.payload, f.len);  break;
        case BT_ROLE_HID_INTR:
        case BT_ROLE_HID_CTRL:    handle_hid(dev, c, f.payload, f.len);    break;
        case BT_ROLE_AVDTP_MEDIA: /* inbound media is a sink role: not us */ break;
        default: break;
    }
}

/* ===================== receive: HCI events ===================== */

static void eir_scan_name(bluetooth_device_t *dev, bt_device_t *d,
                          const uint8_t *eir, uint32_t n) {
    uint32_t i = 0;
    while (i < n) {
        uint8_t flen = eir[i];
        if (flen == 0) return;                 /* end of significant part */
        if (i + 1u + flen > n) return;         /* truncated: stop, never overread */
        uint8_t type = eir[i + 1];
        if (type == 0x08 || type == 0x09) {    /* shortened / complete name */
            uint32_t nl = flen - 1u;
            if (nl > sizeof(d->name) - 1u) nl = (uint32_t)sizeof(d->name) - 1u;
            for (uint32_t k = 0; k < nl; k++) d->name[k] = (char)eir[i + 2u + k];
            d->name[nl] = '\0';
            d->name_valid = true;
            dev->stats.name_updates++;
            return;
        }
        i += 1u + flen;
    }
}

static void note_inquiry_entry(bluetooth_device_t *dev, const uint8_t *addr,
                               uint32_t cod, int16_t rssi, bool have_rssi) {
    int idx = dev_add(dev, addr);
    if (idx < 0) return;                       /* table full: count nothing */
    bt_device_t *d = &dev->devices[idx];
    d->cod = cod;
    d->dev_class = bt_class_from_cod(cod);
    if (have_rssi) d->rssi = rssi;
    dev->stats.inquiry_results++;
}

static int handle_cmd_complete(bluetooth_device_t *dev, const uint8_t *p, uint32_t n) {
    if (n < 3) return BT_EPROTO;
    dev->cmd_credits = p[0];
    uint16_t opcode = bts_le16(p + 1);
    const uint8_t *r = p + 3;
    uint32_t rn = n - 3;

    switch (opcode) {
        case BT_HCI_RESET:
            if (rn >= 1 && r[0] == 0x00) {
                dev->inquiring = false;
                dev->reg_status = 0;
            }
            break;
        case BT_HCI_READ_BD_ADDR:
            if (rn >= 7 && r[0] == 0x00) {
                bts_memcpy(dev->local_addr, r + 1, 6);
                dev->local_addr_valid = true;
            }
            break;
        case BT_HCI_READ_BUFFER_SIZE:
            if (rn >= 8 && r[0] == 0x00) {
                dev->reg_acl_mtu     = bts_le16(r + 1);
                dev->reg_sco_mtu     = r[3];
                dev->acl_total_pkts  = bts_le16(r + 4);
                dev->acl_credits     = dev->acl_total_pkts;
            }
            break;
        case BT_HCI_READ_LOCAL_VERSION:
            if (rn >= 9 && r[0] == 0x00) {
                switch (r[1]) {           /* HCI_Version, Assigned Numbers */
                    case 0x06: dev->version = BT_VERSION_4_0; dev->version_valid = true; break;
                    case 0x07: dev->version = BT_VERSION_4_1; dev->version_valid = true; break;
                    case 0x08: dev->version = BT_VERSION_4_2; dev->version_valid = true; break;
                    case 0x09: dev->version = BT_VERSION_5_0; dev->version_valid = true; break;
                    case 0x0A: dev->version = BT_VERSION_5_1; dev->version_valid = true; break;
                    case 0x0B: dev->version = BT_VERSION_5_2; dev->version_valid = true; break;
                    case 0x0C: dev->version = BT_VERSION_5_3; dev->version_valid = true; break;
                    case 0x0D: dev->version = BT_VERSION_5_4; dev->version_valid = true; break;
                    default: break;       /* unknown coding: leave it unknown */
                }
            }
            break;
        case BT_HCI_WRITE_SCAN_ENABLE:
            if (rn >= 1 && r[0] == 0x00 && dev->scan_enable_pending) {
                dev->discoverable = (dev->pending_scan_enable & BT_SCAN_INQUIRY) != 0;
                dev->connectable  = (dev->pending_scan_enable & BT_SCAN_PAGE) != 0;
            }
            dev->scan_enable_pending = false;
            break;
        default:
            break;
    }
    return BT_OK;
}

static int handle_event(bluetooth_device_t *dev, const bt_hci_event_t *e) {
    const uint8_t *p = e->params;
    uint32_t n = e->plen;

    switch (e->code) {
        case BT_EVT_CMD_COMPLETE:
            return handle_cmd_complete(dev, p, n);

        case BT_EVT_CMD_STATUS: {
            if (n < 4) return BT_EPROTO;
            dev->cmd_credits = p[1];
            uint16_t opcode = bts_le16(p + 2);
            if (p[0] != 0x00) {
                if (opcode == BT_HCI_INQUIRY) dev->inquiring = false;
                if (opcode == BT_HCI_CREATE_CONNECTION) {
                    for (uint32_t i = 0; i < BT_MAX_CONNECTIONS; i++) {
                        bt_conn_t *c = &dev->conns[i];
                        if (c->in_use && c->handle == 0 &&
                            c->state == BT_STATE_CONNECTING) {
                            conn_set_state(dev, c, BT_STATE_DISCONNECTED);
                            conn_free(dev, c);
                            break;
                        }
                    }
                }
            }
            return BT_OK;
        }

        case BT_EVT_INQUIRY_COMPLETE:
            if (n < 1) return BT_EPROTO;
            dev->inquiring = false;
            dev->irq_inquiry_done = true;
            return BT_OK;

        case BT_EVT_INQUIRY_RESULT: {
            if (n < 1) return BT_EPROTO;
            uint32_t num = p[0];
            if (n != 1u + num * 14u) return BT_EPROTO;
            for (uint32_t i = 0; i < num; i++) {
                const uint8_t *e14 = p + 1u + i * 14u;
                uint32_t cod = (uint32_t)e14[9] | ((uint32_t)e14[10] << 8) |
                               ((uint32_t)e14[11] << 16);
                note_inquiry_entry(dev, e14, cod, 0, false);
            }
            return BT_OK;
        }

        case BT_EVT_INQUIRY_RESULT_RSSI: {
            if (n < 1) return BT_EPROTO;
            uint32_t num = p[0];
            if (n != 1u + num * 14u) return BT_EPROTO;
            for (uint32_t i = 0; i < num; i++) {
                const uint8_t *e14 = p + 1u + i * 14u;
                uint32_t cod = (uint32_t)e14[8] | ((uint32_t)e14[9] << 8) |
                               ((uint32_t)e14[10] << 16);
                note_inquiry_entry(dev, e14, cod, (int16_t)(int8_t)e14[13], true);
            }
            return BT_OK;
        }

        case BT_EVT_EXT_INQUIRY_RESULT: {
            /* Always exactly one response, and the parameter block is padded
             * to the full 255 bytes by the spec. */
            if (n != 255 || p[0] != 1) return BT_EPROTO;
            const uint8_t *b = p + 1;
            uint32_t cod = (uint32_t)b[8] | ((uint32_t)b[9] << 8) |
                           ((uint32_t)b[10] << 16);
            note_inquiry_entry(dev, b, cod, (int16_t)(int8_t)b[13], true);
            int idx = dev_find_index(dev, b);
            if (idx >= 0) eir_scan_name(dev, &dev->devices[idx], b + 14, 240);
            return BT_OK;
        }

        case BT_EVT_REMOTE_NAME_COMPLETE: {
            if (n != 255) return BT_EPROTO;
            if (p[0] != 0x00) return BT_OK;              /* failed: no name */
            int idx = dev_find_index(dev, p + 1);
            if (idx < 0) idx = dev_add(dev, p + 1);
            if (idx < 0) return BT_OK;
            bt_device_t *d = &dev->devices[idx];
            uint32_t k = 0;
            while (k < 247u && p[7u + k] != 0) { d->name[k] = (char)p[7u + k]; k++; }
            d->name[k] = '\0';
            d->name_valid = true;
            dev->stats.name_updates++;
            return BT_OK;
        }

        case BT_EVT_CONN_REQUEST: {
            if (n != 10) return BT_EPROTO;
            int idx = dev_add(dev, p);
            uint32_t cod = (uint32_t)p[6] | ((uint32_t)p[7] << 8) |
                           ((uint32_t)p[8] << 16);
            if (idx >= 0) {
                dev->devices[idx].cod = cod;
                dev->devices[idx].dev_class = bt_class_from_cod(cod);
            }
            if (!dev->connectable) {
                uint8_t rej[7];
                bts_memcpy(rej, p, 6);
                rej[6] = 0x0D;              /* rejected: limited resources */
                hci_cmd(dev, BT_HCI_REJECT_CONN_REQ, rej, 7);
                return BT_OK;
            }
            bt_conn_t *c = bt_find_conn_addr(dev, p);
            if (!c) c = conn_alloc(dev, p, true);
            if (!c) return BT_ENOSPC;
            c->dev_index = idx;
            conn_set_state(dev, c, BT_STATE_CONNECTING);
            uint8_t acc[7];
            bts_memcpy(acc, p, 6);
            acc[6] = 0x01;                  /* remain peripheral */
            hci_cmd(dev, BT_HCI_ACCEPT_CONN_REQ, acc, 7);
            return BT_OK;
        }

        case BT_EVT_CONN_COMPLETE: {
            if (n != 11) return BT_EPROTO;
            uint8_t status = p[0];
            uint16_t handle = (uint16_t)(bts_le16(p + 1) & 0x0FFF);
            const uint8_t *addr = p + 3;
            bt_conn_t *c = bt_find_conn_addr(dev, addr);
            int idx = dev_find_index(dev, addr);
            if (status != 0x00) {
                if (c) {
                    conn_set_state(dev, c, BT_STATE_DISCONNECTED);
                    conn_free(dev, c);
                }
                if (idx >= 0) dev->devices[idx].connected = false;
                return BT_OK;
            }
            if (idx < 0) idx = dev_add(dev, addr);
            if (!c) {
                c = conn_alloc(dev, addr, true);
                if (!c) return BT_ENOSPC;
                conn_set_state(dev, c, BT_STATE_CONNECTING);
            }
            c->dev_index = idx;
            c->handle = handle;
            if (!conn_set_state(dev, c, BT_STATE_CONNECTED)) return BT_EPROTO;
            conn_sync(dev);
            if (idx >= 0) dev->devices[idx].connected = true;
            dev->irq_connected = true;
            if (c->requested_profile)
                profile_bringup(dev, c, c->requested_profile);
            return BT_OK;
        }

        case BT_EVT_DISCONN_COMPLETE: {
            if (n != 4) return BT_EPROTO;
            uint16_t handle = (uint16_t)(bts_le16(p + 1) & 0x0FFF);
            bt_conn_t *c = bt_find_conn_handle(dev, handle);
            if (!c) return BT_OK;
            if (c->dev_index >= 0 && (uint32_t)c->dev_index < dev->num_devices) {
                dev->devices[c->dev_index].connected = false;
                dev->devices[c->dev_index].state = BT_STATE_DISCONNECTED;
            }
            if (dev->audio_handle == handle) {
                dev->audio_streaming = false;
                dev->audio_handle = 0;
            }
            conn_free(dev, c);
            dev->irq_disconnected = true;
            return BT_OK;
        }

        case BT_EVT_AUTH_COMPLETE: {
            if (n != 3) return BT_EPROTO;
            uint16_t handle = (uint16_t)(bts_le16(p + 1) & 0x0FFF);
            bt_conn_t *c = bt_find_conn_handle(dev, handle);
            if (!c) return BT_OK;
            /* A PEER-initiated authentication never went through bt_pair(), so
             * the link is still CONNECTED and CONNECTED -> PAIRED is illegal.
             * Walk the legal edge instead of teleporting, and then read the
             * flag back OUT of the state machine: marking the device paired
             * from the event alone would claim a promotion that the transition
             * table refused. Symmetrically, a failure on an already-PAIRED link
             * has to step back down through PAIRING. */
            if (p[0] == 0x00) {
                if (c->state == BT_STATE_CONNECTED)
                    conn_set_state(dev, c, BT_STATE_PAIRING);
                conn_set_state(dev, c, BT_STATE_PAIRED);
            } else {
                if (c->state == BT_STATE_PAIRED)
                    conn_set_state(dev, c, BT_STATE_PAIRING);
                conn_set_state(dev, c, BT_STATE_CONNECTED);
            }
            if (c->dev_index >= 0 && (uint32_t)c->dev_index < dev->num_devices)
                dev->devices[c->dev_index].paired = (c->state == BT_STATE_PAIRED);
            return BT_OK;
        }

        case BT_EVT_PIN_CODE_REQUEST: {
            if (n != 6) return BT_EPROTO;
            dev->irq_pairing_req = true;
            int idx = dev_find_index(dev, p);
            if (idx >= 0 && dev->devices[idx].pin_len > 0) {
                uint8_t rep[23];
                bts_memset(rep, 0, (uint32_t)sizeof(rep));
                bts_memcpy(rep, p, 6);
                rep[6] = dev->devices[idx].pin_len;
                bts_memcpy(rep + 7, dev->devices[idx].pin,
                           dev->devices[idx].pin_len);
                if (hci_cmd(dev, BT_HCI_PIN_CODE_REQ_REPLY, rep, 23) == BT_OK)
                    dev->stats.pin_replies++;
            } else {
                /* No PIN was offered for this peer. Refusing is the only
                 * honest answer; guessing "0000" would be a silent downgrade. */
                hci_cmd(dev, BT_HCI_PIN_CODE_REQ_NEG_REPLY, p, 6);
            }
            return BT_OK;
        }

        case BT_EVT_LINK_KEY_REQUEST: {
            if (n != 6) return BT_EPROTO;
            int idx = dev_find_index(dev, p);
            if (idx >= 0 && dev->devices[idx].link_key_valid) {
                uint8_t rep[22];
                bts_memcpy(rep, p, 6);
                bts_memcpy(rep + 6, dev->devices[idx].link_key, 16);
                hci_cmd(dev, BT_HCI_LINK_KEY_REQ_REPLY, rep, 22);
            } else {
                hci_cmd(dev, BT_HCI_LINK_KEY_REQ_NEG_REPLY, p, 6);
            }
            return BT_OK;
        }

        case BT_EVT_LINK_KEY_NOTIFY: {
            if (n != 23) return BT_EPROTO;
            int idx = dev_find_index(dev, p);
            if (idx < 0) idx = dev_add(dev, p);
            if (idx < 0) return BT_OK;
            bts_memcpy(dev->devices[idx].link_key, p + 6, 16);
            dev->devices[idx].link_key_type = p[22];
            dev->devices[idx].link_key_valid = true;
            dev->stats.link_keys++;
            return BT_OK;
        }

        case BT_EVT_NUM_COMP_PKTS: {
            if (n < 1) return BT_EPROTO;
            uint32_t nh = p[0];
            if (n != 1u + nh * 4u) return BT_EPROTO;
            for (uint32_t i = 0; i < nh; i++) {
                uint16_t cnt = bts_le16(p + 1u + i * 4u + 2u);
                dev->acl_credits += cnt;
                if (dev->acl_total_pkts && dev->acl_credits > dev->acl_total_pkts)
                    dev->acl_credits = dev->acl_total_pkts;
            }
            return BT_OK;
        }

        default:
            return BT_OK;              /* unhandled but well-formed */
    }
}

int bt_hci_ingest(bluetooth_device_t *dev, uint8_t pkt_type,
                  const uint8_t *pkt, uint32_t len) {
    if (!dev || !pkt) return BT_EINVAL;

    if (pkt_type == BT_H4_EVT) {
        bt_hci_event_t e;
        if (bt_hci_parse_event(pkt, len, &e) != BT_OK) {
            dev->stats.hci_events_bad++;
            return BT_EPROTO;
        }
        dev->stats.hci_events_rx++;
        int rc = handle_event(dev, &e);
        if (rc != BT_OK) dev->stats.hci_events_bad++;
        bt_tx_flush(dev);
        return rc;
    }

    if (pkt_type == BT_H4_ACL) {
        bt_hci_acl_t a;
        if (bt_hci_parse_acl(pkt, len, &a) != BT_OK) {
            dev->stats.acl_rx_bad++;
            return BT_EPROTO;
        }
        bt_conn_t *c = bt_find_conn_handle(dev, a.handle);
        if (!c) { dev->stats.acl_rx_bad++; return BT_ENOENT; }
        dev->stats.acl_rx_pkts++;
        int r = bt_reasm_push(&c->reasm, pkt, len);
        if (r < 0) {
            dev->stats.reasm_drops++;
            return r;
        }
        if (r > 0) {
            uint32_t plen = 0;
            const uint8_t *pdu = bt_reasm_pdu(&c->reasm, &plen);
            if (pdu && plen) handle_l2cap_pdu(dev, c, pdu, plen);
            bt_reasm_init(&c->reasm);
        }
        bt_tx_flush(dev);
        return BT_OK;
    }

    /* SCO is not carried (see LIMITATIONS) and anything else is not HCI. */
    return BT_ENOTSUP;
}

/* ===================== public API ===================== */

void bt_init(bluetooth_device_t *dev, const char *name) {
    if (!dev) return;
    bts_memset(dev, 0, (uint32_t)sizeof(*dev));
    bts_strcpy(dev->name, name, (uint32_t)sizeof(dev->name));
    bts_strcpy(dev->local_name, name, (uint32_t)sizeof(dev->local_name));
    /* No address is invented: local_addr stays zero until HCI Read BD_ADDR
     * completes, and local_addr_valid says so. */
    dev->version = BT_VERSION_4_0;
    dev->version_valid = false;
    dev->supported_profiles = (uint32_t)BT_PROFILE_A2DP_SRC |
                              (uint32_t)BT_PROFILE_HID |
                              (uint32_t)BT_PROFILE_SPP;
    dev->discoverable = false;
    dev->connectable = false;
    dev->cmd_credits = 1;          /* the spec's start-of-day allowance */
    dev->acl_credits = 0;          /* unknown until Read Buffer Size completes */
    dev->next_scid = BT_L2CAP_CID_DYN_BASE;
    dev->next_sig_ident = 0;
    dev->audio_samples_per_frame = 128;   /* SBC 8 subbands x 16 blocks */
    dev->audio_frames_per_pkt = 1;
    for (uint32_t i = 0; i < BT_MAX_CONNECTIONS; i++) dev->conns[i].dev_index = -1;
    dev->m5.omega = 0;
    dev->m5.r = SR_ONE;
    dev->m5.ell = SR_ONE;
    dev->m5.phi = SR_ZERO;
    dev->m5.chi = 0;
    dev->coverage_r = 1.0;
    dev->coverage_l = 1.0;
}

int bt_bind_ops(bluetooth_device_t *dev, const bt_ops_t *ops) {
    if (!dev) return BT_EINVAL;
    if (!ops) {
        bts_memset(&dev->ops, 0, (uint32_t)sizeof(dev->ops));
        dev->ops_bound = false;
        return BT_OK;
    }
    if (!ops->hci_send || !ops->hci_recv) return BT_EINVAL;
    dev->ops = *ops;
    dev->ops_bound = true;
    return BT_OK;
}

bool bt_has_radio(const bluetooth_device_t *dev) {
    return dev && dev->ops_bound;
}

const bt_stats_t *bt_get_stats(const bluetooth_device_t *dev) {
    if (!dev) return (const bt_stats_t *)0;
    return &dev->stats;
}

int bt_reset(bluetooth_device_t *dev) {
    if (!dev) return BT_EINVAL;
    if (!dev->ops_bound) { dev->stats.hci_cmds_failed++; return BT_ENODEV; }
    int rc = hci_cmd(dev, BT_HCI_RESET, (const uint8_t *)0, 0);
    if (rc != BT_OK) return rc;
    rc = hci_cmd(dev, BT_HCI_READ_BD_ADDR, (const uint8_t *)0, 0);
    if (rc != BT_OK) return rc;
    rc = hci_cmd(dev, BT_HCI_READ_BUFFER_SIZE, (const uint8_t *)0, 0);
    if (rc != BT_OK) return rc;
    return hci_cmd(dev, BT_HCI_READ_LOCAL_VERSION, (const uint8_t *)0, 0);
}

int bt_set_discoverable(bluetooth_device_t *dev, bool on) {
    if (!dev) return BT_EINVAL;
    /* discoverable is NOT touched here: it reflects what the controller has
     * confirmed, not what we asked for. */
    if (!dev->ops_bound) { dev->stats.hci_cmds_failed++; return BT_ENODEV; }
    uint8_t v = on ? BT_SCAN_BOTH : BT_SCAN_DISABLED;
    dev->pending_scan_enable = v;
    dev->scan_enable_pending = true;
    int rc = hci_cmd(dev, BT_HCI_WRITE_SCAN_ENABLE, &v, 1);
    if (rc != BT_OK) dev->scan_enable_pending = false;
    return rc;
}

int bt_set_name(bluetooth_device_t *dev, const char *name) {
    if (!dev || !name) return BT_EINVAL;
    if (bts_strlen(name) > 247) return BT_EMSGSIZE;
    bts_strcpy(dev->local_name, name, (uint32_t)sizeof(dev->local_name));
    if (!dev->ops_bound) { dev->stats.hci_cmds_failed++; return BT_ENODEV; }
    uint8_t p[248];
    bts_memset(p, 0, (uint32_t)sizeof(p));
    uint32_t l = bts_strlen(name);
    bts_memcpy(p, name, l);
    return hci_cmd(dev, BT_HCI_WRITE_LOCAL_NAME, p, 248);
}

int bt_inquiry(bluetooth_device_t *dev, uint32_t duration) {
    if (!dev) return BT_EINVAL;
    if (duration == 0) return BT_EINVAL;
    /* No radio: return the error and leave the device table exactly as it is.
     * An inquiry that never went out cannot have found anybody. */
    if (!dev->ops_bound) { dev->stats.hci_cmds_failed++; return BT_ENODEV; }
    if (dev->inquiring) return BT_EAGAIN;

    uint32_t units = (duration * 100u) / 128u;      /* seconds -> 1.28 s units */
    if (units < BT_INQUIRY_LEN_MIN) units = BT_INQUIRY_LEN_MIN;
    if (units > BT_INQUIRY_LEN_MAX) units = BT_INQUIRY_LEN_MAX;

    uint8_t p[5];
    p[0] = (uint8_t)(BT_GIAC & 0xFF);               /* LAP, LSB first */
    p[1] = (uint8_t)((BT_GIAC >> 8) & 0xFF);
    p[2] = (uint8_t)((BT_GIAC >> 16) & 0xFF);
    p[3] = (uint8_t)units;
    p[4] = 0x00;                                    /* unlimited responses */
    int rc = hci_cmd(dev, BT_HCI_INQUIRY, p, 5);
    if (rc != BT_OK) return rc;
    dev->inquiring = true;
    dev->irq_inquiry_done = false;
    return BT_OK;
}

int bt_pair(bluetooth_device_t *dev, const uint8_t *bdaddr, const char *pin) {
    if (!dev || !bdaddr || !pin) return BT_EINVAL;
    uint32_t pl = bts_strlen(pin);
    if (pl == 0 || pl > 16) return BT_EINVAL;
    if (!dev->ops_bound) { dev->stats.hci_cmds_failed++; return BT_ENODEV; }
    int idx = dev_find_index(dev, bdaddr);
    if (idx < 0) return BT_ENOENT;               /* never heard of this peer */
    bt_conn_t *c = bt_find_conn_addr(dev, bdaddr);
    if (!c || c->handle == 0) return BT_ESTATE;  /* authentication needs a link */
    if (!bt_state_transition_ok(c->state, BT_STATE_PAIRING)) return BT_ESTATE;

    bts_memcpy(dev->devices[idx].pin, pin, pl);
    dev->devices[idx].pin_len = (uint8_t)pl;

    uint8_t p[2];
    bts_put16(p, c->handle);
    int rc = hci_cmd(dev, BT_HCI_AUTH_REQUESTED, p, 2);
    if (rc != BT_OK) return rc;
    conn_set_state(dev, c, BT_STATE_PAIRING);
    return BT_OK;
}

int bt_connect(bluetooth_device_t *dev, const uint8_t *bdaddr, bt_profile_t profile) {
    if (!dev || !bdaddr) return BT_EINVAL;
    if (profile == BT_PROFILE_NONE) return BT_EINVAL;
    if (profile != BT_PROFILE_SPP && profile != BT_PROFILE_HID &&
        profile != BT_PROFILE_A2DP_SRC)
        return BT_ENOTSUP;                       /* see LIMITATIONS */
    if (!dev->ops_bound) { dev->stats.hci_cmds_failed++; return BT_ENODEV; }
    int idx = dev_find_index(dev, bdaddr);
    if (idx < 0) return BT_ENOENT;

    bt_conn_t *c = bt_find_conn_addr(dev, bdaddr);
    if (c) {
        c->requested_profile |= (uint32_t)profile;
        if (c->state == BT_STATE_CONNECTING) return BT_EAGAIN;
        if (c->state == BT_STATE_CONNECTED || c->state == BT_STATE_PAIRED)
            return profile_bringup(dev, c, (uint32_t)profile);
        return BT_ESTATE;
    }

    c = conn_alloc(dev, bdaddr, false);
    if (!c) return BT_ENOSPC;
    c->dev_index = idx;
    c->requested_profile = (uint32_t)profile;

    uint8_t p[13];
    bts_memcpy(p, bdaddr, 6);
    bts_put16(p + 6, 0xCC18);        /* DM1/DH1/DM3/DH3/DM5/DH5 */
    p[8] = 0x01;                     /* page scan repetition mode R1 */
    p[9] = 0x00;                     /* reserved */
    bts_put16(p + 10, 0x0000);       /* clock offset unknown */
    p[12] = 0x01;                    /* role switch allowed */
    int rc = hci_cmd(dev, BT_HCI_CREATE_CONNECTION, p, 13);
    if (rc != BT_OK) { conn_free(dev, c); return rc; }
    conn_set_state(dev, c, BT_STATE_CONNECTING);
    return BT_OK;
}

int bt_disconnect(bluetooth_device_t *dev, const uint8_t *bdaddr) {
    if (!dev || !bdaddr) return BT_EINVAL;
    if (!dev->ops_bound) { dev->stats.hci_cmds_failed++; return BT_ENODEV; }
    bt_conn_t *c = bt_find_conn_addr(dev, bdaddr);
    if (!c) return BT_ENOENT;
    if (c->handle == 0) return BT_ESTATE;
    uint8_t p[3];
    bts_put16(p, c->handle);
    p[2] = 0x13;                     /* remote user terminated connection */
    return hci_cmd(dev, BT_HCI_DISCONNECT, p, 3);
}

int bt_send_data(bluetooth_device_t *dev, const uint8_t *bdaddr,
                 const void *data, uint32_t len) {
    if (!dev || !bdaddr || !data || len == 0) return BT_EINVAL;
    if (!dev->ops_bound) return BT_ENODEV;
    bt_conn_t *c = bt_find_conn_addr(dev, bdaddr);
    if (!c) return BT_ENOENT;
    if (c->rfcomm_state != BT_RFCOMM_OPEN) return BT_ESTATE;
    bt_l2cap_chan_t *ch = chan_by_role(c, BT_ROLE_RFCOMM);
    if (!ch || ch->state != BT_CHAN_OPEN) return BT_ESTATE;
    /* UIH overhead: address + control + up to 2 length octets + FCS. Bound len
     * against the RFCOMM length field first, so len + overhead cannot wrap. */
    if (len > 0x3FFFu) return BT_EMSGSIZE;
    uint32_t overhead = (len < 128) ? 4u : 5u;
    if (len + overhead > ch->out_mtu) return BT_EMSGSIZE;

    int rc = rfcomm_send_frame(dev, c, c->rfcomm_dlci, 1, BT_RFCOMM_UIH, false,
                               (const uint8_t *)data, len);
    if (rc != BT_OK) return rc;
    return (int)len;
}

int bt_recv_data(bluetooth_device_t *dev, uint8_t *bdaddr, void *data,
                 uint32_t max_len) {
    if (!dev || !data) return BT_EINVAL;
    uint32_t size = (uint32_t)sizeof(dev->rx_dma);
    if (ring_used(dev->rx_head, dev->rx_tail, size) < 8u) {
        dev->irq_rx_ready = false;
        return BT_EAGAIN;
    }
    uint8_t hdr[8];
    ring_peek(dev->rx_dma, size, dev->rx_tail, 0, hdr, 8);
    uint32_t n = bts_le16(hdr);
    if (ring_used(dev->rx_head, dev->rx_tail, size) < 8u + n) {
        /* Only reachable if the ring was corrupted; drop it rather than
         * hand back bytes we cannot vouch for. */
        dev->rx_tail = dev->rx_head;
        dev->stats.rx_ring_drops++;
        return BT_EPROTO;
    }
    if (n > max_len) return BT_EMSGSIZE;   /* record stays queued, nothing lost */

    if (bdaddr) bts_memcpy(bdaddr, hdr + 2, 6);
    ring_peek(dev->rx_dma, size, dev->rx_tail, 8, (uint8_t *)data, n);
    ring_drop(size, &dev->rx_tail, 8u + n);
    if (ring_used(dev->rx_head, dev->rx_tail, size) < 8u) dev->irq_rx_ready = false;
    return (int)n;
}

/* ===================== audio (A2DP source) ===================== */

int bt_audio_connect(bluetooth_device_t *dev, const uint8_t *bdaddr) {
    if (!dev || !bdaddr) return BT_EINVAL;
    if (!dev->ops_bound) return BT_ENODEV;
    bt_conn_t *c = bt_find_conn_addr(dev, bdaddr);
    if (!c) return BT_ENOENT;
    if (c->state != BT_STATE_CONNECTED && c->state != BT_STATE_PAIRED)
        return BT_ESTATE;
    c->requested_profile |= (uint32_t)BT_PROFILE_A2DP_SRC;
    return chan_open(dev, c, BT_PSM_AVDTP, BT_ROLE_AVDTP_SIG);
}

int bt_audio_set_frame_geometry(bluetooth_device_t *dev,
                                uint16_t samples_per_frame, uint8_t frames_per_pkt) {
    if (!dev) return BT_EINVAL;
    if (samples_per_frame == 0 || frames_per_pkt == 0 || frames_per_pkt > 0x0F)
        return BT_EINVAL;
    dev->audio_samples_per_frame = samples_per_frame;
    dev->audio_frames_per_pkt = frames_per_pkt;
    return BT_OK;
}

/* The four SBC codec-specific octets of a Media Codec capability (A2DP 4.3.2).
 * Only SBC exists here; every other codec is refused rather than silently
 * mis-encoded. */
static int sbc_caps(uint8_t *out, uint32_t rate) {
    uint8_t freq;
    switch (rate) {
        case 16000: freq = 0x80; break;
        case 32000: freq = 0x40; break;
        case 44100: freq = 0x20; break;
        case 48000: freq = 0x10; break;
        default: return BT_EINVAL;
    }
    out[0] = (uint8_t)(freq | 0x02); /* sampling frequency | stereo */
    out[1] = 0x15;                   /* 16 blocks, 8 subbands, loudness */
    out[2] = 2;                      /* min bitpool */
    out[3] = 53;                     /* max bitpool */
    return BT_OK;
}

int bt_audio_start(bluetooth_device_t *dev, uint16_t codec, uint32_t sample_rate) {
    if (!dev) return BT_EINVAL;
    if (!dev->ops_bound) return BT_ENODEV;
    if (codec != BT_A2DP_CODEC_SBC) return BT_ENOTSUP;   /* no other encoder */
    uint8_t sbc[4];
    if (sbc_caps(sbc, sample_rate) != BT_OK) return BT_EINVAL;

    bt_conn_t *c = (bt_conn_t *)0;
    for (uint32_t i = 0; i < BT_MAX_CONNECTIONS; i++) {
        if (dev->conns[i].in_use && chan_by_role(&dev->conns[i], BT_ROLE_AVDTP_SIG)) {
            c = &dev->conns[i];
            break;
        }
    }
    if (!c) return BT_ENOENT;
    if (c->a2dp_state != BT_A2DP_DISCOVERED) return BT_ESTATE;

    /* SET_CONFIGURATION: ACP SEID, INT SEID, then service capabilities.
     * Category 0x01 Media Transport (empty), category 0x07 Media Codec. */
    uint8_t p[16];
    p[0] = c->a2dp_seid;             /* already in the (seid << 2) wire form */
    p[1] = (uint8_t)(1u << 2);       /* our stream endpoint id = 1 */
    p[2] = 0x01; p[3] = 0x00;        /* Media Transport, length 0 */
    p[4] = 0x07; p[5] = 0x06;        /* Media Codec, length 6 */
    p[6] = 0x00;                     /* media type: audio */
    p[7] = BT_A2DP_CODEC_SBC;
    p[8]  = sbc[0];
    p[9]  = sbc[1];
    p[10] = sbc[2];
    p[11] = sbc[3];

    int rc = avdtp_send_cmd(dev, c, BT_AVDTP_SET_CONFIG, p, 12);
    if (rc != BT_OK) return rc;
    c->a2dp_state = BT_A2DP_CONFIGURING;
    dev->audio_codec = codec;
    dev->audio_sample_rate = sample_rate;
    return BT_OK;
}

int bt_audio_stop(bluetooth_device_t *dev) {
    if (!dev) return BT_EINVAL;
    if (!dev->ops_bound) return BT_ENODEV;
    for (uint32_t i = 0; i < BT_MAX_CONNECTIONS; i++) {
        bt_conn_t *c = &dev->conns[i];
        if (!c->in_use || c->a2dp_state != BT_A2DP_STREAMING) continue;
        int rc = avdtp_send_cmd(dev, c, BT_AVDTP_SUSPEND, &c->a2dp_seid, 1);
        if (rc != BT_OK) return rc;
        c->a2dp_state = BT_A2DP_SUSPENDING;
        return BT_OK;
    }
    return BT_ESTATE;               /* nothing is streaming */
}

int bt_audio_send(bluetooth_device_t *dev, const void *data, uint32_t len) {
    if (!dev || !data || len == 0) return BT_EINVAL;
    if (!dev->ops_bound) return BT_ENODEV;
    if (!dev->audio_streaming) return BT_ESTATE;
    bt_conn_t *c = bt_find_conn_handle(dev, (uint16_t)dev->audio_handle);
    if (!c || c->a2dp_state != BT_A2DP_STREAMING) return BT_ESTATE;
    bt_l2cap_chan_t *ch = chan_by_role(c, BT_ROLE_AVDTP_MEDIA);
    if (!ch || ch->state != BT_CHAN_OPEN) return BT_ESTATE;

    /* Bound `len` BEFORE any addition. BT_A2DP_RTP_HDR_LEN + 1 + len wraps for
     * len near UINT32_MAX, which would let both tests below pass and then run
     * the memcpy off the end of pkt[]. */
    if (len > BT_REASM_MAX) return BT_EMSGSIZE;
    uint32_t total = BT_A2DP_RTP_HDR_LEN + 1u + len;
    if (total > ch->out_mtu || total > BT_REASM_MAX) return BT_EMSGSIZE;

    uint8_t pkt[BT_REASM_MAX];
    int hl = bt_a2dp_media_header(pkt, (uint32_t)sizeof(pkt), c->rtp_seq,
                                  c->rtp_ts, c->rtp_ssrc,
                                  dev->audio_frames_per_pkt);
    if (hl < 0) return hl;
    bts_memcpy(pkt + hl, data, len);

    int rc = chan_data_send(dev, c, ch, pkt, (uint32_t)hl + len);
    if (rc != BT_OK) return rc;
    /* Only advance the media clock for a packet that was really queued. */
    c->rtp_seq = (uint16_t)(c->rtp_seq + 1u);
    c->rtp_ts += (uint32_t)dev->audio_samples_per_frame *
                 (uint32_t)dev->audio_frames_per_pkt;
    return (int)len;
}

/* ===================== HID ===================== */

int bt_hid_connect(bluetooth_device_t *dev, const uint8_t *bdaddr) {
    if (!dev || !bdaddr) return BT_EINVAL;
    if (!dev->ops_bound) return BT_ENODEV;
    bt_conn_t *c = bt_find_conn_addr(dev, bdaddr);
    if (!c) return BT_ENOENT;
    if (c->state != BT_STATE_CONNECTED && c->state != BT_STATE_PAIRED)
        return BT_ESTATE;
    c->requested_profile |= (uint32_t)BT_PROFILE_HID;
    int rc = chan_open(dev, c, BT_PSM_HID_CTRL, BT_ROLE_HID_CTRL);
    if (rc != BT_OK) return rc;
    return chan_open(dev, c, BT_PSM_HID_INTR, BT_ROLE_HID_INTR);
}

int bt_hid_send_report(bluetooth_device_t *dev, const uint8_t *bdaddr,
                       const void *report, uint32_t len) {
    if (!dev || !bdaddr || !report || len == 0) return BT_EINVAL;
    if (!dev->ops_bound) return BT_ENODEV;
    bt_conn_t *c = bt_find_conn_addr(dev, bdaddr);
    if (!c) return BT_ENOENT;
    bt_l2cap_chan_t *ch = chan_by_role(c, BT_ROLE_HID_INTR);
    if (!ch || ch->state != BT_CHAN_OPEN) return BT_ESTATE;
    if (len > BT_REASM_MAX) return BT_EMSGSIZE;      /* before len + 1 can wrap */
    if (len + 1u > ch->out_mtu || len + 1u > BT_REASM_MAX) return BT_EMSGSIZE;

    uint8_t pkt[BT_REASM_MAX];
    int hl = bt_hidp_build(pkt, (uint32_t)sizeof(pkt), BT_HIDP_DATA,
                           BT_HIDP_RTYPE_OUTPUT, (const uint8_t *)report, len);
    if (hl < 0) return hl;
    int rc = chan_data_send(dev, c, ch, pkt, (uint32_t)hl);
    if (rc != BT_OK) return rc;
    return (int)len;
}

/* ===================== IRQ / polling ===================== */

int bt_poll(bluetooth_device_t *dev, uint32_t max_packets) {
    if (!dev) return BT_EINVAL;
    if (!dev->ops_bound || !dev->ops.hci_recv) return BT_ENODEV;
    uint32_t got = 0;
    for (uint32_t i = 0; i < max_packets; i++) {
        uint8_t type = 0;
        uint8_t buf[BT_MAX_HCI_PKT];
        int n = dev->ops.hci_recv(dev->ops.ctx, &type, buf, (uint32_t)sizeof(buf));
        if (n <= 0) break;
        bt_hci_ingest(dev, type, buf, (uint32_t)n);
        got++;
    }
    bt_tx_flush(dev);
    return (int)got;
}

void bt_handle_irq(bluetooth_device_t *dev) {
    if (!dev) return;
    /* With no transport there is no source of events, so nothing is latched.
     * Fabricating IRQ flags here is exactly the defect this tree was audited
     * for. */
    if (!dev->ops_bound) return;
    bt_poll(dev, 16);
}

/* ===================== coverage =====================
 * A real audit of this stack's internal consistency, with named ways to fail:
 *
 *   S1  num_devices / num_gatt_services within their arrays
 *   S2  the rx and tx ring indices point inside their buffers
 *   S3  num_connections equals the number of in-use slots
 *   S4  audio_streaming implies some slot is actually in AVDTP STREAMING
 *   S1..S4 are structural: any violation fails outright.
 *
 *   r   fraction of device records that are self-consistent:
 *         connected  => an in-use slot with this address and a live handle
 *         paired     => a link key really was reported by the controller
 *   l   fraction of in-use connection slots that are self-consistent:
 *         dev_index in range, and a nonzero handle unless still CONNECTING
 *
 * Empty tables score 1.0 — a stack with nothing on it has nothing to get
 * wrong. Note what this is NOT: it is not the EDP r*l >= 1.8 hyperbola. Both
 * factors here are fractions in [0,1], so that floor would be unreachable by
 * construction, and a check that cannot pass is as useless as one that cannot
 * fail. */
bool bt_verify_coverage(bluetooth_device_t *dev) {
    if (!dev) return false;

    /* S1 */
    if (dev->num_devices > BT_MAX_DEVICES) return false;
    if (dev->num_gatt_services > BT_MAX_GATT_SERVICES) return false;
    /* S2 */
    if (dev->rx_head >= sizeof(dev->rx_dma) || dev->rx_tail >= sizeof(dev->rx_dma))
        return false;
    if (dev->tx_head >= sizeof(dev->tx_dma) || dev->tx_tail >= sizeof(dev->tx_dma))
        return false;

    uint32_t in_use = 0, conn_ok = 0;
    bool streaming_backed = false;
    for (uint32_t i = 0; i < BT_MAX_CONNECTIONS; i++) {
        const bt_conn_t *c = &dev->conns[i];
        if (!c->in_use) continue;
        in_use++;
        bool ok = true;
        if (c->dev_index < 0 || (uint32_t)c->dev_index >= dev->num_devices) ok = false;
        if (c->handle == 0 && c->state != BT_STATE_CONNECTING) ok = false;
        if (c->handle > 0x0FFF) ok = false;
        if (dev->connections[i] != c->handle) ok = false;
        if (ok) conn_ok++;
        if (c->a2dp_state == BT_A2DP_STREAMING) streaming_backed = true;
    }
    /* S3 */
    if (in_use != dev->num_connections) return false;
    /* S4 */
    if (dev->audio_streaming && !streaming_backed) return false;

    uint32_t dev_ok = 0;
    for (uint32_t i = 0; i < dev->num_devices && i < BT_MAX_DEVICES; i++) {
        const bt_device_t *d = &dev->devices[i];
        bool ok = true;
        if (d->connected) {
            const bt_conn_t *match = (const bt_conn_t *)0;
            for (uint32_t j = 0; j < BT_MAX_CONNECTIONS; j++)
                if (dev->conns[j].in_use &&
                    bts_memcmp(dev->conns[j].bdaddr, d->bdaddr, 6) == 0)
                    match = &dev->conns[j];
            if (!match || match->handle == 0) ok = false;
        }
        if (d->paired && !d->link_key_valid) ok = false;
        if (ok) dev_ok++;
    }

    dev->coverage_r = (dev->num_devices == 0) ? (uint32_t) Q16_ONE
                                              : (dev_ok * (uint32_t) Q16_ONE) /
                                                    dev->num_devices; /* <= BT_MAX_DEVICES */
    dev->coverage_l = (in_use == 0) ? (uint32_t) Q16_ONE : (conn_ok * (uint32_t) Q16_ONE) / in_use;

    dev->m5.omega = dev->stats.hci_events_rx;
    dev->m5.r   = (dev->num_devices == 0) ? SR_ONE
                : SR_DIV(SR_FROM_INT((int64_t)dev_ok), SR_FROM_INT((int64_t)dev->num_devices));
    dev->m5.ell = (in_use == 0) ? SR_ONE
                : SR_DIV(SR_FROM_INT((int64_t)conn_ok), SR_FROM_INT((int64_t)in_use));
    dev->m5.phi = SR_ZERO;
    dev->m5.chi = in_use;

    return (uint64_t) dev->coverage_r * dev->coverage_l >=
           (uint64_t) BT_COVERAGE_FLOOR * (uint64_t) Q16_ONE;
}

/* ---- DECLARATION -----------------------------------------------------------

 * PROVIDES bt_host_stack_ready -- the HOST half above HCI, which is exactly
 * what this file contains. There is no radio, and the bring-up says so by
 * checking the encode/decode identities rather than by trying to page a peer.
 *
 * REQUIRES_NONE is measured: bluetooth.o's `nm -u` is {__divti3, __udivti3},
 * which are libgcc 128-bit division helpers, not module edges.
 *
 * The opcode round trip is the right boot check because OGF and OCF are packed
 * into one 16-bit word (ogf << 10 | ocf); getting that split wrong sends every
 * command to the wrong controller group, and the symptom would be a silent
 * controller rather than an obvious fault.
 */
#include "zxv_decl.h"
static int zxvd_bluetooth_bringup(void) {
    static bluetooth_device_t dev;
    uint16_t op;

    bt_init(&dev, "zxv-bt");
    op = bt_hci_opcode(0x03u, 0x0003u);           /* HCI_Reset */
    if (bt_hci_opcode_ogf(op) != 0x03u)   return -1;
    if (bt_hci_opcode_ocf(op) != 0x0003u) return -1;

    /* the state machine must refuse what it says it refuses */
    if (!bt_state_transition_ok(BT_STATE_DISCONNECTED, BT_STATE_CONNECTING))
        return -1;
    if (bt_state_transition_ok(BT_STATE_DISCONNECTED, BT_STATE_CONNECTED))
        return -1;
    return 0;
}

ZXV_DECLARE(bluetooth,
    ZXV_PROVIDES(bt_host_stack_ready),
    ZXV_REQUIRES_NONE,
    ZXV_BRINGUP(zxvd_bluetooth_bringup));
