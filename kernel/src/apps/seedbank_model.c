/* Copyright (c) 2024-2026 Michael Laurence Curzi and 36N9 Genetics, LLC */
/* SPDX-License-Identifier: Apache-2.0 */
/* seedbank_model.c — Seedbank, IPFS (see seedbank_model.h). Every limit is
 * checked before anything is sent, and a pin is kept only after zxvd has
 * answered and its record fits. */
#include <string.h>

#include "seedbank_model.h"

static const char B58[] = "123456789ABCDEFGHJKLMNPQRSTUVWXYZabcdefghijkmnopqrstuvwxyz";
static const char B32[] = "abcdefghijklmnopqrstuvwxyz234567";
static const char HEX[] = "0123456789abcdef";

void seedbank_init(seedbank_t *s, zxm_link_t *link)
{
    memset(s, 0, sizeof *s);
    s->link = link;
    s->store_x100 = SEEDBANK_REFUSED;
    s->got_pin = -1;
}

/* ---- SM1: aipi's "z" name to the "b" CID text ---------------------------------- */

static int32_t b58_digit(char c)
{
    for (int32_t i = 0; B58[i]; i++)
        if (B58[i] == c) return i;
    return -1;
}

int seedbank_block_to_cid(const char *z, char out[SEEDBANK_CID_LEN + 1])
{
    uint8_t bin[64];
    uint32_t n, zeros = 0;
    out[0] = 0;
    if (!z || z[0] != 'z') return ZXM_E_ARG;
    n = (uint32_t) strlen(z + 1);
    if (!n || n > SEEDBANK_BLOCK_MAX) return ZXM_E_ARG;
    memset(bin, 0, sizeof bin);
    while (zeros < n && z[1 + zeros] == '1') zeros++;
    for (uint32_t i = 0; i < n; i++) { /* bin = bin * 58 + digit, big-endian */
        int32_t d = b58_digit(z[1 + i]);
        if (d < 0) return ZXM_E_ARG;
        uint32_t carry = (uint32_t) d;
        for (uint32_t j = sizeof bin; j-- > 0;) {
            carry += 58u * bin[j];
            bin[j] = (uint8_t) (carry & 0xffu);
            carry >>= 8;
        }
        if (carry) return ZXM_E_ARG; /* longer than any CID */
    }
    uint32_t first = 0;
    while (first < sizeof bin && !bin[first]) first++;
    uint32_t len = zeros + (uint32_t) sizeof bin - first;
    if (len != SEEDBANK_BIN_LEN || zeros) return ZXM_E_ARG;
    const uint8_t *c = bin + first;
    if (c[0] != 0x01u || c[1] != 0x55u || c[2] != 0x12u || c[3] != 0x20u) return ZXM_E_ARG;
    /* "b" + base32 lower, no padding: 36 bytes are 58 letters */
    uint32_t o = 0, acc = 0, bits = 0;
    out[o++] = 'b';
    for (uint32_t i = 0; i < SEEDBANK_BIN_LEN; i++) {
        acc = (acc << 8 | c[i]) & 0xfffu;
        bits += 8;
        while (bits >= 5) {
            bits -= 5;
            out[o++] = B32[(acc >> bits) & 31u];
        }
    }
    if (bits) out[o++] = B32[(acc << (5 - bits)) & 31u];
    out[o] = 0;
    if (!seedbank_cid_ok(out, o)) { /* SR2: never kept otherwise */
        out[0] = 0;
        return ZXM_E_ARG;
    }
    return ZXM_OK;
}

/* ---- aipi.status ------------------------------------------------------------------ */

int seedbank_status(seedbank_t *s)
{
    zxm_link_t *l = s->link;
    uint64_t blocks = 0, bytes = 0, cap = 0;
    bool online = false;
    zxm_begin(l, "aipi.status");
    int rc = zxm_call(l);
    if (rc) return rc;
    if (zxm_get_bool(l, l->result, "online", &online)) return ZXM_E_PROTOCOL;
    /* an offline store with no counts says only that */
    (void) zxm_get_u64(l, l->result, "blocks", &blocks);
    (void) zxm_get_u64(l, l->result, "store_bytes", &bytes);
    (void) zxm_get_u64(l, l->result, "store_cap", &cap);
    if (bytes > cap || cap > (uint64_t) INT64_MAX) return ZXM_E_PROTOCOL;
    s->online = online;
    s->blocks = blocks;
    s->store_bytes = bytes;
    s->store_cap = cap;
    s->store_x100 = seedbank_used_x100((int64_t) cap, (int64_t) (cap - bytes));
    return ZXM_OK;
}

/* ---- SM2: pinning ------------------------------------------------------------------ */

int32_t seedbank_find(const seedbank_t *s, const char *text)
{
    if (!text || !text[0]) return -1;
    for (uint32_t i = 0; i < s->npins; i++)
        if (!strcmp(s->pin[i].cid, text) || !strcmp(s->pin[i].block, text)) return (int32_t) i;
    return -1;
}

/* What a put answered: its block name and size, turned into a CID text. */
static int put_answer(zxm_link_t *l, int64_t want_bytes, bool sealed, char *block,
                      char cid[SEEDBANK_CID_LEN + 1])
{
    uint64_t bytes = 0;
    bool priv = false;
    if (zxm_get_str(l, l->result, "cid", block, SEEDBANK_BLOCK_MAX + 1) ||
        zxm_get_u64(l, l->result, "bytes", &bytes) || bytes != (uint64_t) want_bytes)
        return ZXM_E_PROTOCOL;
    if (!zxm_get_bool(l, l->result, "private", &priv) && priv != sealed) return ZXM_E_PROTOCOL;
    return seedbank_block_to_cid(block, cid) ? ZXM_E_PROTOCOL : ZXM_OK;
}

/* Keep a pin (or find the one already kept): the record must fit SR5. */
static int keep(seedbank_t *s, const char *block, const char *cid, int64_t bytes, bool sealed,
                int64_t now, uint32_t *at)
{
    int32_t had = seedbank_find(s, cid);
    if (had >= 0) {
        *at = (uint32_t) had;
        return ZXM_OK;
    }
    if (s->npins >= SEEDBANK_PINS) return ZXM_E_TOOBIG;
    if (!seedbank_pin_record_ok(s->npins, bytes, sealed, now)) return ZXM_E_ARG;
    seedbank_pin_t *p = &s->pin[s->npins];
    memset(p, 0, sizeof *p);
    memcpy(p->block, block, strlen(block) + 1u);
    memcpy(p->cid, cid, SEEDBANK_CID_LEN + 1u);
    p->bytes = bytes;
    p->stamp = now;
    p->sealed = sealed;
    *at = s->npins++;
    return ZXM_OK;
}

static bool stamp_ok(int64_t now)
{
    return now >= 0 && now <= SEEDBANK_STAMP_FIELD;
}

static void put_hex(zxm_link_t *l, const char *key, const uint8_t *data, uint32_t len)
{
    static char hex[2u * SEEDBANK_HEX_PUT + 1u];
    for (uint32_t i = 0; i < len; i++) {
        hex[2u * i] = HEX[data[i] >> 4];
        hex[2u * i + 1u] = HEX[data[i] & 15u];
    }
    zxm_p_strn(l, key, hex, 2u * len);
}

int seedbank_pin_bytes(seedbank_t *s, const uint8_t *data, uint32_t len, bool sealed, int64_t now,
                       uint32_t *at)
{
    char named[SEEDBANK_CID_LEN + 2], cid[SEEDBANK_CID_LEN + 1], block[SEEDBANK_BLOCK_MAX + 1];
    zxm_link_t *l = s->link;
    if ((!data && len) || !at || !stamp_ok(now)) return ZXM_E_ARG;
    if (!seedbank_put_ok(len) || len > SEEDBANK_HEX_PUT) return ZXM_E_TOOBIG;
    if (s->npins >= SEEDBANK_PINS) return ZXM_E_TOOBIG;
    /* web4.cid names the bytes on this device first */
    zxm_begin(l, "web4.cid");
    put_hex(l, "data_hex", data, len);
    int rc = zxm_call(l);
    if (rc) return rc;
    if (zxm_get_str(l, l->result, "cid", named, sizeof named) ||
        !seedbank_cid_ok(named, (uint32_t) strlen(named)))
        return ZXM_E_PROTOCOL;
    zxm_begin(l, "aipi.put");
    put_hex(l, "hex", data, len);
    zxm_p_bool(l, "private", sealed);
    rc = zxm_call(l);
    if (rc) return rc;
    rc = put_answer(l, len, sealed, block, cid);
    if (rc) return rc;
    if (strcmp(cid, named)) return ZXM_E_PROTOCOL; /* the store named other bytes */
    return keep(s, block, cid, len, sealed, now, at);
}

int seedbank_pin_file(seedbank_t *s, const char *path, bool sealed, int64_t now, uint32_t *at)
{
    static char full[SEEDBANK_PATH_MAX + 2];
    char root[SEEDBANK_PATH_MAX + 2], cid[SEEDBANK_CID_LEN + 1], block[SEEDBANK_BLOCK_MAX + 1];
    zxm_link_t *l = s->link;
    uint64_t size = 0;
    bool dir = true;
    if (!path || !path[0] || path[0] == '/' || !at || !stamp_ok(now)) return ZXM_E_ARG;
    if (s->npins >= SEEDBANK_PINS) return ZXM_E_TOOBIG;
    zxm_begin(l, "files.stat");
    zxm_p_str(l, "path", path);
    int rc = zxm_call(l);
    if (rc) return rc;
    if (zxm_get_bool(l, l->result, "dir", &dir) || zxm_get_u64(l, l->result, "size", &size))
        return ZXM_E_PROTOCOL;
    if (dir) return ZXM_E_ARG;
    if (size > (uint64_t) INT64_MAX || !seedbank_put_ok((int64_t) size))
        return ZXM_E_TOOBIG; /* SR3: nothing put */
    zxm_begin(l, "files.root");
    rc = zxm_call(l);
    if (rc) return rc;
    if (zxm_get_str(l, l->result, "root", root, sizeof root) || !root[0]) return ZXM_E_PROTOCOL;
    size_t rn = strlen(root), pn = strlen(path);
    if (rn + 1u + pn > SEEDBANK_PATH_MAX) return ZXM_E_TOOBIG;
    memcpy(full, root, rn);
    full[rn] = '/';
    memcpy(full + rn + 1u, path, pn + 1u);
    zxm_begin(l, "aipi.put");
    zxm_p_str(l, "path", full);
    zxm_p_bool(l, "private", sealed);
    rc = zxm_call(l);
    if (rc) return rc;
    rc = put_answer(l, (int64_t) size, sealed, block, cid);
    return rc ? rc : keep(s, block, cid, (int64_t) size, sealed, now, at);
}

/* ---- SM3: fetching, and the ipfs:// form ------------------------------------------ */

static int32_t hex_val(char c)
{
    if (c >= '0' && c <= '9') return c - '0';
    if (c >= 'a' && c <= 'f') return c - 'a' + 10;
    return -1;
}

int seedbank_fetch(seedbank_t *s, uint32_t at)
{
    static char hex[2u * SEEDBANK_GET_MAX + 2u];
    zxm_link_t *l = s->link;
    uint64_t bytes = 0;
    if (at >= s->npins) return ZXM_E_ARG;
    const seedbank_pin_t *p = &s->pin[at];
    if (!seedbank_get_ok(p->bytes)) return ZXM_E_TOOBIG; /* SR4: nothing asked */
    zxm_begin(l, "aipi.get");
    zxm_p_str(l, "cid", p->block);
    int rc = zxm_call(l);
    if (rc) return rc;
    if (zxm_get_u64(l, l->result, "bytes", &bytes) || bytes != (uint64_t) p->bytes ||
        zxm_get_str(l, l->result, "hex", hex, sizeof hex) || strlen(hex) != 2u * bytes)
        return ZXM_E_PROTOCOL;
    for (uint32_t i = 0; i < (uint32_t) bytes; i++) {
        int32_t hi = hex_val(hex[2u * i]), lo = hex_val(hex[2u * i + 1u]);
        if (hi < 0 || lo < 0) return ZXM_E_PROTOCOL;
        s->got[i] = (uint8_t) (hi << 4 | lo);
    }
    s->got_len = (uint32_t) bytes;
    s->got_pin = (int32_t) at;
    return ZXM_OK;
}

int seedbank_uri(seedbank_t *s, uint32_t at)
{
    char cid[SEEDBANK_CID_LEN + 2], uri[SEEDBANK_URI_MAX + 1];
    zxm_link_t *l = s->link;
    if (at >= s->npins) return ZXM_E_ARG;
    seedbank_pin_t *p = &s->pin[at];
    zxm_begin(l, "web4.cid");
    zxm_p_str(l, "cid", p->cid);
    int rc = zxm_call(l);
    if (rc) return rc;
    if (zxm_get_str(l, l->result, "cid", cid, sizeof cid) || strcmp(cid, p->cid) ||
        zxm_get_str(l, l->result, "uri", uri, sizeof uri) || strncmp(uri, "ipfs://", 7) ||
        strcmp(uri + 7, p->cid))
        return ZXM_E_PROTOCOL;
    memcpy(p->uri, uri, strlen(uri) + 1u);
    return ZXM_OK;
}

/* ---- SM4: publishing, SM5: forgetting --------------------------------------------- */

static bool space_ok(const char *space)
{
    return space && space[0] && strlen(space) <= ZXM_SPACE_MAX && strcmp(space, "home");
}

static int set_cid(seedbank_t *s, const char *space, const char *cid)
{
    char back[SEEDBANK_CID_LEN + 2];
    zxm_link_t *l = s->link;
    zxm_begin(l, "spaces.set_cid");
    zxm_p_str(l, "space", space);
    zxm_p_str(l, "cid", cid);
    int rc = zxm_call(l);
    if (rc) return rc;
    if (zxm_get_str(l, l->result, "cid", back, sizeof back) || strcmp(back, cid))
        return ZXM_E_PROTOCOL;
    for (uint32_t i = 0; i < s->npins; i++) /* a space has one CID */
        if (!strcmp(s->pin[i].space, space)) s->pin[i].space[0] = 0;
    return ZXM_OK;
}

int seedbank_publish(seedbank_t *s, uint32_t at, const char *space, bool owner_confirmed)
{
    if (at >= s->npins || !space_ok(space)) return ZXM_E_ARG;
    seedbank_pin_t *p = &s->pin[at];
    if (!seedbank_cid_ok(p->cid, (uint32_t) strlen(p->cid))) return ZXM_E_ARG;
    if (!owner_confirmed) return ZXM_E_STATE; /* SM4: nothing sent */
    int rc = set_cid(s, space, p->cid);
    if (rc) return rc;
    memcpy(p->space, space, strlen(space) + 1u);
    return ZXM_OK;
}

int seedbank_unpublish(seedbank_t *s, const char *space, bool owner_confirmed)
{
    if (!space_ok(space)) return ZXM_E_ARG;
    if (!owner_confirmed) return ZXM_E_STATE;
    return set_cid(s, space, "");
}

int seedbank_forget(seedbank_t *s, uint32_t at)
{
    if (at >= s->npins) return ZXM_E_ARG;
    if (s->pin[at].space[0]) return ZXM_E_STATE; /* unpublish it first */
    memmove(&s->pin[at], &s->pin[at + 1u], (s->npins - at - 1u) * sizeof s->pin[0]);
    s->npins--;
    memset(&s->pin[s->npins], 0, sizeof s->pin[0]);
    if (s->got_pin == (int32_t) at) {
        s->got_pin = -1;
        s->got_len = 0;
    } else if (s->got_pin > (int32_t) at) {
        s->got_pin--;
    }
    return ZXM_OK;
}
