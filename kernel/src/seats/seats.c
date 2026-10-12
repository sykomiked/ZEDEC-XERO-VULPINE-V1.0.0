/* Copyright (c) 2024-2026 Michael Laurence Curzi and 36N9 Genetics, LLC */
/* SPDX-License-Identifier: Apache-2.0 */
/* seats.c — people on one device and the login after boot. See seats.h. */
#include "seats.h"

static void wipe(void *p, uint32_t n)
{
    volatile uint8_t *b = (volatile uint8_t *) p;
    while (n--) *b++ = 0;
}

static uint32_t slen(const char *s, uint32_t max)
{
    uint32_t n = 0;
    while (n < max && s[n]) n++;
    return n;
}

static bool same_name(const char *a, const char *b)
{
    for (uint32_t i = 0; i < SEAT_NAME_MAX; i++) {
        if (a[i] != b[i]) return false;
        if (!a[i]) return true;
    }
    return true;
}

static bool eq_ct(const uint8_t *a, const uint8_t *b, uint32_t n)
{
    uint8_t d = 0;
    for (uint32_t i = 0; i < n; i++) d |= (uint8_t) (a[i] ^ b[i]);
    return d == 0;
}

static bool valid(const seat_roster_t *r, int32_t i)
{
    return r && i >= 0 && (uint32_t) i < r->n;
}

static uint32_t owners(const seat_roster_t *r)
{
    uint32_t k = 0;
    for (uint32_t i = 0; i < r->n; i++) k += r->v[i].role == SEAT_OWNER;
    return k;
}

void seat_roster_init(seat_roster_t *r, seat_t *storage, uint32_t cap)
{
    if (!r) return;
    r->v = storage;
    r->n = 0;
    r->cap = storage ? cap : 0;
    r->auto_enter = true;
    id_kdf_default(&r->kdf);
}

seat_status_t seat_roster_grow(seat_roster_t *r, seat_t *bigger, uint32_t cap)
{
    if (!r || !bigger || cap <= r->n || bigger == r->v) return SEAT_ERR_ARG;
    for (uint32_t i = 0; i < r->n; i++) bigger[i] = r->v[i];
    if (r->v) wipe(r->v, r->n * (uint32_t) sizeof(seat_t));
    r->v = bigger;
    r->cap = cap;
    return SEAT_OK;
}

static seat_status_t hash_pass(const seat_roster_t *r, const seat_env_t *env, seat_t *s,
                               const uint8_t *pass, uint32_t len, bool new_salt)
{
    if (!env || !env->mem || (new_salt && !env->rng)) return SEAT_ERR_ARG;
    if (len < SEAT_PASS_MIN || len > SEAT_PASS_MAX || !pass) return SEAT_ERR_ARG;
    id_kdf_params_t kp = r->kdf;
    if (!new_salt) {
        kp.m_kib = s->kdf_m_kib;
        kp.t = s->kdf_t;
        kp.p = s->kdf_p;
    }
    if (new_salt) env->rng(s->salt, SEAT_SALT_LEN, env->rng_ctx);
    uint8_t out[SEAT_HASH_LEN];
    if (id_argon2id(&kp, pass, len, s->salt, SEAT_SALT_LEN, 0, 0, s->id, 16, env->mem,
                    env->mem_blocks, out, SEAT_HASH_LEN) != ID_OK) {
        wipe(out, sizeof out);
        return SEAT_ERR_KDF;
    }
    if (new_salt) {
        for (uint32_t i = 0; i < SEAT_HASH_LEN; i++) s->hash[i] = out[i];
        s->kdf_m_kib = kp.m_kib;
        s->kdf_t = kp.t;
        s->kdf_p = kp.p;
        wipe(out, sizeof out);
        return SEAT_OK;
    }
    bool ok = eq_ct(out, s->hash, SEAT_HASH_LEN);
    wipe(out, sizeof out);
    return ok ? SEAT_OK : SEAT_ERR_WRONG;
}

seat_status_t seat_add(seat_roster_t *r, const seat_env_t *env, int32_t actor, const char *name,
                       seat_role_t role, seat_lock_t lock, const uint8_t *pass, uint32_t pass_len,
                       int32_t *out_index)
{
    if (!r || !name || role > SEAT_GUEST || lock > SEAT_LOCK_PASSKEY) return SEAT_ERR_ARG;
    if (!env || !env->rng) return SEAT_ERR_ARG;
    if (r->n == 0)
        role = SEAT_OWNER; /* the first seat owns the device */
    else if (!valid(r, actor) || r->v[actor].role != SEAT_OWNER)
        return SEAT_ERR_DENIED;
    if (role == SEAT_GUEST) lock = SEAT_LOCK_NONE;
    uint32_t nl = slen(name, SEAT_NAME_MAX);
    if (nl == 0 || nl >= SEAT_NAME_MAX) return SEAT_ERR_NAME;
    for (uint32_t i = 0; i < r->n; i++)
        if (same_name(r->v[i].name, name)) return SEAT_ERR_NAME;
    if (r->n >= r->cap) return SEAT_ERR_FULL;

    seat_t s;
    wipe(&s, sizeof s);
    for (uint32_t i = 0; i < nl; i++) s.name[i] = name[i];
    s.role = (uint8_t) role;
    s.lock = (uint8_t) lock;
    env->rng(s.id, 16, env->rng_ctx);
    if (lock == SEAT_LOCK_PASS) {
        seat_status_t st = hash_pass(r, env, &s, pass, pass_len, true);
        if (st != SEAT_OK) {
            wipe(&s, sizeof s);
            return st;
        }
    }
    r->v[r->n] = s;
    wipe(&s, sizeof s);
    if (out_index) *out_index = (int32_t) r->n;
    r->n++;
    return SEAT_OK;
}

seat_status_t seat_remove(seat_roster_t *r, int32_t actor, int32_t index)
{
    if (!valid(r, index) || !valid(r, actor)) return SEAT_ERR_NOTFOUND;
    if (r->v[actor].role != SEAT_OWNER) return SEAT_ERR_DENIED;
    if (r->v[index].role == SEAT_OWNER && owners(r) <= 1) return SEAT_ERR_LAST_OWNER;
    for (uint32_t i = (uint32_t) index; i + 1 < r->n; i++) r->v[i] = r->v[i + 1];
    r->n--;
    wipe(&r->v[r->n], sizeof(seat_t));
    return SEAT_OK;
}

seat_status_t seat_set_lock(seat_roster_t *r, const seat_env_t *env, int32_t actor, int32_t index,
                            seat_lock_t lock, const uint8_t *old_pass, uint32_t old_len,
                            const uint8_t *pass, uint32_t pass_len)
{
    if (!valid(r, index) || !valid(r, actor)) return SEAT_ERR_NOTFOUND;
    if (lock > SEAT_LOCK_PASSKEY) return SEAT_ERR_ARG;
    seat_t *s = &r->v[index];
    if (actor != index && r->v[actor].role != SEAT_OWNER) return SEAT_ERR_DENIED;
    if (s->role == SEAT_GUEST && lock != SEAT_LOCK_NONE) return SEAT_ERR_DENIED;
    if (actor == index && s->lock == SEAT_LOCK_PASS) { /* your own: prove the old one */
        if (!env || !env->mem) return SEAT_ERR_ARG;
        seat_status_t st = hash_pass(r, env, s, old_pass, old_len, false);
        if (st == SEAT_ERR_ARG) st = SEAT_ERR_WRONG; /* missing or malformed old one */
        if (st != SEAT_OK) return st;
    }
    if (lock == SEAT_LOCK_PASS) {
        seat_t t = *s;
        seat_status_t st = hash_pass(r, env, &t, pass, pass_len, true);
        if (st != SEAT_OK) {
            wipe(&t, sizeof t);
            return st;
        }
        t.lock = SEAT_LOCK_PASS;
        t.fails = 0;
        t.wait_until_ms = 0;
        *s = t;
        wipe(&t, sizeof t);
        return SEAT_OK;
    }
    wipe(s->salt, SEAT_SALT_LEN);
    wipe(s->hash, SEAT_HASH_LEN);
    s->kdf_m_kib = 0;
    s->kdf_t = 0;
    s->kdf_p = 0;
    s->lock = (uint8_t) lock;
    s->fails = 0;
    s->wait_until_ms = 0;
    return SEAT_OK;
}

uint64_t seat_wait_ms(uint32_t fails)
{
    if (fails < SEAT_FREE_TRIES) return 0;
    uint32_t k = fails - SEAT_FREE_TRIES;
    uint64_t w = SEAT_WAIT_FIRST_MS;
    while (k-- && w < SEAT_WAIT_MAX_MS) w *= 2u;
    return w > SEAT_WAIT_MAX_MS ? SEAT_WAIT_MAX_MS : w;
}

seat_status_t seat_unlock(seat_roster_t *r, const seat_env_t *env, int32_t index,
                          const uint8_t *pass, uint32_t pass_len, bool passkey_ok, uint64_t now_ms,
                          uint64_t *wait_ms)
{
    if (wait_ms) *wait_ms = 0;
    if (!valid(r, index)) return SEAT_ERR_NOTFOUND;
    seat_t *s = &r->v[index];
    if (s->lock == SEAT_LOCK_NONE) return SEAT_OK;
    if (now_ms < s->wait_until_ms) {
        if (wait_ms) *wait_ms = s->wait_until_ms - now_ms;
        return SEAT_ERR_WAIT;
    }
    if (s->lock == SEAT_LOCK_PASS && (!env || !env->mem)) return SEAT_ERR_ARG;
    seat_status_t st;
    if (s->lock == SEAT_LOCK_PASSKEY)
        st = passkey_ok ? SEAT_OK : SEAT_ERR_WRONG;
    else
        st = hash_pass(r, env, s, pass, pass_len, false);
    if (st == SEAT_ERR_ARG) st = SEAT_ERR_WRONG; /* empty or oversized entry */
    if (st == SEAT_OK) {
        s->fails = 0;
        s->wait_until_ms = 0;
        return SEAT_OK;
    }
    if (st != SEAT_ERR_WRONG) return st;
    s->fails++;
    uint64_t w = seat_wait_ms(s->fails);
    s->wait_until_ms = w ? now_ms + w : 0;
    if (wait_ms) *wait_ms = w;
    return SEAT_ERR_WRONG;
}

int32_t seat_auto_enter(const seat_roster_t *r)
{
    if (!r || !r->auto_enter || r->n != 1) return -1;
    return r->v[0].lock == SEAT_LOCK_NONE ? 0 : -1;
}
