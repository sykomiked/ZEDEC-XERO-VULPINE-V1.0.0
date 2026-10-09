/* Copyright (c) 2024-2026 Michael Laurence Curzi and 36N9 Genetics, LLC */
/* SPDX-License-Identifier: Apache-2.0 */
/* tcap.c — see tcap.h. Minimal definite-length BER + TCAP + MAP carriage. */
#include "tcap.h"
#include "legacy_util.h"

/* ===================== BER primitives ===================== */

/* Read a definite-length BER TLV header at *pos (index into buf[len]).
 * Supports single-octet low-tag-number tags (what our structures use) and
 * short/long definite length up to 0x84. Sets *tag, *vlen (value length) and
 * *vpos (index of the value). Returns false on malformed/overrun. */
static bool ber_tlv(const uint8_t *buf, uint32_t len, uint32_t pos, uint8_t *tag, uint32_t *vlen,
                    uint32_t *vpos)
{
    if (pos + 2 > len) return false;
    *tag = buf[pos];
    /* reject high-tag-number form (bits 4-0 all set in first octet) */
    if ((*tag & 0x1F) == 0x1F) return false;
    uint32_t lp = pos + 1;
    uint8_t l0 = buf[lp++];
    uint32_t vl;
    if (l0 < 0x80) {
        vl = l0;
    } else {
        uint32_t nb = l0 & 0x7F;
        if (nb == 0 || nb > 4) return false; /* indefinite or absurd */
        if (lp + nb > len) return false;
        vl = 0;
        for (uint32_t i = 0; i < nb; i++) vl = (vl << 8) | buf[lp++];
    }
    if (lp + vl > len) return false;
    *vlen = vl;
    *vpos = lp;
    return true;
}

/* Write a BER tag + definite length, then return writer position for value. */
static void ber_write_tl(lg_writer *w, uint8_t tag, uint32_t vlen)
{
    lg_w_byte(w, tag);
    if (vlen < 0x80) {
        lg_w_byte(w, (uint8_t) vlen);
    } else if (vlen < 0x100) {
        lg_w_byte(w, 0x81);
        lg_w_byte(w, (uint8_t) vlen);
    } else {
        lg_w_byte(w, 0x82);
        lg_w_byte(w, (uint8_t) (vlen >> 8));
        lg_w_byte(w, (uint8_t) vlen);
    }
}

/* minimal unsigned INTEGER encode (value < 2^31); always >=1 content octet */
static void ber_write_uint(lg_writer *w, uint32_t v)
{
    uint8_t b[5];
    uint32_t n = 0;
    /* big-endian minimal, with leading 0 if high bit set to keep it positive */
    if (v == 0) {
        b[n++] = 0;
    } else {
        uint8_t tmp[4];
        uint32_t t = 0;
        while (v) {
            tmp[t++] = (uint8_t) v;
            v >>= 8;
        }
        if (tmp[t - 1] & 0x80) b[n++] = 0;
        while (t) b[n++] = tmp[--t];
    }
    ber_write_tl(w, 0x02, n);
    lg_w_bytes(w, b, n);
}

static bool ber_read_uint(const uint8_t *v, uint32_t vlen, uint32_t *out)
{
    if (vlen == 0 || vlen > 5) return false;
    uint32_t r = 0;
    for (uint32_t i = 0; i < vlen; i++) r = (r << 8) | v[i];
    *out = r;
    return true;
}

/* ===================== TCAP ===================== */

bool tcap_parse(const uint8_t *buf, uint32_t len, tcap_msg *m)
{
    uint8_t tag;
    uint32_t vlen, vpos;
    if (!ber_tlv(buf, len, 0, &tag, &vlen, &vpos)) return false;
    switch (tag) {
    case TCAP_TAG_BEGIN:
        m->type = TCAP_BEGIN;
        break;
    case TCAP_TAG_END:
        m->type = TCAP_END;
        break;
    case TCAP_TAG_CONTINUE:
        m->type = TCAP_CONTINUE;
        break;
    case TCAP_TAG_ABORT:
        m->type = TCAP_ABORT;
        break;
    default:
        return false;
    }
    m->has_otid = m->has_dtid = false;
    m->otid = m->dtid = 0;
    m->comp_len = 0;

    uint32_t end = vpos + vlen;
    uint32_t pos = vpos;
    while (pos < end) {
        uint8_t t;
        uint32_t l, vp;
        if (!ber_tlv(buf, end, pos, &t, &l, &vp)) return false;
        if (t == TCAP_TAG_OTID) {
            if (l == 0 || l > 4) return false;
            uint32_t v = 0;
            for (uint32_t i = 0; i < l; i++) v = (v << 8) | buf[vp + i];
            m->otid = v;
            m->has_otid = true;
        } else if (t == TCAP_TAG_DTID) {
            if (l == 0 || l > 4) return false;
            uint32_t v = 0;
            for (uint32_t i = 0; i < l; i++) v = (v << 8) | buf[vp + i];
            m->dtid = v;
            m->has_dtid = true;
        } else if (t == TCAP_TAG_COMP) {
            if (l > TCAP_MAX_COMP) return false;
            lg_copy(m->comp, buf + vp, l);
            m->comp_len = l;
        }
        pos = vp + l;
    }
    return true;
}

uint32_t tcap_build(const tcap_msg *m, uint8_t *out, uint32_t cap)
{
    uint8_t tag;
    switch (m->type) {
    case TCAP_BEGIN:
        tag = TCAP_TAG_BEGIN;
        break;
    case TCAP_END:
        tag = TCAP_TAG_END;
        break;
    case TCAP_CONTINUE:
        tag = TCAP_TAG_CONTINUE;
        break;
    default:
        tag = TCAP_TAG_ABORT;
        break;
    }
    /* build the content first into a temp to know its length */
    uint8_t body[TCAP_MAX_COMP + 32];
    lg_writer bw;
    lg_w_init(&bw, body, sizeof body);
    if (m->has_otid) {
        ber_write_tl(&bw, TCAP_TAG_OTID, 4);
        uint8_t t[4];
        lg_put32(t, m->otid);
        lg_w_bytes(&bw, t, 4);
    }
    if (m->has_dtid) {
        ber_write_tl(&bw, TCAP_TAG_DTID, 4);
        uint8_t t[4];
        lg_put32(t, m->dtid);
        lg_w_bytes(&bw, t, 4);
    }
    if (m->comp_len) {
        ber_write_tl(&bw, TCAP_TAG_COMP, m->comp_len);
        lg_w_bytes(&bw, m->comp, m->comp_len);
    }
    if (!lg_w_ok(&bw)) return 0;

    lg_writer w;
    lg_w_init(&w, out, cap);
    ber_write_tl(&w, tag, bw.len);
    lg_w_bytes(&w, body, bw.len);
    if (!lg_w_ok(&w)) return 0;
    return w.len;
}

/* ===================== MAP Invoke ===================== */

uint32_t map_build_invoke(const map_invoke *iv, uint8_t *out, uint32_t cap)
{
    if (iv->arg_len > sizeof iv->arg) return 0;
    uint8_t body[300];
    lg_writer bw;
    lg_w_init(&bw, body, sizeof body);
    ber_write_uint(&bw, iv->invoke_id);    /* invokeID INTEGER */
    ber_write_uint(&bw, iv->opcode);       /* operationCode localValue INTEGER */
    lg_w_bytes(&bw, iv->arg, iv->arg_len); /* argument (already BER) */
    if (!lg_w_ok(&bw)) return 0;

    lg_writer w;
    lg_w_init(&w, out, cap);
    ber_write_tl(&w, TCAP_TAG_INVOKE, bw.len);
    lg_w_bytes(&w, body, bw.len);
    if (!lg_w_ok(&w)) return 0;
    return w.len;
}

bool map_parse_invoke(const uint8_t *comp, uint32_t len, map_invoke *iv)
{
    uint8_t tag;
    uint32_t vlen, vpos;
    if (!ber_tlv(comp, len, 0, &tag, &vlen, &vpos)) return false;
    if (tag != TCAP_TAG_INVOKE) return false;
    uint32_t end = vpos + vlen;
    uint32_t pos = vpos;
    /* invokeID */
    uint8_t t;
    uint32_t l, vp;
    if (!ber_tlv(comp, end, pos, &t, &l, &vp) || t != 0x02) return false;
    uint32_t v;
    if (!ber_read_uint(comp + vp, l, &v)) return false;
    iv->invoke_id = (uint8_t) v;
    pos = vp + l;
    /* operationCode (local INTEGER) — skip optional linkedID if an INTEGER
     * appears twice we treat the second as opcode is not needed here */
    if (!ber_tlv(comp, end, pos, &t, &l, &vp) || t != 0x02) return false;
    if (!ber_read_uint(comp + vp, l, &iv->opcode)) return false;
    pos = vp + l;
    /* argument: remaining bytes */
    uint32_t al = end - pos;
    if (al > sizeof iv->arg) return false;
    lg_copy(iv->arg, comp + pos, al);
    iv->arg_len = al;
    return true;
}

/* ===================== USSD-Arg ===================== */

uint32_t map_build_ussd_arg(uint8_t dcs, const uint8_t *str, uint32_t nstr, uint8_t *out,
                            uint32_t cap)
{
    uint8_t body[300];
    lg_writer bw;
    lg_w_init(&bw, body, sizeof body);
    ber_write_tl(&bw, 0x04, 1); /* OCTET STRING dcs */
    lg_w_byte(&bw, dcs);
    ber_write_tl(&bw, 0x04, nstr); /* OCTET STRING ussd-String */
    lg_w_bytes(&bw, str, nstr);
    if (!lg_w_ok(&bw)) return 0;

    lg_writer w;
    lg_w_init(&w, out, cap);
    ber_write_tl(&w, 0x30, bw.len); /* SEQUENCE */
    lg_w_bytes(&w, body, bw.len);
    if (!lg_w_ok(&w)) return 0;
    return w.len;
}

bool map_parse_ussd_arg(const uint8_t *arg, uint32_t len, uint8_t *dcs, uint8_t *str, uint32_t cap,
                        uint32_t *nstr)
{
    uint8_t tag;
    uint32_t vlen, vpos;
    if (!ber_tlv(arg, len, 0, &tag, &vlen, &vpos) || tag != 0x30) return false;
    uint32_t end = vpos + vlen, pos = vpos;
    uint8_t t;
    uint32_t l, vp;
    if (!ber_tlv(arg, end, pos, &t, &l, &vp) || t != 0x04 || l != 1) return false;
    *dcs = arg[vp];
    pos = vp + l;
    if (!ber_tlv(arg, end, pos, &t, &l, &vp) || t != 0x04) return false;
    if (l > cap) return false;
    lg_copy(str, arg + vp, l);
    *nstr = l;
    return true;
}

/* ===================== forwardSM arg ===================== */
/* SEQUENCE { sm-RP-UI OCTET STRING }. Minimal: DA/OA omitted (see header). */
uint32_t map_build_forwardsm_arg(const uint8_t *tpdu, uint32_t n, uint8_t *out, uint32_t cap)
{
    uint8_t body[300];
    lg_writer bw;
    lg_w_init(&bw, body, sizeof body);
    ber_write_tl(&bw, 0x04, n); /* sm-RP-UI */
    lg_w_bytes(&bw, tpdu, n);
    if (!lg_w_ok(&bw)) return 0;
    lg_writer w;
    lg_w_init(&w, out, cap);
    ber_write_tl(&w, 0x30, bw.len);
    lg_w_bytes(&w, body, bw.len);
    if (!lg_w_ok(&w)) return 0;
    return w.len;
}

bool map_parse_forwardsm_arg(const uint8_t *arg, uint32_t len, uint8_t *tpdu, uint32_t cap,
                             uint32_t *n)
{
    uint8_t tag;
    uint32_t vlen, vpos;
    if (!ber_tlv(arg, len, 0, &tag, &vlen, &vpos) || tag != 0x30) return false;
    uint8_t t;
    uint32_t l, vp;
    if (!ber_tlv(arg, vpos + vlen, vpos, &t, &l, &vp) || t != 0x04) return false;
    if (l > cap) return false;
    lg_copy(tpdu, arg + vp, l);
    *n = l;
    return true;
}
