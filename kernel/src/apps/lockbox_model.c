/* Copyright (c) 2024-2026 Michael Laurence Curzi and 36N9 Genetics, LLC */
/* SPDX-License-Identifier: Apache-2.0 */
/* lockbox_model.c — Lockbox, keys and passwords (see lockbox_model.h).
 * Integers only; no secret byte ever passes through here. */
#include <string.h>

#include "lockbox_model.h"

/* ---- text ---------------------------------------------------------------- */

/* Append s to out at *at, bounded by cap (room for a NUL is kept). */
static void put(char *out, uint32_t cap, uint32_t *at, const char *s)
{
    while (*s && *at + 1u < cap) out[(*at)++] = *s++;
    out[*at] = 0;
}

static void put_num(char *out, uint32_t cap, uint32_t *at, int64_t v)
{
    char t[24];
    int k = 23;
    uint64_t u = v < 0 ? 0u : (uint64_t) v;
    t[k] = 0;
    do {
        t[--k] = (char) ('0' + (int) (u % 10u));
        u /= 10u;
    } while (u && k > 0);
    put(out, cap, at, t + k);
}

/* 1..max bytes, no control characters. */
static bool name_ok(const char *s, uint32_t max)
{
    if (!s || !s[0]) return false;
    uint32_t n = 0;
    for (const unsigned char *p = (const unsigned char *) s; *p; p++, n++)
        if (*p < 0x20u || *p == 0x7fu || n >= max) return false;
    return true;
}

/* Exactly LOCKBOX_ID_LEN hex digits. */
static bool hex_id(const char *s)
{
    uint32_t n = 0;
    for (; s[n]; n++) {
        char c = s[n];
        if (!((c >= '0' && c <= '9') || (c >= 'a' && c <= 'f') || (c >= 'A' && c <= 'F')))
            return false;
    }
    return n == LOCKBOX_ID_LEN;
}

/* A whole non-negative decimal that fits int64, no leading zero. */
static bool dec_u63(const char *s, int64_t *v)
{
    uint64_t x = 0;
    if (!*s || (s[0] == '0' && s[1])) return false;
    for (const char *p = s; *p; p++) {
        if (*p < '0' || *p > '9') return false;
        uint32_t d = (uint32_t) (*p - '0');
        if (x > ((uint64_t) INT64_MAX - d) / 10u) return false;
        x = x * 10u + d;
    }
    *v = (int64_t) x;
    return true;
}

/* A key id: a 64-digit hex string, or null when zxvd's keys are closed. */
static int read_id(zxm_link_t *k, const char *key, bool open, char *id)
{
    int32_t t = zxm_get(k, k->result, key);
    if (t < 0) return ZXM_E_PROTOCOL;
    if (!open) return k->tok[t].type == W4_J_NULL ? ZXM_OK : ZXM_E_PROTOCOL;
    if (zxm_get_str(k, k->result, key, id, LOCKBOX_ID_LEN + 1u) != ZXM_OK || !hex_id(id))
        return ZXM_E_PROTOCOL;
    return ZXM_OK;
}

/* ---- keys ------------------------------------------------------------------ */

void lockbox_init(lockbox_t *l, zxm_link_t *link)
{
    memset(l, 0, sizeof *l);
    l->link = link;
}

int lockbox_load(lockbox_t *l)
{
    char id[LOCKBOX_KEYS][LOCKBOX_ID_LEN + 1];
    static const char *const NAME[LOCKBOX_KEYS] = {"sign_id", "kem_id"};
    bool open = false, seal = false;
    memset(id, 0, sizeof id);
    zxm_begin(l->link, "fab.status");
    int rc = zxm_call(l->link);
    if (rc != ZXM_OK) return rc;
    if (zxm_get_bool(l->link, l->link->result, "open", &open) != ZXM_OK ||
        zxm_get_bool(l->link, l->link->result, "seal", &seal) != ZXM_OK)
        return ZXM_E_PROTOCOL;
    for (uint32_t i = 0; i < LOCKBOX_KEYS; i++)
        if (read_id(l->link, NAME[i], open, id[i]) != ZXM_OK) return ZXM_E_PROTOCOL;
    /* LB6: the whole answer is read; now replace (LB2: a new id loses its expiry) */
    for (uint32_t i = 0; i < LOCKBOX_KEYS; i++) {
        lockbox_key_t *k = &l->key[i];
        if (!open || !k->present || strcmp(k->id, id[i])) k->expires = 0;
        k->present = open;
        memcpy(k->id, id[i], sizeof k->id);
    }
    l->fab_open = open;
    l->sealed = seal;
    l->loaded = true;
    return ZXM_OK;
}

int lockbox_set_expiry(lockbox_t *l, uint32_t kind, int64_t expires)
{
    if (kind >= LOCKBOX_KEYS || expires < 0 || expires > LOCKBOX_STAMP_MAX) return ZXM_E_ARG;
    if (!l->loaded || !l->key[kind].present) return ZXM_E_STATE;
    l->key[kind].expires = expires;
    return ZXM_OK;
}

int lockbox_key_days(const lockbox_t *l, uint32_t kind, int64_t now, int64_t *days)
{
    if (kind >= LOCKBOX_KEYS || !days || now < 0 || now > LOCKBOX_STAMP_MAX) return ZXM_E_ARG;
    const lockbox_key_t *k = &l->key[kind];
    if (!k->present || !k->expires) return ZXM_E_STATE;
    *days = lockbox_days_left(k->expires, now);
    return ZXM_OK;
}

int lockbox_due(lockbox_t *l, int64_t now, int64_t within, uint32_t *n)
{
    static const char *const LABEL[LOCKBOX_KEYS] = {"the signing key (ML-DSA-87)",
                                                    "the key exchange key (ML-KEM-1024)"};
    if (!n || now < 0 || now > LOCKBOX_STAMP_MAX || within < 0 ||
        within > LOCKBOX_STAMP_MAX / LOCKBOX_DAY)
        return ZXM_E_ARG;
    if (!l->loaded) return ZXM_E_STATE;
    uint32_t at = 0, due = 0;
    l->notice[0] = 0;
    for (uint32_t i = 0; i < LOCKBOX_KEYS; i++) {
        int64_t d = 0;
        if (lockbox_key_days(l, i, now, &d) != ZXM_OK || d > within) continue;
        put(l->notice, sizeof l->notice, &at, due ? "; " : "Renew ");
        put(l->notice, sizeof l->notice, &at, LABEL[i]);
        put(l->notice, sizeof l->notice, &at, ": ");
        put_num(l->notice, sizeof l->notice, &at, d);
        put(l->notice, sizeof l->notice, &at, d == 1 ? " day left" : " days left");
        due++;
    }
    if (due) put(l->notice, sizeof l->notice, &at, ". The owner decides when.");
    *n = due;
    return ZXM_OK;
}

/* ---- saved passwords ------------------------------------------------------ */

const lockbox_item_t *lockbox_item(const lockbox_t *l, const char *name)
{
    if (!name) return 0;
    for (uint32_t i = 0; i < l->n; i++)
        if (!strcmp(l->item[i].name, name)) return &l->item[i];
    return 0;
}

int lockbox_propose(lockbox_t *l, const char *name, int64_t length, int64_t classes,
                    int64_t repeats)
{
    if (!name_ok(name, LOCKBOX_NAME_MAX) || !lockbox_secret_ok(length, classes, repeats) ||
        repeats >= length)
        return ZXM_E_ARG;
    if (l->pending) return ZXM_E_STATE;
    if (!lockbox_item(l, name) && l->n >= LOCKBOX_ITEMS) return ZXM_E_TOOBIG;
    int64_t s = lockbox_strength(length, classes, repeats);
    if (s == LOCKBOX_REFUSED) return ZXM_E_ARG;
    lockbox_item_t *h = &l->held;
    memset(h, 0, sizeof *h);
    memcpy(h->name, name, strlen(name) + 1u);
    h->length = length;
    h->classes = classes;
    h->repeats = repeats;
    h->strength = s;
    l->pending = true;
    uint32_t at = 0;
    l->notice[0] = 0;
    put(l->notice, sizeof l->notice, &at, "Strength ");
    put_num(l->notice, sizeof l->notice, &at, s);
    put(l->notice, sizeof l->notice, &at,
        s < LOCKBOX_STRONG ? " of 100: weak. Keep it anyway, or choose a longer one with more "
                             "kinds of characters."
                           : " of 100. Keep it?");
    return ZXM_OK;
}

int lockbox_confirm(lockbox_t *l)
{
    if (!l->pending) return ZXM_E_STATE;
    lockbox_item_t *it = (lockbox_item_t *) lockbox_item(l, l->held.name);
    if (!it) {
        if (l->n >= LOCKBOX_ITEMS) return ZXM_E_TOOBIG;
        it = &l->item[l->n++];
    }
    *it = l->held;
    l->pending = false;
    l->notice[0] = 0;
    return ZXM_OK;
}

int lockbox_cancel(lockbox_t *l)
{
    if (!l->pending) return ZXM_E_STATE;
    l->pending = false;
    memset(&l->held, 0, sizeof l->held);
    l->notice[0] = 0;
    return ZXM_OK;
}

int lockbox_remove(lockbox_t *l, const char *name)
{
    if (!name_ok(name, LOCKBOX_NAME_MAX)) return ZXM_E_ARG;
    for (uint32_t i = 0; i < l->n; i++) {
        if (strcmp(l->item[i].name, name)) continue;
        for (uint32_t j = i; j + 1u < l->n; j++) l->item[j] = l->item[j + 1u];
        l->n--;
        memset(&l->item[l->n], 0, sizeof l->item[l->n]);
        return ZXM_OK;
    }
    return ZXM_E_STATE;
}

uint32_t lockbox_weak(const lockbox_t *l)
{
    uint32_t w = 0;
    for (uint32_t i = 0; i < l->n; i++)
        if (l->item[i].strength < LOCKBOX_STRONG) w++;
    return w;
}

/* ---- the books' seal -------------------------------------------------------- */

int lockbox_books(lockbox_t *l)
{
    char head[LOCKBOX_ID_LEN + 1], rec[24];
    bool saved = false;
    int64_t records = 0;
    zxm_begin(l->link, "books.file");
    int rc = zxm_call(l->link);
    if (rc != ZXM_OK) return rc;
    int32_t r = l->link->result;
    if (zxm_get_bool(l->link, r, "saved", &saved) != ZXM_OK ||
        zxm_get_str(l->link, r, "head", head, sizeof head) != ZXM_OK || !hex_id(head) ||
        zxm_get_str(l->link, r, "records", rec, sizeof rec) != ZXM_OK || !dec_u63(rec, &records))
        return ZXM_E_PROTOCOL;
    l->books_saved = saved;
    memcpy(l->books_head, head, sizeof head);
    l->books_records = records;
    return ZXM_OK;
}
