/* Copyright (c) 2024-2026 Michael Laurence Curzi and 36N9 Genetics, LLC */
/* SPDX-License-Identifier: Apache-2.0 */
/* test_seats.c — people on one device, login and back-off. */
#include <stdio.h>
#include <string.h>
#include "seats.h"

static int pass_n, fail_n;
#define CHECK(c, m)                                                                                \
    do {                                                                                           \
        if (c)                                                                                     \
            pass_n++;                                                                              \
        else {                                                                                     \
            fail_n++;                                                                              \
            printf("FAIL: %s (line %d)\n", m, __LINE__);                                           \
        }                                                                                          \
    } while (0)

static void rng(uint8_t *o, uint32_t n, void *ctx)
{
    uint32_t *st = (uint32_t *) ctx;
    for (uint32_t i = 0; i < n; i++) {
        *st = *st * 1103515245u + 12345u;
        o[i] = (uint8_t) (*st >> 16);
    }
}

static uint64_t mem[64 * 128];

int main(void)
{
    printf("=== seats: people on one device, login after boot ===\n");
    static seat_t small[2], big[40];
    seat_roster_t r;
    uint32_t seed = 7;
    seat_env_t env = {rng, &seed, mem, 64};
    seat_roster_init(&r, small, 2);
    CHECK(r.auto_enter && r.kdf.m_kib == 65536u, "defaults: auto-enter on, 64 MiB Argon2id");
    r.kdf.m_kib = 64;
    r.kdf.t = 1; /* small for the test */

    int32_t owner = -1, ana = -1, g = -1, k = -1;
    CHECK(seat_auto_enter(&r) == -1, "no seats: the screen is needed (to create the first)");
    CHECK(seat_add(&r, &env, -1, "Michael", SEAT_GUEST, SEAT_LOCK_NONE, 0, 0, &owner) == SEAT_OK,
          "the first seat is created without an actor");
    CHECK(r.v[owner].role == SEAT_OWNER, "and it is the owner whatever was asked");
    CHECK(seat_auto_enter(&r) == 0, "one unlocked seat: straight to the desktop");
    r.auto_enter = false;
    CHECK(seat_auto_enter(&r) == -1, "unless the owner wants the login screen anyway");
    r.auto_enter = true;

    const uint8_t pw[] = "golden fox 168";
    CHECK(seat_add(&r, &env, owner, "Ana", SEAT_PERSON, SEAT_LOCK_PASS, pw, sizeof pw - 1, &ana) ==
              SEAT_OK,
          "the owner adds a person with a passphrase");
    CHECK(seat_auto_enter(&r) == -1, "two seats: the login screen shows");
    uint8_t zero[SEAT_HASH_LEN] = {0};
    CHECK(memcmp(r.v[ana].hash, zero, SEAT_HASH_LEN) != 0 &&
              strstr((char *) &r.v[ana], "golden") == 0,
          "only a hash is kept, never the passphrase");
    CHECK(seat_add(&r, &env, owner, "Guest", SEAT_GUEST, SEAT_LOCK_PASS, pw, 4, &g) ==
              SEAT_ERR_FULL,
          "a full array asks to grow");
    CHECK(seat_roster_grow(&r, big, 40) == SEAT_OK && r.cap == 40 && r.n == 2,
          "grown, nothing lost");
    CHECK(seat_add(&r, &env, owner, "Guest", SEAT_GUEST, SEAT_LOCK_PASS, pw, 4, &g) == SEAT_OK &&
              r.v[g].lock == SEAT_LOCK_NONE,
          "a guest never has a lock");
    CHECK(seat_add(&r, &env, ana, "Kid", SEAT_PERSON, SEAT_LOCK_NONE, 0, 0, &k) == SEAT_ERR_DENIED,
          "only an owner adds people");
    CHECK(seat_add(&r, &env, owner, "Ana", SEAT_PERSON, SEAT_LOCK_NONE, 0, 0, &k) == SEAT_ERR_NAME,
          "names are unique");
    CHECK(seat_add(&r, &env, owner, "", SEAT_PERSON, SEAT_LOCK_NONE, 0, 0, &k) == SEAT_ERR_NAME,
          "and not empty");
    for (int i = 0; i < 30; i++) {
        char nm[24];
        snprintf(nm, sizeof nm, "person %d", i);
        if (seat_add(&r, &env, owner, nm, SEAT_PERSON, SEAT_LOCK_NONE, 0, 0, &k) != SEAT_OK) {
            CHECK(0, "many people");
            break;
        }
    }
    CHECK(r.n == 33, "as many people as the owner wants");

    uint64_t w = 0;
    CHECK(seat_unlock(&r, &env, owner, 0, 0, false, 1000, &w) == SEAT_OK,
          "an unlocked seat enters");
    CHECK(seat_unlock(&r, &env, ana, pw, sizeof pw - 1, false, 1000, &w) == SEAT_OK,
          "the right passphrase enters");
    for (int i = 0; i < 4; i++)
        CHECK(seat_unlock(&r, &env, ana, (const uint8_t *) "wrong!", 6, false, 2000, &w) ==
                      SEAT_ERR_WRONG &&
                  w == 0,
              "the first wrong tries cost nothing");
    CHECK(seat_unlock(&r, &env, ana, (const uint8_t *) "wrong!", 6, false, 2000, &w) ==
                  SEAT_ERR_WRONG &&
              w == SEAT_WAIT_FIRST_MS,
          "then a 30 s wait");
    CHECK(seat_unlock(&r, &env, ana, pw, sizeof pw - 1, false, 3000, &w) == SEAT_ERR_WAIT &&
              w == 29000,
          "even the right one waits it out");
    CHECK(seat_unlock(&r, &env, ana, (const uint8_t *) "x", 1, false, 40000, &w) ==
                  SEAT_ERR_WRONG &&
              w == 2 * SEAT_WAIT_FIRST_MS,
          "each further miss doubles");
    CHECK(seat_wait_ms(200) == SEAT_WAIT_MAX_MS, "never more than an hour");
    CHECK(seat_unlock(&r, &env, ana, pw, sizeof pw - 1, false, 200000, &w) == SEAT_OK &&
              r.v[ana].fails == 0,
          "after the wait the right one enters and clears it");

    const uint8_t pw2[] = "1234";
    CHECK(seat_set_lock(&r, &env, ana, ana, SEAT_LOCK_PASS, (const uint8_t *) "nope", 4, pw2, 4) ==
              SEAT_ERR_WRONG,
          "changing your own needs the old one");
    CHECK(seat_set_lock(&r, &env, ana, ana, SEAT_LOCK_PASS, 0, 0, pw2, 4) == SEAT_ERR_WRONG,
          "leaving out the old one is a wrong try, not a bad call");
    r.kdf.p = 2;
    r.kdf.m_kib = 64;
    CHECK(seat_unlock(&r, &env, ana, pw, sizeof pw - 1, false, 250000, &w) == SEAT_OK,
          "a later change to the roster's KDF does not break existing hashes");
    r.kdf.p = 1;
    CHECK(seat_set_lock(&r, &env, ana, ana, SEAT_LOCK_PASS, pw, sizeof pw - 1, pw2, 4) == SEAT_OK,
          "with it, a short PIN is fine");
    CHECK(seat_unlock(&r, &env, ana, pw2, 4, false, 300000, &w) == SEAT_OK, "the new PIN works");
    CHECK(seat_set_lock(&r, &env, ana, owner, SEAT_LOCK_PASS, 0, 0, pw, 6) == SEAT_ERR_DENIED,
          "a person cannot set someone else's lock");
    CHECK(seat_set_lock(&r, &env, owner, ana, SEAT_LOCK_PASSKEY, 0, 0, 0, 0) == SEAT_OK &&
              memcmp(r.v[ana].hash, zero, SEAT_HASH_LEN) == 0,
          "an owner can reset it; the old hash is wiped");
    CHECK(seat_unlock(&r, &env, ana, 0, 0, false, 300000, &w) == SEAT_ERR_WRONG,
          "a refused passkey fails");
    CHECK(seat_unlock(&r, &env, ana, 0, 0, true, 300000, &w) == SEAT_OK,
          "the device's own unlock enters");
    CHECK(seat_set_lock(&r, &env, owner, g, SEAT_LOCK_PASS, 0, 0, pw, 6) == SEAT_ERR_DENIED,
          "guests stay open");

    CHECK(seat_remove(&r, ana, owner) == SEAT_ERR_DENIED, "a person cannot remove anyone");
    CHECK(seat_remove(&r, owner, owner) == SEAT_ERR_LAST_OWNER, "the last owner stays");
    uint32_t n0 = r.n;
    CHECK(seat_remove(&r, owner, ana) == SEAT_OK && r.n == n0 - 1 &&
              strcmp(r.v[ana].name, "Guest") == 0,
          "removing closes the gap");
    CHECK(seat_add(0, &env, -1, "x", SEAT_OWNER, SEAT_LOCK_NONE, 0, 0, 0) == SEAT_ERR_ARG,
          "null roster refused");

    printf("%d/%d checks passed\n", pass_n, pass_n + fail_n);
    return fail_n ? 1 : 0;
}
