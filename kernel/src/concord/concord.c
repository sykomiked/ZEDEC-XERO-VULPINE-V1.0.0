/* concord.c — the Commons: ISF-driven, non-coercive social matching.
 * See concord.h. */
#include "concord.h"

/* Standing thresholds on a 0..100 score. */
#define CON_OK_MIN        60
#define CON_QUAR_MAX      19
#define CON_START_SCORE   100
#define CON_BOUNDARY_HIT  40     /* score lost per crossed boundary */
#define CON_RECOVER_RATE  1      /* score regained per tick (rehabilitation) */

void con_init(con_commons_t *c) {
    if (!c) return;
    for (uint32_t i = 0; i < CON_MAX_PEOPLE; i++) c->person[i].present = false;
    c->n_people = 0;
    c->n_divides = 0;
    c->now = 0;
}

con_person_t *con_get(con_commons_t *c, uint32_t id) {
    if (!c) return 0;
    for (uint32_t i = 0; i < CON_MAX_PEOPLE; i++)
        if (c->person[i].present && c->person[i].id == id) return &c->person[i];
    return 0;
}
static const con_person_t *con_getc(const con_commons_t *c, uint32_t id) {
    for (uint32_t i = 0; i < CON_MAX_PEOPLE; i++)
        if (c->person[i].present && c->person[i].id == id) return &c->person[i];
    return 0;
}

int32_t con_join(con_commons_t *c, uint32_t id, const surplus_real_t interest[CON_DIM],
                 uint8_t sociability) {
    if (!c || !interest) return -1;
    if (con_get(c, id)) return -1;               /* already a member */
    for (uint32_t i = 0; i < CON_MAX_PEOPLE; i++) {
        if (c->person[i].present) continue;
        con_person_t *p = &c->person[i];
        p->id = id; p->present = true;
        for (uint32_t d = 0; d < CON_DIM; d++) p->interest[d] = interest[d];
        p->standing_score = CON_START_SCORE;
        p->sociability = sociability;
        p->active_contacts = 0;
        c->n_people++;
        return (int32_t)i;
    }
    return -1;                                    /* commons full */
}

con_standing_t con_standing(const con_person_t *p) {
    if (!p) return CON_QUARANTINED;
    if (p->standing_score >= CON_OK_MIN) return CON_OK;
    if (p->standing_score <= CON_QUAR_MAX) return CON_QUARANTINED;
    return CON_STRAINED;
}

/* The interaction surplus of a pair. chg_interaction returns u = 1-(a.b)^2/
 * (|a|^2|b|^2), scale-invariant and exactly the complementarity we want:
 * maximal for complementary interests, zero for identical or opposite. */
surplus_real_t con_surplus(const con_person_t *a, const con_person_t *b) {
    if (!a || !b) return SR_ZERO;
    return chg_interaction(a->interest, b->interest, CON_DIM);
}

/* A person's contact budget from their sociability. An introvert (low
 * sociability) is offered few connections; an extrovert many. Nobody is
 * pushed past their own budget — this is the anti-coercion guarantee. */
static uint32_t con_budget(const con_person_t *p) {
    /* 0..255 -> 0..16 contacts, so the deepest introvert is left alone until
     * THEY reach out (active_contacts they initiate still count normally). */
    return ((uint32_t)p->sociability * 16u) / 255u;
}

bool con_divided(const con_commons_t *c, uint32_t a, uint32_t b) {
    if (!c) return false;
    for (uint32_t i = 0; i < c->n_divides; i++) {
        const con_divide_t *d = &c->divide[i];
        if ((d->a == a && d->b == b) || (d->a == b && d->b == a)) return true;
    }
    return false;
}

bool con_can_see(const con_commons_t *c, uint32_t viewer, uint32_t other) {
    /* A divide hides BOTH directions. There is deliberately no asymmetric
     * form of this function — a one-way block that lets the blocker keep
     * watching cannot be represented. */
    return !con_divided(c, viewer, other);
}

bool con_may_match(const con_commons_t *c, uint32_t a, uint32_t b) {
    if (!c || a == b) return false;
    const con_person_t *pa = con_getc(c, a), *pb = con_getc(c, b);
    if (!pa || !pb) return false;
    if (con_divided(c, a, b)) return false;

    con_standing_t sa = con_standing(pa), sb = con_standing(pb);
    /* Standing pools: a quarantined person is matched ONLY with other
     * quarantined people (bully meets bullies), until their standing
     * recovers. Everyone else (OK/STRAINED) shares the general commons. */
    if ((sa == CON_QUARANTINED) != (sb == CON_QUARANTINED)) return false;

    /* Respect both people's sociability budgets. */
    if (pa->active_contacts >= con_budget(pa)) return false;
    if (pb->active_contacts >= con_budget(pb)) return false;
    return true;
}

uint32_t con_recommend(const con_commons_t *c, uint32_t self,
                       con_match_t *out, uint32_t max) {
    if (!c || !out || !max) return 0;
    const con_person_t *me = con_getc(c, self);
    if (!me) return 0;
    uint32_t remaining = con_budget(me);
    if (me->active_contacts >= remaining) return 0;
    remaining -= me->active_contacts;

    /* gather eligible candidates with their surplus */
    con_match_t buf[CON_MAX_PEOPLE];
    uint32_t n = 0;
    for (uint32_t i = 0; i < CON_MAX_PEOPLE; i++) {
        if (!c->person[i].present) continue;
        uint32_t oid = c->person[i].id;
        if (oid == self) continue;
        if (!con_may_match(c, self, oid)) continue;
        buf[n].id = oid;
        buf[n].surplus = con_surplus(me, &c->person[i]);
        n++;
    }
    /* selection sort by surplus, descending — best complementarity first.
     * No engagement/recency/retention term participates in the ranking. */
    uint32_t want = (max < remaining) ? max : remaining;
    if (want > n) want = n;
    for (uint32_t k = 0; k < want; k++) {
        uint32_t best = k;
        for (uint32_t j = k + 1; j < n; j++)
            if (buf[j].surplus > buf[best].surplus) best = j;
        con_match_t t = buf[k]; buf[k] = buf[best]; buf[best] = t;
        out[k] = buf[k];
    }
    return want;
}

bool con_report_boundary(con_commons_t *c, uint32_t reporter, uint32_t subject,
                         uint32_t duration) {
    if (!c || reporter == subject) return false;
    con_person_t *sub = con_get(c, subject);
    if (!sub || !con_getc(c, reporter)) return false;

    /* the divide is MUTUAL and TEMPORARY */
    if (!con_divided(c, reporter, subject)) {
        if (c->n_divides >= CON_MAX_DIVIDES) return false;
        con_divide_t *d = &c->divide[c->n_divides++];
        d->a = reporter; d->b = subject; d->expires_at = c->now + duration;
    }
    /* the subject's standing drops toward quarantine; repeated crossings
     * accumulate, but time will lift it again */
    sub->standing_score -= CON_BOUNDARY_HIT;
    if (sub->standing_score < 0) sub->standing_score = 0;
    return true;
}

void con_tick(con_commons_t *c, uint32_t dt) {
    if (!c) return;
    c->now += dt;
    /* standing recovers (rehabilitation) */
    for (uint32_t i = 0; i < CON_MAX_PEOPLE; i++) {
        if (!c->person[i].present) continue;
        int32_t s = c->person[i].standing_score + (int32_t)(CON_RECOVER_RATE * dt);
        if (s > CON_START_SCORE) s = CON_START_SCORE;
        c->person[i].standing_score = s;
    }
    /* expired divides are lifted (compact the array) */
    uint32_t w = 0;
    for (uint32_t i = 0; i < c->n_divides; i++)
        if (c->divide[i].expires_at > c->now) c->divide[w++] = c->divide[i];
    c->n_divides = w;
}

/* ---------------------------- the companion ---------------------------- */
void con_companion_init(con_companion_t *co) {
    if (!co) return;
    co->n_instances = 0;
    for (uint32_t i = 0; i < CHG_MAX_EXPERTS; i++)
        for (uint32_t d = 0; d < CHG_DIM; d++) co->evidence[i][d] = SR_ZERO;
}

bool con_companion_link(con_companion_t *co, const surplus_real_t ev[CHG_DIM]) {
    if (!co || !ev || co->n_instances >= CHG_MAX_EXPERTS) return false;
    for (uint32_t d = 0; d < CHG_DIM; d++) co->evidence[co->n_instances][d] = ev[d];
    co->n_instances++;
    return true;
}

surplus_real_t con_companion_reach(const con_companion_t *co, uint32_t *distinct_out) {
    if (distinct_out) *distinct_out = 0;
    if (!co || co->n_instances == 0) return SR_ZERO;
    /* R over the linked instances' evidence: diverse usage across devices
     * raises it; redundant instances collapse (chg_effective_experts merges
     * near-identical directions), so "more instances" only helps when they
     * bring genuinely different experience. */
    return chg_effective_experts((const surplus_real_t (*)[CHG_DIM])co->evidence,
                                 co->n_instances, CHG_DIM, distinct_out);
}

const char *con_standing_name(con_standing_t s) {
    switch (s) {
    case CON_OK:          return "OK";
    case CON_STRAINED:    return "STRAINED";
    case CON_QUARANTINED: return "QUARANTINED";
    default:              return "?";
    }
}
