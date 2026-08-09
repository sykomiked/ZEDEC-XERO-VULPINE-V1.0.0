/* mrschema.c — MegaROM observed dynamics -> Sutra schemas. See mrschema.h.
 *
 * Freestanding: integer only, no libc, no allocation, no float.
 *
 * Author: H.M. Michael-Laurence: Curzi (c)  (ZXV MegaROM slice)
 * Copyright (c) 2024-2026 Michael Laurence Curzi and 36N9 Genetics, LLC
 * SPDX-License-Identifier: LicenseRef-OPL-1.1 AND CC-BY-SA-4.0
 * Licensed under OPL-1.1, SEL-3.3, the Royal Writ of the Sicilian Crown,
 * and CC BY-SA 4.0. See LICENSE at the repository root.
 */
#include "mrschema.h"

/* value/scale as permille, saturating at 1000. */
static uint16_t pm(uint32_t v, uint32_t scale) {
    if (scale == 0) return 0;
    uint64_t r = ((uint64_t)v * 1000u) / scale;
    return (uint16_t)(r > 1000u ? 1000u : r);
}
static uint32_t absdiff(uint32_t a, uint32_t b) { return a > b ? a - b : b - a; }

static void vector_of(const mrs_run_t *r, uint16_t d[MRS_DIM]) {
    d[MRS_TEMPO]     = pm(r->insn, 200000u);
    d[MRS_RENDER]    = pm(r->ppu_writes, 8000u);
    d[MRS_SYNC]      = pm(r->vblank_polls, 40000u);
    d[MRS_INTERRUPT] = pm(r->nmis, 150u);
    d[MRS_FOOTPRINT] = pm(r->bytes, 65536u);
    d[MRS_LIVENESS]  = r->running ? 800u : 200u;
}

int mrs_observe(const uint8_t id[MRS_ID_LEN], const mrs_run_t *runs,
                uint32_t nruns, mrs_profile_t *out) {
    if (!id || !runs || !out || nruns == 0) return -1;
    for (uint32_t i = 0; i < MRS_ID_LEN; i++) out->id[i] = id[i];
    out->runs = (uint8_t)(nruns > 255u ? 255u : nruns);

    /* dynamics = the mean across runs */
    uint32_t acc[MRS_DIM] = {0,0,0,0,0,0};
    for (uint32_t i = 0; i < nruns; i++) {
        uint16_t d[MRS_DIM];
        vector_of(&runs[i], d);
        for (uint32_t k = 0; k < MRS_DIM; k++) acc[k] += d[k];
    }
    for (uint32_t k = 0; k < MRS_DIM; k++) out->d[k] = (uint16_t)(acc[k] / nruns);

    /* AGENCY: mean divergence between runs driven by DIFFERENT input storms.
     * A title that ignores the player traces identically no matter what is
     * pressed; one that responds diverges. Runs sharing a seed prove nothing
     * about responsiveness, so they are not counted — and if no differing pair
     * exists we report "not measured" rather than inventing a number. */
    uint32_t pairs = 0, total = 0;
    for (uint32_t i = 0; i < nruns; i++) {
        for (uint32_t j = i + 1; j < nruns; j++) {
            if (runs[i].input_seed == runs[j].input_seed) continue;
            uint16_t a[MRS_DIM], b[MRS_DIM];
            vector_of(&runs[i], a); vector_of(&runs[j], b);
            uint32_t diff = 0;
            for (uint32_t k = 0; k < MRS_DIM; k++) diff += absdiff(a[k], b[k]);
            total += diff / MRS_DIM;
            pairs++;
        }
    }
    if (pairs == 0) { out->agency = 0; out->agency_measured = false; }
    else { out->agency = (uint16_t)(total / pairs); out->agency_measured = true; }
    return 0;
}

/* ---- Sutra emission ---- */
static uint32_t sput(char *b, uint32_t max, uint32_t o, const char *s) {
    while (*s && o + 1 < max) b[o++] = *s++;
    return o;
}
static uint32_t sputu(char *b, uint32_t max, uint32_t o, uint32_t v) {
    char t[12]; int n = 0;
    if (v == 0) t[n++] = '0';
    while (v) { t[n++] = (char)('0' + v % 10); v /= 10; }
    while (n && o + 1 < max) b[o++] = t[--n];
    return o;
}
static uint32_t sputhex(char *b, uint32_t max, uint32_t o, const uint8_t *p, uint32_t n) {
    const char *h = "0123456789abcdef";
    for (uint32_t i = 0; i < n && o + 2 < max; i++) {
        b[o++] = h[p[i] >> 4]; b[o++] = h[p[i] & 0xF];
    }
    return o;
}

static const char *DIMNAME[MRS_DIM] = {
    "TEMPO", "RENDER-PRESSURE", "FRAME-SYNC", "INTERRUPT-CADENCE",
    "FOOTPRINT", "LIVENESS"
};

int mrs_emit_sutra(const mrs_profile_t *p, char *buf, uint32_t max) {
    if (!p || !buf || max < 256) return -1;
    uint32_t o = 0;
    o = sput(buf, max, o, "SUTRA-PROGRAM. MEGAROM-DYNAMICS.\n");
    o = sput(buf, max, o, "ENVIRONMENT SECTION.\n");
    o = sput(buf, max, o, "    CONFIGURATION.\n");
    o = sput(buf, max, o, "        SOURCE. OBSERVED-EMULATOR-RUN.\n");
    o = sput(buf, max, o, "        CONTENT. NONE-CARRIED.\n");
    o = sput(buf, max, o, "        MODE. PARACONSISTENT.\n");
    o = sput(buf, max, o, "\nDATA SECTION.\n");

    /* the corpus id is a DEV REFERENCE (digest prefix), never a title */
    o = sput(buf, max, o, "    01 CORPUS-REF        PIC X(16) VALUE \"");
    o = sputhex(buf, max, o, p->id, 8);
    o = sput(buf, max, o, "\".\n");
    o = sput(buf, max, o, "    01 RUNS-OBSERVED    SUTRA-AMOUNT VALUE ");
    o = sputu(buf, max, o, p->runs); o = sput(buf, max, o, "/1.\n");

    for (uint32_t k = 0; k < MRS_DIM; k++) {
        o = sput(buf, max, o, "    01 ");
        o = sput(buf, max, o, DIMNAME[k]);
        o = sput(buf, max, o, "  SUTRA-AMOUNT VALUE ");
        o = sputu(buf, max, o, p->d[k]);
        o = sput(buf, max, o, "/1000.\n");
    }

    /* Agency is reported as UNMEASURED rather than as zero when only one input
     * storm was run — "we did not look" and "it does not respond" are different
     * claims, and a schema that conflates them would poison the corpus. */
    o = sput(buf, max, o, "    01 AGENCY            SUTRA-AMOUNT VALUE ");
    if (p->agency_measured) {
        o = sputu(buf, max, o, p->agency);
        o = sput(buf, max, o, "/1000.\n");
        o = sput(buf, max, o, "    01 AGENCY-STATUS     SUTRA-STATUS VALUE MEASURED.\n");
    } else {
        o = sput(buf, max, o, "0/1000.\n");
        o = sput(buf, max, o, "    01 AGENCY-STATUS     SUTRA-STATUS VALUE UNMEASURED.\n");
    }

    o = sput(buf, max, o, "\nEXECUTION SECTION.\n");
    o = sput(buf, max, o, "    STEP-1 @ORDINAL(1). RECORD DYNAMICS.\n");
    o = sput(buf, max, o, "    STEP-2 @ORDINAL(2). RELATE ACROSS CORPUS.\n");
    if (o + 1 >= max) return -2;
    buf[o] = '\0';
    return (int)o;
}

/* ---- relations ---- */
mrs_relation_t mrs_relate(const mrs_profile_t *a, const mrs_profile_t *b) {
    mrs_relation_t r = { 0, 0 };
    if (!a || !b) return r;
    uint32_t diff = 0;
    for (uint32_t k = 0; k < MRS_DIM; k++) diff += absdiff(a->d[k], b->d[k]);
    /* Agency participates only when BOTH sides measured it. When exactly one
     * did, the pair is separated by an UNCERTAINTY term instead: we have not
     * established that they differ, but neither have we established that they
     * match, and declaring two titles identical on a dimension one of them
     * never measured would be asserting knowledge we do not have. The penalty
     * is deliberately large enough that such a pair can never be deduped as a
     * duplicate — an unmeasured title stays in the corpus until it is measured. */
    uint32_t dims = MRS_DIM;
    if (a->agency_measured && b->agency_measured) {
        diff += absdiff(a->agency, b->agency);
        dims++;
    } else if (a->agency_measured != b->agency_measured) {
        diff += 500u;            /* maximal uncertainty on one dimension */
        dims++;
    }
    uint32_t mean = diff / dims;                 /* 0..1000 */
    if (mean > 1000u) mean = 1000u;
    r.complementarity = (uint16_t)mean;
    r.affinity = (uint16_t)(1000u - mean);
    return r;
}

int mrs_matrix(const mrs_profile_t *p, uint32_t n, uint16_t affinity_threshold,
               mrs_relation_t *rel, uint32_t *distinct_out) {
    if (!p || !rel || n == 0 || n > MRS_MAX_TITLES) return -1;
    for (uint32_t i = 0; i < n; i++)
        for (uint32_t j = 0; j < n; j++)
            rel[i * n + j] = mrs_relate(&p[i], &p[j]);

    /* Distinct = titles not already represented by an earlier one at or above
     * the affinity threshold. This is the dedup number the corpus exists to
     * produce: how many DESIGNS are in it, as opposed to how many files. */
    uint32_t distinct = 0;
    for (uint32_t i = 0; i < n; i++) {
        bool dup = false;
        for (uint32_t j = 0; j < i; j++)
            if (rel[i * n + j].affinity >= affinity_threshold) { dup = true; break; }
        if (!dup) distinct++;
    }
    if (distinct_out) *distinct_out = distinct;
    return 0;
}

int mrs_widest_axis(const mrs_profile_t *p, uint32_t n,
                    uint32_t *a_out, uint32_t *b_out) {
    if (!p || n < 2) return -1;
    uint16_t best = 0; uint32_t ba = 0, bb = 1;
    for (uint32_t i = 0; i < n; i++)
        for (uint32_t j = i + 1; j < n; j++) {
            mrs_relation_t r = mrs_relate(&p[i], &p[j]);
            if (r.complementarity > best) { best = r.complementarity; ba = i; bb = j; }
        }
    if (a_out) *a_out = ba;
    if (b_out) *b_out = bb;
    return 0;
}
