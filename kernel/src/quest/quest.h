/* Copyright (c) 2024-2026 Michael Laurence Curzi and 36N9 Genetics, LLC */
/* SPDX-License-Identifier: Apache-2.0 */
/* quest.h — engagement through mastery, cooperation and real standing.
 *
 * WHAT THIS IS
 * ------------
 * The game layer of ZXV, built on self-determination theory: people stay
 * because they are getting better at something (mastery), working with
 * others (relatedness) and choosing what to do (autonomy). It is never
 * built on compulsion. docs/ENGAGEMENT.md has the design and the research.
 *
 *   Q1  MASTERY FROM REAL CONTRIBUTION. Eight skills grow only from
 *       VERIFIED contributions that other modules report as events:
 *       sharing compute, storage or bandwidth (pay_farm, devmesh), helping
 *       a peer (the peer confirms), reviews that others endorse (feed,
 *       market), corrections the assistant accepted, accepted translations
 *       (i18n), verification work whose result matched, and trades
 *       delivered as promised (market). An event must name a verifier
 *       other than the contributor (QST_VERIFIER_SYSTEM for an automated
 *       check such as a pay_farm spot check), must come from a source
 *       allowed for its kind, and its evidence id may be claimed once.
 *   Q2  NO GRIND. At most QST_DAILY_CAP mastery points per skill per day
 *       count toward a level. Work past the cap still counts for co-op
 *       goals and is still paid through pay, so nobody loses by stopping,
 *       and nobody gains a level by marathoning.
 *   Q3  LEVELS NEVER DECAY. Thresholds are every other Fibonacci number
 *       (0, 8, 21, 55, 144, 377, 987, 2584). Being away for a day or a year
 *       costs nothing: there are no streaks anywhere in this module. The
 *       only thing that lowers a level is evidence found to be fraudulent
 *       (qst_reject_evidence).
 *   Q4  TIME WELL SPENT. Each person may set a daily minute budget (0 = no
 *       budget). qst_time_spent returns a gentle hint at 80% and at 100%,
 *       each at most once a day. A hint never blocks and never threatens;
 *       the text says progress is saved. For a minor whose policy asks for
 *       it, the 100% hint is QST_HINT_STOP and the UI ends the session.
 *   Q5  MINORS. An optional age-policy hook (identity / guardian settings)
 *       is asked at enrolment and on qst_refresh_policy. If a hook is
 *       installed and cannot say, the person is treated as a minor. Minors
 *       get a budget no larger than the guardian's ceiling, no money paid
 *       to them directly (quest_pay routes it to a custodian account or
 *       holds it), and badges that are not shown publicly.
 *   Q6  FULL OPT-OUT, NOTHING LOST. qst_opt_out turns the game layer off:
 *       no mastery tracking, no hints, no goals in the view, no badges
 *       issued. Earned levels and badges are kept exactly as they were, and
 *       verified work keeps counting for co-op goals and keeps being paid,
 *       because that is money owed, not a game. qst_opt_in resumes.
 *   Q7  NOTIFICATIONS ONLY ON REQUEST. Quest posts to the zx_notify bus in
 *       exactly one case: a person asked to be told when a goal they belong
 *       to completes (qst_notify_request). The request is used once. Quest
 *       has no "come back" message, no reminder and no re-engagement path.
 *   Q8  INPUTS FROM OTHER MODULES. feed, market, devmesh, farm, assistant
 *       and i18n call qst_record with a qst_event_t. Quest never reaches
 *       into them.
 *   Q9  A CALM PROGRESS VIEW. qst_view fills a plain data model for the UI:
 *       levels, progress to the next level, what counted today, goals and
 *       the time budget. It has no rank, no streak, no countdown and no
 *       unread badge counter, on purpose.
 *
 * quest_guard.h holds the guardrails, quest_coop.h the cooperative goals
 * and seasons, quest_pay.h the shared VFV rewards (through pay only),
 * quest_badge.h the signed, portable, revocable badges.
 *
 * HONEST LIMITS. Quest trusts the verifier named on an event: a verifier
 * colluding with a contributor can mint points, which is why badges need
 * several distinct verifiers and fraud can be rejected later. The record
 * table is bounded (QST_MAX_RECORDS); when full, new events are refused
 * until the operator archives a season. Days come from the caller (a
 * local day number), never a wall clock, so a caller that lies about the
 * day can stretch the daily cap. Not thread-safe. Freestanding C11: no
 * libc, no allocation, no floating point, no 64-bit division.
 */
#ifndef ZXV_QUEST_H
#define ZXV_QUEST_H

#include <stdint.h>
#include <stdbool.h>
#include "quest_guard.h"
#include "zx_notify.h"

#define QST_MAX_PROFILES    64u
#define QST_MAX_RECORDS     1024u
#define QST_MAX_GOALS       32u
#define QST_GOAL_MEMBERS    16u
#define QST_VERIFIER_SET    8u  /* distinct verifiers remembered per skill */
#define QST_NOTIFY_REQ      4u  /* open notification requests per person   */
#define QST_DAILY_CAP       21u /* mastery points per skill per day (Q2)   */
#define QST_LEVELS          8u
#define QST_POINTS_MAX      1000000u
#define QST_TITLE_MAX       48u
#define QST_VIEW_GOALS      8u
#define QST_VERIFIER_SYSTEM 0xFFFFFFFFu /* an automated check vouched for it */

typedef enum {
    QST_SKILL_COMPUTE = 0, /* sharing compute                     */
    QST_SKILL_HOSTING,     /* sharing storage and bandwidth       */
    QST_SKILL_MENTOR,      /* helping peers                       */
    QST_SKILL_REVIEW,      /* reviews others found helpful        */
    QST_SKILL_TEACH,       /* teaching the assistant              */
    QST_SKILL_TRANSLATE,   /* accepted translations               */
    QST_SKILL_VERIFY,      /* checking other people's work        */
    QST_SKILL_TRADE,       /* trades delivered as promised        */
    QST_SKILL_COUNT
} qst_skill_t;

typedef enum {
    QST_SRC_FEED = 0,
    QST_SRC_MARKET,
    QST_SRC_DEVMESH,
    QST_SRC_FARM,
    QST_SRC_ASSISTANT,
    QST_SRC_I18N,
    QST_SRC_PEER,
    QST_SRC_COUNT
} qst_source_t;

typedef enum {
    QST_EV_COMPUTE_SHARED = 0,   /* qty token-cycles; unit 2^20       */
    QST_EV_STORAGE_SHARED,       /* qty byte-hours;   unit 2^30       */
    QST_EV_BANDWIDTH_SHARED,     /* qty bytes;        unit 2^30       */
    QST_EV_PEER_HELPED,          /* qty helps; verifier = the peer    */
    QST_EV_REVIEW_ENDORSED,      /* qty endorsements by others        */
    QST_EV_ASSISTANT_TAUGHT,     /* qty accepted corrections          */
    QST_EV_TRANSLATION_ACCEPTED, /* qty accepted strings              */
    QST_EV_CHECK_PASSED,         /* qty checks whose result matched   */
    QST_EV_TRADE_DELIVERED,      /* qty deliveries the buyer confirmed */
    QST_EV_COUNT
} qst_ev_kind_t;

#define QST_EV_BIT(k) (1u << (uint32_t) (k))

typedef struct {
    uint32_t subject;     /* who contributed                                */
    uint8_t source;       /* qst_source_t                                   */
    uint8_t kind;         /* qst_ev_kind_t                                  */
    uint64_t qty;         /* in the kind's native measure                   */
    uint8_t evidence[32]; /* id of the verified record upstream (job id,    */
                          /* post id, review id): claimable once per kind   */
    uint32_t verifier;    /* who confirmed it; 0 = unverified (refused)     */
    uint32_t goal;        /* co-op goal to credit, 0 = none                 */
    uint32_t day;         /* caller's local day number                      */
} qst_event_t;

typedef enum {
    QST_OK = 0,
    QST_OK_CAPPED = 1,      /* recorded; today's mastery cap already reached */
    QST_OK_NOT_TRACKED = 2, /* opted out: co-op credited, no mastery         */
    QST_ERR_ARG = -1,
    QST_ERR_UNVERIFIED = -2,
    QST_ERR_SELF_VERIFIED = -3,
    QST_ERR_SOURCE = -4,
    QST_ERR_DUPLICATE = -5,
    QST_ERR_FULL = -6,
    QST_ERR_NO_PROFILE = -7,
    QST_ERR_GUARD = -8,
    QST_ERR_NOT_FOUND = -9,
    QST_ERR_STATE = -10,
    QST_ERR_NOT_ELIGIBLE = -11,
    QST_ERR_PAY = -12
} qst_status_t;

/* Q5: what the guardian / identity layer says about a person. */
typedef struct {
    bool is_minor;
    uint32_t max_daily_minutes; /* guardian ceiling; 0 = none            */
    bool hard_stop;             /* QST_HINT_STOP when the budget is spent */
    bool allow_money;           /* VFV may be paid to the person directly */
    uint32_t custodian_acct;    /* pay account for withheld money; 0 = hold */
    bool public_badges;         /* badges may be shown publicly           */
} qst_age_policy_t;

/* Fill *out and return true, or return false when the age is unknown
 * (quest then applies the minor policy with no custodian). */
typedef bool (*qst_age_fn)(void *ctx, uint32_t subject, qst_age_policy_t *out);

typedef enum {
    QST_HINT_NONE = 0,
    QST_HINT_NEARING = 1, /* 80% of the budget, once a day          */
    QST_HINT_REACHED = 2, /* 100%, once a day                       */
    QST_HINT_STOP = 3     /* 100% for a minor with hard_stop        */
} qst_hint_t;

typedef struct {
    bool used;
    uint32_t subject;
    bool opted_out;
    qst_age_policy_t policy;
    uint32_t points[QST_SKILL_COUNT];
    uint32_t day_points[QST_SKILL_COUNT];
    uint32_t points_day;
    uint32_t verifier[QST_SKILL_COUNT][QST_VERIFIER_SET];
    uint8_t n_verifiers[QST_SKILL_COUNT];
    uint64_t contributed_units; /* lifetime verified units, all kinds   */
    /* Q4 */
    uint32_t budget_minutes;
    uint32_t used_minutes;
    uint32_t budget_day;
    uint8_t hints_given; /* bit 0 nearing, bit 1 reached, for budget_day */
    /* Q7 */
    uint32_t notify_goal[QST_NOTIFY_REQ];
    uint32_t seasons_active;
    uint32_t last_season;
} qst_profile_t;

typedef struct {
    bool used;
    bool rejected;
    uint32_t subject;
    uint8_t kind, skill;
    uint32_t points; /* mastery points this record added       */
    uint64_t units;  /* co-op units this record added          */
    uint32_t goal;
    uint32_t verifier;
    uint8_t evidence[32];
} qst_record_t;

typedef enum {
    QST_GOAL_OPEN = 0,
    QST_GOAL_COMPLETE = 1,
    QST_GOAL_SETTLED = 2,
    QST_GOAL_ARCHIVED = 3
} qst_goal_state_t;

typedef struct {
    bool used;
    uint32_t id;
    uint32_t group; /* co-op or syndicate id */
    char title[QST_TITLE_MAX];
    uint32_t ev_mask; /* QST_EV_BIT kinds that count */
    uint64_t target, progress;
    uint32_t member[QST_GOAL_MEMBERS];
    uint64_t share_units[QST_GOAL_MEMBERS];
    uint32_t n_members;
    uint32_t pool_acct;   /* VFV pay account funding the shared reward; 0 = none */
    uint64_t pool_amount; /* VFV minor units to share on completion        */
    uint8_t state;        /* qst_goal_state_t */
    uint32_t season_opened, season_completed;
    bool fraud_after_settle; /* evidence rejected after payout: reverse in pay */
    bool settle_started;     /* quest_pay P3: first attempt's tick and initiator */
    uint64_t settle_tick;    /* are reused on reruns so they stay idempotent    */
    uint32_t settle_initiator;
} qst_goal_t;

typedef struct {
    qst_profile_t profile[QST_MAX_PROFILES];
    uint32_t n_profiles;
    qst_record_t record[QST_MAX_RECORDS];
    uint32_t n_records;
    qst_goal_t goal[QST_MAX_GOALS];
    uint32_t next_goal_id;
    uint32_t season;
    uint32_t season_start_day;
    qst_age_fn age;
    void *age_ctx;
    zxn_bus_t *bus; /* optional; used only for requested results (Q7) */
    uint32_t notified;
} qst_world_t;

/* ---- lifecycle ---- */
void qst_init(qst_world_t *w, qst_age_fn age, void *age_ctx, zxn_bus_t *bus);
qst_status_t qst_enrol(qst_world_t *w, uint32_t subject);
qst_profile_t *qst_profile(qst_world_t *w, uint32_t subject);
const qst_profile_t *qst_profile_c(const qst_world_t *w, uint32_t subject);
qst_status_t qst_refresh_policy(qst_world_t *w, uint32_t subject);

/* ---- Q1-Q3, Q8: contributions ---- */
qst_status_t qst_record(qst_world_t *w, const qst_event_t *ev);
/* Fraud found later: undo the record's points and goal units. */
qst_status_t qst_reject_evidence(qst_world_t *w, uint8_t kind, const uint8_t evidence[32]);
qst_skill_t qst_skill_of(qst_ev_kind_t kind);
uint32_t qst_level_of(uint32_t points);
uint32_t qst_level_threshold(uint32_t level); /* points needed; level < QST_LEVELS */
const char *qst_skill_name(qst_skill_t s);

/* ---- Q4 ---- */
qst_status_t qst_set_budget(qst_world_t *w, uint32_t subject, uint32_t minutes);
qst_hint_t qst_time_spent(qst_world_t *w, uint32_t subject, uint32_t day, uint32_t minutes);
const char *qst_hint_text(qst_hint_t h);

/* ---- Q6 ---- */
qst_status_t qst_opt_out(qst_world_t *w, uint32_t subject);
qst_status_t qst_opt_in(qst_world_t *w, uint32_t subject);

/* ---- Q7 ---- */
qst_status_t qst_notify_request(qst_world_t *w, uint32_t subject, uint32_t goal);
qst_status_t qst_notify_cancel(qst_world_t *w, uint32_t subject, uint32_t goal);

/* ---- Q9: the calm progress view ---- */
typedef struct {
    uint8_t level;
    uint32_t points;
    uint32_t level_at, next_at; /* next_at == level_at at the top level */
    uint16_t permille;          /* progress from level_at to next_at    */
    uint32_t counted_today;
    uint32_t daily_cap;
    uint32_t verifiers; /* distinct verifiers seen (up to QST_VERIFIER_SET) */
} qst_skill_view_t;

typedef struct {
    uint32_t id;
    char title[QST_TITLE_MAX];
    uint16_t progress_permille;
    uint16_t my_share_permille;
    uint8_t state;
    uint32_t members;
} qst_goal_view_t;

typedef struct {
    bool opted_out;
    bool minor;
    uint32_t season;
    qst_skill_view_t skill[QST_SKILL_COUNT];
    qst_goal_view_t goal[QST_VIEW_GOALS];
    uint32_t n_goals;
    uint32_t budget_minutes, used_minutes;
    uint64_t contributed_units;
    /* Deliberately absent: rank, streak, countdown, unread counter. */
} qst_view_t;

qst_status_t qst_view(const qst_world_t *w, uint32_t subject, uint32_t day, qst_view_t *out);

/* Every mechanic quest itself runs, as declared to the guard. Tests check
 * that each passes qst_guard_check. Returns the count (<= max). */
uint32_t qst_own_mechanics(qst_mechanic_t *out, uint32_t max);

/* internal helpers shared by the quest files */
void qst__zero(void *p, uint32_t n);
void qst__copy(void *d, const void *s, uint32_t n);
bool qst__eq(const void *a, const void *b, uint32_t n);
void qst__strcpy(char *d, const char *s, uint32_t cap);

#endif /* ZXV_QUEST_H */
