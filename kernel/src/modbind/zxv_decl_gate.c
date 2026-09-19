/* zxv_decl_gate.c — THE DECLARATION-GRAPH BOOT GATE, once, for every arch.
 *
 * Author: H.M. Michael-Laurence: Curzi (c)  (ZXV composition slice)
 * Copyright (c) 2024-2026 Michael Laurence Curzi and 36N9 Genetics, LLC
 * SPDX-License-Identifier: LicenseRef-OPL-1.1 AND CC-BY-SA-4.0
 * Licensed under OPL-1.1, SEL-3.3, the Royal Writ of the Sicilian Crown,
 * and CC BY-SA 4.0. See LICENSE at the repository root.
 *
 * WHY THIS FILE EXISTS AT ALL
 * ---------------------------
 * The gate was written inline in kernel/arch/arm64/kernel_main_arm64.c and ran
 * on arm64 ONLY. The mechanism COMPILED on all five architectures and EXECUTED
 * on one: x86_64, riscv64, riscv32 and arm32 booted without ever asking whether
 * their declaration graph was sound. The obvious repair -- paste the block into
 * four more mains -- would have created four copies of a check whose entire
 * purpose is to catch drift, and four copies drift. So the block moved HERE,
 * whole, and every arch main calls one function.
 *
 * WHY ONE puts CALLBACK AND NOTHING ELSE
 * --------------------------------------
 * The five mains do not agree on a console. arm64 has uart_puts + uart_put_dec;
 * arm32's main has NO put_dec at all (measured: zero occurrences). A shared gate
 * that took a put_dec callback would therefore be a shared gate that three
 * architectures had to grow a helper for -- so it takes the ONE primitive all
 * five already have, and formats its own decimals. Same shape as
 * kernel/src/tolvovina/tvl_bringup.c, which takes tvl_puts_t and is called as
 * (void)tvl_bringup((tvl_puts_t)puts) from bootfeat/boot_features.c:200.
 *
 * WHY EVERY DECIMAL IS uint32_t AND NEVER uint64_t
 * ------------------------------------------------
 * A 64-bit divide by a variable lowers to __udivdi3, and Makefile.riscv32 sets
 * LIBGCC to nothing -- there is no libgcc for ilp32d on this toolchain. The
 * identical class of defect has already broken this build once (__builtin_bswap
 * lowering to __bswapsi2/__bswapdi2 in ramfb.c). Every quantity the gate prints
 * is a count of modules; uint32_t holds all of them with room to spare, and
 * rv32imafd/armv7-a both divide 32-bit values without a helper call.
 *
 * WHY EVERY NAME IS PRINTED BOUNDED
 * ---------------------------------
 * mb_module_t.name is char[MB_NAME_LEN] and mb_cap_t.name is char[
 * MB_CAP_NAME_LEN], both 24, and NEITHER is guaranteed NUL-terminated -- a
 * 24-character literal legally drops the terminator, and modbind's own
 * comparators bound at the array length rather than at a NUL. So every print of
 * one bounds too, or the gate reporting the bug becomes the bug.
 *
 * Freestanding: integer only, no libc, no libgcc, no allocation, no float.
 */
#include "zxv_decl.h"

/* The console for the current call. Set on entry, cleared on exit: the gate
 * runs once, from one core, at boot, before any scheduler exists. */
static zxv_puts_t g_puts;

static void gp(const char *s) { g_puts(s); }

/* uint32 -> decimal. Digits come out backwards and are reversed into a second
 * buffer rather than printed one at a time, because `puts` is the only output
 * primitive and a one-char-per-call loop through it would be nine calls for a
 * three-digit count. 10 digits + NUL is the widest a uint32_t can be. */
static void gp_u32(uint32_t v)
{
    char rev[11];
    char out[12];
    uint32_t n = 0, i = 0;

    if (v == 0u) { gp("0"); return; }
    while (v != 0u) { rev[n++] = (char)('0' + (v % 10u)); v /= 10u; }
    while (n != 0u) out[i++] = rev[--n];
    out[i] = 0;
    gp(out);
}

/* ---- bounded name printing ------------------------------------------------
 * The 24-char bound is the contract, not a buffer-size convenience. Keep it.  */
static void gate_put_bounded(const char *n, uint32_t max)
{
    char b[32];
    uint32_t i = 0;
    if (!n) { gp("(null)"); return; }
    if (max > 31u) max = 31u;
    for (; i < max && n[i]; i++) b[i] = n[i];
    b[i] = 0;
    gp(b);
}
static void gate_put_mod(const char *n) { gate_put_bounded(n, MB_NAME_LEN); }
static void gate_put_cap(const char *n) { gate_put_bounded(n, MB_CAP_NAME_LEN); }

static bool gate_cap_eq(const char *a, const char *b)
{
    for (uint32_t i = 0; i < MB_CAP_NAME_LEN; i++) {
        if (a[i] != b[i]) return false;
        if (a[i] == 0)    return true;
    }
    return true;
}

/* Is this capability PROVIDED by anybody at all, ready or not? Same question
 * modbind_verify_graph's first check asks -- asked again here only so the
 * offending capability can be named, which the count cannot carry. */
static bool gate_provided_anywhere(const char *cap)
{
    uint32_t n = modbind_count();
    for (uint32_t k = 0; k < n; k++) {
        const mb_module_t *o = modbind_get(k);
        if (!o) continue;
        for (uint8_t l = 0; l < o->n_provides; l++)
            if (gate_cap_eq(o->provides[l].name, cap)) return true;
    }
    return false;
}

/* Print every problem, not just the first. Call AFTER modbind_verify_graph,
 * whose cycle check has already run the fixpoint. */
static void gate_report_all(void)
{
    uint32_t n = modbind_count();
    for (uint32_t i = 0; i < n; i++) {
        const mb_module_t *m = modbind_get(i);
        if (!m) continue;
        for (uint8_t j = 0; j < m->n_requires; j++) {
            if (gate_provided_anywhere(m->requires[j].name)) continue;
            gp("         UNPROVIDED: module '");
            gate_put_mod(m->name);
            gp("' requires '");
            gate_put_cap(m->requires[j].name);
            gp("' -- nothing provides it\n");
        }
        /* WHY IS IT HELD? This used to reason by elimination -- "every
         * requirement is named by somebody and the fixpoint still never
         * arrived, therefore a cycle" -- and that inference was sound only
         * while coupling WAS naming. It is not any more: a provider can be
         * present, named, contract-compatible and simply not delivering real
         * power, in a graph with no cycle anywhere in it. Reporting that as a
         * cycle would send the reader hunting a loop that does not exist, and
         * would make ordinary fluid behaviour look like a broken build.
         *
         * modbind_hold_reason answers it properly, as a fixpoint over the
         * requires-graph, so a module held merely because its PROVIDER is held
         * inherits its provider's reason instead of being mislabelled. */
        if (m->ready == MB_HELD && m->n_requires > 0) {
            mb_hold_t why = modbind_hold_reason(m);
            if (why == MB_HOLD_CYCLE) {
                gp("         CYCLE: module '");
                gate_put_mod(m->name);
                gp("' never resolves; its requirements close on themselves (");
                for (uint8_t j = 0; j < m->n_requires; j++) {
                    if (j) gp(", ");
                    gate_put_cap(m->requires[j].name);
                }
                gp(")\n");
            } else if (why == MB_HOLD_PHASE || why == MB_HOLD_WITHDRAWN) {
                /* NOT AN ERROR AND NOT A SKIP. Printed so it is legible, and
                 * deliberately NOT counted by modbind_verify_graph. */
                gp("         S0 "); gp(mb_hold_name(why));
                gp(": module '"); gate_put_mod(m->name);
                gp("' is present and not coupled\n");
            }
        }
        /* Alternative provision is legal; disagreement about the contract is
         * not, because then which provider wins would decide behaviour. */
        for (uint8_t j = 0; j < m->n_provides; j++) {
            for (uint32_t k = i + 1; k < n; k++) {
                const mb_module_t *o = modbind_get(k);
                if (!o) continue;
                for (uint8_t l = 0; l < o->n_provides; l++) {
                    if (!gate_cap_eq(m->provides[j].name, o->provides[l].name)) continue;
                    if (m->provides[j].contract == o->provides[l].contract) continue;
                    gp("         CONTRACT: '");
                    gate_put_cap(m->provides[j].name);
                    gp("' provided by '"); gate_put_mod(m->name);
                    gp("' at v"); gp_u32((uint32_t)m->provides[j].contract);
                    gp(" and by '"); gate_put_mod(o->name);
                    gp("' at v"); gp_u32((uint32_t)o->provides[l].contract);
                    gp("\n");
                }
            }
        }
    }
}

/* ---- THE POWER DISTRIBUTION OF THE WHOLE GRAPH ----------------------------
 * Every declaration in this tree used to expand phase to 0, so every edge
 * coupled at the in-phase factor (1000 + cos 0)/2 = 1000 permille and the
 * electrical model, though it ran, measured NOTHING: one number, repeated 79
 * times. Phases are now derived from
 * each module's DRC layer, and the interesting object is the distribution they
 * produce -- which nobody has ever seen, because nothing ever printed it.
 *
 * WHY A DISTRIBUTION AND NOT ONE LINE PER EDGE. arm64 carries 79 provider ->
 * requirer edges; a line each is 79 lines on a serial console, inside a boot
 * that already prints 175 messages, and the reader would have to tally them by
 * hand to see the shape. The shape IS the finding, so the shape is what gets
 * printed: a bucket per phase distance, plus the weakest edges BY NAME (a list,
 * because "the weakest edge is 121" is not actionable and "hkdf requires
 * sha256_ready from sha256" is), plus the cumulative sweep that answers what a
 * threshold would cost. Nothing is summarised away that a reader would have to
 * reconstruct.
 *
 * WHY IT RUNS BEFORE modbind_verify_graph. The coupling readout that already
 * existed lives inside gate_negative_tests, which only runs when the gate PASSES
 * -- so on x86_64, riscv64, riscv32 and arm32, where the graph is not fully
 * provided, it has never printed a single power number. The distribution is a
 * property of the REGISTERED graph and needs no verdict, so it is measured here
 * and prints on all five architectures whatever the gate goes on to say.
 *
 * NO THRESHOLD IS APPLIED. The sweep says what each cut WOULD cost and stops
 * there. Making weak couplings fail is a separate act with separate evidence,
 * exactly as differentiating the phases was separate from wiring the law up --
 * and modbind.h already refuses tolerances in capability decisions on the
 * grounds that a tolerance in a permission check is a vulnerability. */
#define GATE_WEAK_NAMED 8u

static void gate_power_readout(void)
{
    uint32_t n = modbind_count();
    uint32_t kcnt[MB_PHASES];      /* edges at each phase distance 0..12      */
    uint32_t phcnt[MB_PHASES];     /* modules seated at each phase            */
    uint32_t pw_val[MB_PHASES];    /* distinct real-power magnitudes present  */
    uint32_t pw_cnt[MB_PHASES];
    uint32_t npw = 0;
    uint32_t edges = 0, weakest = 0xFFFFFFFFu, phases_used = 0;
    uint32_t i, x, t;
    uint8_t  j, q;

    for (i = 0; i < MB_PHASES; i++) { kcnt[i] = 0; phcnt[i] = 0; pw_val[i] = 0; pw_cnt[i] = 0; }

    for (i = 0; i < n; i++) {
        const mb_module_t *m = modbind_get(i);
        if (m) phcnt[m->phase % MB_PHASES]++;
    }

    /* Pass 1: every (provider cap, requirer cap) pair that NAMES the same
     * capability. Name-matching is the right enumeration here and not a
     * regression to the old predicate: an edge is a DECLARED intention, and the
     * whole point is to measure how much power that intention actually carries
     * -- including the ones carrying little. Filtering by mb_real_power first
     * would hide exactly the edges being looked for. */
    for (i = 0; i < n; i++) {
        const mb_module_t *r = modbind_get(i);
        if (!r) continue;
        for (j = 0; j < r->n_requires; j++) {
            for (x = 0; x < n; x++) {
                const mb_module_t *pv = modbind_get(x);
                if (!pv) continue;
                for (q = 0; q < pv->n_provides; q++) {
                    if (!gate_cap_eq(pv->provides[q].name, r->requires[j].name))
                        continue;
                    {
                        uint8_t  a = pv->provides[q].phase;
                        uint8_t  b = r->requires[j].phase;
                        uint32_t k = (uint32_t)((a >= b) ? (a - b) : (b - a)) % MB_PHASES;
                        int32_t  P = mb_real_power(&pv->provides[q], &r->requires[j]);
                        uint32_t mag = (uint32_t)((P < 0) ? -P : P);
                        edges++;
                        kcnt[k]++;
                        if (mag < weakest) weakest = mag;
                        for (t = 0; t < npw; t++) if (pw_val[t] == mag) break;
                        if (t == npw && npw < MB_PHASES) { pw_val[npw] = mag; pw_cnt[npw] = 0; npw++; }
                        if (t < MB_PHASES) pw_cnt[t]++;
                    }
                }
            }
        }
    }

    for (i = 0; i < MB_PHASES; i++) if (phcnt[i]) phases_used++;

    gp("  coupling: "); gp_u32(edges);
    gp(" declared edge(s) over ");  gp_u32(n);
    gp(" module(s); carrier ");     gp(mb_carrier() ? "up" : "DOWN");
    gp("\n         module phases (DERIVED from the DRC layer, none typed):");
    for (i = 0; i < MB_PHASES; i++) {
        if (!phcnt[i]) continue;
        gp(" L"); gp_u32(i); gp("="); gp_u32(phcnt[i]);
    }
    gp("  -- "); gp_u32(phases_used); gp(" of "); gp_u32(MB_PHASES); gp(" phases populated\n");

    if (edges == 0u) {
        gp("         no requirer names a declared capability: there is no"
           " edge to measure, and that is the measurement\n");
        return;
    }

    /* THE INTERFERENCE FRINGE. The factor is (1000 + cos(2*pi*k/13))/2, which is
     * MONOTONIC in phase distance: 1000 in phase (k=0), falling to its floor of
     * 14 at ANTIPHASE (k=6). So phase distance now reads exactly as "further is
     * weaker" -- constructive in phase, destructive antiphase, the Young's-slits
     * reading. A low number here means WEAK coupling, a high number means strong;
     * antiphase (k=6) is the weakest edge in the graph, not the strongest.
     * Printing the distance beside the factor is what makes that visible. */
    gp("         dphase  interf permille   edges\n");
    for (i = 0; i < MB_PHASES; i++) {
        if (!kcnt[i]) continue;
        gp("              ");  gp_u32(i);
        gp("            ");    gp_u32(mb_power_factor((uint8_t)i, 0u));
        gp("      ");          gp_u32(kcnt[i]);
        gp("\n");
    }

    /* Pass 2: NAME the weakest, bounded. A minimum without names is a number
     * nobody can act on, and this project has already paid for counts that
     * could not be turned into a file to open. */
    {
        uint32_t named = 0, total_weak = 0;
        for (i = 0; i < n; i++) {
            const mb_module_t *r = modbind_get(i);
            if (!r) continue;
            for (j = 0; j < r->n_requires; j++) {
                for (x = 0; x < n; x++) {
                    const mb_module_t *pv = modbind_get(x);
                    if (!pv) continue;
                    for (q = 0; q < pv->n_provides; q++) {
                        int32_t  P;
                        uint32_t mag;
                        if (!gate_cap_eq(pv->provides[q].name, r->requires[j].name))
                            continue;
                        P = mb_real_power(&pv->provides[q], &r->requires[j]);
                        mag = (uint32_t)((P < 0) ? -P : P);
                        if (mag != weakest) continue;
                        total_weak++;
                        if (named >= GATE_WEAK_NAMED) continue;
                        named++;
                        gp("         weakest: '");   gate_put_mod(r->name);
                        gp("' requires '");          gate_put_cap(r->requires[j].name);
                        gp("' <- '");                gate_put_mod(pv->name);
                        gp("'  L");                  gp_u32(pv->provides[q].phase);
                        gp("->L");                   gp_u32(r->requires[j].phase);
                        gp("  P=");
                        if (P < 0) { gp("-"); gp_u32((uint32_t)(-P)); } else gp_u32((uint32_t)P);
                        gp("/1000\n");
                    }
                }
            }
        }
        if (total_weak > named) {
            gp("         (+"); gp_u32(total_weak - named);
            gp(" more edge(s) at the same floor, not named)\n");
        }
    }

    /* THE SWEEP: what a threshold WOULD cost, at every cut the data itself
     * offers. The breakpoints are the distinct powers present, not three round
     * numbers somebody liked -- so this answers "how many fail at 500" for the
     * 500 that actually exists in this graph rather than for an invented one.
     * Selection sort over at most 13 values; no allocation, no 64-bit divide. */
    {
        for (i = 0; i + 1 < npw; i++) {
            uint32_t lo = i;
            for (x = i + 1; x < npw; x++) if (pw_val[x] < pw_val[lo]) lo = x;
            if (lo != i) {
                uint32_t tv = pw_val[i]; pw_val[i] = pw_val[lo]; pw_val[lo] = tv;
                tv = pw_cnt[i]; pw_cnt[i] = pw_cnt[lo]; pw_cnt[lo] = tv;
            }
        }
        gp("         threshold sweep -- NOT APPLIED, this is only what each cut"
           " would cost:\n");
        {
            uint32_t cum = 0;
            for (i = 0; i < npw; i++) {
                cum += pw_cnt[i];
                gp("           cut at P>"); gp_u32(pw_val[i]);
                gp(" would drop ");         gp_u32(cum);
                gp(" of ");                 gp_u32(edges);
                gp(" edge(s)\n");
            }
        }
        /* 13 IS ODD, SO NOTHING FULLY DECOUPLES. The interference floor is
         * min (1000 + cos(2*pi*k/13))/2 = (1000 - 971)/2 = 14 permille at k=6
         * (antiphase) and is never 0: an odd modulus makes cos never reach
         * exactly -1000. That is why moving antiphase from strong to WEAK CANNOT
         * change readiness under the `mb_real_power != 0` predicate -- the
         * weakest edge still couples -- and it is checked here rather than
         * asserted, because the whole invariance claim rests on it. */
        if (weakest == 0u)
            gp("  [FAIL] an edge reached EXACTLY ZERO real power -- readiness is"
               " NOT phase-invariant; 13 being odd should have made this"
               " impossible\n");
    }
}

/* ---- S− AND THE CARRIER: THE TWO NEGATIVE TESTS ---------------------------
 * modbind_withdraw was compiled and then DISCARDED by --gc-sections, because
 * nothing anywhere called it. That is why the S− column across the thirteen
 * layers was empty: teardown had no runtime at all. A capability the system can
 * only ever GRANT is not a capability system, it is a bring-up script with
 * ambitions, and "withdrawal works" was a claim resting on a function that had
 * never executed on any architecture.
 *
 * Both tests run on the REAL declaration graph, after bring-up, and both are
 * NEGATIVE: they make the system fail on purpose and check that it fails in the
 * predicted shape, then restore it and check the restoration is exact. A test
 * that only ever observes success cannot distinguish a working mechanism from an
 * absent one -- which is the exact defect that produced the empty column.
 *
 * NOTHING IS HARDCODED. The capability to withdraw is not named in this file; it
 * is chosen from the live graph as the one the most modules depend on, so the
 * test asks the data which edge is load-bearing rather than assuming. On a graph
 * with no requirements at all it says so and claims nothing. */
static bool gate_requires_cap(const mb_module_t *m, const char *cap)
{
    for (uint8_t j = 0; j < m->n_requires; j++)
        if (gate_cap_eq(m->requires[j].name, cap)) return true;
    return false;
}

static uint32_t gate_count_requirers(const char *cap)
{
    uint32_t n = modbind_count(), c = 0;
    for (uint32_t i = 0; i < n; i++) {
        const mb_module_t *m = modbind_get(i);
        if (m && gate_requires_cap(m, cap)) c++;
    }
    return c;
}

static uint32_t gate_count_ready(void)
{
    uint32_t n = modbind_count(), c = 0;
    for (uint32_t i = 0; i < n; i++) {
        const mb_module_t *m = modbind_get(i);
        if (m && m->ready == MB_READY) c++;
    }
    return c;
}

/* Returns the number of PROBLEMS found (0 = both mechanisms behaved). */
static uint32_t gate_negative_tests(void)
{
    uint32_t problems = 0;
    uint32_t n = modbind_count();
    uint32_t ready_at_entry = gate_count_ready();

    /* ---- 1. NO CARRIER, NO CIRCUIT ---------------------------------------
     * The carrier is raised by the arch main before this gate runs, which makes
     * boot ORDER load-bearing -- an assertion worth exactly nothing until it is
     * demonstrated. Drop the line and re-run the fixpoint: every module that
     * requires anything must fall to S0, because mb_real_power has no phase
     * reference to compute against and returns 0. Modules that require nothing
     * are unaffected: they were never in a circuit. */
    {
        uint32_t indep = 0;
        for (uint32_t i = 0; i < n; i++) {
            const mb_module_t *m = modbind_get(i);
            if (m && m->n_requires == 0) indep++;
        }
        mb_carrier_down();
        uint32_t dead = modbind_resolve();
        mb_carrier_up();
        uint32_t back = modbind_resolve();

        gp("         S- carrier test: line down -> S+ ready=");
        gp_u32(dead);
        gp(" (the ");
        gp_u32(indep);
        gp(" modules that require nothing); line up -> S+ ready=");
        gp_u32(back);
        gp("\n");

        if (dead != indep) {
            gp("  [FAIL] carrier down did not open every circuit --"
               " coupling is not carrier-conditional\n");
            problems++;
        }
        if (back != ready_at_entry) {
            gp("  [FAIL] carrier up did not restore the graph it took down\n");
            problems++;
        }
        if (dead == indep && back == ready_at_entry)
            gp("  [OK] carrier is load-bearing: no carrier, no circuit, and"
               " raising it restores the graph exactly\n");
    }

    /* ---- 2. S−: WITHDRAW, THEN RESTORE ------------------------------------ */
    {
        const char *cap = (const char *)0;
        uint32_t best = 0;
        for (uint32_t i = 0; i < n; i++) {
            const mb_module_t *m = modbind_get(i);
            if (!m) continue;
            for (uint8_t j = 0; j < m->n_provides; j++) {
                uint32_t c = gate_count_requirers(m->provides[j].name);
                if (c > best) { best = c; cap = m->provides[j].name; }
            }
        }
        if (!cap) {
            gp("         S- withdraw test: SKIPPED -- no declared capability has"
               " a requirer in this graph, so there is no reverse edge to test\n");
            return problems;
        }

        gp("         S- withdraw test: capability '");
        gate_put_cap(cap);
        gp("' chosen from the live graph -- ");
        gp_u32(best);
        gp(" module(s) require it\n");

        /* MEASURE THE EDGE BEFORE BREAKING IT. The numbers below are the whole
         * point of the electrical model, read off a real binding in the shipped
         * graph rather than from a fixture: the power factor is the interference
         * fringe (1000 + cos(dphase))/2 over the 13 phases as permille, and the
         * real power is that times the trit drive level.
         *
         * READ THIS AS A SAMPLE OF ONE, NEVER AS THE DISTRIBUTION. It picks the
         * FIRST provider and the FIRST requirer of this capability in LINK
         * ORDER, so its two numbers depend on the link line. That was harmless
         * while every declaration sat at phase 0 and every edge read 1000/1000;
         * it is misleading now that phases differ, because one sampled edge
         * says nothing about the other 78. gate_power_readout() above prints the
         * actual distribution. This line stays because it measures the specific
         * edge that is about to be broken, which is the right thing to measure
         * immediately before breaking it. */
        {
            const mb_cap_t *prov = (const mb_cap_t *)0;
            const mb_cap_t *req  = (const mb_cap_t *)0;
            for (uint32_t i = 0; i < n && !prov; i++) {
                const mb_module_t *m = modbind_get(i);
                if (!m) continue;
                for (uint8_t j = 0; j < m->n_provides; j++)
                    if (gate_cap_eq(m->provides[j].name, cap)) { prov = &m->provides[j]; break; }
            }
            for (uint32_t i = 0; i < n && !req; i++) {
                const mb_module_t *m = modbind_get(i);
                if (!m) continue;
                for (uint8_t j = 0; j < m->n_requires; j++)
                    if (gate_cap_eq(m->requires[j].name, cap)) { req = &m->requires[j]; break; }
            }
            if (prov && req) {
                int32_t p = mb_real_power(prov, req);
                gp("         coupling: phase provided="); gp_u32((uint32_t)prov->phase);
                gp(" required=");                          gp_u32((uint32_t)req->phase);
                gp("  power factor=");
                gp_u32(mb_power_factor(prov->phase, req->phase));
                gp("/1000  real power=");
                if (p < 0) { gp("-"); gp_u32((uint32_t)(-p)); } else gp_u32((uint32_t)p);
                gp("/1000 (carrier ");
                gp(mb_carrier() ? "up" : "DOWN");
                gp(")\n");
            }
        }

        uint32_t unreadied = modbind_withdraw(cap);
        uint32_t after_w   = gate_count_ready();

        gp("         S- withdrew: S+ ready ");
        gp_u32(ready_at_entry); gp(" -> "); gp_u32(after_w);
        gp(", un-readied="); gp_u32(unreadied); gp("\n");

        /* Every module that REQUIRED it must have left S+. Checked one by one,
         * not inferred from the count: a count that happens to match is not the
         * same claim as "these specific modules came down". */
        uint32_t still_ready = 0;
        const mb_module_t *witness = (const mb_module_t *)0;
        for (uint32_t i = 0; i < n; i++) {
            const mb_module_t *m = modbind_get(i);
            if (!m || !gate_requires_cap(m, cap)) continue;
            if (m->ready == MB_READY) { still_ready++; continue; }
            if (!witness) witness = m;
        }
        if (witness) {
            gp("         S- held: '"); gate_put_mod(witness->name);
            gp("' is now S0 -- ");
            gp(mb_hold_name(modbind_hold_reason(witness)));
            gp("\n");
        }
        if (still_ready != 0u) {
            gp("  [FAIL] "); gp_u32(still_ready);
            gp(" module(s) requiring '"); gate_put_cap(cap);
            gp("' stayed S+ after it was withdrawn\n");
            problems++;
        }

        uint32_t regained = modbind_restore(cap);
        uint32_t after_r  = gate_count_ready();
        gp("         S- restored: S+ ready ");
        gp_u32(after_w); gp(" -> "); gp_u32(after_r);
        gp(", regained="); gp_u32(regained); gp("\n");

        /* EXACT restoration, not approximate. A teardown test that leaves the
         * system slightly different from how it found it has changed the thing
         * it was measuring, and every later check inherits the damage. */
        if (after_r != ready_at_entry) {
            gp("  [FAIL] restore did not return the graph to its entry state\n");
            problems++;
        } else if (still_ready == 0u) {
            gp("  [OK] S-: withdrawal un-readied every requirer and restore"
               " returned all of them\n");
        }
    }
    return problems;
}

/* ---- the gate -------------------------------------------------------------
 * THE GATE RUNS BEFORE ANY BRING-UP, and that ordering is the whole point. An
 * unprovided requirement or a cycle would otherwise present as a boot that
 * stops with nothing to say. Verified first, run second.
 *
 * Return value: the TOTAL number of problems, so 0 means "sound, and every
 * bring-up that ran succeeded" and a caller can branch on it without re-deriving
 * anything. It is a sum of four independent faults, each of which is also
 * printed on its own line:
 *     +1        the linker collected NO declarations
 *     +refused  declarations modbind would not accept
 *     +problems modbind_verify_graph's own count (unprovided / cycle / contract)
 *     +failed   ready modules whose bring-up returned non-zero
 * Saturating at UINT32_MAX is not a concern: each term is bounded by the module
 * table size. A caller that only wants "may I proceed" compares against 0. */
uint32_t zxv_decl_boot_gate(zxv_puts_t puts)
{
    uint32_t total_problems = 0;
    uint32_t refused = 0, found = 0, registered = 0;

    /* No console, no gate. Returning 0 here would mean "sound", so return 1:
     * a gate that could not report is a problem, not a pass. */
    if (!puts) return 1u;
    g_puts = puts;

    gp("[BOOT] Declaration graph: modules declare provides/requires...\n");

    found      = zxv_decl_count();
    registered = zxv_decl_register_all(&refused);

    gp("  declarations collected="); gp_u32(found);
    gp(" registered=");              gp_u32(registered);
    gp(" refused=");                 gp_u32(refused);
    gp("\n");

    /* THE SHAPE OF THE GRAPH, BEFORE ANY VERDICT ABOUT IT. Deliberately ahead
     * of modbind_verify_graph: the distribution is a property of what was
     * registered, not of whether it is sound, and four of the five
     * architectures never reach the sound branch -- so measuring it after the
     * verdict would mean never measuring it there at all. */
    gate_power_readout();

    /* Zero collected is NOT "nothing to do". It means the linker script did
     * not place .rodata.zxv_decl, and a gate that then reports a sound graph
     * would be certifying an empty one -- this project has been burned by a
     * gate certifying a subsystem with zero symbols before. */
    if (found == 0u) {
        gp("  [FAIL] no declarations collected -- .rodata.zxv_decl was not placed\n");
        total_problems++;
    }
    if (refused != 0u) {
        gp("  [FAIL] modbind REFUSED declarations (table full, duplicate, or malformed)\n");
        total_problems += refused;
    }

    {
        mb_err_t err = MB_OK;
        const char *who = (const char *)0;
        uint32_t problems = modbind_verify_graph(&err, &who);

        if (problems != 0u) {
            total_problems += problems;
            gp("  [FAIL] GATE modbind_verify_graph: the graph is NOT sound\n");
            gp("         problems="); gp_u32(problems);
            gp("  first="); gp(mb_err_name(err));
            gp("  at module '"); gate_put_mod(who); gp("'\n");
            gate_report_all();
            /* No bring-up. Running one now would be running code whose
             * preconditions are known to be unsatisfiable. */
            gp("  [FAIL] bring-up REFUSED: fix the graph, not the symptom\n");
        } else {
            /* The Huygens fixpoint: every module whose requirements are already
             * provided becomes a provider, and therefore a source for the next
             * pass. No clock, no wave number, no barrier. */
            uint32_t total = modbind_count();
            uint32_t ready = modbind_resolve();
            uint32_t held  = (total > ready) ? (total - ready) : 0u;

            gp("  [OK] GATE modbind_verify_graph: acyclic, fully provided, contracts agree\n");
            gp("         S+ ready="); gp_u32(ready);
            gp("  S0 held=");         gp_u32(held);
            gp("  of ");              gp_u32(total);
            gp(" declared\n");

            /* S0 is an honest state, not a failure and not a silent skip. Name
             * every held module: "unreached" has to be legible or it is just
             * the old silence with a nicer word on it. */
            for (uint32_t i = 0; i < total; i++) {
                const mb_module_t *m = modbind_get(i);
                if (!m || m->ready == MB_READY) continue;
                gp("         S0 HELD: '"); gate_put_mod(m->name);
                /* WHICH KIND of held. "requirements not yet satisfied" was true
                 * of all of them and told the reader nothing about which fix
                 * applies: a missing provider needs one written, an out-of-phase
                 * one needs its phase or drive changed, a withdrawn one needs
                 * restoring, and a cycle needs the graph rethought. */
                gp("' -- "); gp(mb_hold_name(modbind_hold_reason(m))); gp("\n");
            }

            {
                uint32_t failed = 0, skipped = 0, decl_only = 0;
                uint32_t up = zxv_decl_bringup_ready(&failed, &skipped, &decl_only);
                gp("         bring-up: ran="); gp_u32(up);
                gp(" failed=");                gp_u32(failed);
                gp(" held=");                  gp_u32(skipped);
                gp(" declared-only=");         gp_u32(decl_only);
                gp("\n");
                /* S0-BY-OWN-REPORT. A bring-up that RAN and returned
                 * MB_BRINGUP_HELD met its requirements but fronts absent
                 * hardware/precondition. Not a failure, not counted in ran(S+):
                 * it is HELD, and named here with its reason, the same way a
                 * requirements-held module is named above -- so an absent MMU
                 * reads as a hardware fact, never as a [FAIL]. Named BEFORE the
                 * OK/FAIL verdict because it is true regardless of the verdict. */
                for (uint32_t i = 0; i < ZXV_DECL_MAX_FAILED; i++) {
                    const char *hn = zxv_decl_held_name(i);
                    if (!hn) break;
                    gp("         S0 HELD (bring-up): '");
                    gate_put_mod(hn);
                    gp("' -- its requirements are met but its"
                       " precondition/hardware is not present\n");
                }
                if (failed == 0u) {
                    /* Say what actually happened. `up` modules RAN a bring-up and
                     * it succeeded; `decl_only` declared themselves into the graph
                     * and ran nothing. Merging the two would report work that was
                     * never done. */
                    gp("  [OK] every bring-up that RAN succeeded or held on absent"
                       " hardware (declared-only modules ran nothing)\n");
                } else {
                    /* Name them. "failed=1" with no name means bisecting by
                     * rebuild to find one module -- the same defect
                     * gate_report_all exists to fix for the graph itself. */
                    total_problems += failed;
                    gp("  [FAIL] a ready module's bring-up reported failure\n");
                    for (uint32_t i = 0; i < failed; i++) {
                        const char *fn = zxv_decl_failed_name(i);
                        if (!fn) {
                            gp("         (further failures counted but not named;"
                               " raise ZXV_DECL_MAX_FAILED)\n");
                            break;
                        }
                        gp("         BRING-UP FAILED: '");
                        gate_put_mod(fn);
                        gp("' -- its own check returned non-zero\n");
                    }
                }
            }

            /* THE REVERSE EDGE, ON THE REAL GRAPH, AFTER BRING-UP.
             * Everything above this line is bring-up: the system only ever goes
             * forwards, which is precisely how modbind_withdraw came to be
             * compiled, garbage-collected and never executed on any
             * architecture. These two tests run last because they need a fully
             * resolved graph to take apart, and they put it back exactly as
             * they found it -- checked, not assumed. */
            total_problems += gate_negative_tests();
        }
    }

    g_puts = (zxv_puts_t)0;
    return total_problems;
}
