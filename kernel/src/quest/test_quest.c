/* Copyright (c) 2024-2026 Michael Laurence Curzi and 36N9 Genetics, LLC */
/* SPDX-License-Identifier: Apache-2.0 */
/* test_quest.c — host tests for kernel/src/quest: the guardrails, mastery,
 * time budget, minors, opt-out, requested notifications, co-op goals and
 * seasons, VFV shares through pay, and ML-DSA-65 signed badges. */
#include <stdio.h>
#include <string.h>
#include "quest.h"
#include "quest_coop.h"
#include "quest_pay.h"
#include "quest_badge.h"
#include "quest_sign_mldsa.h"

static int pass, fail;
#define CHECK(c, m)                                                                                \
    do {                                                                                           \
        if (c) {                                                                                   \
            pass++;                                                                                \
            printf("[PASS] %s\n", m);                                                              \
        } else {                                                                                   \
            fail++;                                                                                \
            printf("[FAIL] %s (line %d)\n", m, __LINE__);                                          \
        }                                                                                          \
    } while (0)

static qst_world_t W;
static pay_ledger_t L;

/* ---- age hook: a tiny guardian table ---- */
typedef struct {
    uint32_t subject;
    qst_age_policy_t pol;
    bool known;
} age_row_t;
static age_row_t ages[8];
static uint32_t n_ages;

static bool age_hook(void *ctx, uint32_t subject, qst_age_policy_t *out)
{
    (void) ctx;
    for (uint32_t i = 0; i < n_ages; i++)
        if (ages[i].subject == subject) {
            if (!ages[i].known) return false;
            *out = ages[i].pol;
            return true;
        }
    out->is_minor = false;
    out->allow_money = true;
    out->public_badges = true;
    return true;
}

static void evid(uint8_t e[32], uint32_t a, uint32_t b)
{
    memset(e, 0, 32);
    memcpy(e, &a, 4);
    memcpy(e + 4, &b, 4);
    e[31] = 0x5a;
}

static qst_event_t ev(uint32_t subject, uint8_t src, uint8_t kind, uint64_t qty, uint32_t verifier,
                      uint32_t serial, uint32_t day, uint32_t goal)
{
    qst_event_t e;
    memset(&e, 0, sizeof e);
    e.subject = subject;
    e.source = src;
    e.kind = kind;
    e.qty = qty;
    e.verifier = verifier;
    e.day = day;
    e.goal = goal;
    evid(e.evidence, subject * 7919u + kind, serial);
    return e;
}

static uint32_t serial_ctr = 1;
static qst_status_t help(uint32_t who, uint32_t verifier, uint32_t day, uint32_t goal)
{
    qst_event_t e = ev(who, QST_SRC_PEER, QST_EV_PEER_HELPED, 1, verifier, serial_ctr++, day, goal);
    return qst_record(&W, &e);
}

/* ===================================================================== */

static void test_guard(void)
{
    printf("\n=== guardrails (quest_guard.h) ===\n");
    qst_mechanic_t own[16];
    uint32_t n = qst_own_mechanics(own, 16);
    bool all = n >= 7;
    for (uint32_t i = 0; i < n; i++) all = all && qst_guard_check(&own[i]) == 0;
    CHECK(all, "every mechanic quest runs passes its own guard");

    qst_mechanic_t m;
    qst_mechanic_plain(&m, QST_RW_ITEM);
    m.paid_entry = true;
    m.random_outcome = true;
    CHECK(qst_guard_check(&m) & QST_V_LOOTBOX, "G1 paid chance at an item is a loot box");
    CHECK(qst_guard_check(&m) & QST_V_PAID_RANDOM, "G2 ... and paid randomness");
    CHECK(!strcmp(qst_guard_violation_name(QST_V_LOOTBOX), "loot box"), "violation names");

    qst_mechanic_plain(&m, QST_RW_MONEY);
    m.random_outcome = true;
    CHECK(qst_guard_check(&m) & QST_V_PAID_RANDOM, "G2 a random money prize is refused");

    qst_mechanic_plain(&m, QST_RW_ITEM);
    m.random_outcome = true;
    CHECK(qst_guard_check(&m) == 0, "free, unpaid in-game chance (games food spawn) is allowed");

    qst_mechanic_plain(&m, QST_RW_STANDING);
    m.loss_on_lapse = true;
    CHECK(qst_guard_check(&m) == QST_V_STREAK_LOSS, "G3 streak-loss punishment refused");

    qst_mechanic_plain(&m, QST_RW_MONEY);
    m.schedule = QST_SCHED_VARIABLE_RATIO;
    CHECK(qst_guard_check(&m) & QST_V_VARIABLE_MONEY, "G4 variable-ratio money refused");
    m.schedule = QST_SCHED_VARIABLE_INTERVAL;
    CHECK(qst_guard_check(&m) & QST_V_VARIABLE_MONEY, "G4 variable-interval money refused");

    qst_mechanic_plain(&m, QST_RW_NONE);
    m.purchase_timer = true;
    CHECK(qst_guard_check(&m) == QST_V_PURCHASE_PRESS, "G5 countdown on a purchase refused");
    qst_mechanic_plain(&m, QST_RW_NONE);
    m.purchase_scarcity = true;
    CHECK(qst_guard_check(&m) == QST_V_PURCHASE_PRESS, "G5 scarcity claim on a purchase refused");

    qst_mechanic_plain(&m, QST_RW_NONE);
    m.unrequested_push = true;
    CHECK(qst_guard_check(&m) == QST_V_PULL_NOTIFY, "G6 pull notification refused");

    qst_mechanic_plain(&m, QST_RW_BADGE);
    m.paid_entry = true;
    CHECK(qst_guard_check(&m) & QST_V_PAY_STANDING, "G7 buying a badge refused");

    qst_mechanic_plain(&m, QST_RW_BADGE);
    m.schedule = QST_SCHED_VARIABLE_RATIO;
    CHECK(qst_guard_check(&m) & QST_V_BADGE_UNEARNED, "G8 random badge drop refused");

    qst_mechanic_plain(&m, QST_RW_NONE);
    m.no_stopping_point = true;
    CHECK(qst_guard_check(&m) == QST_V_NO_STOP, "G9 endless feed refused");

    qst_mechanic_plain(&m, QST_RW_STANDING);
    m.for_minors = true;
    CHECK(qst_guard_check(&m) == 0, "standing mechanic for minors allowed");
    m.ranks_people = true;
    CHECK(qst_guard_check(&m) == QST_V_MINORS, "G10 ranking minors refused");
    qst_mechanic_plain(&m, QST_RW_MONEY);
    m.for_minors = true;
    CHECK(qst_guard_check(&m) & QST_V_MINORS, "G10 money mechanic aimed at minors refused");

    qst_mechanic_plain(&m, QST_RW_STANDING);
    m.opt_out_forfeits = true;
    CHECK(qst_guard_check(&m) == QST_V_OPT_OUT, "G11 opt-out forfeit refused");
    qst_mechanic_plain(&m, QST_RW_STANDING);
    m.hides_opt_out = true;
    CHECK(qst_guard_check(&m) == QST_V_OPT_OUT, "G11 hidden opt-out refused");

    qst_mechanic_plain(&m, QST_RW_STANDING);
    m.reward = 9;
    CHECK(qst_guard_check(&m) & QST_V_BAD_ENUM, "out-of-range reward refused");
    CHECK(qst_guard_check(0) == QST_V_BAD_ENUM, "NULL mechanic refused");

    CHECK(!qst_copy_is_calm("HURRY, offer ends in 2 hours"), "calm copy: urgency caught");
    CHECK(!qst_copy_is_calm("Don't lose your Streak"), "calm copy: streak caught, any case");
    CHECK(!qst_copy_is_calm("Only 3 left!"), "calm copy: scarcity caught");
    CHECK(!qst_copy_is_calm("We miss you"), "calm copy: come-back message caught");
    CHECK(qst_copy_is_calm("Your co-op reached 1 TB of shared storage."),
          "calm copy: plain fact ok");
    CHECK(qst_copy_is_calm(qst_hint_text(QST_HINT_NEARING)) &&
              qst_copy_is_calm(qst_hint_text(QST_HINT_REACHED)) &&
              qst_copy_is_calm(qst_hint_text(QST_HINT_STOP)),
          "every hint quest shows is calm");
}

static void test_mastery(void)
{
    printf("\n=== mastery from verified contribution ===\n");
    qst_init(&W, 0, 0, 0);
    CHECK(qst_enrol(&W, 10) == QST_OK && qst_enrol(&W, 11) == QST_OK, "enrol");
    CHECK(qst_enrol(&W, 0) == QST_ERR_ARG, "subject 0 refused");

    qst_event_t e = ev(10, QST_SRC_PEER, QST_EV_PEER_HELPED, 1, 0, 1, 1, 0);
    CHECK(qst_record(&W, &e) == QST_ERR_UNVERIFIED, "unverified help refused");
    e.verifier = 10;
    CHECK(qst_record(&W, &e) == QST_ERR_SELF_VERIFIED, "self-verified help refused");
    e.verifier = 11;
    e.source = QST_SRC_MARKET;
    CHECK(qst_record(&W, &e) == QST_ERR_SOURCE, "help reported by the wrong module refused");
    e.source = QST_SRC_PEER;
    CHECK(qst_record(&W, &e) == QST_OK, "peer-confirmed help counts");
    CHECK(qst_record(&W, &e) == QST_ERR_DUPLICATE, "the same evidence counts once");
    CHECK(qst_profile(&W, 10)->points[QST_SKILL_MENTOR] == 3, "help is worth 3 points");

    qst_event_t c =
        ev(10, QST_SRC_FARM, QST_EV_COMPUTE_SHARED, (1u << 20) - 1, QST_VERIFIER_SYSTEM, 2, 1, 0);
    CHECK(qst_record(&W, &c) == QST_ERR_ARG, "less than one unit of compute refused");
    c.qty = 5u << 20;
    CHECK(qst_record(&W, &c) == QST_OK && qst_profile(&W, 10)->points[QST_SKILL_COMPUTE] == 5,
          "farm spot-checked compute: 5 Mi token-cycles = 5 points");
    c = ev(10, QST_SRC_FEED, QST_EV_COMPUTE_SHARED, 5u << 20, QST_VERIFIER_SYSTEM, 3, 1, 0);
    CHECK(qst_record(&W, &c) == QST_ERR_SOURCE, "compute claimed through the feed refused");

    CHECK(qst_level_of(0) == 0 && qst_level_of(7) == 0 && qst_level_of(8) == 1 &&
              qst_level_of(21) == 2 && qst_level_of(2584) == 7 && qst_level_of(999999) == 7,
          "levels on every other Fibonacci number");
    CHECK(qst_skill_of(QST_EV_STORAGE_SHARED) == QST_SKILL_HOSTING &&
              qst_skill_of(QST_EV_TRADE_DELIVERED) == QST_SKILL_TRADE,
          "event kinds map to skills");

    /* Q2: no grind */
    qst_status_t last = QST_OK;
    uint32_t capped = 0;
    for (uint32_t i = 0; i < 30; i++) {
        last = help(11, 10, 5, 0);
        if (last == QST_OK_CAPPED) capped++;
    }
    CHECK(qst_profile(&W, 11)->points[QST_SKILL_MENTOR] == QST_DAILY_CAP,
          "30 helps in one day: mastery stops at the daily cap");
    CHECK(capped >= 23 && last == QST_OK_CAPPED,
          "capped events are still recorded, flagged CAPPED");
    help(11, 10, 6, 0);
    CHECK(qst_profile(&W, 11)->points[QST_SKILL_MENTOR] == QST_DAILY_CAP + 3,
          "the next day counts again");

    /* Q3: no decay */
    uint32_t before = qst_profile(&W, 11)->points[QST_SKILL_MENTOR];
    qst_time_spent(&W, 11, 400, 1);
    help(11, 10, 406, 0);
    CHECK(qst_profile(&W, 11)->points[QST_SKILL_MENTOR] == before + 3,
          "a year away costs nothing: no streak, no decay");

    qst_view_t v;
    CHECK(qst_view(&W, 11, 406, &v) == QST_OK, "view");
    const qst_skill_view_t *sv = &v.skill[QST_SKILL_MENTOR];
    CHECK(sv->level == 2 && sv->points == 27 && sv->level_at == 21 && sv->next_at == 55 &&
              sv->permille == 176 && sv->counted_today == 3 && sv->daily_cap == QST_DAILY_CAP,
          "calm view: level, progress to next, what counted today");

    /* fraud */
    uint8_t e1[32];
    evid(e1, 10 * 7919u + QST_EV_PEER_HELPED, 1);
    CHECK(qst_reject_evidence(&W, QST_EV_PEER_HELPED, e1) == QST_OK &&
              qst_profile(&W, 10)->points[QST_SKILL_MENTOR] == 0 &&
              qst_profile(&W, 10)->n_verifiers[QST_SKILL_MENTOR] == 0,
          "fraudulent evidence: points and verifier removed");
    CHECK(qst_reject_evidence(&W, QST_EV_PEER_HELPED, e1) == QST_ERR_STATE, "reject once");
    CHECK(qst_record(&W, &e) == QST_ERR_DUPLICATE, "rejected evidence cannot be re-claimed");
}

static void test_budget_minors(void)
{
    printf("\n=== time well spent, minors ===\n");
    n_ages = 0;
    ages[n_ages].subject = 30;
    ages[n_ages].known = true;
    ages[n_ages].pol.is_minor = true;
    ages[n_ages].pol.max_daily_minutes = 30;
    ages[n_ages].pol.hard_stop = true;
    ages[n_ages].pol.allow_money = false;
    ages[n_ages].pol.custodian_acct = 0;
    ages[n_ages].pol.public_badges = false;
    n_ages++;
    ages[n_ages].subject = 31;
    ages[n_ages].known = false;
    n_ages++;
    qst_init(&W, age_hook, 0, 0);
    qst_enrol(&W, 20);
    qst_enrol(&W, 30);
    qst_enrol(&W, 31);

    CHECK(qst_time_spent(&W, 20, 1, 500) == QST_HINT_NONE, "no budget set: no hints at all");
    qst_set_budget(&W, 20, 60);
    CHECK(qst_time_spent(&W, 20, 2, 47) == QST_HINT_NONE, "47 of 60: quiet");
    CHECK(qst_time_spent(&W, 20, 2, 1) == QST_HINT_NEARING, "48 of 60: gentle hint");
    CHECK(qst_time_spent(&W, 20, 2, 5) == QST_HINT_NONE, "the hint is not repeated");
    CHECK(qst_time_spent(&W, 20, 2, 7) == QST_HINT_REACHED, "60 of 60: reached, once");
    CHECK(qst_time_spent(&W, 20, 2, 30) == QST_HINT_NONE, "an adult is never blocked or nagged");
    CHECK(qst_time_spent(&W, 20, 3, 10) == QST_HINT_NONE, "a new day starts fresh");

    const qst_profile_t *k = qst_profile_c(&W, 30);
    CHECK(k->policy.is_minor && k->budget_minutes == 30, "minor gets the guardian's ceiling");
    qst_set_budget(&W, 30, 120);
    CHECK(qst_profile_c(&W, 30)->budget_minutes == 30, "a minor cannot raise past the ceiling");
    qst_set_budget(&W, 30, 20);
    CHECK(qst_profile_c(&W, 30)->budget_minutes == 20, "a minor may choose less");
    CHECK(qst_time_spent(&W, 30, 2, 20) == QST_HINT_STOP &&
              qst_time_spent(&W, 30, 2, 1) == QST_HINT_STOP,
          "minor with hard stop: STOP holds for the rest of the day");
    const qst_profile_t *u = qst_profile_c(&W, 31);
    CHECK(u->policy.is_minor && !u->policy.allow_money && !u->policy.public_badges,
          "age unknown with a hook installed: minor protections apply");
    ages[1].known = true;
    ages[1].pol.is_minor = false;
    ages[1].pol.allow_money = true;
    ages[1].pol.public_badges = true;
    qst_refresh_policy(&W, 31);
    CHECK(!qst_profile_c(&W, 31)->policy.is_minor, "policy refresh once age is confirmed");
}

static zxn_note_t slots[16];
static zxn_bus_t bus;
static uint32_t delivered;
static void sub(void *ctx, const zxn_note_t *n, bool co)
{
    (void) ctx;
    (void) co;
    if (n->kind == ZXN_ECONOMY && n->pri == ZXN_PRI_LOW) delivered++;
}

static void test_coop_notify_optout(void)
{
    printf("\n=== co-op goals, seasons, requested notifications, opt-out ===\n");
    zxn_init(&bus, slots, 16);
    zxn_subscribe(&bus, sub, 0, ZXN_ALL_KINDS, ZXN_PRI_LOW);
    qst_init(&W, 0, 0, &bus);
    for (uint32_t s = 40; s < 44; s++) qst_enrol(&W, s);

    qst_mechanic_t bad;
    qst_mechanic_plain(&bad, QST_RW_STANDING);
    bad.loss_on_lapse = true;
    CHECK(qst_goal_create(&W, 1, "Raise storage", QST_EV_BIT(QST_EV_STORAGE_SHARED), 10, 0, 0,
                          &bad) == QST_ERR_GUARD,
          "a goal with a streak-loss mechanic is refused");
    CHECK(qst_goal_create(&W, 1, "Last chance to join!", QST_EV_BIT(QST_EV_STORAGE_SHARED), 10, 0,
                          0, 0) == QST_ERR_GUARD,
          "a goal titled with pressure copy is refused");

    int32_t g = qst_goal_create(&W, 1, "Co-op storage: 10 GiB-hours",
                                QST_EV_BIT(QST_EV_STORAGE_SHARED), 10, 0, 0, 0);
    CHECK(g > 0, "co-op capacity goal created");
    qst_goal_join(&W, (uint32_t) g, 40);
    qst_goal_join(&W, (uint32_t) g, 41);
    qst_goal_join(&W, (uint32_t) g, 42);
    CHECK(qst_notify_request(&W, 43, (uint32_t) g) == QST_ERR_STATE,
          "only members may ask to be told");
    CHECK(qst_notify_request(&W, 40, (uint32_t) g) == QST_OK,
          "member asks to be told on completion");

    qst_event_t s1 = ev(43, QST_SRC_FARM, QST_EV_STORAGE_SHARED, 4ull << 30, QST_VERIFIER_SYSTEM, 1,
                        1, (uint32_t) g);
    qst_record(&W, &s1);
    CHECK(qst_goal_c(&W, (uint32_t) g)->progress == 0, "a non-member's work does not count");
    qst_event_t s2 = ev(41, QST_SRC_FARM, QST_EV_COMPUTE_SHARED, 4ull << 20, QST_VERIFIER_SYSTEM, 1,
                        1, (uint32_t) g);
    qst_record(&W, &s2);
    CHECK(qst_goal_c(&W, (uint32_t) g)->progress == 0, "a kind outside the goal does not count");

    qst_opt_out(&W, 42);
    qst_event_t s3 = ev(42, QST_SRC_DEVMESH, QST_EV_STORAGE_SHARED, 3ull << 30, QST_VERIFIER_SYSTEM,
                        2, 1, (uint32_t) g);
    CHECK(qst_record(&W, &s3) == QST_OK_NOT_TRACKED &&
              qst_profile(&W, 42)->points[QST_SKILL_HOSTING] == 0 &&
              qst_goal_c(&W, (uint32_t) g)->progress == 3,
          "opted out: no mastery tracked, real work still counts for the co-op");
    qst_view_t v;
    qst_view(&W, 42, 1, &v);
    CHECK(v.opted_out && v.n_goals == 0 && v.skill[QST_SKILL_HOSTING].points == 0,
          "opted out: the view shows no game");

    qst_event_t s4 = ev(40, QST_SRC_FARM, QST_EV_STORAGE_SHARED, 5ull << 30, QST_VERIFIER_SYSTEM, 3,
                        1, (uint32_t) g);
    qst_record(&W, &s4);
    qst_view(&W, 40, 1, &v);
    CHECK(v.n_goals == 1 && v.goal[0].progress_permille == 800 &&
              v.goal[0].my_share_permille == 625,
          "view: goal 80% done, my share 5 of 8 units");
    CHECK(delivered == 0, "nothing posted before completion");
    qst_event_t s5 = ev(41, QST_SRC_FARM, QST_EV_STORAGE_SHARED, 2ull << 30, QST_VERIFIER_SYSTEM, 4,
                        1, (uint32_t) g);
    qst_record(&W, &s5);
    CHECK(qst_goal_c(&W, (uint32_t) g)->state == QST_GOAL_COMPLETE, "goal complete");
    CHECK(delivered == 1 && W.notified == 1, "exactly one note: the one member who asked");
    CHECK(qst_profile(&W, 40)->notify_goal[0] == 0, "the request is used once");

    /* fraud un-completes an unpaid goal */
    CHECK(qst_reject_evidence(&W, QST_EV_STORAGE_SHARED, s5.evidence) == QST_OK &&
              qst_goal_c(&W, (uint32_t) g)->state == QST_GOAL_OPEN &&
              qst_goal_c(&W, (uint32_t) g)->progress == 8,
          "rejected evidence re-opens an unpaid goal");

    /* C4 seasons */
    uint32_t pts = qst_profile(&W, 40)->points[QST_SKILL_HOSTING];
    CHECK(qst_season_begin(&W, 90) == 2, "season 2 begins");
    CHECK(qst_goal_c(&W, (uint32_t) g)->state == QST_GOAL_OPEN &&
              qst_goal_c(&W, (uint32_t) g)->progress == 8,
          "unfinished goal carries over with its progress: no deadline");
    CHECK(qst_profile(&W, 40)->points[QST_SKILL_HOSTING] == pts, "season change keeps standing");
    qst_event_t s6 = ev(41, QST_SRC_FARM, QST_EV_STORAGE_SHARED, 2ull << 30, QST_VERIFIER_SYSTEM, 5,
                        91, (uint32_t) g);
    qst_record(&W, &s6);
    CHECK(qst_goal_c(&W, (uint32_t) g)->state == QST_GOAL_COMPLETE &&
              qst_goal_c(&W, (uint32_t) g)->season_completed == 2,
          "completed in season 2");
    CHECK(delivered == 1, "no unrequested note on the second completion");
    uint32_t acct[QST_GOAL_MEMBERS] = {0};
    pay_rat_t cap = {8, 21};
    CHECK(qst_goal_settle(&W, (uint32_t) g, &L, acct, 0, cap, 1, 1, 0) == QST_OK &&
              qst_goal_c(&W, (uint32_t) g)->state == QST_GOAL_SETTLED,
          "standing-only goal settles with no money");
    qst_season_begin(&W, 180);
    CHECK(qst_goal_c(&W, (uint32_t) g)->state == QST_GOAL_ARCHIVED, "settled goal archived");
    qst_season_begin(&W, 270);
    CHECK(qst_goal_c(&W, (uint32_t) g) == 0, "slot freed a season later");

    qst_opt_in(&W, 42);
    qst_view(&W, 42, 1, &v);
    CHECK(!v.opted_out && v.contributed_units == 3, "opt back in: everything is still there");
}

static uint64_t bal(uint32_t a)
{
    return pay_ledger_account(&L, a)->debit;
}

static void test_pay(void)
{
    printf("\n=== shared VFV rewards through pay ===\n");
    n_ages = 0;
    ages[n_ages].subject = 53;
    ages[n_ages].known = true;
    ages[n_ages].pol.is_minor = true;
    ages[n_ages].pol.allow_money = false;
    n_ages++;
    qst_init(&W, age_hook, 0, 0);
    pay_ledger_init(&L, 0);
    uint32_t issuer, pool, fk[PAY_ASSURE_BUCKETS], custodian, acct[QST_GOAL_MEMBERS] = {0};
    pay_ledger_open(&L, 1, L.vfv_asset, PAY_CAP_FINANCIAL, PAY_ACCT_ISSUER, &issuer);
    pay_ledger_open(&L, 2, L.vfv_asset, PAY_CAP_FINANCIAL, 0, &pool);
    for (int b = 0; b < PAY_ASSURE_BUCKETS; b++)
        pay_ledger_open(&L, 30 + (uint32_t) b, L.vfv_asset, PAY_CAP_FINANCIAL, PAY_ACCT_COMMONS,
                        &fk[b]);
    pay_ledger_open(&L, 4, L.vfv_asset, PAY_CAP_FINANCIAL, 0, &custodian);
    pay_posting_req_t req;
    memset(&req, 0, sizeof req);
    uint8_t rnd[16] = {1, 2, 3};
    pay_uetr_from_random(rnd, req.uetr);
    strcpy(req.e2e, "FUND-POOL-1");
    req.idem_key[0] = 0xF0;
    pay_receipt_t rc;
    CHECK(pay_ledger_issue(&L, &req, issuer, pool, 100000, &rc) == PAY_OK,
          "sponsor funds the pool");

    int32_t g = qst_goal_create(&W, 7, "Syndicate: verify 100 jobs",
                                QST_EV_BIT(QST_EV_CHECK_PASSED), 100, pool, 10000, 0);
    CHECK(g > 0, "syndicate goal with a VFV pool");
    uint32_t who[4] = {50, 51, 52, 53};
    uint32_t units[4] = {40, 30, 20, 10};
    for (uint32_t i = 0; i < 4; i++) {
        qst_enrol(&W, who[i]);
        qst_goal_join(&W, (uint32_t) g, who[i]);
        pay_ledger_open(&L, who[i], L.vfv_asset, PAY_CAP_FINANCIAL, 0, &acct[i]);
    }
    qst_opt_out(&W, 51);
    for (uint32_t i = 0; i < 4; i++) {
        qst_event_t e =
            ev(who[i], QST_SRC_MARKET, QST_EV_CHECK_PASSED, units[i], 99, 1, 1, (uint32_t) g);
        qst_record(&W, &e);
    }
    CHECK(qst_goal_c(&W, (uint32_t) g)->state == QST_GOAL_COMPLETE, "goal complete");

    pay_rat_t cap = {8, 21};
    qst_payout_plan_t p1, p2;
    CHECK(qst_payout_plan(&W, (uint32_t) g, acct, cap, &p1) == QST_OK, "plan");
    qst_payout_plan(&W, (uint32_t) g, acct, cap, &p2);
    CHECK(memcmp(&p1, &p2, sizeof p1) == 0, "the split is deterministic: no chance anywhere");
    uint64_t sum = p1.unallocated;
    bool cap_ok = true, fee_ok = true;
    for (uint32_t i = 0; i < p1.n; i++) {
        sum += p1.line[i].gross;
        cap_ok = cap_ok && p1.line[i].gross <= 3809;
        fee_ok = fee_ok && p1.line[i].fee == pay_assure_fee(p1.line[i].gross) &&
                 p1.line[i].net + p1.line[i].fee == p1.line[i].gross;
    }
    CHECK(sum == 10000, "shares plus what nobody may take equal the pool");
    CHECK(cap_ok, "no member above the 8/21 no-monopoly cap");
    CHECK(fee_ok, "each share pays the exact 0.08889% assurance fee");
    CHECK(p1.line[0].gross == 3809 && p1.line[0].gross > p1.line[1].gross &&
              p1.line[1].gross > p1.line[2].gross,
          "shares follow verified units; the largest stops at the cap (3809 of 10000)");
    CHECK(p1.line[3].held && p1.held_total == p1.line[3].gross,
          "minor without a custodian: share held in the pool, not paid to the child");

    CHECK(qst_goal_settle(&W, (uint32_t) g, &L, acct, fk, cap, 5, 2, &p1) == QST_OK,
          "settle through pay_ledger_post");
    CHECK(bal(acct[0]) == p1.line[0].net && bal(acct[1]) == p1.line[1].net &&
              bal(acct[2]) == p1.line[2].net && bal(acct[3]) == 0,
          "members paid net; minor's account untouched");
    CHECK(p1.line[1].net > 0, "the opted-out member is paid exactly the same (nothing lost)");
    CHECK(p1.line[0].fee == 3 && p1.fee_total == p1.line[0].fee + p1.line[1].fee + p1.line[2].fee,
          "fee on 3809 is floor(3.385...) = 3; the held minor's share pays none yet");
    CHECK(bal(fk[0]) + bal(fk[1]) + bal(fk[2]) + bal(fk[3]) == p1.fee_total,
          "the four fee buckets received every fee, exactly");
    CHECK(bal(pool) == 100000 - (10000 - p1.unallocated - p1.held_total),
          "pool paid only what was shared");
    CHECK(pay_ledger_check(&L) && pay_ledger_verify_chain(&L), "ledger invariants and chain hold");

    /* idempotent re-run */
    qst_goal(&W, (uint32_t) g)->state = QST_GOAL_COMPLETE;
    CHECK(qst_goal_settle(&W, (uint32_t) g, &L, acct, fk, cap, 6, 2, 0) == QST_OK &&
              bal(acct[0]) == p1.line[0].net &&
              bal(fk[0]) + bal(fk[1]) + bal(fk[2]) + bal(fk[3]) == p1.fee_total,
          "rerunning a settlement pays nobody twice");
    CHECK(qst_goal_settle(&W, (uint32_t) g, &L, acct, fk, cap, 6, 2, 0) == QST_ERR_STATE,
          "a settled goal cannot be settled again");

    /* fraud after payout is flagged, not silently clawed back */
    qst_event_t e0 = ev(50, QST_SRC_MARKET, QST_EV_CHECK_PASSED, 40, 99, 1, 1, (uint32_t) g);
    qst_reject_evidence(&W, QST_EV_CHECK_PASSED, e0.evidence);
    CHECK(qst_goal_c(&W, (uint32_t) g)->fraud_after_settle, "fraud after payout flagged for pay");

    /* custodian route and VFV-only */
    ages[0].pol.custodian_acct = custodian;
    qst_refresh_policy(&W, 53);
    int32_t g2 =
        qst_goal_create(&W, 7, "Second batch", QST_EV_BIT(QST_EV_CHECK_PASSED), 10, pool, 1000, 0);
    qst_goal_join(&W, (uint32_t) g2, 50);
    qst_goal_join(&W, (uint32_t) g2, 53);
    qst_event_t a = ev(50, QST_SRC_MARKET, QST_EV_CHECK_PASSED, 5, 99, 2, 2, (uint32_t) g2);
    qst_event_t b = ev(53, QST_SRC_MARKET, QST_EV_CHECK_PASSED, 5, 99, 2, 2, (uint32_t) g2);
    qst_record(&W, &a);
    qst_record(&W, &b);
    qst_payout_plan_t p3;
    qst_payout_plan(&W, (uint32_t) g2, acct, cap, &p3);
    CHECK(p3.line[0].gross == p3.line[1].gross, "equal verified work, equal share");
    CHECK(p3.line[1].to_acct == custodian && !p3.line[1].held,
          "minor's share goes to the custodian");

    uint16_t eur;
    pay_ledger_add_fiat(&L, "EUR", 978, 2, &eur);
    uint32_t eur_pool;
    pay_ledger_open(&L, 9, eur, PAY_CAP_FINANCIAL, 0, &eur_pool);
    int32_t g3 =
        qst_goal_create(&W, 7, "Fiat pool", QST_EV_BIT(QST_EV_CHECK_PASSED), 1, eur_pool, 100, 0);
    qst_goal_join(&W, (uint32_t) g3, 50);
    qst_event_t c = ev(50, QST_SRC_MARKET, QST_EV_CHECK_PASSED, 1, 99, 3, 3, (uint32_t) g3);
    qst_record(&W, &c);
    CHECK(qst_goal_settle(&W, (uint32_t) g3, &L, acct, fk, cap, 7, 2, 0) == QST_ERR_PAY,
          "rewards are VFV only: a fiat pool is refused");
}

static uint8_t pk[QST_MLDSA_PK_LEN], sk[QST_MLDSA_SK_LEN], pk2[QST_MLDSA_PK_LEN],
    sk2[QST_MLDSA_SK_LEN];
static uint8_t sig[QST_MLDSA_SIG_LEN], rsig[QST_MLDSA_SIG_LEN];
static rep_state_t rep;

static void test_badges(void)
{
    printf("\n=== signed, portable, revocable badges (ML-DSA-65) ===\n");
    uint8_t seed[32], seed2[32], kid[32], kid2[32], holder[32];
    for (uint32_t i = 0; i < 32; i++) {
        seed[i] = (uint8_t) i;
        seed2[i] = (uint8_t) (0xA0 + i);
        holder[i] = (uint8_t) (0x11 * (i & 7));
    }
    qst_mldsa_keypair(seed, pk, sk, kid);
    qst_mldsa_keypair(seed2, pk2, sk2, kid2);
    qst_mldsa_key_t key = {sk, 0}, key2 = {sk2, 0};
    qst_mldsa_dir_t dir = {{pk, pk2}, 2};

    n_ages = 0;
    ages[n_ages].subject = 61;
    ages[n_ages].known = true;
    ages[n_ages].pol.is_minor = true;
    ages[n_ages].pol.public_badges = false;
    n_ages++;
    qst_init(&W, age_hook, 0, 0);
    qst_enrol(&W, 60);
    qst_enrol(&W, 61);
    for (uint32_t d = 1; d <= 3; d++) help(60, 70, d, 0); /* 9 points, one verifier */
    CHECK(qst_badge_eligible(&W, 60, QST_SKILL_MENTOR, 1), "level 1 with one verifier");
    for (uint32_t d = 4; d <= 8; d++) help(60, 70, d, 0); /* 24 points */
    CHECK(qst_level_of(qst_profile(&W, 60)->points[QST_SKILL_MENTOR]) == 2 &&
              !qst_badge_eligible(&W, 60, QST_SKILL_MENTOR, 2),
          "level 2 points from one verifier are not enough for a level-2 badge");
    help(60, 71, 9, 0);
    CHECK(qst_badge_eligible(&W, 60, QST_SKILL_MENTOR, 2), "a second, distinct verifier: eligible");
    CHECK(!qst_badge_eligible(&W, 60, QST_SKILL_MENTOR, 3), "level 3 not reached");

    qst_badge_t b, d;
    uint32_t sl = 0;
    CHECK(qst_badge_issue(&W, 60, QST_SKILL_MENTOR, 3, holder, kid, 9, 1, qst_mldsa_sign, &key, &b,
                          sig, sizeof sig, &sl) == QST_ERR_NOT_ELIGIBLE,
          "no badge for what was not earned");
    CHECK(qst_badge_issue(&W, 60, QST_SKILL_MENTOR, 2, holder, kid, 9, 1, qst_mldsa_sign, &key, &b,
                          sig, sizeof sig, &sl) == QST_OK &&
              sl == QST_MLDSA_SIG_LEN,
          "badge issued and signed with ML-DSA-65");
    uint8_t enc[QST_BADGE_ENC_LEN];
    qst_badge_encode(&b, enc);
    CHECK(qst_badge_decode(enc, sizeof enc, &d) && memcmp(&d, &b, sizeof b) == 0,
          "portable: encode/decode round trip");
    CHECK(qst_badge_check(&d, sig, sl, qst_mldsa_verify, &dir, 0) == QST_BADGE_VALID,
          "anyone with the issuer key verifies it offline");
    uint8_t root[32];
    qst_evidence_root(&W, 60, QST_SKILL_MENTOR, root);
    CHECK(memcmp(root, b.evidence_root, 32) == 0, "evidence root recomputes from the records");
    d.level = 5;
    CHECK(qst_badge_check(&d, sig, sl, qst_mldsa_verify, &dir, 0) == QST_BADGE_BAD_SIG,
          "an edited level fails the signature");
    enc[2] = 0;
    CHECK(!qst_badge_decode(enc, sizeof enc, &d), "level 0 record refused on decode");
    qst_badge_t e = b;
    memcpy(e.issuer_key, kid2, 32);
    CHECK(qst_badge_check(&e, sig, sl, qst_mldsa_verify, &dir, 0) == QST_BADGE_BAD_SIG,
          "a signature does not transfer to another issuer");

    CHECK(qst_badge_to_reputation(&rep, &b, sig, sl, qst_mldsa_verify, &dir, 0) == 2 &&
              badge_has(&rep, 60, QST_REP_BADGE_BASE + QST_SKILL_MENTOR, 2),
          "valid public badge shown through reputation");

    /* revocation */
    qst_revlist_t rl;
    qst_revlist_init(&rl);
    qst_revocation_t r;
    memset(&r, 0, sizeof r);
    qst_badge_digest(&b, r.badge_digest);
    memcpy(r.issuer_key, kid, 32);
    r.reason = QST_REVOKE_FRAUD;
    r.day = 20;
    uint8_t rd[32];
    uint32_t rl_len = 0;
    qst_revocation_digest(&r, rd);
    qst_mldsa_sign(&key2, rd, rsig, sizeof rsig, &rl_len);
    CHECK(!qst_revlist_add(&rl, &r, rsig, rl_len, qst_mldsa_verify, &dir) && rl.n == 0,
          "a revocation signed by someone else is refused");
    qst_mldsa_sign(&key, rd, rsig, sizeof rsig, &rl_len);
    CHECK(qst_revlist_add(&rl, &r, rsig, rl_len, qst_mldsa_verify, &dir) && rl.n == 1,
          "issuer-signed revocation accepted");
    CHECK(qst_revlist_add(&rl, &r, rsig, rl_len, qst_mldsa_verify, &dir) && rl.n == 1,
          "revocation is idempotent");
    CHECK(qst_badge_check(&b, sig, sl, qst_mldsa_verify, &dir, &rl) == QST_BADGE_REVOKED,
          "revoked badge reports REVOKED");
    CHECK(qst_badge_to_reputation(&rep, &b, sig, sl, qst_mldsa_verify, &dir, &rl) == -1,
          "revoked badge is not mirrored");

    /* fraud un-backs */
    CHECK(qst_badge_still_backed(&W, &b), "badge backed by its evidence");
    uint8_t ev71[32];
    evid(ev71, 60 * 7919u + QST_EV_PEER_HELPED, serial_ctr - 1);
    qst_reject_evidence(&W, QST_EV_PEER_HELPED, ev71);
    CHECK(!qst_badge_still_backed(&W, &b), "rejected evidence leaves the badge unbacked");

    /* opt-out and minors */
    qst_opt_out(&W, 60);
    CHECK(qst_badge_issue(&W, 60, QST_SKILL_MENTOR, 1, holder, kid, 21, 2, qst_mldsa_sign, &key, &b,
                          sig, sizeof sig, &sl) == QST_ERR_NOT_ELIGIBLE,
          "no badges issued while opted out");
    qst_opt_in(&W, 60);
    CHECK(qst_badge_eligible(&W, 60, QST_SKILL_MENTOR, 1), "opt back in: eligibility returns");
    for (uint32_t dd = 1; dd <= 3; dd++) help(61, 70, dd, 0);
    CHECK(qst_badge_issue(&W, 61, QST_SKILL_MENTOR, 1, holder, kid, 3, 3, qst_mldsa_sign, &key, &b,
                          sig, sizeof sig, &sl) == QST_OK &&
              !b.is_public,
          "a minor's badge is issued non-public");
    CHECK(qst_badge_to_reputation(&rep, &b, sig, sl, qst_mldsa_verify, &dir, 0) == -1,
          "a minor's badge is not shown publicly");
}

int main(void)
{
    rep_init(&rep);
    test_guard();
    test_mastery();
    test_budget_minors();
    test_coop_notify_optout();
    test_pay();
    test_badges();
    printf("\nquest: %d passed, %d failed\n", pass, fail);
    return fail ? 1 : 0;
}
