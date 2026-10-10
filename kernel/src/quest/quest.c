/* Copyright (c) 2024-2026 Michael Laurence Curzi and 36N9 Genetics, LLC */
/* SPDX-License-Identifier: Apache-2.0 */
#include "quest.h"
#include "quest_coop.h"

/* ===== helpers ===== */

void qst__zero(void *p, uint32_t n)
{
    volatile uint8_t *b = (volatile uint8_t *) p;
    for (uint32_t i = 0; i < n; i++) b[i] = 0;
}

void qst__copy(void *d, const void *s, uint32_t n)
{
    volatile uint8_t *a = (volatile uint8_t *) d;
    const uint8_t *b = (const uint8_t *) s;
    for (uint32_t i = 0; i < n; i++) a[i] = b[i];
}

bool qst__eq(const void *a, const void *b, uint32_t n)
{
    const uint8_t *x = (const uint8_t *) a, *y = (const uint8_t *) b;
    uint8_t d = 0;
    for (uint32_t i = 0; i < n; i++) d |= (uint8_t) (x[i] ^ y[i]);
    return d == 0;
}

void qst__strcpy(char *d, const char *s, uint32_t cap)
{
    if (!d || cap == 0) return;
    uint32_t i = 0;
    if (s)
        for (; i + 1 < cap && s[i]; i++) d[i] = s[i];
    if (s && s[i]) { /* truncated: never end inside a UTF-8 sequence */
        while (i > 0 && ((uint8_t) d[i - 1] & 0xC0u) == 0x80u) i--;
        if (i > 0 && ((uint8_t) d[i - 1] & 0x80u)) i--;
    }
    d[i] = 0;
}

static uint16_t permille(uint64_t num, uint64_t den)
{
    if (den == 0) return 0;
    if (num >= den) return 1000;
    while (den > 0xFFFFFu) {
        den >>= 1;
        num >>= 1;
    }
    if (den == 0) return 0;
    return (uint16_t) (((uint32_t) num * 1000u) / (uint32_t) den);
}

/* ===== the kind table (Q1) ===== */

#define SRC(s) (1u << (uint32_t) (s))

static const struct {
    uint8_t skill;
    uint8_t shift; /* units = qty >> shift */
    uint8_t ppu;   /* mastery points per unit */
    uint32_t sources;
} kinds[QST_EV_COUNT] = {
    [QST_EV_COMPUTE_SHARED] = {QST_SKILL_COMPUTE, 20, 1, SRC(QST_SRC_FARM) | SRC(QST_SRC_DEVMESH)},
    [QST_EV_STORAGE_SHARED] = {QST_SKILL_HOSTING, 30, 1, SRC(QST_SRC_FARM) | SRC(QST_SRC_DEVMESH)},
    [QST_EV_BANDWIDTH_SHARED] = {QST_SKILL_HOSTING, 30, 1,
                                 SRC(QST_SRC_FARM) | SRC(QST_SRC_DEVMESH)},
    [QST_EV_PEER_HELPED] = {QST_SKILL_MENTOR, 0, 3, SRC(QST_SRC_FEED) | SRC(QST_SRC_PEER)},
    [QST_EV_REVIEW_ENDORSED] = {QST_SKILL_REVIEW, 0, 2, SRC(QST_SRC_FEED) | SRC(QST_SRC_MARKET)},
    [QST_EV_ASSISTANT_TAUGHT] = {QST_SKILL_TEACH, 0, 2, SRC(QST_SRC_ASSISTANT)},
    [QST_EV_TRANSLATION_ACCEPTED] = {QST_SKILL_TRANSLATE, 0, 1,
                                     SRC(QST_SRC_I18N) | SRC(QST_SRC_FEED)},
    [QST_EV_CHECK_PASSED] = {QST_SKILL_VERIFY, 0, 2,
                             SRC(QST_SRC_FARM) | SRC(QST_SRC_MARKET) | SRC(QST_SRC_PEER)},
    [QST_EV_TRADE_DELIVERED] = {QST_SKILL_TRADE, 0, 2, SRC(QST_SRC_MARKET)},
};

static const uint32_t thresholds[QST_LEVELS] = {0, 8, 21, 55, 144, 377, 987, 2584};

static const char *const skill_names[QST_SKILL_COUNT] = {"compute",   "hosting",  "mentoring",
                                                         "reviewing", "teaching", "translating",
                                                         "verifying", "trading"};

qst_skill_t qst_skill_of(qst_ev_kind_t kind)
{
    return kind < QST_EV_COUNT ? (qst_skill_t) kinds[kind].skill : QST_SKILL_COUNT;
}

uint32_t qst_level_of(uint32_t points)
{
    uint32_t l = 0;
    while (l + 1 < QST_LEVELS && points >= thresholds[l + 1]) l++;
    return l;
}

uint32_t qst_level_threshold(uint32_t level)
{
    return level < QST_LEVELS ? thresholds[level] : thresholds[QST_LEVELS - 1];
}

const char *qst_skill_name(qst_skill_t s)
{
    return s < QST_SKILL_COUNT ? skill_names[s] : "";
}

/* ===== lifecycle ===== */

void qst_init(qst_world_t *w, qst_age_fn age, void *age_ctx, zxn_bus_t *bus)
{
    if (!w) return;
    qst__zero(w, (uint32_t) sizeof *w);
    w->age = age;
    w->age_ctx = age_ctx;
    w->bus = bus;
    w->season = 1;
    w->next_goal_id = 1;
}

qst_profile_t *qst_profile(qst_world_t *w, uint32_t subject)
{
    if (!w) return 0;
    for (uint32_t i = 0; i < w->n_profiles; i++)
        if (w->profile[i].used && w->profile[i].subject == subject) return &w->profile[i];
    return 0;
}

const qst_profile_t *qst_profile_c(const qst_world_t *w, uint32_t subject)
{
    return qst_profile((qst_world_t *) w, subject);
}

static void clamp_budget(qst_profile_t *p)
{
    const qst_age_policy_t *a = &p->policy;
    if (a->is_minor && a->max_daily_minutes > 0 &&
        (p->budget_minutes == 0 || p->budget_minutes > a->max_daily_minutes))
        p->budget_minutes = a->max_daily_minutes;
}

static void load_policy(qst_world_t *w, qst_profile_t *p)
{
    qst_age_policy_t *a = &p->policy;
    qst__zero(a, (uint32_t) sizeof *a);
    if (!w->age) { /* single-user desktop: no restrictions */
        a->allow_money = true;
        a->public_badges = true;
    } else if (!w->age(w->age_ctx, p->subject, a)) { /* unknown: protect */
        qst__zero(a, (uint32_t) sizeof *a);
        a->is_minor = true;
    } else if (!a->is_minor) {
        a->max_daily_minutes = 0;
        a->hard_stop = false;
    }
    clamp_budget(p);
}

qst_status_t qst_enrol(qst_world_t *w, uint32_t subject)
{
    if (!w || subject == 0 || subject == QST_VERIFIER_SYSTEM) return QST_ERR_ARG;
    if (qst_profile(w, subject)) return QST_OK;
    if (w->n_profiles >= QST_MAX_PROFILES) return QST_ERR_FULL;
    qst_profile_t *p = &w->profile[w->n_profiles++];
    qst__zero(p, (uint32_t) sizeof *p);
    p->used = true;
    p->subject = subject;
    load_policy(w, p);
    return QST_OK;
}

qst_status_t qst_refresh_policy(qst_world_t *w, uint32_t subject)
{
    qst_profile_t *p = qst_profile(w, subject);
    if (!p) return QST_ERR_NO_PROFILE;
    load_policy(w, p);
    return QST_OK;
}

/* ===== contributions ===== */

static qst_record_t *find_record(qst_world_t *w, uint8_t kind, const uint8_t ev[32])
{
    for (uint32_t i = 0; i < w->n_records; i++) {
        qst_record_t *r = &w->record[i];
        if (r->used && r->kind == kind && qst__eq(r->evidence, ev, 32)) return r;
    }
    return 0;
}

static void add_verifier(qst_profile_t *p, uint8_t skill, uint32_t v)
{
    for (uint32_t i = 0; i < p->n_verifiers[skill]; i++)
        if (p->verifier[skill][i] == v) return;
    if (p->n_verifiers[skill] < QST_VERIFIER_SET) p->verifier[skill][p->n_verifiers[skill]++] = v;
}

qst_status_t qst_record(qst_world_t *w, const qst_event_t *ev)
{
    if (!w || !ev || ev->kind >= QST_EV_COUNT || ev->source >= QST_SRC_COUNT) return QST_ERR_ARG;
    qst_profile_t *p = qst_profile(w, ev->subject);
    if (!p) return QST_ERR_NO_PROFILE;
    if (ev->verifier == 0) return QST_ERR_UNVERIFIED;
    if (ev->verifier == ev->subject) return QST_ERR_SELF_VERIFIED;
    if (!(kinds[ev->kind].sources & SRC(ev->source))) return QST_ERR_SOURCE;
    uint64_t units = ev->qty >> kinds[ev->kind].shift;
    if (units == 0) return QST_ERR_ARG; /* below one unit: aggregate upstream */
    if (find_record(w, ev->kind, ev->evidence)) return QST_ERR_DUPLICATE;
    if (w->n_records >= QST_MAX_RECORDS) return QST_ERR_FULL;

    uint8_t skill = kinds[ev->kind].skill;
    qst_record_t *r = &w->record[w->n_records++];
    qst__zero(r, (uint32_t) sizeof *r);
    r->used = true;
    r->subject = ev->subject;
    r->kind = ev->kind;
    r->skill = skill;
    r->verifier = ev->verifier;
    qst__copy(r->evidence, ev->evidence, 32);

    /* co-op credit counts real value, whether or not the game layer is on */
    if (ev->goal && qst__goal_credit(w, ev->goal, ev->subject, ev->kind, units)) {
        r->goal = ev->goal;
        r->units = units;
    }
    p->contributed_units = p->contributed_units + units < p->contributed_units
                               ? ~(uint64_t) 0
                               : p->contributed_units + units;
    if (p->last_season != w->season) {
        p->last_season = w->season;
        p->seasons_active++;
    }

    if (p->opted_out) return QST_OK_NOT_TRACKED;

    /* Q2: daily cap; late events count against the current day */
    if (ev->day > p->points_day) {
        p->points_day = ev->day;
        for (uint32_t s = 0; s < QST_SKILL_COUNT; s++) p->day_points[s] = 0;
    }
    uint32_t want = units > QST_DAILY_CAP ? QST_DAILY_CAP : (uint32_t) units;
    want *= kinds[ev->kind].ppu;
    uint32_t room = QST_DAILY_CAP - p->day_points[skill];
    uint32_t got = want < room ? want : room;
    if (p->points[skill] + got > QST_POINTS_MAX) got = QST_POINTS_MAX - p->points[skill];
    p->day_points[skill] += got;
    p->points[skill] += got;
    r->points = got;
    add_verifier(p, skill, ev->verifier);
    return got < want ? QST_OK_CAPPED : QST_OK;
}

qst_status_t qst_reject_evidence(qst_world_t *w, uint8_t kind, const uint8_t evidence[32])
{
    if (!w || !evidence || kind >= QST_EV_COUNT) return QST_ERR_ARG;
    qst_record_t *r = find_record(w, kind, evidence);
    if (!r) return QST_ERR_NOT_FOUND;
    if (r->rejected) return QST_ERR_STATE;
    r->rejected = true;
    qst_profile_t *p = qst_profile(w, r->subject);
    if (p) {
        p->points[r->skill] = p->points[r->skill] > r->points ? p->points[r->skill] - r->points : 0;
        p->contributed_units =
            p->contributed_units > r->units ? p->contributed_units - r->units : 0;
        /* rebuild the distinct-verifier set from the records that stand */
        p->n_verifiers[r->skill] = 0;
        for (uint32_t i = 0; i < w->n_records; i++) {
            const qst_record_t *q = &w->record[i];
            if (q->used && !q->rejected && q->subject == r->subject && q->skill == r->skill &&
                q->points > 0)
                add_verifier(p, r->skill, q->verifier);
        }
    }
    if (r->goal) qst__goal_uncredit(w, r->goal, r->subject, r->units);
    return QST_OK;
}

/* ===== Q4: time well spent ===== */

qst_status_t qst_set_budget(qst_world_t *w, uint32_t subject, uint32_t minutes)
{
    qst_profile_t *p = qst_profile(w, subject);
    if (!p) return QST_ERR_NO_PROFILE;
    p->budget_minutes = minutes > 24u * 60u ? 24u * 60u : minutes;
    clamp_budget(p);
    return QST_OK;
}

qst_hint_t qst_time_spent(qst_world_t *w, uint32_t subject, uint32_t day, uint32_t minutes)
{
    qst_profile_t *p = qst_profile(w, subject);
    if (!p) return QST_HINT_NONE;
    if (day != p->budget_day) {
        p->budget_day = day;
        p->used_minutes = 0;
        p->hints_given = 0;
    }
    p->used_minutes =
        p->used_minutes + minutes < p->used_minutes ? 0xFFFFFFFFu : p->used_minutes + minutes;
    uint32_t b = p->budget_minutes;
    if (b == 0) return QST_HINT_NONE;
    if (p->used_minutes >= b) {
        if (p->policy.is_minor && p->policy.hard_stop) return QST_HINT_STOP;
        if (p->hints_given & 2u) return QST_HINT_NONE;
        p->hints_given |= 3u;
        return QST_HINT_REACHED;
    }
    if ((uint64_t) p->used_minutes * 5u >= (uint64_t) b * 4u && !(p->hints_given & 1u)) {
        p->hints_given |= 1u;
        return QST_HINT_NEARING;
    }
    return QST_HINT_NONE;
}

const char *qst_hint_text(qst_hint_t h)
{
    switch (h) {
    case QST_HINT_NEARING:
        return "You are close to the time you set for today.";
    case QST_HINT_REACHED:
        return "You have reached the time you set for today. Your progress is saved.";
    case QST_HINT_STOP:
        return "That is today's time. Everything is saved for next time.";
    default:
        return "";
    }
}

/* ===== Q6 ===== */

qst_status_t qst_opt_out(qst_world_t *w, uint32_t subject)
{
    qst_profile_t *p = qst_profile(w, subject);
    if (!p) return QST_ERR_NO_PROFILE;
    p->opted_out = true;
    for (uint32_t i = 0; i < QST_NOTIFY_REQ; i++) p->notify_goal[i] = 0;
    return QST_OK;
}

qst_status_t qst_opt_in(qst_world_t *w, uint32_t subject)
{
    qst_profile_t *p = qst_profile(w, subject);
    if (!p) return QST_ERR_NO_PROFILE;
    p->opted_out = false;
    return QST_OK;
}

/* ===== Q7 ===== */

qst_status_t qst_notify_request(qst_world_t *w, uint32_t subject, uint32_t goal)
{
    qst_profile_t *p = qst_profile(w, subject);
    if (!p) return QST_ERR_NO_PROFILE;
    if (p->opted_out) return QST_ERR_STATE;
    const qst_goal_t *g = qst_goal_c(w, goal);
    if (!g) return QST_ERR_NOT_FOUND;
    if (qst_goal_member(g, subject) < 0 || g->state != QST_GOAL_OPEN) return QST_ERR_STATE;
    for (uint32_t i = 0; i < QST_NOTIFY_REQ; i++)
        if (p->notify_goal[i] == goal) return QST_OK;
    for (uint32_t i = 0; i < QST_NOTIFY_REQ; i++)
        if (p->notify_goal[i] == 0) {
            p->notify_goal[i] = goal;
            return QST_OK;
        }
    return QST_ERR_FULL;
}

qst_status_t qst_notify_cancel(qst_world_t *w, uint32_t subject, uint32_t goal)
{
    qst_profile_t *p = qst_profile(w, subject);
    if (!p) return QST_ERR_NO_PROFILE;
    for (uint32_t i = 0; i < QST_NOTIFY_REQ; i++)
        if (p->notify_goal[i] == goal) p->notify_goal[i] = 0;
    return QST_OK;
}

static uint32_t put_u32(char *out, uint32_t at, uint32_t cap, uint32_t v)
{
    char tmp[10];
    uint32_t n = 0;
    do {
        tmp[n++] = (char) ('0' + v % 10u);
        v /= 10u;
    } while (v && n < sizeof tmp);
    while (n && at + 1 < cap) out[at++] = tmp[--n];
    out[at] = 0;
    return at;
}

void qst__notify_goal_complete(qst_world_t *w, const qst_goal_t *g)
{
    for (uint32_t m = 0; m < g->n_members; m++) {
        qst_profile_t *p = qst_profile(w, g->member[m]);
        if (!p) continue;
        bool asked = false;
        for (uint32_t i = 0; i < QST_NOTIFY_REQ; i++)
            if (p->notify_goal[i] == g->id) {
                p->notify_goal[i] = 0; /* used once */
                asked = true;
            }
        if (!asked || !w->bus || p->opted_out) continue;
        char action[ZXN_ACTION_MAX];
        const char *pre = "quest:goal:";
        uint32_t n = 0;
        while (pre[n]) {
            action[n] = pre[n];
            n++;
        }
        put_u32(action, n, sizeof action, g->id);
        zxn_spec_t s;
        qst__zero(&s, (uint32_t) sizeof s);
        s.kind = ZXN_ECONOMY;
        s.pri = ZXN_PRI_LOW;
        s.source = "quest";
        s.title = "A goal you follow is complete";
        s.body = qst_copy_is_calm(g->title) ? g->title : "Your group reached its goal.";
        s.action = action;
        s.key = ((uint64_t) g->id << 32) | p->subject;
        if (zxn_post(w->bus, &s)) w->notified++;
    }
}

/* ===== Q9 ===== */

qst_status_t qst_view(const qst_world_t *w, uint32_t subject, uint32_t day, qst_view_t *out)
{
    if (!out) return QST_ERR_ARG;
    qst__zero(out, (uint32_t) sizeof *out);
    const qst_profile_t *p = qst_profile_c(w, subject);
    if (!p) return QST_ERR_NO_PROFILE;
    out->opted_out = p->opted_out;
    out->minor = p->policy.is_minor;
    out->season = w->season;
    out->budget_minutes = p->budget_minutes;
    out->used_minutes = p->budget_day == day ? p->used_minutes : 0;
    if (p->opted_out) return QST_OK; /* the game layer is off; data is kept */
    out->contributed_units = p->contributed_units;
    for (uint32_t s = 0; s < QST_SKILL_COUNT; s++) {
        qst_skill_view_t *v = &out->skill[s];
        uint32_t l = qst_level_of(p->points[s]);
        v->level = (uint8_t) l;
        v->points = p->points[s];
        v->level_at = thresholds[l];
        v->next_at = l + 1 < QST_LEVELS ? thresholds[l + 1] : thresholds[l];
        v->permille = v->next_at > v->level_at
                          ? permille(v->points - v->level_at, v->next_at - v->level_at)
                          : 1000;
        v->counted_today = p->points_day == day ? p->day_points[s] : 0;
        v->daily_cap = QST_DAILY_CAP;
        v->verifiers = p->n_verifiers[s];
    }
    for (uint32_t i = 0; i < QST_MAX_GOALS && out->n_goals < QST_VIEW_GOALS; i++) {
        const qst_goal_t *g = &w->goal[i];
        if (!g->used || g->state == QST_GOAL_ARCHIVED) continue;
        int32_t m = qst_goal_member(g, subject);
        if (m < 0) continue;
        qst_goal_view_t *v = &out->goal[out->n_goals++];
        v->id = g->id;
        qst__strcpy(v->title, g->title, QST_TITLE_MAX);
        v->progress_permille = permille(g->progress, g->target);
        v->my_share_permille = permille(g->share_units[m], g->progress);
        v->state = g->state;
        v->members = g->n_members;
    }
    return QST_OK;
}

uint32_t qst_own_mechanics(qst_mechanic_t *out, uint32_t max)
{
    static const uint8_t rewards[] = {
        QST_RW_STANDING, /* mastery levels (Q1-Q3)               */
        QST_RW_STANDING, /* co-op goal progress (quest_coop C1)   */
        QST_RW_MONEY,    /* co-op VFV share (quest_pay)            */
        QST_RW_BADGE,    /* signed badges (quest_badge)            */
        QST_RW_NONE,     /* time-budget hints (Q4)                 */
        QST_RW_NONE,     /* requested notifications (Q7)           */
        QST_RW_STANDING, /* seasons (quest_coop C4)                */
    };
    uint32_t n = 0;
    for (; n < sizeof rewards && n < max; n++)
        qst_mechanic_plain(&out[n], (qst_reward_kind_t) rewards[n]);
    return n;
}
