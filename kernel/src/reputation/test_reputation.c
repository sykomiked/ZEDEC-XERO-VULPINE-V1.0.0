/* Copyright (c) 2024-2026 Michael Laurence Curzi and 36N9 Genetics, LLC */
/* SPDX-License-Identifier: LicenseRef-OPL-1.1 AND CC-BY-SA-4.0 AND LicenseRef-Royal-Writ-Sicilian-Crown-1.0 AND LicenseRef-SEL-3.3 */
/* test_reputation.c — the Pig Badge and the earnable badges.
 *
 * Each check pins one property: the operative can't see their own snout, the
 * room can, the badge maxes out at "over 9000", it is NOT a ban, and the
 * earnable badges actually gamify.
 */
#include <stdio.h>
#include <string.h>
#include "reputation.h"

/* Paths for the source-grep mandate checks (no-ban proof). Overridable via -D;
 * default to the tree layout when the harness runs from kernel/. */
#ifndef REP_HDR
#define REP_HDR "src/reputation/reputation.h"
#endif
#ifndef REP_SRC
#define REP_SRC "src/reputation/reputation.c"
#endif

static int failures = 0;
#define CHECK(c,m) do{ if(!(c)){printf("[FAIL] %s\n",m);failures++;} \
    else printf("[PASS] %s\n",m);}while(0)

/* Anchor (5): prove there is NO ban/mute/remove FUNCTION — grep the source for
 * the call/definition form `name(`. (We can't grep the bare word: the source
 * openly DISCUSSES not banning people, and prose like "it is NOT a ban" is the
 * whole point. What must be absent is a callable capability, i.e. `ban(`.) */
static int source_mentions(const char *path, const char *needle)
{
    FILE *f = fopen(path, "r");
    if (!f) { printf("[WARN] could not open %s for grep\n", path); return -1; }
    char line[512];
    int hit = 0;
    while (fgets(line, sizeof line, f)) {
        if (strstr(line, needle)) { hit = 1; break; }
    }
    fclose(f);
    return hit;
}

int main(void)
{
    printf("=== the Pig Badge: a warning label the room can read, and the operative cannot ===\n");
    rep_state_t st;
    rep_init(&st);

    const uint32_t SPY   = 1337u;   /* the operative running an op            */
    const uint32_t ALICE = 7u;      /* an ordinary member                     */

    /* ---- anchor (3): three confirmed flags -> level 3 ---- */
    CHECK(pig_flag(&st, SPY) == 1, "first confirmed flag -> pig level 1");
    CHECK(pig_flag(&st, SPY) == 2, "second confirmed flag -> pig level 2");
    CHECK(pig_flag(&st, SPY) == 3, "third confirmed flag -> pig level 3");

    /* ---- anchor (2): everyone else sees the accrued level ---- */
    CHECK(pig_level_seen_by(&st, SPY, ALICE) == 3u,
          "everyone else sees the accrued pig level (3)");

    /* ---- anchor (1): the subject CANNOT see their own pig badge ---- */
    CHECK(pig_level_seen_by(&st, SPY, SPY) == 0u,
          "the operative sees 0 for their OWN badge (never their own snout)");

    /* an unflagged member reads as 0 to everyone */
    CHECK(pig_level_seen_by(&st, ALICE, SPY) == 0u,
          "an unflagged member has pig level 0");

    /* ---- anchor (4): badge text + saturation ---- */
    char buf[32];
    pig_badge_text(42u, buf, sizeof buf);
    CHECK(strcmp(buf, "42") == 0, "pig_badge_text(42) writes '42'");
    pig_badge_text(9001u, buf, sizeof buf);
    CHECK(strcmp(buf, "over 9000") == 0, "pig_badge_text(9001) writes 'over 9000'");
    pig_badge_text(9000u, buf, sizeof buf);
    CHECK(strcmp(buf, "9000") == 0, "pig_badge_text(9000) writes '9000' (boundary, not over)");
    pig_badge_text(1u, buf, sizeof buf);
    CHECK(strcmp(buf, "1") == 0, "pig_badge_text(1) writes '1'");

    /* saturation: flag WAY past 9999, level must clamp at 9999 */
    for (int i = 0; i < 12000; i++) (void)pig_flag(&st, SPY);
    CHECK(pig_level_seen_by(&st, SPY, ALICE) == 9999u,
          "pig level saturates at 9999 (flagging past 9999 stays 9999)");
    pig_badge_text(pig_level_seen_by(&st, SPY, ALICE), buf, sizeof buf);
    CHECK(strcmp(buf, "over 9000") == 0, "saturated badge reads 'over 9000'");

    /* the operative STILL can't see it, even maxed out */
    CHECK(pig_level_seen_by(&st, SPY, SPY) == 0u,
          "even at 9999 the operative still sees 0 for themselves");

    /* ---- anchor (5): it is NOT a ban — capability retained + no ban API ---- */
    /* The flagged subject can still earn badges, hold score, everything: */
    CHECK(badge_award(&st, SPY, 100u, 1u) == 1,
          "flagged subject retains full capability (can still earn a badge)");
    CHECK(badge_has(&st, SPY, 100u, 1u),
          "flagged subject's earned badge is real — no muting");
    /* And there is literally no ban/mute/remove function in the source: */
    int ban_h  = source_mentions(REP_HDR, "ban(");
    int mute_h = source_mentions(REP_HDR, "mute(");
    int rem_h  = source_mentions(REP_HDR, "remove(");
    int ban_c  = source_mentions(REP_SRC, "ban(");
    int mute_c = source_mentions(REP_SRC, "mute(");
    int rem_c  = source_mentions(REP_SRC, "remove(");
    CHECK(ban_h == 0 && ban_c == 0, "no ban() function in the API/source (free speech preserved)");
    CHECK(mute_h == 0 && mute_c == 0, "no mute() function in the API/source");
    CHECK(rem_h == 0 && rem_c == 0, "no remove() function in the API/source");

    /* ---- anchor (6): earnable badges gamify ---- */
    printf("--- earnable badges: gamified productivity ---\n");
    const uint32_t BUILDER = 55u;   /* the 'Builder' badge id                 */
    CHECK(badge_award(&st, ALICE, BUILDER, 3u) == 3,
          "award Builder at level 3");
    CHECK(badge_has(&st, ALICE, BUILDER, 2u),
          "level-3 badge satisfies badge_has(min_level 2)");
    CHECK(!badge_has(&st, ALICE, BUILDER, 4u),
          "level-3 badge fails badge_has(min_level 4)");

    uint32_t score_before = badge_score(&st, ALICE);
    (void)badge_award(&st, ALICE, 56u, 5u);   /* a second badge               */
    uint32_t score_after = badge_score(&st, ALICE);
    CHECK(score_after > score_before, "badge_score rises with awards");
    CHECK(score_after == 8u, "badge_score is the sum of levels (3 + 5 = 8)");

    /* monotonic: a lower re-award never demotes */
    CHECK(badge_award(&st, ALICE, BUILDER, 1u) == 3,
          "re-award at a lower level does NOT demote (monotonic)");
    /* raising works */
    CHECK(badge_award(&st, ALICE, BUILDER, 7u) == 7,
          "re-award at a higher level raises it");

    printf("\n%s: %d failure(s)\n", failures ? "FAILURES" : "ALL PASS", failures);
    return failures ? 1 : 0;
}
