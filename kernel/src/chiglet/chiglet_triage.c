/* Copyright (c) 2024-2026 Michael Laurence Curzi and 36N9 Genetics, LLC */
/* SPDX-License-Identifier: LicenseRef-OPL-1.1 AND CC-BY-SA-4.0 AND LicenseRef-Royal-Writ-Sicilian-Crown-1.0 AND LicenseRef-SEL-3.3 */
/* chiglet_triage.c — give Chiglet its FIRST REAL DECISION: how many of the faults
 * a Game Master campaign reports are GENUINELY INDEPENDENT bugs?
 *
 * ============================================================================
 * WHY THIS IS THE HONEST FIRST JOB FOR CHIGLET
 * ============================================================================
 * PROVENANCE/CHIGLET_BONDING.md states it plainly: "The companion has never
 * decided anything." Measured against the shipped arm64 ELF, that is exactly
 * right -- chg_infer and chg_load_model are NOT LINKED, and the single
 * production call site is chg_init(&chg, 0u), i.e. capabilities ZERO, so
 * inference is not even granted. What IS in the binary is:
 *
 *     chg_interaction        u = 1 - cos^2(theta)   -- independence of two vectors
 *     chg_effective_experts  R = n^2 / sum(cos^2)   -- participation ratio
 *
 * Those two are precisely a REDUNDANCY DETECTOR, and redundancy is exactly the
 * campaign's problem. 14,343 ROM boots produce a pile of crash signatures in
 * which most entries are THE SAME BUG reached from different ROMs. The previous
 * 16,200-run campaign reported "2 kernel faults" -- both at proc_restore_el0,
 * at +0x2c and +0x3c. Two reports. Almost certainly one bug.
 *
 * So this tool asks Chiglet the one question its built math can actually answer,
 * using ONLY functions that are in the ELF today. No GPU. No new hardness. No
 * inference path that does not exist.
 *
 * ============================================================================
 * THE BOUND WE DO NOT PRETEND AWAY
 * ============================================================================
 * CHG_MAX_EXPERTS is 8. chg_effective_experts therefore accepts at most EIGHT
 * evidence vectors per call -- it cannot be handed 500 fault signatures. Rather
 * than reimplement a bigger version and call it Chiglet, this tool:
 *   1. uses the REAL chg_interaction pairwise for arbitrary N, and
 *   2. clusters with union-find at the SAME CHG_U_MERGE threshold that
 *      chg_effective_experts uses internally, then
 *   3. calls the REAL chg_effective_experts on cluster representatives, in
 *      windows of <= 8, to obtain R for the surviving set.
 * Step 2 is the same rule, applied outside the 8-vector bound. That is a
 * deliberate, stated extension -- not a claim that Chiglet scales to 500.
 *
 * ============================================================================
 * HOW A FAULT BECOMES AN 8-DIMENSIONAL VECTOR
 * ============================================================================
 * CHG_DIM is 8, so the encoding must be 8 numbers, and each must mean something
 * a human can argue with:
 *   [0] fault class      (data abort / prefetch / undef / panic / watchdog)
 *   [1] faulting PC, high bits   -- which function
 *   [2] faulting PC, low bits    -- which instruction inside it
 *   [3] fault address locality   -- null-ish, stack-ish, heap-ish, wild
 *   [4] exception level / mode
 *   [5] console family the ROM belongs to
 *   [6] boot phase reached before the fault
 *   [7] tri-space polarity of the run (S+/S-)
 * Two faults from the same bug agree on [0..4] and differ on [5..7].
 *
 * MEASURED CORRECTION -- THE FIRST VERSION OF THIS ENCODING DID NOT WORK.
 * The comment here used to assert that the cosine "is therefore dominated by
 * the parts that identify the BUG". It is not, and saying so did not make it
 * so. cos^2 weights every dimension EQUALLY by construction, so the ROM-identity
 * dims swing the result exactly as hard as the bug-identity dims. Measured on
 * five synthetic reports of which three were one bug at proc_restore_el0
 * +0x2c/+0x30/+0x3c, the tool merged +0x2c with +0x30 and left +0x3c ALONE --
 * because its polarity dim differed. Wrong answer, right machinery.
 *
 * The fix is magnitude, not a threshold tweak: dominance in a cosine is bought
 * with LENGTH. Bug-identity dims [0..4] are scaled by W_BUG and ROM-identity
 * dims [5..7] by W_ROM << W_BUG, so ROM identity perturbs the direction instead
 * of steering it. This is a property of the ENCODING; chg_interaction is
 * untouched and remains the verified in-ELF primitive. */
#define W_BUG  1.0   /* dims 0..4 -- fault class, PC, address locality, EL      */
#define W_ROM  0.15  /* dims 5..7 -- console, boot phase, polarity: context only
 *
 * Host tool. Build:
 *   gcc -std=c11 -Wall -Werror -Wextra -DTEST_HOST -O2 \
 *       -Ikernel/src/chiglet -Ikernel/src/surplus -Ikernel/include \
 *       chiglet_triage.c ../chiglet/chiglet.c -lm -o chiglet_triage
 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdint.h>

#include "chiglet.h"

/* DISPLAY ONLY. surplus.h supplies SR_FROM_* but no SR_TO_*, deliberately: the
 * kernel never needs to leave the fixed-point domain, and a to-float macro in a
 * freestanding header invites exactly the host/target divergence that
 * surplus.h's own header warns about. This tool only prints, so a local helper
 * is honest -- and it is written to be correct in BOTH builds rather than
 * assuming the host double:
 *     TEST_HOST : SR_ONE == 1.0   -> x / 1.0  == x
 *     target    : SR_ONE == 2^32  -> x / 2^32 == the Q32.32 value
 * Never use this in a computation; use the SR_* operators for that. */
static double sr_show(surplus_real_t x) { return (double)(x) / (double)SR_ONE; }

#define MAX_FAULTS 4096

typedef struct {
    char  id[64];
    char  sig[128];
    surplus_real_t v[CHG_DIM];
    int   cluster;
} fault_t;

static fault_t g_f[MAX_FAULTS];
static int     g_n = 0;

/* union-find at CHG_U_MERGE -- the same threshold chg_effective_experts uses */
static int g_parent[MAX_FAULTS];
static int uf_find(int x){ while(g_parent[x]!=x){ g_parent[x]=g_parent[g_parent[x]]; x=g_parent[x]; } return x; }
static void uf_union(int a,int b){ a=uf_find(a); b=uf_find(b); if(a!=b) g_parent[b]=a; }

/* Trim, then parse "id<TAB>d0,d1,...,d7<TAB>signature" */
static int load_faults(const char *path)
{
    FILE *f = fopen(path, "r");
    char line[512];
    if (!f) { fprintf(stderr, "cannot open %s\n", path); return -1; }
    while (fgets(line, sizeof line, f) && g_n < MAX_FAULTS) {
        char *id, *vec, *sig, *save = NULL;
        unsigned d;
        if (line[0] == '#' || line[0] == '\n') continue;
        id  = strtok_r(line, "\t", &save);
        vec = strtok_r(NULL, "\t", &save);
        sig = strtok_r(NULL, "\t\n", &save);
        if (!id || !vec) continue;
        snprintf(g_f[g_n].id,  sizeof g_f[g_n].id,  "%s", id);
        snprintf(g_f[g_n].sig, sizeof g_f[g_n].sig, "%s", sig ? sig : "");
        {   char *t, *s2 = NULL;
            d = 0;
            for (t = strtok_r(vec, ",", &s2); t && d < CHG_DIM; t = strtok_r(NULL, ",", &s2)) {
                double w = (d < 5u) ? W_BUG : W_ROM;   /* see MEASURED CORRECTION */
                g_f[g_n].v[d++] = SR_FROM_FLOAT(atof(t) * w);
            }
            while (d < CHG_DIM) g_f[g_n].v[d++] = SR_FROM_FLOAT(0.0);
        }
        g_f[g_n].cluster = -1;
        g_n++;
    }
    fclose(f);
    return g_n;
}

int main(int argc, char **argv)
{
    int i, j, clusters = 0;
    chiglet_t chg;

    if (argc != 2) { fprintf(stderr, "usage: %s <faults.tsv>\n", argv[0]); return 2; }
    if (load_faults(argv[1]) <= 0) { fprintf(stderr, "no faults loaded\n"); return 1; }

    /* caps 0: we use only the metric functions, never chg_infer. Asking for
     * CHG_CAP_INFER would be asking for a capability whose code is not linked. */
    chg_init(&chg, 0u);

    printf("Chiglet fault triage\n");
    printf("  input faults          : %d\n", g_n);
    printf("  CHG_DIM               : %u\n", (unsigned)CHG_DIM);
    printf("  CHG_MAX_EXPERTS       : %u   (chg_effective_experts bound)\n", (unsigned)CHG_MAX_EXPERTS);
    printf("  merge threshold u <   : %.4f (CHG_U_MERGE)\n", sr_show(CHG_U_MERGE));
    printf("\n");

    for (i = 0; i < g_n; i++) g_parent[i] = i;

    /* Pairwise independence via the REAL chg_interaction. u near 0 means the two
     * signatures point the same way in evidence space, i.e. the same bug. */
    for (i = 0; i < g_n; i++)
        for (j = i + 1; j < g_n; j++) {
            surplus_real_t u = chg_interaction(g_f[i].v, g_f[j].v, CHG_DIM);
            if (sr_show(u) < sr_show(CHG_U_MERGE)) uf_union(i, j);
        }

    /* label clusters */
    for (i = 0; i < g_n; i++) if (uf_find(i) == i) g_f[i].cluster = clusters++;
    for (i = 0; i < g_n; i++) if (g_f[i].cluster < 0) g_f[i].cluster = g_f[uf_find(i)].cluster;

    printf("VERDICT: %d reported fault%s -> %d genuinely independent\n",
           g_n, g_n == 1 ? "" : "s", clusters);
    printf("         %d report%s collapsed as duplicates of another\n\n",
           g_n - clusters, (g_n - clusters) == 1 ? "" : "s");

    for (int c = 0; c < clusters; c++) {
        int members = 0, first = -1;
        for (i = 0; i < g_n; i++) if (g_f[i].cluster == c) { members++; if (first < 0) first = i; }
        printf("  cluster %d  (%d report%s)  representative: %s\n",
               c, members, members == 1 ? "" : "s", g_f[first].id);
        if (g_f[first].sig[0]) printf("      %s\n", g_f[first].sig);
        if (members > 1) {
            printf("      merged:");
            for (i = 0; i < g_n; i++) if (g_f[i].cluster == c && i != first) printf(" %s", g_f[i].id);
            printf("\n");
        }
    }

    /* R over cluster representatives, honouring the 8-vector bound. */
    if (clusters >= 2) {
        surplus_real_t reps[CHG_MAX_EXPERTS][CHG_DIM];
        uint32_t distinct = 0, k = 0;
        for (int c = 0; c < clusters && k < CHG_MAX_EXPERTS; c++)
            for (i = 0; i < g_n; i++)
                if (g_f[i].cluster == c) { memcpy(reps[k], g_f[i].v, sizeof reps[k]); k++; break; }
        {
            surplus_real_t R = chg_effective_experts(reps, k, CHG_DIM, &distinct);
            printf("\n  chg_effective_experts over %u representative%s: R = %.3f, distinct = %u\n",
                   k, k == 1 ? "" : "s", sr_show(R), distinct);
            if (clusters > (int)CHG_MAX_EXPERTS)
                printf("  NOTE: %d clusters exceed CHG_MAX_EXPERTS=%u; R covers the first %u only.\n",
                       clusters, (unsigned)CHG_MAX_EXPERTS, k);
        }
    }
    return 0;
}
