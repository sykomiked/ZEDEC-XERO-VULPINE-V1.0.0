/* Copyright (c) 2024-2026 Michael Laurence Curzi and 36N9 Genetics, LLC */
/* SPDX-License-Identifier: Apache-2.0 */
/* navport_sim.c — deterministic stress simulation of a three-member naval
 * port (navport.h). Prints a Markdown report; exits 1 if any invariant
 * (navp_check) breaks. ADVISORY ONLY: the flows are synthetic, the oracle
 * is a rule of thumb, and nothing here predicts how a real unit behaves.
 * Pending counsel. Integer only. */
#include <stdio.h>
#include <string.h>
#include "navport.h"
#include "pay_assure.h"
#include "../robin_debanks/sha256.h"

static void sign(uint32_t who, const uint8_t *m, uint32_t n, uint8_t sig[32])
{
    uint8_t b[8 + 128];
    memset(b, 0, sizeof b);
    memcpy(b, &who, 4);
    b[4] = 0x4E;
    memcpy(b + 8, m, n);
    sha256(b, 8 + n, sig);
}
static bool verify(void *ctx, uint32_t who, const uint8_t *m, uint32_t n, const uint8_t *sig,
                   uint32_t sl)
{
    (void) ctx;
    uint8_t want[32];
    if (sl != 32 || n > 128) return false;
    sign(who, m, n, want);
    return memcmp(want, sig, 32) == 0;
}

static uint64_t rs;
static uint64_t rnd(void)
{
    rs ^= rs << 13;
    rs ^= rs >> 7;
    rs ^= rs << 17;
    return rs;
}

static int broken;

typedef struct {
    const char *name;
    navp_mode_t mode;
    uint64_t share;    /* starting share of every member, V-Bill units */
    uint32_t run_on;   /* member under a run (3 = none)                */
    uint32_t bias_pct; /* % of flows that sell the run member's unit   */
    uint32_t run_from; /* first cycle of the run                       */
    uint32_t silent;   /* member that stops reporting / voting (3 = none) */
    uint32_t silent_from, silent_to;
    uint32_t wash; /* extra round trips per cycle between A and C  */
    uint32_t fee_weight;
    uint32_t cycles;
} scen_t;

typedef struct {
    uint64_t ok, refused[17];
    uint64_t gross, settled;
    uint64_t min_cov_bps[3];
    int64_t first_refusal_cycle, both_cycles, false_cycles;
    uint64_t start[3], end[3];
    uint64_t fees_v[3], yield[3];
    uint64_t carried_closes, held_closes;
} res_t;

static const uint64_t NUM[3] = {150, 1, 7}, DEN[3] = {1, 1, 3};
static const char *CODE[3] = {"TCBDC", "TFIAT", "TGOLD"};
static const uint32_t KEY[3] = {11, 22, 33};

static void evidence(navp_t *p, uint64_t start, uint32_t m)
{
    /* rule-of-thumb oracle: two feeds watch the issuer's coverage */
    uint64_t cov = p->m[m].share * 10000 / start;
    uint32_t on = 0, off = 0;
    if (cov >= 2500) on++;
    if (cov < 3500) off++;
    navp_evidence(p, m, on, off);
}

static void run(const scen_t *s, res_t *r)
{
    navp_t p;
    navp_config_t c;
    navp_config_default(&c);
    c.mode = s->mode;
    c.quorum = 2;
    c.yield_enabled = true;
    c.fee_weight = s->fee_weight;
    navp_init(&p, 1, &c, verify, NULL);
    memset(r, 0, sizeof *r);
    r->first_refusal_cycle = -1;
    for (uint32_t i = 0; i < 3; i++) {
        navp_member_add(&p, KEY[i], CODE[i], NUM[i], DEN[i], s->share, NULL);
        r->start[i] = s->share;
        r->min_cov_bps[i] = 10000;
    }
    rs = 0x2545F4914F6CDD1Dull;
    for (uint32_t cy = 0; cy < s->cycles; cy++) {
        for (uint32_t i = 0; i < 3; i++) evidence(&p, s->share, i);
        if (navp_health(&p, 1) == NAVP_BOTH || navp_health(&p, 0) == NAVP_BOTH ||
            navp_health(&p, 2) == NAVP_BOTH)
            r->both_cycles++;
        for (uint32_t i = 0; i < 3; i++)
            if (navp_health(&p, i) == NAVP_FALSE) {
                r->false_cycles++;
                break;
            }
        uint64_t fees_before[3];
        for (uint32_t i = 0; i < 3; i++) fees_before[i] = p.m[i].fee_due;
        for (uint32_t k = 0; k < 300; k++) {
            uint32_t f = (uint32_t) (rnd() % 3), t = (uint32_t) (rnd() % 3);
            if (s->run_on < 3 && cy >= s->run_from && rnd() % 100 < s->bias_pct) f = s->run_on;
            if (f == t) t = (f + 1 + (uint32_t) (rnd() % 2)) % 3;
            uint64_t vv = rnd() % 5000 + 1; /* V-Bill value of the order */
            uint64_t x = vv * NUM[f] / DEN[f] + NUM[f];
            navp_quote_t q;
            navp_status_t st = navp_convert(&p, f, t, x, &q);
            if (st == NAVP_OK)
                r->ok++;
            else {
                r->refused[-st]++;
                if (r->first_refusal_cycle < 0) r->first_refusal_cycle = cy;
            }
        }
        for (uint32_t k = 0; k < s->wash; k++) {
            navp_quote_t q;
            navp_convert(&p, 0, 2, 150000, &q);
            navp_convert(&p, 2, 0, 2340, &q);
        }
        /* close: reports, prepare, votes */
        bool quiet = s->silent < 3 && cy >= s->silent_from && cy < s->silent_to;
        for (uint32_t i = 0; i < 3; i++) {
            if (quiet && i == s->silent) continue;
            uint64_t no[8] = {0}, vo[8] = {0};
            for (uint32_t j = 0; j < 3; j++) {
                no[j] = p.hold[i][j];
                vo[j] = p.hold[j][i];
            }
            navp_books_report(&p, i, no, vo);
        }
        if (navp_close_prepare(&p) != NAVP_OK) {
            r->held_closes++;
            continue;
        }
        for (uint32_t i = 0; i < 3; i++) {
            if (quiet && i == s->silent) continue;
            uint8_t msg[NAVP_CLOSE_MSG], sig[32];
            navp_close_msg(p.port_id, p.cycle, p.hash, msg);
            sign(KEY[i], msg, sizeof msg, sig);
            navp_close_vote(&p, i, p.hash, sig, 32);
        }
        uint64_t g = p.gross, st = p.settled;
        if (navp_close_commit(&p) != NAVP_OK) {
            navp_close_abort(&p);
            r->held_closes++;
            continue;
        }
        r->gross += g;
        r->settled += st;
        for (uint32_t i = 0; i < 3; i++)
            for (uint32_t j = 0; j < 3; j++)
                if (p.hold[i][j]) {
                    r->carried_closes++;
                    i = j = 3;
                }
        /* pipe yield: 25% of this cycle's realised fees, in V-Bill value */
        uint64_t pool = 0;
        for (uint32_t i = 0; i < 3; i++) {
            uint64_t fv = (p.m[i].fee_due - fees_before[i]) * DEN[i] / NUM[i];
            r->fees_v[i] += fv;
            pool += fv;
        }
        pool /= 4;
        uint64_t sh[8];
        if (pool && navp_yield(&p, pool, sh) == NAVP_OK)
            for (uint32_t i = 0; i < 3; i++) r->yield[i] += sh[i];
        for (uint32_t i = 0; i < 3; i++) {
            uint64_t cov = p.m[i].share * 10000 / s->share;
            if (cov < r->min_cov_bps[i]) r->min_cov_bps[i] = cov;
        }
        if (!navp_check(&p)) broken++;
    }
    for (uint32_t i = 0; i < 3; i++) r->end[i] = p.m[i].share;
}

static void pct(uint64_t bps)
{
    printf("%llu.%02llu%%", (unsigned long long) (bps / 100), (unsigned long long) (bps % 100));
}

static void ratio(uint64_t a, uint64_t b)
{
    if (!b) {
        printf("n/a");
        return;
    }
    uint64_t x = a * 100 / b;
    printf("%llu.%02llux", (unsigned long long) (x / 100), (unsigned long long) (x % 100));
}

static void report(const scen_t *s, const res_t *r)
{
    uint64_t refused = 0;
    for (int i = 0; i < 17; i++) refused += r->refused[i];
    printf("\n### %s\n\n", s->name);
    printf("| | |\n|---|---|\n");
    printf("| cycles | %u |\n", s->cycles);
    printf("| starting share per member | %llu V |\n", (unsigned long long) s->share);
    printf("| mode on FALSE | %s |\n", s->mode == NAVP_DEFEND ? "DEFEND" : "SUSPEND");
    printf("| conversions done / refused | %llu / %llu |\n", (unsigned long long) r->ok,
           (unsigned long long) refused);
    printf("| refused: LIMIT / DEPEG / HELD / STALE | %llu / %llu / %llu / %llu |\n",
           (unsigned long long) r->refused[5], (unsigned long long) r->refused[6],
           (unsigned long long) r->refused[7], (unsigned long long) r->refused[8]);
    printf("| first refusal at cycle | %lld |\n", (long long) r->first_refusal_cycle);
    printf("| cycles with a BOTH / FALSE member | %lld / %lld |\n", (long long) r->both_cycles,
           (long long) r->false_cycles);
    printf("| closes held / closes with carried pairs | %llu / %llu |\n",
           (unsigned long long) r->held_closes, (unsigned long long) r->carried_closes);
    printf("| gross settled value / net moved | %llu / %llu V |\n", (unsigned long long) r->gross,
           (unsigned long long) r->settled);
    printf("| compression (gross / net) | ");
    ratio(r->gross, r->settled);
    printf(" |\n");
    for (uint32_t i = 0; i < 3; i++) {
        printf("| %s share start -> end (lowest) | %llu -> %llu (", CODE[i],
               (unsigned long long) r->start[i], (unsigned long long) r->end[i]);
        pct(r->min_cov_bps[i]);
        printf(") |\n");
    }
    for (uint32_t i = 0; i < 3; i++)
        printf("| %s fees paid / pipe yield received | %llu / %llu V |\n", CODE[i],
               (unsigned long long) r->fees_v[i], (unsigned long long) r->yield[i]);
}

int main(void)
{
    const scen_t sc[] = {
        {"S1 Calm: balanced flows", NAVP_SUSPEND, 1000000, 3, 0, 0, 3, 0, 0, 0, 0, 60},
        {"S2 Run on TFIAT, SUSPEND", NAVP_SUSPEND, 1000000, 1, 60, 10, 3, 0, 0, 0, 0, 60},
        {"S3 Run on TFIAT, DEFEND", NAVP_DEFEND, 1000000, 1, 60, 10, 3, 0, 0, 0, 0, 60},
        {"S4 Same run, 10x the reserves", NAVP_SUSPEND, 10000000, 1, 60, 10, 3, 0, 0, 0, 0, 60},
        {"S5 TGOLD silent for cycles 20-29", NAVP_SUSPEND, 1000000, 3, 0, 0, 2, 20, 30, 0, 0, 60},
        {"S6 Wash trading A<->C, yield by locked V-Bills only", NAVP_SUSPEND, 1000000, 3, 0, 0, 3,
         0, 0, 50, 0, 60},
        {"S7 Wash trading A<->C, fee-weighted yield (10 units per fee V)", NAVP_SUSPEND, 1000000, 3,
         0, 0, 3, 0, 0, 50, 10, 60},
    };
    printf("## Naval port stress simulation (advisory, pending counsel)\n\n");
    printf("Three members, pegs TCBDC 150/1, TFIAT 1/1, TGOLD 7/3 (minor units per V-Bill).\n");
    printf("300 random conversions per cycle of 1..5000 V each, quorum 2 of 3, fee 0.08889%%.\n");
    printf("Oracle rule: on-peg while a share is >= 25%% of its start, off-peg below 35%%\n");
    printf("(so 25-35%% is BOTH). Pipe yield pool = 25%% of the cycle's realised fees.\n");
    for (size_t i = 0; i < sizeof sc / sizeof sc[0]; i++) {
        res_t r;
        run(&sc[i], &r);
        report(&sc[i], &r);
    }
    printf("\ninvariant violations: %d\n", broken);
    return broken ? 1 : 0;
}
