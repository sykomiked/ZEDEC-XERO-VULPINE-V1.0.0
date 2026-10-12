/* Copyright (c) 2024-2026 Michael Laurence Curzi and 36N9 Genetics, LLC */
/* SPDX-License-Identifier: Apache-2.0 */
/* seats.h — the people who use one device, and the login after boot.
 *
 * After the boot vault opens, ZXV shows a login screen (owner, 2026-10-12):
 * as many people as the owner wants on one device, each with their own space,
 * and protection is the person's choice. A seat is one person on this device:
 *
 *   SEAT_LOCK_NONE     no secret; choosing the seat enters it
 *   SEAT_LOCK_PASS     a passphrase, stored only as an Argon2id hash (RFC 9106,
 *                      kernel/src/ident) with a per-seat random salt
 *   SEAT_LOCK_PASSKEY  the device's own unlock (Touch ID, Windows Hello, a
 *                      security key) through an ident passkey; the host checks
 *                      it and reports the result, this module never sees a
 *                      biometric
 *
 * Roles: OWNER (may add and remove seats; there is always at least one),
 * PERSON, GUEST (no lock, its space is wiped when it signs out).
 *
 * The roster has no fixed size: the caller owns the array and may grow it
 * (seat_roster_grow hands over a bigger one). Wrong passphrases back off:
 * after SEAT_FREE_TRIES failures each further try waits twice as long, from
 * 30 s to at most an hour, and a correct entry clears it.
 *
 * If the device has exactly one seat and it has no lock, the login screen is
 * skipped (seat_auto_enter) unless the owner turned that off.
 *
 * Integer only, freestanding, no allocation.
 */
#ifndef ZXV_SEATS_H
#define ZXV_SEATS_H

#include <stdint.h>
#include <stdbool.h>
#include "ident.h"

#define SEAT_NAME_MAX      48u
#define SEAT_SALT_LEN      16u
#define SEAT_HASH_LEN      32u
#define SEAT_PASS_MIN      4u /* a short PIN is allowed; it is the person's choice */
#define SEAT_PASS_MAX      256u
#define SEAT_FREE_TRIES    5u
#define SEAT_WAIT_FIRST_MS 30000u
#define SEAT_WAIT_MAX_MS   3600000u

typedef enum { SEAT_LOCK_NONE = 0, SEAT_LOCK_PASS, SEAT_LOCK_PASSKEY } seat_lock_t;
typedef enum { SEAT_OWNER = 0, SEAT_PERSON, SEAT_GUEST } seat_role_t;

typedef enum {
    SEAT_OK = 0,
    SEAT_ERR_ARG,  /* bad argument                                  */
    SEAT_ERR_FULL, /* roster array is full: grow it                 */
    SEAT_ERR_NOTFOUND,
    SEAT_ERR_DENIED,     /* the acting seat may not do this               */
    SEAT_ERR_LAST_OWNER, /* would leave the device with no owner          */
    SEAT_ERR_WRONG,      /* wrong passphrase / passkey refused            */
    SEAT_ERR_WAIT,       /* backing off; see *wait_ms                     */
    SEAT_ERR_KDF,        /* Argon2id failed (memory too small)            */
    SEAT_ERR_NAME        /* empty, too long, or already used              */
} seat_status_t;

typedef struct {
    uint8_t id[16];
    char name[SEAT_NAME_MAX];
    uint8_t role; /* seat_role_t */
    uint8_t lock; /* seat_lock_t */
    uint8_t salt[SEAT_SALT_LEN];
    uint8_t hash[SEAT_HASH_LEN];
    uint32_t kdf_m_kib, kdf_t; /* parameters the hash was made with */
    uint8_t passkey_id[32];    /* ident credential id when lock == PASSKEY */
    uint32_t fails;
    uint64_t wait_until_ms;
    uint32_t color; /* the person's accent (0 = gold) */
} seat_t;

typedef struct {
    seat_t *v;
    uint32_t n, cap;
    bool auto_enter;     /* skip login for a single unlocked seat (default on) */
    id_kdf_params_t kdf; /* 64 MiB, 3 passes, 1 lane by default */
} seat_roster_t;

/* Randomness and Argon2id memory come from the caller. */
typedef void (*seat_rng_fn)(uint8_t *out, uint32_t len, void *ctx);
typedef struct {
    seat_rng_fn rng;
    void *rng_ctx;
    uint64_t *mem;
    uint32_t mem_blocks; /* >= kdf.m_kib 1 KiB blocks */
} seat_env_t;

void seat_roster_init(seat_roster_t *r, seat_t *storage, uint32_t cap);
/* Move to a bigger array (cap > r->n); the old array is wiped. */
seat_status_t seat_roster_grow(seat_roster_t *r, seat_t *bigger, uint32_t cap);

/* The first seat on a device is the owner and needs no acting seat. After
 * that only an owner (actor) may add, remove or change roles. pass may be
 * NULL for SEAT_LOCK_NONE / PASSKEY; GUEST seats are always unlocked. */
seat_status_t seat_add(seat_roster_t *r, const seat_env_t *env, int32_t actor, const char *name,
                       seat_role_t role, seat_lock_t lock, const uint8_t *pass, uint32_t pass_len,
                       int32_t *out_index);
seat_status_t seat_remove(seat_roster_t *r, int32_t actor, int32_t index);

/* A person changes their own lock (actor == index) or an owner resets one.
 * Changing your own existing passphrase needs the old one (old may be NULL
 * when there was none, or when an owner resets someone else's). */
seat_status_t seat_set_lock(seat_roster_t *r, const seat_env_t *env, int32_t actor, int32_t index,
                            seat_lock_t lock, const uint8_t *old_pass, uint32_t old_len,
                            const uint8_t *pass, uint32_t pass_len);

/* Login. For SEAT_LOCK_PASSKEY pass the host's verdict in passkey_ok (the
 * host verified an ident assertion bound to seat.passkey_id). */
seat_status_t seat_unlock(seat_roster_t *r, const seat_env_t *env, int32_t index,
                          const uint8_t *pass, uint32_t pass_len, bool passkey_ok, uint64_t now_ms,
                          uint64_t *wait_ms);

/* -1 when the login screen is needed, else the seat to enter directly. */
int32_t seat_auto_enter(const seat_roster_t *r);

/* Back-off length after `fails` failures (0 while still free). */
uint64_t seat_wait_ms(uint32_t fails);

#endif /* ZXV_SEATS_H */
