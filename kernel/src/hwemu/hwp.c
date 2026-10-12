/* Copyright (c) 2024-2026 Michael Laurence Curzi and 36N9 Genetics, LLC */
/* SPDX-License-Identifier: Apache-2.0 */
/* hwp.c — ZHP-1 hardware profiles. See hwp.h (P1-P3). */
#include "hwp.h"
#include "hwe_ids.h"
#include "../robin_debanks/sha256.h"

#define HWP_MAGIC0       'Z'
#define HWP_MAGIC1       'H'
#define HWP_MAGIC2       'P'
#define HWP_MAGIC3       '1'
#define HWP_FORMAT       1u
#define HWP_KNOWN_MAX_ID 24u /* highest public cap id this reader knows */

void hwp_init(hwp_profile_t *p)
{
    if (!p) return;
    uint8_t *b = (uint8_t *) p;
    for (uint32_t i = 0; i < (uint32_t) sizeof *p; i++) b[i] = 0;
}

int hwp_node_find(const hwp_profile_t *p, uint16_t id, uint16_t *out_index)
{
    if (!p) return HWP_E_ARG;
    for (uint16_t i = 0; i < p->n; i++)
        if (p->node[i].id == id) {
            if (out_index) *out_index = i;
            return HWP_OK;
        }
    return HWP_E_NOT_FOUND;
}

int hwp_node_add(hwp_profile_t *p, uint16_t id, uint16_t parent, uint32_t cls, uint16_t *out_index)
{
    if (!p || id == HWP_ROOT || cls == 0) return HWP_E_ARG;
    if (p->n >= HWP_MAX_NODES) return HWP_E_FULL;
    if (p->n && p->node[p->n - 1u].id >= id) return HWP_E_CANONICAL; /* P1 */
    if (parent != HWP_ROOT && (parent >= id || hwp_node_find(p, parent, 0) != HWP_OK))
        return HWP_E_CANONICAL; /* P1 */
    hwp_node_t *n = &p->node[p->n];
    n->id = id;
    n->parent = parent;
    n->cls = cls;
    n->ncap = 0;
    n->state_len = 0;
    n->ext_len = 0;
    if (out_index) *out_index = p->n;
    p->n++;
    return HWP_OK;
}

int hwp_cap_set(hwp_profile_t *p, uint16_t node_index, uint16_t key, uint64_t val)
{
    if (!p || node_index >= p->n || HWP_KEY_ID(key) == 0) return HWP_E_ARG;
    hwp_node_t *n = &p->node[node_index];
    uint16_t i = 0;
    while (i < n->ncap && n->cap[i].key < key) i++;
    if (i < n->ncap && n->cap[i].key == key) {
        n->cap[i].val = val;
        return HWP_OK;
    }
    /* Two keys with one id but different flags would be ambiguous. */
    for (uint16_t j = 0; j < n->ncap; j++)
        if (HWP_KEY_ID(n->cap[j].key) == HWP_KEY_ID(key)) return HWP_E_DUPLICATE;
    if (n->ncap >= HWP_MAX_CAPS) return HWP_E_FULL;
    for (uint16_t j = n->ncap; j > i; j--) n->cap[j] = n->cap[j - 1u];
    n->cap[i].key = key;
    n->cap[i].val = val;
    n->ncap++;
    return HWP_OK;
}

bool hwp_cap_get(const hwp_node_t *n, uint16_t key, uint64_t *out)
{
    if (!n) return false;
    for (uint16_t i = 0; i < n->ncap; i++)
        if (HWP_KEY_ID(n->cap[i].key) == HWP_KEY_ID(key)) {
            if (out) *out = n->cap[i].val;
            return true;
        }
    return false;
}

int hwp_state_set(hwp_profile_t *p, uint16_t node_index, const uint8_t *state, uint16_t len)
{
    if (!p || node_index >= p->n || len > HWP_MAX_STATE || (len && !state)) return HWP_E_ARG;
    hwp_node_t *n = &p->node[node_index];
    for (uint16_t i = 0; i < len; i++) n->state[i] = state[i];
    n->state_len = len;
    return HWP_OK;
}

int hwp_ext_set(hwp_profile_t *p, uint16_t node_index, const uint8_t *ext, uint16_t len)
{
    if (!p || node_index >= p->n || len > HWP_MAX_EXT || (len && !ext)) return HWP_E_ARG;
    hwp_node_t *n = &p->node[node_index];
    for (uint16_t i = 0; i < len; i++) n->ext[i] = ext[i];
    n->ext_len = len;
    return HWP_OK;
}

/* ---- encoding ---- */

static void put16(uint8_t *b, uint16_t v)
{
    b[0] = (uint8_t) v;
    b[1] = (uint8_t) (v >> 8);
}
static void put32(uint8_t *b, uint32_t v)
{
    for (int i = 0; i < 4; i++) b[i] = (uint8_t) (v >> (8 * i));
}
static void put64(uint8_t *b, uint64_t v)
{
    for (int i = 0; i < 8; i++) b[i] = (uint8_t) (v >> (8 * i));
}
static uint16_t get16(const uint8_t *b)
{
    return (uint16_t) (b[0] | (b[1] << 8));
}
static uint32_t get32(const uint8_t *b)
{
    return (uint32_t) b[0] | ((uint32_t) b[1] << 8) | ((uint32_t) b[2] << 16) |
           ((uint32_t) b[3] << 24);
}
static uint64_t get64(const uint8_t *b)
{
    uint64_t v = 0;
    for (int i = 7; i >= 0; i--) v = (v << 8) | b[i];
    return v;
}

#define NODE_HDR 16u
#define CAP_LEN  10u

uint32_t hwp_encoded_len(const hwp_profile_t *p)
{
    if (!p) return 0;
    uint32_t len = HWP_HEADER;
    for (uint16_t i = 0; i < p->n; i++)
        len += NODE_HDR + CAP_LEN * p->node[i].ncap + p->node[i].state_len + p->node[i].ext_len;
    return len;
}

static void header(const hwp_profile_t *p, uint32_t total, uint8_t h[HWP_HEADER])
{
    h[0] = HWP_MAGIC0;
    h[1] = HWP_MAGIC1;
    h[2] = HWP_MAGIC2;
    h[3] = HWP_MAGIC3;
    put16(h + 4, HWP_FORMAT);
    put16(h + 6, 0);
    put16(h + 8, p->n);
    put16(h + 10, 0);
    put32(h + 12, total);
}

static void node_hdr(const hwp_node_t *n, uint8_t b[NODE_HDR])
{
    put16(b, n->id);
    put16(b + 2, n->parent);
    put32(b + 4, n->cls);
    put16(b + 8, n->ncap);
    put16(b + 10, n->state_len);
    put16(b + 12, n->ext_len);
    put16(b + 14, 0);
}

int hwp_encode(const hwp_profile_t *p, uint8_t *out, uint32_t cap, uint32_t *out_len)
{
    if (!p) return HWP_E_ARG;
    uint32_t total = hwp_encoded_len(p);
    if (out_len) *out_len = total;
    if (!out || cap < total) return HWP_E_BUFFER;
    header(p, total, out);
    uint32_t o = HWP_HEADER;
    for (uint16_t i = 0; i < p->n; i++) {
        const hwp_node_t *n = &p->node[i];
        node_hdr(n, out + o);
        o += NODE_HDR;
        for (uint16_t k = 0; k < n->ncap; k++) {
            put16(out + o, n->cap[k].key);
            put64(out + o + 2, n->cap[k].val);
            o += CAP_LEN;
        }
        for (uint16_t k = 0; k < n->state_len; k++) out[o++] = n->state[k];
        for (uint16_t k = 0; k < n->ext_len; k++) out[o++] = n->ext[k];
    }
    return HWP_OK;
}

int hwp_decode(const uint8_t *buf, uint32_t len, hwp_profile_t *p)
{
    if (!buf || !p) return HWP_E_ARG;
    hwp_init(p);
    if (len < HWP_HEADER || buf[0] != HWP_MAGIC0 || buf[1] != HWP_MAGIC1 || buf[2] != HWP_MAGIC2 ||
        buf[3] != HWP_MAGIC3 || get16(buf + 4) != HWP_FORMAT)
        return HWP_E_FORMAT;
    if (get16(buf + 6) != 0 || get16(buf + 10) != 0 || get32(buf + 12) != len)
        return HWP_E_CANONICAL;
    uint16_t nodes = get16(buf + 8);
    if (nodes > HWP_MAX_NODES) return HWP_E_FULL;
    uint32_t o = HWP_HEADER;
    for (uint16_t i = 0; i < nodes; i++) {
        if (len - o < NODE_HDR) return HWP_E_FORMAT;
        uint16_t id = get16(buf + o), parent = get16(buf + o + 2);
        uint32_t cls = get32(buf + o + 4);
        uint16_t ncap = get16(buf + o + 8), slen = get16(buf + o + 10);
        uint16_t elen = get16(buf + o + 12);
        if (get16(buf + o + 14) != 0) return HWP_E_CANONICAL; /* P3 */
        o += NODE_HDR;
        uint16_t idx;
        int rc = hwp_node_add(p, id, parent, cls, &idx);
        if (rc == HWP_E_ARG) return HWP_E_CANONICAL;
        if (rc != HWP_OK) return rc;
        if (ncap > HWP_MAX_CAPS || slen > HWP_MAX_STATE || elen > HWP_MAX_EXT) return HWP_E_FULL;
        if ((len - o) < (uint32_t) ncap * CAP_LEN + slen + elen) return HWP_E_FORMAT;
        hwp_node_t *n = &p->node[idx];
        for (uint16_t k = 0; k < ncap; k++) {
            uint16_t key = get16(buf + o);
            if (HWP_KEY_ID(key) == 0) return HWP_E_CANONICAL;
            if (k && n->cap[k - 1u].key >= key) return HWP_E_CANONICAL; /* P2 */
            for (uint16_t j = 0; j < k; j++)
                if (HWP_KEY_ID(n->cap[j].key) == HWP_KEY_ID(key)) return HWP_E_CANONICAL;
            n->cap[k].key = key;
            n->cap[k].val = get64(buf + o + 2);
            o += CAP_LEN;
        }
        n->ncap = ncap;
        for (uint16_t k = 0; k < slen; k++) n->state[k] = buf[o++];
        n->state_len = slen;
        for (uint16_t k = 0; k < elen; k++) n->ext[k] = buf[o++];
        n->ext_len = elen;
    }
    return o == len ? HWP_OK : HWP_E_CANONICAL; /* P3 */
}

void hwp_digest(const hwp_profile_t *p, uint8_t out[32])
{
    sha256_ctx_t c;
    uint8_t b[NODE_HDR];
    sha256_init(&c);
    uint8_t h[HWP_HEADER];
    header(p, hwp_encoded_len(p), h);
    sha256_update(&c, h, HWP_HEADER);
    for (uint16_t i = 0; i < p->n; i++) {
        const hwp_node_t *n = &p->node[i];
        node_hdr(n, b);
        sha256_update(&c, b, NODE_HDR);
        for (uint16_t k = 0; k < n->ncap; k++) {
            uint8_t cb[CAP_LEN];
            put16(cb, n->cap[k].key);
            put64(cb + 2, n->cap[k].val);
            sha256_update(&c, cb, CAP_LEN);
        }
        sha256_update(&c, n->state, n->state_len);
        sha256_update(&c, n->ext, n->ext_len);
    }
    sha256_final(&c, out);
}

/* ---- matching ---- */

static bool key_known(uint16_t key)
{
    uint16_t id = HWP_KEY_ID(key);
    return id >= 1u && id <= HWP_KNOWN_MAX_ID;
}

bool hwp_node_satisfies(const hwp_node_t *have, const hwp_node_t *need, uint16_t *out_unmet)
{
    if (out_unmet) *out_unmet = 0;
    if (!have || !need || have->cls != need->cls) return false;
    /* A critical cap this reader does not understand makes the host
     * component unusable natively: it says "do not use me blind". */
    for (uint16_t i = 0; i < have->ncap; i++)
        if ((have->cap[i].key & HWP_KEY_CRITICAL) && !key_known(have->cap[i].key)) {
            if (out_unmet) *out_unmet = have->cap[i].key;
            return false;
        }
    for (uint16_t i = 0; i < need->ncap; i++) {
        uint16_t key = need->cap[i].key;
        uint64_t want = need->cap[i].val, got;
        uint16_t cmp = HWP_KEY_CMP(key);
        if (cmp == HWP_CMP_INFO) continue;
        bool ok;
        if ((key & HWP_KEY_CRITICAL) && !key_known(key))
            ok = false;
        else if (!hwp_cap_get(have, key, &got))
            ok = false;
        else if (cmp == HWP_CMP_MIN)
            ok = got >= want;
        else if (cmp == HWP_CMP_EXACT)
            ok = got == want;
        else
            ok = (got & want) == want;
        if (!ok) {
            if (out_unmet) *out_unmet = key;
            return false;
        }
    }
    return true;
}

int hwp_match(const hwp_profile_t *host, const hwp_profile_t *target, hwp_match_t *out)
{
    if (!host || !target || !out) return HWP_E_ARG;
    bool used[HWP_MAX_NODES] = {false};
    out->n = target->n;
    out->native = out->emulated = 0;
    for (uint16_t t = 0; t < target->n; t++) {
        const hwp_node_t *need = &target->node[t];
        hwp_pair_t *pr = &out->pair[t];
        pr->need_id = need->id;
        pr->have_id = HWP_NONE;
        pr->unmet_key = 0;
        if (need->cls == HWP_CLASS_MACHINE || need->cls == HWP_CLASS_PACKAGE) {
            pr->kind = HWP_M_GROUP;
            continue;
        }
        bool seen_class = false;
        uint16_t first_unmet = 0;
        for (uint16_t h = 0; h < host->n; h++) {
            if (used[h] || host->node[h].cls != need->cls) continue;
            uint16_t unmet;
            if (hwp_node_satisfies(&host->node[h], need, &unmet)) {
                used[h] = true;
                pr->have_id = host->node[h].id;
                break;
            }
            if (!seen_class) first_unmet = unmet;
            seen_class = true;
        }
        if (pr->have_id != HWP_NONE) {
            pr->kind = HWP_M_NATIVE;
            out->native++;
        } else {
            pr->kind = HWP_M_EMULATE;
            pr->unmet_key = first_unmet;
            out->emulated++;
        }
    }
    return HWP_OK;
}

/* ---- the reference machine R1 ----
 * Minimums only. Everything here is something a commodity laptop either
 * has (and runs natively) or does not (and emulates). */
static hwp_profile_t r1;
static bool r1_built;

static void r1_node(uint16_t id, uint16_t parent, uint32_t cls)
{
    hwp_node_add(&r1, id, parent, cls, 0);
}
static void r1_cap(uint16_t key, uint64_t val)
{
    hwp_cap_set(&r1, (uint16_t) (r1.n - 1u), key, val);
}

const hwp_profile_t *hwp_reference_r1(void)
{
    if (r1_built) return &r1;
    hwp_init(&r1);
    r1_node(1, HWP_ROOT, HWP_CLASS_MACHINE);
    r1_node(2, 1, HWP_CLASS_PACKAGE);
    r1_node(3, 2, HWP_CLASS_CPU_CLUSTER);
    r1_cap(HWP_CAP_WORD_BITS, 64);
    r1_cap(HWP_CAP_COUNT, 1);
    r1_node(4, 2, HWP_CLASS_MEMORY);
    r1_cap(HWP_CAP_BYTES, 64ull << 20);
    r1_node(5, 2, HWP_CLASS_VECTOR_UNIT);
    r1_cap(HWP_CAP_INT_WIDTHS, HWP_INT_8 | HWP_INT_16 | HWP_INT_32);
    r1_node(6, 2, HWP_CLASS_MATRIX_UNIT);
    r1_cap(HWP_CAP_INT_WIDTHS, HWP_INT_8 | HWP_INT_32);
    r1_cap(HWP_CAP_MACS_PER_CYCLE, 64);
    r1_cap(HWP_CAP_CONTRACT_ID, HWE_FN_TENSOR);
    r1_cap(HWP_CAP_CONTRACT_VER, HWE_CONTRACT_V1);
    r1_node(7, 2, HWP_CLASS_LOGIC_UNIT);
    r1_cap(HWP_CAP_CONTRACT_ID, HWE_FN_LOGIC64);
    r1_cap(HWP_CAP_CONTRACT_VER, HWE_CONTRACT_V1);
    r1_node(8, 2, HWP_CLASS_TERNARY_UNIT);
    r1_cap(HWP_CAP_RADIX, 3);
    r1_node(9, 2, HWP_CLASS_HASH_UNIT);
    r1_cap(HWP_CAP_HASH_ALGS, HWP_HASH_SHA256);
    r1_cap(HWP_CAP_CONTRACT_ID, HWE_FN_SHA256);
    r1_cap(HWP_CAP_CONTRACT_VER, HWE_CONTRACT_V1);
    r1_node(10, 2, HWP_CLASS_HASH_UNIT);
    r1_cap(HWP_CAP_HASH_ALGS, HWP_HASH_SHA3);
    r1_cap(HWP_CAP_CONTRACT_ID, HWE_FN_KECCAK);
    r1_cap(HWP_CAP_CONTRACT_VER, HWE_CONTRACT_V1);
    r1_node(11, 2, HWP_CLASS_GCD_UNIT);
    r1_cap(HWP_CAP_CONTRACT_ID, HWE_FN_GCD);
    r1_cap(HWP_CAP_CONTRACT_VER, HWE_CONTRACT_V1);
    r1_node(12, 2, HWP_CLASS_CRYPTO_UNIT);
    r1_cap(HWP_CAP_PQ_ALGS, HWP_PQ_MLKEM | HWP_PQ_MLDSA | HWP_PQ_SLHDSA);
    r1_cap(HWP_CAP_CONTRACT_ID, HWE_FN_MAC);
    r1_cap(HWP_CAP_CONTRACT_VER, HWE_CONTRACT_V1);
    r1_node(13, 2, HWP_CLASS_RECONF_FABRIC);
    r1_cap(HWP_CAP_REGIONS, 1);
    r1_node(14, 2, HWP_CLASS_CLOCK);
    r1_cap(HWP_CAP_TICK_NS, 0);
    r1_node(15, 2, HWP_CLASS_SECURE_ISLAND);
    r1_node(16, 2, HWP_CLASS_WIRE);
    r1_cap(HWP_CAP_WIRE_BITS, 168);
    r1_built = true;
    return &r1;
}

int hwp_present(const hwp_profile_t *host, const hwp_profile_t *target, hwp_profile_t *out)
{
    if (!host || !target || !out || out == target || out == host) return HWP_E_ARG;
    static hwp_match_t mt; /* large; the kernel is single-threaded here */
    int rc = hwp_match(host, target, &mt);
    if (rc != HWP_OK) return rc;
    *out = *target;
    for (uint16_t i = 0; i < mt.n; i++) {
        uint16_t idx;
        if (hwp_node_find(out, mt.pair[i].need_id, &idx) != HWP_OK) return HWP_E_NOT_FOUND;
        if (mt.pair[i].kind == HWP_M_GROUP) continue;
        rc = hwp_cap_set(out, idx, HWP_CAP_KIND,
                         mt.pair[i].kind == HWP_M_NATIVE ? HWP_KIND_HARD : HWP_KIND_SOFT);
        if (rc != HWP_OK) return rc;
    }
    return HWP_OK;
}

const char *hwp_class_name(uint32_t cls)
{
    static const char *const names[] = {"invalid",
                                        "device",
                                        "cluster",
                                        "core",
                                        "mem.cache",
                                        "mem",
                                        "interconnect",
                                        "engine.tensor.int",
                                        "engine.vector",
                                        "engine.ternary",
                                        "engine.hash",
                                        "engine.crypto",
                                        "engine.codec",
                                        "fabric.reconfig",
                                        "io.storage",
                                        "io.network",
                                        "radio",
                                        "sensor",
                                        "actuator",
                                        "clock",
                                        "power",
                                        "secure.element",
                                        "io.display",
                                        "io.audio",
                                        "io.wire",
                                        "engine.gpu",
                                        "package",
                                        "lane",
                                        "engine.logic64",
                                        "engine.ntt",
                                        "engine.frame.ubh168",
                                        "engine.gcd",
                                        "mem.channel",
                                        "secure.island",
                                        "io"};
    if (cls >= HWP_CLASS_PRIVATE) return "private";
    if (cls > HWP_CLASS_PUBLIC_MAX) return "unknown";
    return names[cls];
}
