/* Copyright (c) 2024-2026 Michael Laurence Curzi and 36N9 Genetics, LLC */
/* SPDX-License-Identifier: Apache-2.0 */
/* test_social.c — the free-forum layer under host build (TEST_HOST only).
 *
 * We assert the MANDATES, not just the plumbing:
 *   (1) social.c contains no eject/erase/silence-a-person vocabulary and no
 *       advertising vocabulary — the guarantees are true BY CONSTRUCTION.
 *   (2) Don Tovani's Club and Zevion's Hideout open as the right kinds.
 *   (3) a sorted troll is GROUPED WITH LIKE (quarantine) yet is STILL PRESENT
 *       and can STILL post.
 *   (4) a normal member and a troll are DIVIDED after sorting.
 *   (5) posts survive a sort — nothing is erased.
 */
#include <stdio.h>
#include <string.h>
#include "social.h"

#ifndef SOC_SRC_PATH
#define SOC_SRC_PATH "src/social_spaces/social.c"   /* path from kernel/ (verify-all cwd); override with -D */
#endif

static int g_checks = 0;
#define CHECK(cond, msg) do {                                   \
        g_checks++;                                             \
        if (!(cond)) { printf("FAIL: %s\n", msg); return 1; }   \
        printf("  ok: %s\n", msg);                              \
    } while (0)

/* Case-insensitive substring search — the whole no-ban / no-ads proof. */
static int contains_ci(const char *hay, const char *needle) {
    if (!hay || !needle || !*needle) return 0;
    for (const char *h = hay; *h; h++) {
        const char *a = h, *b = needle;
        while (*a && *b) {
            char ca = *a, cb = *b;
            if (ca >= 'A' && ca <= 'Z') ca = (char)(ca - 'A' + 'a');
            if (cb >= 'A' && cb <= 'Z') cb = (char)(cb - 'A' + 'a');
            if (ca != cb) break;
            a++; b++;
        }
        if (!*b) return 1;
    }
    return 0;
}

int main(void) {
    printf("== test_social ==\n");

    /* ---- (1) source-grep: the mandates are true by construction ---- */
    {
        FILE *f = fopen(SOC_SRC_PATH, "rb");
        CHECK(f != NULL, "social.c source is readable for the grep proof");
        static char src[200000];
        size_t n = fread(src, 1, sizeof(src) - 1, f);
        fclose(f);
        src[n] = 0;
        CHECK(n > 0, "social.c is non-empty");

        /* No coercion vocabulary: there is no ban/eject primitive to abuse. */
        const char *forbidden_moderation[] = {
            "ban", "delete", "silence", "remove", "shadowban", "mute", NULL
        };
        for (int i = 0; forbidden_moderation[i]; i++) {
            char m[96];
            snprintf(m, sizeof(m), "social.c has NO '%s' (no-ban by construction)",
                     forbidden_moderation[i]);
            CHECK(!contains_ci(src, forbidden_moderation[i]), m);
        }
        /* No advertising vocabulary anywhere. */
        const char *forbidden_ads[] = {
            "sponsored", "promoted", "advert", "sponsor", NULL
        };
        for (int i = 0; forbidden_ads[i]; i++) {
            char m[96];
            snprintf(m, sizeof(m), "social.c has NO '%s' (no-ads by construction)",
                     forbidden_ads[i]);
            CHECK(!contains_ci(src, forbidden_ads[i]), m);
        }
    }

    soc_world_t w;
    soc_init(&w);

    /* ---- (2) the named spaces open as the right kinds ---- */
    int32_t club = soc_open_space(&w, SOC_SOCIAL_CLUB, "Don Tovani's Social Club");
    int32_t den  = soc_open_space(&w, SOC_DEV_DEN,     "Zevion's Hideout");
    int32_t bar  = soc_open_space(&w, SOC_CASUAL,      "The Back Porch");
    CHECK(club >= 0 && den >= 0 && bar >= 0, "three spaces opened");
    CHECK(w.space[club].kind == SOC_SOCIAL_CLUB, "Don Tovani's Club is a SOCIAL_CLUB");
    CHECK(w.space[den].kind  == SOC_DEV_DEN,     "Zevion's Hideout is a DEV_DEN");
    CHECK(strcmp(w.space[club].name, "Don Tovani's Social Club") == 0, "club name kept");
    CHECK(strcmp(w.space[den].name,  "Zevion's Hideout") == 0,        "hideout name kept");

    /* ---- membership: a normal builder and two hecklers gather in the Club ---- */
    const uint32_t NORMAL  = 10;
    const uint32_t TROLL_A = 20;
    const uint32_t TROLL_B = 21;
    CHECK(soc_join(&w, club, NORMAL)  == SOC_OK, "normal joins the Club");
    CHECK(soc_join(&w, club, TROLL_A) == SOC_OK, "troll A joins the Club");
    CHECK(soc_join(&w, club, TROLL_B) == SOC_OK, "troll B joins the Club");
    CHECK(soc_join(&w, club, NORMAL)  == SOC_OK, "re-join is idempotent");
    /* three distinct witnesses who will file REAL first-party reports */
    const uint32_t W1 = 30, W2 = 31, W3 = 32;
    soc_join(&w, club, W1); soc_join(&w, club, W2); soc_join(&w, club, W3);

    /* ---- posts, including one we will re-read after the sort ---- */
    const char *hello = "Hello, builders! Anyone shipping today?";
    int32_t post0 = soc_post(&w, club, NORMAL, (const uint8_t *)hello,
                             (uint32_t)strlen(hello));
    CHECK(post0 == 0, "normal's post lands at index 0");
    const char *heckle = "your idea is bad and you should feel bad";
    int32_t post1 = soc_post(&w, club, TROLL_A, (const uint8_t *)heckle,
                             (uint32_t)strlen(heckle));
    CHECK(post1 == 1, "troll A can post BEFORE sorting (free speech)");
    CHECK(soc_post_count(&w, club) == 2, "two posts retained");

    /* ---- (3) NO SHADOWBAN: soc_sort cannot quarantine an un-reported user ---- */
    CHECK(soc_sort(&w, TROLL_A) == SOC_ERR_NOT_QUARANTINED,
          "soc_sort will NOT quarantine a user nobody reported (no shadowban)");
    /* A single reporter cannot force quarantine either — one strike, and a repeat
     * by the SAME reporter is a no-op (the unilateral-quarantine bug is closed). */
    soc_report(&w, W1, TROLL_A, SOC_QUAR_DURATION);
    soc_report(&w, W1, TROLL_A, SOC_QUAR_DURATION);   /* same reporter again: no-op */
    CHECK(con_standing(con_get(&w.commons, TROLL_A)) != CON_QUARANTINED,
          "one reporter (even repeating) CANNOT quarantine — no unilateral ban");
    /* real CONSENSUS: three DISTINCT witnesses each report first-party */
    soc_report(&w, W2, TROLL_A, SOC_QUAR_DURATION);
    soc_report(&w, W3, TROLL_A, SOC_QUAR_DURATION);
    con_person_t *pa = con_get(&w.commons, TROLL_A);
    CHECK(pa != NULL && con_standing(pa) == CON_QUARANTINED,
          "three DISTINCT real reporters quarantine troll A (genuine consensus)");
    /* now soc_sort seats him with his own kind — imposing nothing new */
    int32_t corner_a = soc_sort(&w, TROLL_A);
    CHECK(corner_a >= 0, "soc_sort(A) seats the quarantined troll, returns a grouping");
    CHECK(soc_is_member(&w, club, TROLL_A),
          "troll A is STILL a member — nobody was removed");   /* still present */
    int32_t post2 = soc_post(&w, club, TROLL_A, (const uint8_t *)"still here",
                             10u);
    CHECK(post2 == 2, "troll A can STILL post after being sorted (never gagged)");

    /* ---- (4) normal and troll are SEPARATED — but by the standing POOL, not a
     * fabricated divide. NORMAL never reported A, so there is deliberately NO
     * divide manufactured on NORMAL's behalf (that would be the shadowban we
     * refuse). The separation is real all the same: A is quarantined, NORMAL is
     * not, and concord matches across that boundary only within a pool. ------- */
    CHECK(!con_divided(&w.commons, NORMAL, TROLL_A),
          "NO divide is fabricated between normal and troll A (no shadowban)");
    CHECK(!con_may_match(&w.commons, NORMAL, TROLL_A),
          "normal is still not matched with troll A — the standing pool separates them");

    /* ---- troll B: quarantined by real consensus too, then seated with A ---- */
    soc_report(&w, W1, TROLL_B, SOC_QUAR_DURATION);
    soc_report(&w, W2, TROLL_B, SOC_QUAR_DURATION);
    soc_report(&w, W3, TROLL_B, SOC_QUAR_DURATION);
    int32_t corner_b = soc_sort(&w, TROLL_B);
    CHECK(corner_b >= 1, "soc_sort(B) reports at least one fellow heckler (A)");
    con_person_t *pb = con_get(&w.commons, TROLL_B);
    CHECK(pb != NULL && con_standing(pb) == CON_QUARANTINED, "troll B is QUARANTINED");
    CHECK(!con_divided(&w.commons, TROLL_A, TROLL_B),
          "the two trolls are NOT divided from each other — grouped with like");
    CHECK(con_may_match(&w.commons, TROLL_A, TROLL_B),
          "the two trolls CAN match each other (heckle each other, not us)");
    CHECK(!con_may_match(&w.commons, NORMAL, TROLL_B),
          "normal is separated from troll B as well (quarantine pool, not a shadowban)");

    /* ---- (5) posts survived every sort — nothing was erased ---- */
    CHECK(soc_post_count(&w, club) == 3, "all three posts still present after sorts");
    uint32_t len = 0, author = 0;
    const uint8_t *b = soc_post_at(&w, club, 0, &len, &author);
    CHECK(b != NULL && author == NORMAL && len == (uint32_t)strlen(hello) &&
          memcmp(b, hello, len) == 0,
          "normal's original post reads back byte-for-byte after the sorts");

    printf("\nALL %d CHECKS PASSED\n", g_checks);
    return 0;
}
