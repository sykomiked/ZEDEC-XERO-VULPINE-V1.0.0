/* concord.h — the Commons: an ISF-driven, non-coercive social layer
 *
 * WHAT THIS IS
 * ------------
 * The matching algorithm for ZXV's peer-to-peer social space. Even a P2P
 * network hosted between its own members has a shared social space, and the
 * question is how people are introduced to one another. This module answers
 * it with the Interaction Surplus Framework instead of an engagement-
 * maximising feed.
 *
 * WHY ISF, NOT AN ENGAGEMENT ENGINE
 * ---------------------------------
 * A feed that optimises time-on-platform drives people apart while claiming
 * to connect them: it rewards outrage and sameness. ISF does the opposite by
 * construction. The interaction surplus of two people is
 *
 *      u = 1 - (x . y)^2
 *
 * over their interest vectors x, y. That quantity is MAXIMISED when they are
 * complementary (orthogonal) and falls to ZERO at BOTH extremes — identical
 * (an echo chamber) and diametrically opposed (a clash). So the commons is
 * pulled toward people who are different-but-not-opposite: the pairings that
 * actually broaden someone. There is no "keep them scrolling" term anywhere
 * in this file, on purpose.
 *
 * MUTUAL SOVEREIGNTY (the whole design stance)
 * --------------------------------------------
 * The operating system's social covenant is one line: treat others the way
 * you want to be treated, and respect each other's boundaries. The mechanics
 * enforce it symmetrically:
 *
 *   * A crossed boundary creates a DIVIDE that is always MUTUAL and
 *     TEMPORARY. Neither person can see or reach the other — there is no
 *     one-way block where the blocked party can still be watched. Blocking
 *     that lets one side spy is not fair, so this module cannot express it.
 *
 *   * Repeated boundary-crossing moves a person's STANDING toward
 *     quarantine, where they are matched only with others in the same
 *     standing — a bully meets bullies, not victims. Standing RECOVERS over
 *     time as behaviour settles, and they rejoin the whole commons. It is
 *     rehabilitative, not a permanent scarlet letter. (This mirrors the
 *     cellular fabric's quarantine/recovery states — same idea, applied to
 *     people instead of cells.)
 *
 *   * SOCIABILITY is a per-person budget. An introvert is never pushed past
 *     the amount of contact they want; the algorithm is a tool that is there
 *     when someone reaches for it, not a machine that reaches for them.
 *
 * Freestanding: integer only (Q32.32 via surplus_real_t), no libc, no float.
 *
 * Author: H.M. Michael-Laurence: Curzi (c)  (ZXV commons slice)
 * License: SEL-3.3
 */
#ifndef ZXV_CONCORD_H
#define ZXV_CONCORD_H

#include <stdint.h>
#include <stdbool.h>
#include "../surplus/surplus.h"
#include "../chiglet/chiglet.h"

#define CON_DIM        CHG_DIM      /* interest-vector dimension */
#define CON_MAX_PEOPLE 64u
#define CON_MAX_DIVIDES 256u

/* Behavioural standing — how much of the commons a person is matched into.
 * Recovers over time; it is a state to grow out of, not a label. */
typedef enum {
    CON_OK = 0,          /* good standing — matched across the whole commons */
    CON_STRAINED,        /* recent friction — a narrower, calmer pool         */
    CON_QUARANTINED      /* repeated boundary-crossing — matched only with    */
                         /* others in quarantine until behaviour settles      */
} con_standing_t;

typedef struct {
    uint32_t       id;
    bool           present;
    surplus_real_t interest[CON_DIM];   /* what a person is about */
    int32_t        standing_score;      /* 0..100; thresholds set standing */
    uint8_t        sociability;         /* 0=deep introvert .. 255=extrovert */
    uint32_t       active_contacts;     /* current connections held */
} con_person_t;

typedef struct { uint32_t a, b; uint32_t expires_at; } con_divide_t;

typedef struct {
    con_person_t person[CON_MAX_PEOPLE];
    uint32_t     n_people;
    con_divide_t divide[CON_MAX_DIVIDES];
    uint32_t     n_divides;
    uint32_t     now;                   /* event-cycle clock (ticks) */
} con_commons_t;

/* A candidate introduction: another person and the ISF surplus of the pair. */
typedef struct { uint32_t id; surplus_real_t surplus; } con_match_t;

void     con_init(con_commons_t *c);
int32_t  con_join(con_commons_t *c, uint32_t id, const surplus_real_t interest[CON_DIM],
                  uint8_t sociability);
con_person_t *con_get(con_commons_t *c, uint32_t id);

con_standing_t con_standing(const con_person_t *p);

/* The interaction surplus of a pair — the complementarity score. Maximal for
 * complementary interests, zero for identical or opposite. */
surplus_real_t con_surplus(const con_person_t *a, const con_person_t *b);

/* May these two be introduced? False if divided, if standing pools disallow
 * it, or if either has spent their sociability budget. Symmetric. */
bool con_may_match(const con_commons_t *c, uint32_t a, uint32_t b);

/* Recommend introductions for `self`, best surplus first, never exceeding the
 * person's remaining sociability budget. Returns the count written. There is
 * NO term that rewards keeping the user engaged. */
uint32_t con_recommend(const con_commons_t *c, uint32_t self,
                       con_match_t *out, uint32_t max);

/* Report that `subject` crossed `reporter`'s boundary. Creates a MUTUAL,
 * temporary divide (duration ticks) and lowers the subject's standing. */
bool con_report_boundary(con_commons_t *c, uint32_t reporter, uint32_t subject,
                         uint32_t duration);

/* Are these two currently divided? Always symmetric — no one-way blocks. */
bool con_divided(const con_commons_t *c, uint32_t a, uint32_t b);
/* Can `viewer` see `other`? Divides hide BOTH directions equally. */
bool con_can_see(const con_commons_t *c, uint32_t viewer, uint32_t other);

/* Advance the commons clock by `dt`: standing recovers (rehabilitation) and
 * expired divides are lifted. */
void con_tick(con_commons_t *c, uint32_t dt);

/* ---- the companion that grows across a person's devices ----
 * A Chiglet grows with its primary user. When someone runs ZXV on several
 * devices they link the SAME companion into all of them; each instance
 * contributes its own evidence direction. Growth is measured as R, the
 * effective count of INDEPENDENT evidence directions (chiglet.h): diverse
 * usage across instances raises it, redundant instances do not — the honest
 * form of "grows by having more instances to draw upon". */
typedef struct {
    surplus_real_t evidence[CHG_MAX_EXPERTS][CHG_DIM];
    uint32_t       n_instances;
} con_companion_t;

void     con_companion_init(con_companion_t *co);
/* Link one device instance's characteristic evidence. Returns false if full. */
bool     con_companion_link(con_companion_t *co, const surplus_real_t ev[CHG_DIM]);
/* The companion's current reach: R = effective independent evidence, and the
 * count of genuinely distinct instances (duplicates collapse). */
surplus_real_t con_companion_reach(const con_companion_t *co, uint32_t *distinct_out);

const char *con_standing_name(con_standing_t s);

#endif /* ZXV_CONCORD_H */
