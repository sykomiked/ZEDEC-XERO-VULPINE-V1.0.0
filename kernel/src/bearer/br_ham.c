/* Copyright (c) 2024-2026 Michael Laurence Curzi and 36N9 Genetics, LLC */
/* SPDX-License-Identifier: Apache-2.0 */
/* br_ham.c — signed cleartext emergency messages over AX.25 (br_ham.h,
 * rules H1..H5). */
#include "br_ham.h"

static const char k_b64[] = "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/";
static const char k_sep[] = "\n-- sig ";
#define SEP_LEN 8u
static const char k_dom[] = "ZXV-EMRG-1\n";
#define DOM_LEN 11u

int32_t br_b64_encode(const uint8_t *in, uint32_t n, char *out, uint32_t cap)
{
    if ((!in && n) || !out || n > 0x5FFFFFFFu) return BR_ERR_ARG;
    uint32_t need = (n + 2u) / 3u * 4u;
    if (cap < need) return BR_ERR_SIZE;
    uint32_t o = 0, i = 0;
    for (; i + 3u <= n; i += 3u) {
        uint32_t v = ((uint32_t) in[i] << 16) | ((uint32_t) in[i + 1] << 8) | in[i + 2];
        out[o++] = k_b64[(v >> 18) & 63u];
        out[o++] = k_b64[(v >> 12) & 63u];
        out[o++] = k_b64[(v >> 6) & 63u];
        out[o++] = k_b64[v & 63u];
    }
    if (n - i == 1u) {
        uint32_t v = (uint32_t) in[i] << 16;
        out[o++] = k_b64[(v >> 18) & 63u];
        out[o++] = k_b64[(v >> 12) & 63u];
        out[o++] = '=';
        out[o++] = '=';
    } else if (n - i == 2u) {
        uint32_t v = ((uint32_t) in[i] << 16) | ((uint32_t) in[i + 1] << 8);
        out[o++] = k_b64[(v >> 18) & 63u];
        out[o++] = k_b64[(v >> 12) & 63u];
        out[o++] = k_b64[(v >> 6) & 63u];
        out[o++] = '=';
    }
    return (int32_t) o;
}

static int b64v(char c)
{
    if (c >= 'A' && c <= 'Z') return c - 'A';
    if (c >= 'a' && c <= 'z') return c - 'a' + 26;
    if (c >= '0' && c <= '9') return c - '0' + 52;
    if (c == '+') return 62;
    if (c == '/') return 63;
    return -1;
}

int32_t br_b64_decode(const char *in, uint32_t n, uint8_t *out, uint32_t cap)
{
    if (!in || !out || n == 0 || (n & 3u)) return BR_ERR_FORMAT;
    uint32_t o = 0;
    for (uint32_t i = 0; i < n; i += 4u) {
        int v[4];
        uint32_t pad = 0;
        for (uint32_t k = 0; k < 4u; k++) {
            char c = in[i + k];
            if (c == '=') {
                if (i + 4u != n || k < 2u) return BR_ERR_FORMAT;
                pad++;
                v[k] = 0;
                continue;
            }
            if (pad) return BR_ERR_FORMAT; /* data after padding */
            v[k] = b64v(c);
            if (v[k] < 0) return BR_ERR_FORMAT;
        }
        uint32_t w = ((uint32_t) v[0] << 18) | ((uint32_t) v[1] << 12) | ((uint32_t) v[2] << 6) |
                     (uint32_t) v[3];
        /* canonical: unused bits must be zero */
        if ((pad == 1u && (w & 0xFFu)) || (pad == 2u && (w & 0xFFFFu))) return BR_ERR_FORMAT;
        uint32_t k = 3u - pad;
        if (o + k > cap) return BR_ERR_SIZE;
        out[o++] = (uint8_t) (w >> 16);
        if (k > 1u) out[o++] = (uint8_t) (w >> 8);
        if (k > 2u) out[o++] = (uint8_t) w;
    }
    return (int32_t) o;
}

int32_t br_ham_signed_bytes(const br_ax25_addr_t *src, const uint8_t *text, uint32_t n,
                            uint8_t *out, uint32_t cap)
{
    char call[10];
    if (!src || (!text && n) || !out || n > BR_HAM_TEXT_MAX) return BR_ERR_ARG;
    int32_t cl = br_ax25_addr_format(src, call);
    if (cl < 0) return BR_ERR_ARG;
    uint32_t need = DOM_LEN + (uint32_t) cl + 1u + n;
    if (cap < need) return BR_ERR_SIZE;
    br_mcpy(out, k_dom, DOM_LEN);
    br_mcpy(out + DOM_LEN, call, (uint32_t) cl);
    out[DOM_LEN + (uint32_t) cl] = '\n';
    br_mcpy(out + DOM_LEN + (uint32_t) cl + 1u, text, n);
    return (int32_t) need;
}

static br_status_t check_sig(br_ham_verify_fn verify, void *ctx, const br_ax25_addr_t *src,
                             const uint8_t *text, uint32_t n, const uint8_t *sig, uint32_t sl,
                             uint8_t *scratch)
{
    char call[10];
    if (!verify) return BR_ERR_AUTH;
    if (br_ax25_addr_format(src, call) < 0) return BR_ERR_ARG;
    int32_t m = br_ham_signed_bytes(src, text, n, scratch, BR_HAM_SIGNED_MAX);
    if (m < 0) return (br_status_t) m;
    return verify(ctx, call, scratch, (uint32_t) m, sig, sl) == 1 ? BR_OK : BR_ERR_AUTH;
}

br_status_t br_ham_tx_begin(br_ham_tx_t *tx, const br_ham_cfg_t *cfg, uint16_t msg_id, uint8_t cls,
                            uint8_t flags, const uint8_t *text, uint32_t n, const uint8_t *sig,
                            uint32_t sig_len)
{
    if (!tx || !cfg) return BR_ERR_ARG;
    br_mset(tx, 0, sizeof *tx);
    if (!cfg->licensed_operator) return BR_ERR_DISABLED; /* H1 */
    if (!br_ax25_is_station(&cfg->src) || !cfg->dst.call[0]) return BR_ERR_ARG;
    br_status_t rc = br_policy_check(BR_HAM_SDR, cls, flags, text, n); /* H2 */
    if (rc != BR_OK) return rc;
    if (n > BR_HAM_TEXT_MAX || !sig || sig_len == 0 || sig_len > BR_HAM_SIG_MAX) /* H4 */
        return BR_ERR_SIZE;
    rc = check_sig(cfg->verify, cfg->verify_ctx, &cfg->src, text, n, sig, sig_len, tx->scratch);
    if (rc != BR_OK) return rc; /* H3 */
    uint32_t b64 = (sig_len + 2u) / 3u * 4u;
    uint32_t total = n + SEP_LEN + b64;
    if (total > BR_HAM_BODY_MAX) return BR_ERR_SIZE;
    br_mcpy(tx->body, text, n);
    br_mcpy(tx->body + n, k_sep, SEP_LEN);
    if (br_b64_encode(sig, sig_len, tx->body + n + SEP_LEN, b64) != (int32_t) b64)
        return BR_ERR_ARG;
    br_mcpy(&tx->cfg, cfg, sizeof *cfg);
    tx->msg_id = msg_id;
    tx->body_len = total;
    tx->count = (total + BR_HAM_CHUNK - 1u) / BR_HAM_CHUNK;
    tx->idx = 0;
    return BR_OK;
}

static const char k_hex[] = "0123456789ABCDEF";

static uint32_t put_dec(char *o, uint32_t v)
{
    if (v >= 10u) {
        o[0] = (char) ('0' + v / 10u);
        o[1] = (char) ('0' + v % 10u);
        return 2;
    }
    o[0] = (char) ('0' + v);
    return 1;
}

int32_t br_ham_tx_next(br_ham_tx_t *tx, uint8_t *out, uint32_t cap)
{
    if (!tx || !out) return BR_ERR_ARG;
    if (tx->count == 0) return BR_ERR_STATE;
    if (tx->idx >= tx->count) return 0;
    char info[BR_HAM_HDR_MAX + BR_HAM_CHUNK];
    uint32_t h = 0;
    info[h++] = 'Z';
    info[h++] = 'X';
    info[h++] = 'V';
    info[h++] = '1';
    info[h++] = ' ';
    for (int s = 12; s >= 0; s -= 4) info[h++] = k_hex[(tx->msg_id >> s) & 15u];
    info[h++] = ' ';
    h += put_dec(info + h, tx->idx + 1u);
    info[h++] = '/';
    h += put_dec(info + h, tx->count);
    info[h++] = ' ';
    uint32_t off = tx->idx * BR_HAM_CHUNK;
    uint32_t take = tx->body_len - off;
    if (take > BR_HAM_CHUNK) take = BR_HAM_CHUNK;
    br_mcpy(info + h, tx->body + off, take);
    int32_t r = br_ax25_ui_encode(&tx->cfg.dst, &tx->cfg.src, 0, 0, BR_AX25_PID_NONE,
                                  (const uint8_t *) info, h + take, out, cap);
    if (r > 0) tx->idx++;
    return r;
}

/* ===== receive ===== */
br_status_t br_ham_rx_init(br_ham_rx_t *rx, br_ham_verify_fn verify, void *ctx)
{
    if (!rx) return BR_ERR_ARG;
    br_mset(rx, 0, sizeof *rx);
    rx->verify = verify;
    rx->verify_ctx = ctx;
    return BR_OK;
}

static int hexv(uint8_t c)
{
    if (c >= '0' && c <= '9') return c - '0';
    if (c >= 'A' && c <= 'F') return c - 'A' + 10;
    return -1;
}

/* Parse 1..2 decimal digits without a leading zero; returns chars used. */
static uint32_t get_dec(const uint8_t *p, uint32_t n, uint32_t *v)
{
    if (n == 0 || p[0] < '1' || p[0] > '9') return 0;
    *v = (uint32_t) (p[0] - '0');
    if (n > 1u && p[1] >= '0' && p[1] <= '9') {
        *v = *v * 10u + (uint32_t) (p[1] - '0');
        return 2;
    }
    return 1;
}

static int refuse(br_ham_rx_t *rx, int rc)
{
    rx->refused++;
    return rc;
}

int br_ham_rx_push(br_ham_rx_t *rx, const uint8_t *frame, uint32_t n, const uint8_t **text,
                   uint32_t *text_len, br_ax25_addr_t *from)
{
    if (!rx || !frame || !text || !text_len) return BR_ERR_ARG;
    br_ax25_ui_t ui;
    br_status_t rc = br_ax25_ui_decode(frame, n, &ui);
    if (rc != BR_OK) return refuse(rx, rc);
    if (ui.pid != BR_AX25_PID_NONE || !br_ax25_is_station(&ui.src)) return refuse(rx, BR_ERR_FORMAT);
    const uint8_t *p = ui.info;
    uint32_t il = ui.info_len;
    if (il < 13u || !br_meq(p, "ZXV1 ", 5) || p[9] != ' ') return refuse(rx, BR_ERR_FORMAT);
    uint32_t id = 0;
    for (uint32_t k = 5; k < 9u; k++) {
        int v = hexv(p[k]);
        if (v < 0) return refuse(rx, BR_ERR_FORMAT);
        id = (id << 4) | (uint32_t) v;
    }
    uint32_t o = 10, idx = 0, cnt = 0, u;
    if (!(u = get_dec(p + o, il - o, &idx))) return refuse(rx, BR_ERR_FORMAT);
    o += u;
    if (o >= il || p[o] != '/') return refuse(rx, BR_ERR_FORMAT);
    o++;
    if (!(u = get_dec(p + o, il - o, &cnt))) return refuse(rx, BR_ERR_FORMAT);
    o += u;
    if (o >= il || p[o] != ' ') return refuse(rx, BR_ERR_FORMAT);
    o++;
    if (cnt > BR_HAM_MAX_FRAMES || idx > cnt) return refuse(rx, BR_ERR_FORMAT);
    uint32_t cl = il - o;
    if (cl == 0 || cl > BR_HAM_CHUNK || (idx < cnt && cl != BR_HAM_CHUNK))
        return refuse(rx, BR_ERR_FORMAT);
    if (!br_printable(p + o, cl)) return refuse(rx, BR_ERR_POLICY_TEXT);
    /* H5: new message? */
    if (!rx->active || rx->msg_id != id || rx->count != cnt || rx->src.ssid != ui.src.ssid ||
        !br_meq(rx->src.call, ui.src.call, sizeof rx->src.call)) {
        rx->active = 1;
        rx->msg_id = (uint16_t) id;
        rx->count = cnt;
        rx->have = 0;
        br_mcpy(&rx->src, &ui.src, sizeof ui.src);
    }
    br_mcpy(rx->body + (idx - 1u) * BR_HAM_CHUNK, p + o, cl);
    if (idx == cnt) rx->last_len = cl;
    rx->have |= (uint64_t) 1 << (idx - 1u);
    uint64_t all = cnt == 64u ? ~(uint64_t) 0 : (((uint64_t) 1 << cnt) - 1u);
    if (rx->have != all) return 0;
    rx->active = 0;
    uint32_t blen = (cnt - 1u) * BR_HAM_CHUNK + rx->last_len;
    /* split at the last separator */
    int32_t sep = -1;
    for (uint32_t i = 0; i + SEP_LEN <= blen; i++)
        if (br_meq(rx->body + i, k_sep, SEP_LEN)) sep = (int32_t) i;
    if (sep < 0) return refuse(rx, BR_ERR_FORMAT);
    uint32_t tl = (uint32_t) sep;
    uint32_t b64l = blen - tl - SEP_LEN;
    int32_t sl = br_b64_decode(rx->body + tl + SEP_LEN, b64l, rx->sig, BR_HAM_SIG_MAX);
    if (sl <= 0) return refuse(rx, BR_ERR_FORMAT);
    if (tl == 0 || tl > BR_HAM_TEXT_MAX) return refuse(rx, BR_ERR_SIZE);
    rc = br_policy_check(BR_HAM_SDR, BR_CLASS_EMERGENCY, BR_F_SIGNED, (const uint8_t *) rx->body,
                         tl);
    if (rc != BR_OK) return refuse(rx, rc);
    rc = check_sig(rx->verify, rx->verify_ctx, &rx->src, (const uint8_t *) rx->body, tl, rx->sig,
                   (uint32_t) sl, rx->scratch);
    if (rc != BR_OK) return refuse(rx, rc);
    *text = (const uint8_t *) rx->body;
    *text_len = tl;
    if (from) br_mcpy(from, &rx->src, sizeof rx->src);
    return 1;
}
