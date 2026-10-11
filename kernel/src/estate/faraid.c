/* Copyright (c) 2024-2026 Michael Laurence Curzi and 36N9 Genetics, LLC */
/* SPDX-License-Identifier: Apache-2.0 */
/* faraid.c — advisory faraid shares. See faraid.h F1-F6. */
#include "faraid.h"

static uint64_t gcd64(uint64_t a, uint64_t b)
{
    while (b) {
        uint64_t t = a % b;
        a = b;
        b = t;
    }
    return a;
}

static void scale(uint64_t v[EST_FA_N], uint64_t k)
{
    for (int i = 0; i < EST_FA_N; i++) v[i] *= k;
}

est_status_t est_faraid(const est_faraid_heirs_t *h, est_faraid_t *out)
{
    if (!h || !out) return EST_ERR_ARG;
    if (h->others) return EST_ERR_UNSUPPORTED;
    if ((h->husband && h->wives) || h->wives > EST_FARAID_MAX_WIVES || h->sons > EST_MAX_HEIRS ||
        h->daughters > EST_MAX_HEIRS)
        return EST_ERR_ARG;
    bool kids = h->sons || h->daughters, spouse = h->husband || h->wives;
    if (!spouse && !kids && !h->father && !h->mother) return EST_ERR_ARG;
    uint64_t v[EST_FA_N] = {0}, D = 24;
    /* F2, in 24ths */
    if (h->husband) v[EST_FA_HUSBAND] = kids ? 6 : 12;
    if (h->wives) v[EST_FA_WIVES] = kids ? 3 : 6;
    uint64_t s = v[EST_FA_HUSBAND] + v[EST_FA_WIVES];
    if (h->mother) {
        if (spouse && h->father && !kids)
            v[EST_FA_MOTHER] = 8 - s / 3; /* Umariyyatan: (24 - s) / 3, s is 6 or 12 */
        else
            v[EST_FA_MOTHER] = kids ? 4 : 8;
    }
    if (h->father && kids) v[EST_FA_FATHER] = 4;
    if (h->daughters && !h->sons) v[EST_FA_DAUGHTERS] = h->daughters == 1 ? 12 : 16;
    uint64_t F = 0;
    for (int i = 0; i < EST_FA_N; i++) F += v[i];
    out->awl = out->radd = false;
    if (F > 24) {
        D = F; /* F4 awl */
        out->awl = true;
    } else if (h->sons) {
        /* F3: units = 2 sons + daughters; scale so the residue splits exactly */
        uint64_t units = 2u * h->sons + h->daughters, r = 24 - F;
        scale(v, units);
        D *= units;
        v[EST_FA_SONS] = r * 2u * h->sons;
        v[EST_FA_DAUGHTERS] = r * h->daughters;
    } else if (h->father) {
        v[EST_FA_FATHER] += 24 - F; /* F3 */
    } else {
        /* F5 radd, the spouse excluded. F < 24 here: no set of these heirs
         * without a residuary has fixed shares of exactly 1. */
        uint64_t O = F - s;
        if (O == 0) {
            v[EST_FA_REST] = 24 - F;
        } else {
            /* spouse s O / 24 O; the others o (24 - s) / 24 O */
            out->radd = true;
            v[EST_FA_HUSBAND] *= O;
            v[EST_FA_WIVES] *= O;
            v[EST_FA_MOTHER] *= 24 - s;
            v[EST_FA_DAUGHTERS] *= 24 - s;
            D *= O;
        }
    }
    for (int i = 0; i < EST_FA_N; i++) {
        uint64_t g = gcd64(v[i], D);
        out->share[i] = (est_frac_t){v[i] / g, D / g};
    }
    out->advisory = true;
    return EST_OK;
}

est_status_t est_set_faraid(est_t *e, const est_faraid_heirs_t *h, const uint32_t id[])
{
    if (!e || !id) return EST_ERR_ARG;
    est_faraid_t f;
    est_status_t st = est_faraid(h, &f);
    if (st != EST_OK) return st;
    if (f.share[EST_FA_REST].num) return EST_ERR_UNSUPPORTED;
    /* persons per class, in id order */
    uint32_t cnt[EST_FA_REST] = {h->husband ? 1u : 0u, h->wives, h->father ? 1u : 0u,
                                 h->mother ? 1u : 0u,  h->sons,  h->daughters};
    uint32_t n = 0;
    for (int c = 0; c < EST_FA_REST; c++) n += cnt[c];
    if (n > EST_MAX_HEIRS) return EST_ERR_FULL;
    uint64_t num[EST_MAX_HEIRS], den[EST_MAX_HEIRS];
    uint32_t k = 0;
    for (int c = 0; c < EST_FA_REST; c++)
        for (uint32_t j = 0; j < cnt[c]; j++, k++) {
            num[k] = f.share[c].num; /* an equal part of the class share */
            den[k] = f.share[c].den * cnt[c];
        }
    return est_set_manifest(e, EST_FARAID, n, id, num, den);
}
