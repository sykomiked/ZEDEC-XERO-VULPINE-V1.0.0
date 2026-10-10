/* Copyright (c) 2024-2026 Michael Laurence Curzi and 36N9 Genetics, LLC */
/* SPDX-License-Identifier: Apache-2.0 */
#include "quest_coop.h"

qst_goal_t *qst_goal(qst_world_t *w, uint32_t goal)
{
    if (!w || goal == 0) return 0;
    for (uint32_t i = 0; i < QST_MAX_GOALS; i++)
        if (w->goal[i].used && w->goal[i].id == goal) return &w->goal[i];
    return 0;
}

const qst_goal_t *qst_goal_c(const qst_world_t *w, uint32_t goal)
{
    return qst_goal((qst_world_t *) w, goal);
}

int32_t qst_goal_member(const qst_goal_t *g, uint32_t subject)
{
    if (!g) return -1;
    for (uint32_t i = 0; i < g->n_members; i++)
        if (g->member[i] == subject) return (int32_t) i;
    return -1;
}

int32_t qst_goal_create(qst_world_t *w, uint32_t group, const char *title, uint32_t ev_mask,
                        uint64_t target, uint32_t pool_acct, uint64_t pool_amount,
                        const qst_mechanic_t *extra)
{
    if (!w || !title || !title[0] || target == 0) return QST_ERR_ARG;
    if (ev_mask == 0 || (ev_mask >> QST_EV_COUNT) != 0) return QST_ERR_ARG;
    if (pool_amount > 0 && pool_acct == 0) return QST_ERR_ARG;

    /* C3: the goal's own mechanic, plus anything the caller adds */
    qst_mechanic_t m;
    if (extra)
        m = *extra;
    else
        qst_mechanic_plain(&m, QST_RW_STANDING);
    m.reward = pool_amount > 0 ? QST_RW_MONEY : QST_RW_STANDING;
    if (qst_guard_check(&m) != 0) return QST_ERR_GUARD;
    if (!qst_copy_is_calm(title)) return QST_ERR_GUARD;

    for (uint32_t i = 0; i < QST_MAX_GOALS; i++) {
        qst_goal_t *g = &w->goal[i];
        if (g->used) continue;
        qst__zero(g, (uint32_t) sizeof *g);
        g->used = true;
        g->id = w->next_goal_id++;
        g->group = group;
        qst__strcpy(g->title, title, QST_TITLE_MAX);
        g->ev_mask = ev_mask;
        g->target = target;
        g->pool_acct = pool_acct;
        g->pool_amount = pool_amount;
        g->state = QST_GOAL_OPEN;
        g->season_opened = w->season;
        return (int32_t) g->id;
    }
    return QST_ERR_FULL;
}

qst_status_t qst_goal_join(qst_world_t *w, uint32_t goal, uint32_t subject)
{
    qst_goal_t *g = qst_goal(w, goal);
    if (!g) return QST_ERR_NOT_FOUND;
    if (!qst_profile(w, subject)) return QST_ERR_NO_PROFILE;
    if (qst_goal_member(g, subject) >= 0) return QST_OK;
    if (g->state != QST_GOAL_OPEN) return QST_ERR_STATE;
    if (g->n_members >= QST_GOAL_MEMBERS) return QST_ERR_FULL;
    g->member[g->n_members] = subject;
    g->share_units[g->n_members] = 0;
    g->n_members++;
    return QST_OK;
}

static uint64_t sat_add(uint64_t a, uint64_t b)
{
    return a + b < a ? ~(uint64_t) 0 : a + b;
}

bool qst__goal_credit(qst_world_t *w, uint32_t goal, uint32_t subject, uint8_t kind, uint64_t units)
{
    qst_goal_t *g = qst_goal(w, goal);
    if (!g || g->state != QST_GOAL_OPEN || kind >= QST_EV_COUNT) return false;
    if (!(g->ev_mask & QST_EV_BIT(kind))) return false;
    int32_t m = qst_goal_member(g, subject);
    if (m < 0) return false;
    g->share_units[m] = sat_add(g->share_units[m], units);
    g->progress = sat_add(g->progress, units);
    if (g->progress >= g->target) {
        g->state = QST_GOAL_COMPLETE;
        g->season_completed = w->season;
        qst__notify_goal_complete(w, g);
    }
    return true;
}

void qst__goal_uncredit(qst_world_t *w, uint32_t goal, uint32_t subject, uint64_t units)
{
    qst_goal_t *g = qst_goal(w, goal);
    if (!g) return;
    if (g->state == QST_GOAL_SETTLED || g->state == QST_GOAL_ARCHIVED) {
        g->fraud_after_settle = true; /* money already moved: reverse it in pay */
        return;
    }
    int32_t m = qst_goal_member(g, subject);
    if (m < 0) return;
    g->share_units[m] = g->share_units[m] > units ? g->share_units[m] - units : 0;
    g->progress = g->progress > units ? g->progress - units : 0;
    if (g->state == QST_GOAL_COMPLETE && g->progress < g->target) {
        g->state = QST_GOAL_OPEN;
        g->season_completed = 0;
    }
}

uint32_t qst_season_begin(qst_world_t *w, uint32_t day)
{
    if (!w) return 0;
    w->season++;
    w->season_start_day = day;
    for (uint32_t i = 0; i < QST_MAX_GOALS; i++) {
        qst_goal_t *g = &w->goal[i];
        /* settled goals retire; complete-but-unpaid goals stay owed; open
         * goals carry over with their progress (C4: nothing expires) */
        if (!g->used) continue;
        if (g->state == QST_GOAL_ARCHIVED && !g->fraud_after_settle)
            g->used = false; /* archived a season ago: free the slot (ids never reused) */
        else if (g->state == QST_GOAL_SETTLED)
            g->state = QST_GOAL_ARCHIVED;
    }
    return w->season;
}
