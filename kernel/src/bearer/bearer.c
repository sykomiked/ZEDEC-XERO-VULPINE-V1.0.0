/* Copyright (c) 2024-2026 Michael Laurence Curzi and 36N9 Genetics, LLC */
/* SPDX-License-Identifier: Apache-2.0 */
/* bearer.c — bearer capabilities, policy gates, link measurement,
 * selection and failover. See bearer.h for the rules (B1..B6). */
#include "bearer.h"

#define ALL_CLASSES                                                                                \
    (BR_CLS(BR_CLASS_AI) | BR_CLS(BR_CLASS_SETTLEMENT) | BR_CLS(BR_CLASS_CHAT) |                   \
     BR_CLS(BR_CLASS_EMERGENCY) | BR_CLS(BR_CLASS_RECORD))

/* kind, mtu, mtu_max, bps, metered, enc, settle, auth, text, classes, max_msg, cost, rtt */
static const br_caps_t g_caps[BR_KIND_COUNT] = {
    [BR_LAN] = {BR_LAN, 1200u, 65507u, 100000000u, 0, 1, 1, 0, 0, ALL_CLASSES, 1u << 20, 0u, 2000u},
    [BR_BLE] = {BR_BLE, 20u, 514u, 1000000u, 0, 1, 1, 0, 0, ALL_CLASSES, 131072u, 0u, 30000u},
    [BR_WIFI_DIRECT] = {BR_WIFI_DIRECT, 1200u, 65507u, 50000000u, 0, 1, 1, 0, 0, ALL_CLASSES,
                        1u << 20, 0u, 5000u},
    [BR_LORA] = {BR_LORA, 255u, 255u, 5470u, 0, 1, 1, 1, 0,
                 BR_CLS(BR_CLASS_SETTLEMENT) | BR_CLS(BR_CLASS_EMERGENCY) | BR_CLS(BR_CLASS_RECORD),
                 BR_LORA_MAX_MSG, 50u, 2000000u},
    [BR_SERIAL] = {BR_SERIAL, 1024u, 4096u, 115200u, 0, 1, 1, 0, 0, ALL_CLASSES, 1u << 20, 0u,
                   20000u},
    [BR_MODEM] = {BR_MODEM, 1024u, 4096u, 33600u, 1, 1, 1, 0, 0, ALL_CLASSES, 1u << 20, 200u,
                  150000u},
    [BR_CELLULAR] = {BR_CELLULAR, 1200u, 65507u, 10000000u, 1, 1, 1, 0, 0, ALL_CLASSES, 1u << 20,
                     100u, 60000u},
    [BR_HAM_SDR] = {BR_HAM_SDR, 256u, 256u, 1200u, 0, 0, 0, 1, 1, BR_CLS(BR_CLASS_EMERGENCY),
                    BR_HAM_MAX_MSG, 0u, 5000000u},
};

static const char *const g_names[BR_KIND_COUNT] = {
    [BR_LAN] = "lan",           [BR_BLE] = "ble",         [BR_WIFI_DIRECT] = "wifi-direct",
    [BR_LORA] = "lora",         [BR_SERIAL] = "serial",   [BR_MODEM] = "modem",
    [BR_CELLULAR] = "cellular", [BR_HAM_SDR] = "ham-sdr",
};

/* B4 tie-break order (lower first). */
static const uint8_t g_rank[BR_KIND_COUNT] = {
    [BR_LAN] = 0,      [BR_WIFI_DIRECT] = 1, [BR_SERIAL] = 2, [BR_BLE] = 3,
    [BR_CELLULAR] = 4, [BR_MODEM] = 5,       [BR_LORA] = 6,   [BR_HAM_SDR] = 7,
};

/* B4 weights per class: time, loss, cost. */
static const uint8_t g_w[BR_CLASS_COUNT][3] = {
    [BR_CLASS_AI] = {1, 0, 100},    [BR_CLASS_SETTLEMENT] = {1, 4, 1},
    [BR_CLASS_CHAT] = {2, 1, 10},   [BR_CLASS_EMERGENCY] = {1, 8, 0},
    [BR_CLASS_RECORD] = {1, 2, 10},
};

/* ===== helpers ===== */
void br_mcpy(void *dst, const void *src, size_t n)
{
    uint8_t *d = (uint8_t *) dst;
    const uint8_t *s = (const uint8_t *) src;
    for (size_t i = 0; i < n; i++) d[i] = s[i];
}

void br_mset(void *dst, uint8_t v, size_t n)
{
    volatile uint8_t *d = (volatile uint8_t *) dst;
    for (size_t i = 0; i < n; i++) d[i] = v;
}

bool br_meq(const void *a, const void *b, size_t n)
{
    const uint8_t *x = (const uint8_t *) a, *y = (const uint8_t *) b;
    uint8_t acc = 0;
    for (size_t i = 0; i < n; i++) acc |= (uint8_t) (x[i] ^ y[i]);
    return acc == 0;
}

bool br_printable(const uint8_t *p, uint32_t n)
{
    for (uint32_t i = 0; i < n; i++) {
        uint8_t c = p[i];
        if (c == '\t' || c == '\r' || c == '\n') continue;
        if (c < 0x20u || c > 0x7Eu) return false;
    }
    return true;
}

uint64_t br_udiv64(uint64_t n, uint64_t d)
{
    if (d == 0) return UINT64_MAX;
    if (d > n) return 0;
    if ((d >> 32) == 0 && (n >> 32) == 0) return (uint64_t) ((uint32_t) n / (uint32_t) d);
    uint64_t q = 0, r = 0;
    for (int i = 63; i >= 0; i--) {
        r = (r << 1) | ((n >> i) & 1u);
        if (r >= d) {
            r -= d;
            q |= (uint64_t) 1 << i;
        }
    }
    return q;
}

const br_caps_t *br_caps(uint8_t kind)
{
    if (kind == BR_KIND_NONE || kind >= BR_KIND_COUNT) return 0;
    return &g_caps[kind];
}

const char *br_kind_name(uint8_t kind)
{
    if (kind == BR_KIND_NONE || kind >= BR_KIND_COUNT) return "unknown";
    return g_names[kind];
}

/* ===== B2: policy gates ===== */
br_status_t br_policy_check(uint8_t kind, uint8_t cls, uint8_t flags, const uint8_t *data,
                            uint32_t len)
{
    const br_caps_t *c = br_caps(kind);
    if (!c || cls == 0 || cls >= BR_CLASS_COUNT || (flags & (uint8_t) ~BR_F_ALL)) return BR_ERR_ARG;
    if (len == 0 || !data) return BR_ERR_ARG;
    if (len > c->max_msg) return BR_ERR_SIZE;
    if (!(c->classes & BR_CLS(cls))) return BR_ERR_POLICY_CLASS;
    if ((flags & BR_F_ENCRYPTED) && !c->encrypt_ok) return BR_ERR_POLICY_CRYPTO;
    /* On a cleartext-only bearer an AEAD tag means a session cipher: refuse. */
    if ((flags & BR_F_AEAD) && c->text_only) return BR_ERR_POLICY_CRYPTO;
    if (((flags & BR_F_PAYMENT) || cls == BR_CLASS_SETTLEMENT) && !c->settle_ok)
        return BR_ERR_POLICY_SETTLE;
    if (c->needs_auth) {
        uint8_t ok = c->text_only ? (uint8_t) (flags & BR_F_SIGNED)
                                  : (uint8_t) (flags & (BR_F_SIGNED | BR_F_AEAD));
        if (!ok) return BR_ERR_POLICY_UNSIGNED;
    }
    if (c->text_only && !br_printable(data, len)) return BR_ERR_POLICY_TEXT;
    return BR_OK;
}

/* ===== links ===== */
void br_table_init(br_table_t *t)
{
    if (t) br_mset(t, 0, sizeof *t);
}

br_link_t *br_link_get(br_table_t *t, uint16_t id)
{
    if (!t) return 0;
    for (uint32_t i = 0; i < BR_MAX_LINKS; i++)
        if (t->l[i].used && t->l[i].id == id) return &t->l[i];
    return 0;
}

br_status_t br_link_add(br_table_t *t, uint8_t kind, uint16_t id, const br_link_ops_t *ops)
{
    const br_caps_t *c = br_caps(kind);
    if (!t || !c || !ops || !ops->send) return BR_ERR_ARG;
    if (br_link_get(t, id)) return BR_ERR_ARG;
    for (uint32_t i = 0; i < BR_MAX_LINKS; i++) {
        br_link_t *l = &t->l[i];
        if (l->used) continue;
        br_mset(l, 0, sizeof *l);
        l->used = 1;
        l->up = 1;
        l->kind = kind;
        l->id = id;
        br_mcpy(&l->caps, c, sizeof *c);
        l->ops.ctx = ops->ctx;
        l->ops.send = ops->send;
        return BR_OK;
    }
    return BR_ERR_FULL;
}

br_status_t br_link_tune(br_table_t *t, uint16_t id, uint16_t mtu, uint32_t bitrate_bps,
                         uint32_t cost_per_kib)
{
    br_link_t *l = br_link_get(t, id);
    if (!l) return BR_ERR_ARG;
    if (mtu > l->caps.mtu_max) return BR_ERR_SIZE;
    if (mtu) l->caps.mtu = mtu;
    if (bitrate_bps) l->caps.bitrate_bps = bitrate_bps;
    if (cost_per_kib) l->caps.cost_per_kib = cost_per_kib;
    return BR_OK;
}

br_status_t br_link_set_up(br_table_t *t, uint16_t id, bool up)
{
    br_link_t *l = br_link_get(t, id);
    if (!l) return BR_ERR_ARG;
    l->up = up ? 1u : 0u;
    return BR_OK;
}

br_status_t br_link_measure(br_table_t *t, uint16_t id, uint32_t rtt_us, bool lost)
{
    br_link_t *l = br_link_get(t, id);
    if (!l) return BR_ERR_ARG;
    uint32_t s = lost ? 1000000u : 0u;
    if (l->loss_n == 0)
        l->loss16 = s * 16u;
    else
        l->loss16 = l->loss16 - (l->loss16 >> 4) + s;
    l->loss_n++;
    if (!lost) {
        if (l->rtt_n == 0)
            l->rtt8 = (uint64_t) rtt_us * 8u;
        else
            l->rtt8 = l->rtt8 - (l->rtt8 >> 3) + rtt_us;
        l->rtt_n++;
    }
    return BR_OK;
}

uint32_t br_link_rtt_us(const br_link_t *l)
{
    if (!l) return 0;
    return l->rtt_n ? (uint32_t) (l->rtt8 >> 3) : l->caps.base_rtt_us;
}

uint32_t br_link_loss_ppm(const br_link_t *l)
{
    if (!l) return 0;
    uint32_t v = l->loss16 >> 4;
    return v > 1000000u ? 1000000u : v;
}

uint64_t br_score(const br_link_t *l, uint8_t cls, uint32_t len)
{
    if (!l || cls == 0 || cls >= BR_CLASS_COUNT || l->caps.bitrate_bps == 0) return UINT64_MAX;
    uint64_t t =
        (uint64_t) br_link_rtt_us(l) + br_udiv64((uint64_t) len * 8000000u, l->caps.bitrate_bps);
    uint32_t loss = br_link_loss_ppm(l);
    if (loss > 999999u) loss = 999999u;
    uint64_t tp = br_udiv64(t * 1000000u, 1000000u - loss);
    uint64_t cost = (uint64_t) l->caps.cost_per_kib * ((len + 1023u) >> 10);
    return (uint64_t) g_w[cls][0] * br_udiv64(tp, 1000u) + (uint64_t) g_w[cls][1] * (loss / 1000u) +
           (uint64_t) g_w[cls][2] * cost;
}

static int select_ex(const br_table_t *t, uint8_t cls, uint8_t flags, const uint8_t *data,
                     uint32_t len, uint32_t excl)
{
    int best = -1;
    uint64_t bs = 0;
    for (uint32_t i = 0; i < BR_MAX_LINKS; i++) {
        const br_link_t *l = &t->l[i];
        if (!l->used || !l->up || (excl & (1u << i))) continue;
        if (br_policy_check(l->kind, cls, flags, data, len) != BR_OK) continue;
        uint64_t s = br_score(l, cls, len);
        if (s == UINT64_MAX) continue;
        if (best < 0 || s < bs) {
            best = (int) i;
            bs = s;
            continue;
        }
        if (s == bs) {
            const br_link_t *b = &t->l[best];
            if (g_rank[l->kind] < g_rank[b->kind] ||
                (g_rank[l->kind] == g_rank[b->kind] && l->id < b->id))
                best = (int) i;
        }
    }
    return best;
}

br_status_t br_select(const br_table_t *t, uint8_t cls, uint8_t flags, const uint8_t *data,
                      uint32_t len, uint16_t *id)
{
    if (!t || !id) return BR_ERR_ARG;
    int i = select_ex(t, cls, flags, data, len, 0);
    if (i < 0) return BR_ERR_NOLINK;
    *id = t->l[i].id;
    return BR_OK;
}

br_status_t br_send(br_table_t *t, const uint8_t to[BR_ID_BYTES], uint8_t cls, uint8_t flags,
                    const uint8_t *data, uint32_t len, uint16_t *used_id)
{
    static const uint8_t zero[BR_ID_BYTES] = {0};
    if (!t) return BR_ERR_ARG;
    if (!to) to = zero;
    uint32_t tried = 0;
    bool busy = false;
    for (;;) {
        int i = select_ex(t, cls, flags, data, len, tried);
        if (i < 0) return busy ? BR_ERR_BUSY : BR_ERR_NOLINK;
        br_link_t *l = &t->l[i];
        tried |= 1u << i;
        int rc = l->ops.send(l->ops.ctx, to, data, len);
        if (rc == BR_OK) {
            if (used_id) *used_id = l->id;
            return BR_OK;
        }
        if (rc == BR_ERR_BUSY) {
            busy = true;
            continue;
        }
        l->fails++;
        l->up = 0;
        t->failovers++;
    }
}
