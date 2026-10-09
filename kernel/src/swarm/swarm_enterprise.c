/* Copyright (c) 2024-2026 Michael Laurence Curzi and 36N9 Genetics, LLC */
/* SPDX-License-Identifier: LicenseRef-OPL-1.1 AND CC-BY-SA-4.0 AND LicenseRef-Royal-Writ-Sicilian-Crown-1.0 AND LicenseRef-SEL-3.3 */
/* swarm_enterprise.c — enterprises of the information economy. See
 * swarm_enterprise.h. */
#include "swarm_enterprise.h"
#include "swarm_budget.h" /* swarm_muldiv, swarm_split_lr */

#define IS_ENT(id) (((id) & 0x80000000u) != 0)
#define ENT_OF(id) ((id) & 0x7FFFFFFFu)

void swarm_ent_init(swarm_economy_t *e, uint64_t *wallet, uint32_t num_agents)
{
    for (uint32_t i = 0; i < SWARM_ENT_MAX; i++) {
        e->ent[i].status = SWARM_ENT_UNUSED;
        e->ent[i].num_members = 0;
        e->ent[i].treasury = 0;
    }
    for (uint32_t i = 0; i < SWARM_ENT_PROJECTS; i++) e->proj[i].open = false;
    e->wallet = wallet;
    e->num_agents = num_agents < SWARM_ENT_AGENTS ? num_agents : SWARM_ENT_AGENTS;
    e->swarm_size = e->num_agents;
}

int32_t swarm_ent_find(const swarm_economy_t *e, uint32_t product)
{
    int32_t retired = -1;
    for (uint32_t i = 0; i < SWARM_ENT_MAX; i++) {
        if (e->ent[i].product != product) continue;
        if (e->ent[i].status == SWARM_ENT_ACTIVE) return (int32_t) i;
        if (e->ent[i].status == SWARM_ENT_RETIRED && retired < 0) retired = (int32_t) i;
    }
    return retired >= 0 ? -(retired + 2) : -1;
}

static uint32_t memberships(const swarm_economy_t *e, uint32_t agent)
{
    uint32_t c = 0;
    for (uint32_t i = 0; i < SWARM_ENT_MAX; i++) {
        const swarm_enterprise_t *x = &e->ent[i];
        if (x->status != SWARM_ENT_ACTIVE) continue;
        for (uint32_t m = 0; m < x->num_members; m++)
            if (x->member[m] == agent) c++;
    }
    return c;
}

static int32_t member_index(const swarm_enterprise_t *x, uint32_t agent)
{
    for (uint32_t m = 0; m < x->num_members; m++)
        if (x->member[m] == agent) return (int32_t) m;
    return -1;
}

static uint32_t member_cap(const swarm_economy_t *e)
{
    uint64_t rem;
    uint64_t cap = swarm_muldiv(e->swarm_size, 8, 21, &rem);
    if (cap < 1) cap = 1;
    return cap < SWARM_ENT_MEMBERS ? (uint32_t) cap : SWARM_ENT_MEMBERS;
}

static void add_member(swarm_enterprise_t *x, uint32_t agent, uint64_t equity)
{
    x->member[x->num_members] = agent;
    x->equity[x->num_members] = equity;
    x->work[x->num_members] = 0;
    x->num_members++;
}

int32_t swarm_ent_found(swarm_economy_t *e, swarm_ent_kind_t kind, uint32_t product,
                        uint64_t charter, const uint32_t *founders, const uint64_t *stakes,
                        uint32_t n, uint32_t cycle)
{
    if (n == 0 || n > member_cap(e)) return -1;
    for (uint32_t i = 0; i < n; i++) {
        if (founders[i] >= e->num_agents || e->wallet[founders[i]] < stakes[i]) return -1;
        if (memberships(e, founders[i]) >= SWARM_ENT_PER_AGENT) return -1;
        for (uint32_t j = 0; j < i; j++)
            if (founders[j] == founders[i]) return -1;
    }
    for (uint32_t s = 0; s < SWARM_ENT_MAX; s++) {
        swarm_enterprise_t *x = &e->ent[s];
        if (x->status != SWARM_ENT_UNUSED) continue;
        x->status = SWARM_ENT_ACTIVE;
        x->kind = kind;
        x->product = product;
        x->charter = charter;
        x->num_members = 0;
        x->treasury = 0;
        x->last_active = cycle;
        x->times_reinstated = 0;
        for (uint32_t i = 0; i < n; i++) {
            e->wallet[founders[i]] -= stakes[i];
            x->treasury += stakes[i];
            add_member(x, founders[i], stakes[i]);
        }
        return (int32_t) s;
    }
    return -1;
}

bool swarm_ent_hire(swarm_economy_t *e, uint32_t ent, uint32_t agent)
{
    if (ent >= SWARM_ENT_MAX || agent >= e->num_agents) return false;
    swarm_enterprise_t *x = &e->ent[ent];
    if (x->status != SWARM_ENT_ACTIVE || member_index(x, agent) >= 0) return false;
    if (x->num_members >= member_cap(e)) return false;
    if (memberships(e, agent) >= SWARM_ENT_PER_AGENT) return false;
    add_member(x, agent, 0);
    return true;
}

/* N1: shares of `amount` for the members of x, by its kind. */
static void member_shares(const swarm_enterprise_t *x, uint64_t amount, uint64_t *out)
{
    uint64_t w[SWARM_ENT_MEMBERS];
    uint64_t sum = 0;
    for (uint32_t m = 0; m < x->num_members; m++) {
        w[m] = x->kind == SWARM_ENT_CORPORATION  ? x->equity[m]
               : x->kind == SWARM_ENT_THINK_TANK ? x->work[m]
                                                 : 1u;
        sum += w[m];
    }
    if (sum == 0)
        for (uint32_t m = 0; m < x->num_members; m++) w[m] = 1;
    swarm_split_lr(amount, w, x->num_members, out);
}

bool swarm_ent_release(swarm_economy_t *e, uint32_t ent, uint32_t agent)
{
    if (ent >= SWARM_ENT_MAX) return false;
    swarm_enterprise_t *x = &e->ent[ent];
    int32_t m = member_index(x, agent);
    if (m < 0 || x->num_members == 1) return false; /* the last member retires it instead */
    if (x->kind == SWARM_ENT_CORPORATION && x->equity[m] > 0) {
        /* the leaver is bought out at its share of the treasury */
        uint64_t total = 0, rem;
        for (uint32_t i = 0; i < x->num_members; i++) total += x->equity[i];
        uint64_t out = swarm_muldiv(x->treasury, x->equity[m], total, &rem);
        x->treasury -= out;
        e->wallet[agent] += out;
    }
    for (uint32_t i = (uint32_t) m; i + 1 < x->num_members; i++) {
        x->member[i] = x->member[i + 1];
        x->equity[i] = x->equity[i + 1];
        x->work[i] = x->work[i + 1];
    }
    x->num_members--;
    return true;
}

static bool has_open_project(const swarm_economy_t *e, uint32_t ent)
{
    for (uint32_t p = 0; p < SWARM_ENT_PROJECTS; p++)
        if (e->proj[p].open && e->proj[p].enterprise == ent) return true;
    return false;
}

bool swarm_ent_retire(swarm_economy_t *e, uint32_t ent)
{
    if (ent >= SWARM_ENT_MAX) return false;
    swarm_enterprise_t *x = &e->ent[ent];
    if (x->status != SWARM_ENT_ACTIVE || has_open_project(e, ent)) return false;
    uint64_t share[SWARM_ENT_MEMBERS];
    member_shares(x, x->treasury, share);
    for (uint32_t m = 0; m < x->num_members; m++) e->wallet[x->member[m]] += share[m];
    x->treasury = 0;
    x->status = SWARM_ENT_RETIRED;
    return true;
}

bool swarm_ent_reinstate(swarm_economy_t *e, uint32_t ent, uint32_t cycle)
{
    if (ent >= SWARM_ENT_MAX) return false;
    swarm_enterprise_t *x = &e->ent[ent];
    if (x->status != SWARM_ENT_RETIRED) return false;
    /* members who have since joined three other enterprises can't come back */
    uint32_t kept = 0;
    for (uint32_t m = 0; m < x->num_members; m++) {
        if (memberships(e, x->member[m]) >= SWARM_ENT_PER_AGENT || kept >= member_cap(e)) continue;
        x->member[kept] = x->member[m];
        x->equity[kept] = x->equity[m];
        x->work[kept] = x->work[m];
        kept++;
    }
    if (kept == 0) return false;
    x->num_members = kept;
    x->status = SWARM_ENT_ACTIVE;
    x->last_active = cycle;
    x->times_reinstated++;
    return true;
}

int32_t swarm_ent_project_open(swarm_economy_t *e, uint32_t ent, uint64_t task, uint32_t cycle)
{
    if (ent >= SWARM_ENT_MAX || e->ent[ent].status != SWARM_ENT_ACTIVE) return -1;
    for (uint32_t p = 0; p < SWARM_ENT_PROJECTS; p++) {
        swarm_project_t *j = &e->proj[p];
        if (j->open) continue;
        j->open = true;
        j->enterprise = ent;
        j->task = task;
        j->num_investors = 0;
        j->num_workers = 0;
        j->escrow = 0;
        e->ent[ent].last_active = cycle;
        return (int32_t) p;
    }
    return -1;
}

static uint64_t *purse(swarm_economy_t *e, uint32_t investor)
{
    if (IS_ENT(investor)) {
        uint32_t x = ENT_OF(investor);
        return x < SWARM_ENT_MAX && e->ent[x].status != SWARM_ENT_UNUSED ? &e->ent[x].treasury : 0;
    }
    return investor < e->num_agents ? &e->wallet[investor] : 0;
}

bool swarm_ent_invest(swarm_economy_t *e, uint32_t project, uint32_t investor, uint64_t amount)
{
    if (project >= SWARM_ENT_PROJECTS || !e->proj[project].open || amount == 0) return false;
    swarm_project_t *j = &e->proj[project];
    uint64_t *src = purse(e, investor);
    if (!src || *src < amount) return false;
    uint32_t k = 0;
    while (k < j->num_investors && j->investor[k] != investor) k++;
    if (k == j->num_investors) {
        if (k >= SWARM_ENT_INVESTORS) return false;
        j->investor[k] = investor;
        j->stake[k] = 0;
        j->num_investors++;
    }
    *src -= amount;
    j->stake[k] += amount;
    j->escrow += amount;
    return true;
}

bool swarm_ent_work(swarm_economy_t *e, uint32_t project, uint32_t agent, uint64_t tokens)
{
    if (project >= SWARM_ENT_PROJECTS || !e->proj[project].open) return false;
    swarm_project_t *j = &e->proj[project];
    swarm_enterprise_t *x = &e->ent[j->enterprise];
    int32_t m = member_index(x, agent);
    if (m < 0) return false; /* only employees work on it */
    x->work[m] += tokens;
    uint32_t k = 0;
    while (k < j->num_workers && j->worker[k] != agent) k++;
    if (k == j->num_workers) {
        j->worker[k] = agent;
        j->worked[k] = 0;
        j->num_workers++;
    }
    j->worked[k] += tokens;
    return true;
}

bool swarm_ent_project_close(swarm_economy_t *e, uint32_t project, bool passed_gate,
                             uint64_t *revenue_src, uint64_t revenue)
{
    if (project >= SWARM_ENT_PROJECTS || !e->proj[project].open) return false;
    swarm_project_t *j = &e->proj[project];
    uint64_t out[SWARM_ENT_MEMBERS > SWARM_ENT_INVESTORS ? SWARM_ENT_MEMBERS : SWARM_ENT_INVESTORS];

    if (!passed_gate) {
        for (uint32_t k = 0; k < j->num_investors; k++) *purse(e, j->investor[k]) += j->stake[k];
        j->escrow = 0;
        j->open = false;
        return true;
    }
    if (revenue && (!revenue_src || *revenue_src < revenue)) return false;
    if (revenue) *revenue_src -= revenue;
    uint64_t pool = j->escrow + revenue, rem;
    uint64_t wages = j->num_workers ? swarm_muldiv(pool, 8, 21, &rem) : 0;
    if (j->num_investors == 0) wages = j->num_workers ? pool : 0;
    if (wages) {
        swarm_split_lr(wages, j->worked, j->num_workers, out);
        uint64_t paid = 0;
        for (uint32_t k = 0; k < j->num_workers; k++) paid += out[k];
        if (paid < wages) { /* all work was zero: share equally */
            uint64_t ones[SWARM_ENT_MEMBERS];
            for (uint32_t k = 0; k < j->num_workers; k++) ones[k] = 1;
            swarm_split_lr(wages, ones, j->num_workers, out);
        }
        for (uint32_t k = 0; k < j->num_workers; k++) e->wallet[j->worker[k]] += out[k];
    }
    uint64_t rest = pool - wages;
    if (rest) {
        if (j->num_investors) {
            swarm_split_lr(rest, j->stake, j->num_investors, out);
            for (uint32_t k = 0; k < j->num_investors; k++) *purse(e, j->investor[k]) += out[k];
        } else {
            e->ent[j->enterprise].treasury += rest;
        }
    }
    j->escrow = 0;
    j->open = false;
    return true;
}

uint32_t swarm_ent_tick(swarm_economy_t *e, uint32_t cycle, uint32_t idle_limit)
{
    uint32_t retired = 0;
    for (uint32_t i = 0; i < SWARM_ENT_MAX; i++) {
        swarm_enterprise_t *x = &e->ent[i];
        if (x->status != SWARM_ENT_ACTIVE) continue;
        if (has_open_project(e, i)) {
            x->last_active = cycle;
            continue;
        }
        if (cycle - x->last_active >= idle_limit && swarm_ent_retire(e, i)) retired++;
    }
    return retired;
}

uint64_t swarm_ent_money(const swarm_economy_t *e)
{
    uint64_t m = 0;
    for (uint32_t a = 0; a < e->num_agents; a++) m += e->wallet[a];
    for (uint32_t i = 0; i < SWARM_ENT_MAX; i++) m += e->ent[i].treasury;
    for (uint32_t p = 0; p < SWARM_ENT_PROJECTS; p++)
        if (e->proj[p].open) m += e->proj[p].escrow;
    return m;
}
