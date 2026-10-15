/* Copyright (c) 2024-2026 Michael Laurence Curzi and 36N9 Genetics, LLC */
/* SPDX-License-Identifier: Apache-2.0 */
/* pmm.c — see pmm.h. */
#include "pmm.h"

static unsigned get2(const pmm_t *p, uint32_t f)
{
    return (unsigned) (p->bits[f >> 2] >> ((f & 3u) * 2u)) & 3u;
}

static void set2(pmm_t *p, uint32_t f, unsigned v)
{
    unsigned sh = (f & 3u) * 2u;
    p->bits[f >> 2] = (uint8_t) ((p->bits[f >> 2] & ~(3u << sh)) | ((v & 3u) << sh));
}

static int frame_of(const pmm_t *p, uint64_t pa, uint32_t *f)
{
    if (!p || !p->bits || pa < p->base || (pa - p->base) % PMM_FRAME) return 0;
    uint64_t idx = (pa - p->base) / PMM_FRAME;
    if (idx >= p->nframes) return 0;
    *f = (uint32_t) idx;
    return 1;
}

int pmm_init(pmm_t *p, uint64_t base, uint32_t nframes, uint8_t *bits)
{
    if (!p || !bits || nframes == 0 || base % PMM_FRAME) return -1;
    p->base = base;
    p->nframes = nframes;
    p->bits = bits;
    p->hint = 0;
    uint32_t nb = PMM_BITS_BYTES(nframes);
    for (uint32_t i = 0; i < nb; i++) bits[i] = 0x55u; /* 01 01 01 01: all N */
    /* the tail of the last byte holds no frames: mark it F so a scan never
     * hands it out */
    for (uint32_t f = nframes; f < nb * 4u; f++) set2(p, f, PMM_F);
    p->n_free = nframes;
    p->n_owned = p->n_fenced = p->n_unusable = 0;
    return 0;
}

unsigned pmm_state(const pmm_t *p, uint64_t pa)
{
    uint32_t f;
    return frame_of(p, pa, &f) ? get2(p, f) : PMM_F;
}

int pmm_owns_range(const pmm_t *p, uint64_t pa)
{
    uint32_t f;
    return frame_of(p, pa, &f);
}

lpres_state_t pmm_reserve(pmm_t *p, uint64_t pa)
{
    uint32_t f;
    if (!frame_of(p, pa, &f) || get2(p, f) != PMM_N) return LPRES_STATE_FALSE;
    set2(p, f, PMM_F);
    p->n_free--;
    p->n_unusable++;
    return LPRES_STATE_TRUE;
}

/* Does this byte hold a free (01) frame? A free frame is a 2-bit field
 * whose low bit is 1 and high bit is 0. */
static int byte_has_free(uint8_t b)
{
    return (b & ~(b >> 1) & 0x55u) != 0;
}

lpres_state_t pmm_alloc(pmm_t *p, uint64_t *pa_out)
{
    if (!p || !p->bits || !pa_out) return LPRES_STATE_FALSE;
    if (p->n_free == 0) return LPRES_STATE_NEITHER;
    uint32_t nb = PMM_BITS_BYTES(p->nframes);
    for (uint32_t k = 0; k < nb; k++) {
        uint32_t i = (p->hint + k) % nb;
        uint8_t b = p->bits[i];
        if (!byte_has_free(b)) continue;
        for (uint32_t j = 0; j < 4u; j++) {
            if (((b >> (j * 2u)) & 3u) == PMM_N) {
                uint32_t f = i * 4u + j;
                set2(p, f, PMM_T);
                p->n_free--;
                p->n_owned++;
                p->hint = i;
                *pa_out = p->base + (uint64_t) f * PMM_FRAME;
                return LPRES_STATE_TRUE;
            }
        }
    }
    /* n_free said there was one: the counters and the bitmap disagree */
    return LPRES_STATE_BOTH;
}

lpres_state_t pmm_free(pmm_t *p, uint64_t pa)
{
    uint32_t f;
    if (!frame_of(p, pa, &f)) return LPRES_STATE_FALSE;
    switch (get2(p, f)) {
    case PMM_T:
        set2(p, f, PMM_N);
        p->n_owned--;
        p->n_free++;
        return LPRES_STATE_TRUE;
    case PMM_N: /* freed while free: a double free. Fence it for good. */
        set2(p, f, PMM_B);
        p->n_free--;
        p->n_fenced++;
        return LPRES_STATE_BOTH;
    default: /* F or B: nothing of ours to free, and no change */
        return LPRES_STATE_FALSE;
    }
}

lpres_state_t pmm_claim(pmm_t *p, uint64_t pa)
{
    uint32_t f;
    if (!frame_of(p, pa, &f)) return LPRES_STATE_FALSE;
    switch (get2(p, f)) {
    case PMM_N:
        set2(p, f, PMM_T);
        p->n_free--;
        p->n_owned++;
        return LPRES_STATE_TRUE;
    case PMM_T: /* two owners claim one frame: fence it */
        set2(p, f, PMM_B);
        p->n_owned--;
        p->n_fenced++;
        return LPRES_STATE_BOTH;
    default:
        return LPRES_STATE_FALSE;
    }
}
