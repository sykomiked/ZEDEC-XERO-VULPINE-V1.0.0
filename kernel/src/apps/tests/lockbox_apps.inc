/* Copyright (c) 2024-2026 Michael Laurence Curzi and 36N9 Genetics, LLC */
/* SPDX-License-Identifier: Apache-2.0 */
/* Lockbox's model against zxvd's real dispatcher (LINK): the keys' ids
 * from fab.status (its store in the scratch Hoard), the books' seal from
 * books.file. Saved passwords are counts only, never a password. */
#include "lockbox_model.h"

static lockbox_t LB;

APPS_TEST(lockbox)
{
    /* the rules */
    CHECK(lockbox_strength(0, 0, 0) == 0 && lockbox_strength(20, 4, 0) == 100 &&
              lockbox_strength(40, 9, 0) == 100 && lockbox_strength(12, 2, 1) == 51 &&
              lockbox_strength(12, 1, 9) == 1 && lockbox_strength(12, 1, 10) == 0 &&
              lockbox_strength(9999, 9, 9999) == 0 && lockbox_strength(10000, 1, 0) == -1 &&
              lockbox_strength(12, 10, 0) == -1 && lockbox_strength(12, 1, -1) == -1,
          "strength: capped, never below 0, refused past the pictures");
    CHECK(lockbox_days_left(86400, 0) == 1 && lockbox_days_left(86399, 0) == 0 &&
              lockbox_days_left(0, 86400) == 0 &&
              lockbox_days_left(LOCKBOX_STAMP_MAX, 0) == 2932896 &&
              lockbox_days_left(-1, 0) == -1 && lockbox_days_left(0, LOCKBOX_STAMP_MAX + 1) == -1,
          "days left: floored, never below 0, refused outside the stamps");
    CHECK(lockbox_secret_ok(12, 1, 0) && lockbox_secret_ok(1024, 4, 1023) &&
              !lockbox_secret_ok(11, 1, 0) && !lockbox_secret_ok(1025, 1, 0) &&
              !lockbox_secret_ok(12, 0, 0) && !lockbox_secret_ok(12, 5, 0) &&
              !lockbox_secret_ok(12, 1, -1) && !lockbox_secret_ok(1024, 1, 1024),
          "limits: their edges");

    /* nothing is asked before it is checked */
    lockbox_init(&LB, &LINK);
    int before = sent;
    uint32_t due = 9;
    int64_t days = 0;
    CHECK(lockbox_set_expiry(&LB, 0, 100) == ZXM_E_STATE &&
              lockbox_set_expiry(&LB, 2, 100) == ZXM_E_ARG &&
              lockbox_set_expiry(&LB, 0, -1) == ZXM_E_ARG &&
              lockbox_due(&LB, 0, 30, &due) == ZXM_E_STATE &&
              lockbox_key_days(&LB, 0, 0, &days) == ZXM_E_STATE &&
              lockbox_propose(&LB, "mail", 11, 2, 0) == ZXM_E_ARG &&
              lockbox_propose(&LB, "", 16, 2, 0) == ZXM_E_ARG &&
              lockbox_propose(&LB, "mail", 12, 2, 12) == ZXM_E_ARG &&
              lockbox_confirm(&LB) == ZXM_E_STATE && lockbox_cancel(&LB) == ZXM_E_STATE &&
              lockbox_remove(&LB, "mail") == ZXM_E_STATE && sent == before,
          "bad input refused before anything is asked");

    /* LB1: zxvd's keys closed, then open in the scratch Hoard */
    char root[256], dir[600];
    CHECK(zxf_root(root, sizeof root) == 0, "lockbox: the scratch Hoard");
    snprintf(dir, sizeof dir, "%s/lockbox-fab", root);
    CHECK(zxvd_rpc_add_group(zxvd_fab_group()) == 0 && zxvd_rpc_enable("fab") == 0,
          "lockbox: fabrication on");
    zxvd_fab_close();
    CHECK(lockbox_load(&LB) == ZXM_OK && LB.loaded && !LB.fab_open && !LB.key[0].present &&
              !LB.key[1].present && !LB.key[0].id[0],
          "LB1: closed keys read as none: %s", LINK.err);
    CHECK(lockbox_set_expiry(&LB, LOCKBOX_KEY_SIGN, 100) == ZXM_E_STATE,
          "LB2: no expiry for a key that is not there");
    CHECK(zxvd_fab_open(dir) == 0, "lockbox: the fab store open");
    CHECK(lockbox_load(&LB) == ZXM_OK && LB.fab_open && LB.key[0].present && LB.key[1].present &&
              strlen(LB.key[0].id) == LOCKBOX_ID_LEN && strlen(LB.key[1].id) == LOCKBOX_ID_LEN &&
              strcmp(LB.key[0].id, LB.key[1].id),
          "LB1: two keys, two ids: %s", LINK.err);
    char sign[LOCKBOX_ID_LEN + 1];
    memcpy(sign, LB.key[0].id, sizeof sign);

    /* LB2: expiry is the owner's */
    int64_t now = INT64_C(1800000000);
    CHECK(lockbox_key_days(&LB, LOCKBOX_KEY_SIGN, now, &days) == ZXM_E_STATE, "LB2: no expiry set");
    CHECK(lockbox_set_expiry(&LB, LOCKBOX_KEY_SIGN, now + 10 * 86400 + 5) == ZXM_OK &&
              lockbox_set_expiry(&LB, LOCKBOX_KEY_KEM, now + 90 * 86400) == ZXM_OK &&
              lockbox_key_days(&LB, LOCKBOX_KEY_SIGN, now, &days) == ZXM_OK && days == 10,
          "LB2: 10 days left: %lld", (long long) days);
    CHECK(lockbox_due(&LB, now, 30, &due) == ZXM_OK && due == 1 &&
              strstr(LB.notice, "signing key (ML-DSA-87): 10 days left") &&
              !strstr(LB.notice, "ML-KEM"),
          "LB2: one key due within 30 days: %s", LB.notice);
    CHECK(lockbox_due(&LB, now, 90, &due) == ZXM_OK && due == 2 && strstr(LB.notice, "ML-KEM"),
          "LB2: both within 90: %s", LB.notice);
    CHECK(lockbox_due(&LB, now, 5, &due) == ZXM_OK && due == 0 && !LB.notice[0],
          "LB2: none within 5");
    CHECK(lockbox_due(&LB, now + 400 * 86400, 0, &due) == ZXM_OK && due == 2 &&
              strstr(LB.notice, "0 days left"),
          "LB2: past is 0, never below: %s", LB.notice);
    CHECK(lockbox_load(&LB) == ZXM_OK && !strcmp(LB.key[0].id, sign) &&
              LB.key[0].expires == now + 10 * 86400 + 5,
          "LB2: the same id keeps its expiry");
    zxvd_fab_close();
    CHECK(lockbox_load(&LB) == ZXM_OK && !LB.key[0].present && LB.key[0].expires == 0,
          "LB2: a key gone loses its expiry");
    CHECK(zxvd_fab_open(dir) == 0 && lockbox_load(&LB) == ZXM_OK && !strcmp(LB.key[0].id, sign) &&
              LB.key[0].expires == 0,
          "LB2: the same key back starts with no expiry");

    /* LB3, LB4: counts only, the owner confirms */
    CHECK(lockbox_propose(&LB, "mail", 12, 1, 9) == ZXM_OK && LB.pending && LB.n == 0 &&
              LB.held.strength == 1 && strstr(LB.notice, "Strength 1 of 100: weak"),
          "LB4: a weak one is held with advice: %s", LB.notice);
    CHECK(lockbox_propose(&LB, "bank", 20, 4, 0) == ZXM_E_STATE, "LB4: one waits at a time");
    CHECK(lockbox_confirm(&LB) == ZXM_OK && LB.n == 1 && !LB.pending && lockbox_weak(&LB) == 1,
          "LB4: kept on the owner's word, still counted weak");
    CHECK(lockbox_propose(&LB, "bank", 20, 4, 0) == ZXM_OK &&
              strstr(LB.notice, "Strength 100 of 100. Keep it?") && lockbox_cancel(&LB) == ZXM_OK &&
              LB.n == 1 && !lockbox_item(&LB, "bank"),
          "LB4: cancelled, nothing kept");
    CHECK(lockbox_propose(&LB, "mail", 24, 3, 0) == ZXM_OK && lockbox_confirm(&LB) == ZXM_OK &&
              LB.n == 1 && lockbox_item(&LB, "mail")->strength == 90 && lockbox_weak(&LB) == 0,
          "LB4: the same name is replaced");
    for (uint32_t i = 1; i < LOCKBOX_ITEMS; i++) {
        char name[16];
        snprintf(name, sizeof name, "site%u", i);
        if (lockbox_propose(&LB, name, 16, 2, 0) != ZXM_OK || lockbox_confirm(&LB) != ZXM_OK) break;
    }
    CHECK(LB.n == LOCKBOX_ITEMS && lockbox_propose(&LB, "one more", 16, 2, 0) == ZXM_E_TOOBIG &&
              lockbox_propose(&LB, "site5", 30, 4, 0) == ZXM_OK && lockbox_confirm(&LB) == ZXM_OK,
          "LB4: full at %u, a replacement still fits", LB.n);
    CHECK(lockbox_remove(&LB, "site5") == ZXM_OK && LB.n == LOCKBOX_ITEMS - 1 &&
              !lockbox_item(&LB, "site5") && lockbox_remove(&LB, "site5") == ZXM_E_STATE,
          "removed once");

    /* LB5: the books' seal, from a scratch books file, never the owner's */
    char scratch[700];
    snprintf(scratch, sizeof scratch, "%s/lockbox-scratch.books", root);
    zxvd_rpc_books_close();
    CHECK(lockbox_books(&LB) == ZXM_E_REMOTE && !LB.books_head[0] && !LB.books_saved,
          "LB6: books closed, nothing kept: %s", LINK.err);
    CHECK(zxvd_rpc_books_file(scratch) == 0 && zxvd_rpc_books_open() == 0, "scratch books open");
    CHECK(lockbox_books(&LB) == ZXM_OK && LB.books_saved &&
              strlen(LB.books_head) == LOCKBOX_ID_LEN && LB.books_records >= 0,
          "LB5: books.file: %s", LINK.err);
    zxvd_rpc_books_close();
    CHECK(zxvd_rpc_books_file("") == 0 && remove(scratch) == 0, "scratch books removed");
}
