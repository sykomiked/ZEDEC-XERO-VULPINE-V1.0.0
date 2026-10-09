/* Copyright (c) 2024-2026 Michael Laurence Curzi and 36N9 Genetics, LLC */
/* SPDX-License-Identifier: LicenseRef-OPL-1.1 AND CC-BY-SA-4.0 AND LicenseRef-Royal-Writ-Sicilian-Crown-1.0 AND LicenseRef-SEL-3.3 */
/* swarm_enterprise.h — the industry of thought: enterprises in the swarm's
 * information economy.
 *
 * Agents form enterprises, as people form firms: corporations, collectives,
 * think tanks and cooperatives. An enterprise makes one kind of product (a
 * kind of task), employs agents, holds a treasury, and runs projects that
 * agents and other enterprises invest in.
 *
 *   N1  KINDS.  Each kind pays out its own way:
 *         CORPORATION  by equity (what each shareholder put in)
 *         COLLECTIVE   equally to every member
 *         COOPERATIVE  equally to every member: one member, one share
 *         THINK_TANK   by work done (tokens contributed)
 *       A corporation buys a leaving member out at its share of the treasury.
 *   N2  SPAWN ON DEMAND.  When work of some product arrives, an active
 *       enterprise that makes it takes it. If none is active, a retired one is
 *       reinstated with its charter and its people. Only then is a new one
 *       founded (swarm_ent_find says which).
 *   N3  EMPLOYMENT.  An enterprise employs at most 13 agents (F(7)) and no
 *       more than 8/21 of the swarm (no monopoly, as in the market). An agent
 *       works for at most 3 enterprises (F(4)).
 *   N4  PROJECTS AND INVESTMENT.  A project is one task an enterprise takes
 *       on. Agents and enterprises invest money in it; the money is held in
 *       escrow. When the project passes the quality gate, its escrow plus its
 *       revenue is paid out: 8/21 as wages to the agents who worked on it (by
 *       tokens of work), 13/21 to its investors (by stake). If it fails, every
 *       investor gets their stake back and nobody is paid.
 *   N5  RETIREMENT.  An enterprise with no open project for `idle_limit`
 *       cycles retires: its treasury goes back to its people by its N1 rule.
 *       Its charter, members and equity record are kept for reinstatement.
 *   N6  MONEY IS CONSERVED.  Money only moves between wallets, treasuries and
 *       escrows. Nothing is created or destroyed here.
 * Freestanding: no libc, no allocation, no floating point.
 */
#ifndef SWARM_ENTERPRISE_H
#define SWARM_ENTERPRISE_H

#include <stdint.h>
#include <stdbool.h>

#define SWARM_ENT_MAX       34u /* F(9) */
#define SWARM_ENT_MEMBERS   13u /* F(7) */
#define SWARM_ENT_PER_AGENT 3u  /* F(4) */
#define SWARM_ENT_PROJECTS  21u /* F(8) */
#define SWARM_ENT_INVESTORS 8u  /* F(6) */
#define SWARM_ENT_AGENTS    64u

/* An investor is an agent id, or an enterprise index with this bit set. */
#define SWARM_ENT_AS_INVESTOR(e) (0x80000000u | (uint32_t) (e))

typedef enum {
    SWARM_ENT_CORPORATION = 0,
    SWARM_ENT_COLLECTIVE,
    SWARM_ENT_COOPERATIVE,
    SWARM_ENT_THINK_TANK
} swarm_ent_kind_t;

typedef enum { SWARM_ENT_UNUSED = 0, SWARM_ENT_ACTIVE, SWARM_ENT_RETIRED } swarm_ent_status_t;

typedef struct {
    swarm_ent_status_t status;
    swarm_ent_kind_t kind;
    uint32_t product; /* what it makes */
    uint64_t charter; /* seed that reinstates the same setup */
    uint32_t member[SWARM_ENT_MEMBERS];
    uint64_t equity[SWARM_ENT_MEMBERS];
    uint64_t work[SWARM_ENT_MEMBERS]; /* tokens contributed, THINK_TANK */
    uint32_t num_members;
    uint64_t treasury;
    uint32_t last_active; /* cycle of the last open project */
    uint32_t times_reinstated;
} swarm_enterprise_t;

typedef struct {
    bool open;
    uint32_t enterprise;
    uint64_t task;
    uint32_t investor[SWARM_ENT_INVESTORS];
    uint64_t stake[SWARM_ENT_INVESTORS];
    uint32_t num_investors;
    uint32_t worker[SWARM_ENT_MEMBERS];
    uint64_t worked[SWARM_ENT_MEMBERS];
    uint32_t num_workers;
    uint64_t escrow;
} swarm_project_t;

typedef struct {
    swarm_enterprise_t ent[SWARM_ENT_MAX];
    swarm_project_t proj[SWARM_ENT_PROJECTS];
    uint32_t swarm_size; /* for the 8/21 employment cap */
    uint64_t *wallet;    /* agent money, indexed by agent id */
    uint32_t num_agents;
} swarm_economy_t;

void swarm_ent_init(swarm_economy_t *e, uint64_t *wallet, uint32_t num_agents);

/* N2: >= 0 an active enterprise; <= -2 a retired one, index -(r + 2), to
 * reinstate; -1 none, so found one. */
int32_t swarm_ent_find(const swarm_economy_t *e, uint32_t product);

/* Founders put stakes from their wallets into the treasury. Returns the
 * enterprise index, or -1. */
int32_t swarm_ent_found(swarm_economy_t *e, swarm_ent_kind_t kind, uint32_t product,
                        uint64_t charter, const uint32_t *founders, const uint64_t *stakes,
                        uint32_t n, uint32_t cycle);
bool swarm_ent_hire(swarm_economy_t *e, uint32_t ent, uint32_t agent);
bool swarm_ent_release(swarm_economy_t *e, uint32_t ent, uint32_t agent);
bool swarm_ent_reinstate(swarm_economy_t *e, uint32_t ent, uint32_t cycle);

/* N4 */
int32_t swarm_ent_project_open(swarm_economy_t *e, uint32_t ent, uint64_t task, uint32_t cycle);
bool swarm_ent_invest(swarm_economy_t *e, uint32_t project, uint32_t investor, uint64_t amount);
bool swarm_ent_work(swarm_economy_t *e, uint32_t project, uint32_t agent, uint64_t tokens);
/* `revenue` is moved out of *revenue_src (the market pot or a buyer). */
bool swarm_ent_project_close(swarm_economy_t *e, uint32_t project, bool passed_gate,
                             uint64_t *revenue_src, uint64_t revenue);

/* N5 */
bool swarm_ent_retire(swarm_economy_t *e, uint32_t ent);
uint32_t swarm_ent_tick(swarm_economy_t *e, uint32_t cycle, uint32_t idle_limit);

/* N6 */
uint64_t swarm_ent_money(const swarm_economy_t *e);

#endif /* SWARM_ENTERPRISE_H */
