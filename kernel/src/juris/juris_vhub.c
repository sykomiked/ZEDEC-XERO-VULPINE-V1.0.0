/* Copyright (c) 2024-2026 Michael Laurence Curzi and 36N9 Genetics, LLC */
/* SPDX-License-Identifier: Apache-2.0 */
/* juris_vhub.c — juris.h J6: the host as a vhub rule, vhub's limit rule as a plug-in. */
#include "juris_vhub.h"

static lpx_t tag(uint8_t base)
{
    return lpx_make(base, LPX_P_NEUTRAL, LPX_PH_POTENTIAL);
}

static void zero(void *p, size_t n)
{
    uint8_t *b = p;
    while (n--) *b++ = 0;
}

/* ---- J6 ---- */
static void from_swap(const vhub_swap_t *sw, juris_transfer_t *t)
{
    zero(t, sizeof *t);
    t->amount = sw->amount;
    t->sender = sw->sender;
    t->receiver = sw->receiver;
    t->epoch = sw->epoch;
}

lpx_t juris_vhub_rule(void *host, const vhub_swap_t *sw, uint8_t side)
{
    if (!host || !sw) return tag(LPX_B_NEITHER);
    if (side != 0) return tag(LPX_B_TRUE); /* the host judges outflows */
    juris_transfer_t t;
    from_swap(sw, &t);
    return juris_check_transfer(host, &t);
}

void juris_vhub_commit(void *host, const vhub_swap_t *sw, uint8_t side)
{
    if (!host || !sw || side != 0) return;
    juris_transfer_t t;
    from_swap(sw, &t);
    juris_commit_transfer(host, &t);
}

static void to_swap(const juris_transfer_t *t, vhub_swap_t *sw)
{
    zero(sw, sizeof *sw);
    sw->amount = t->amount;
    sw->sender = t->sender;
    sw->receiver = t->receiver;
    sw->epoch = t->epoch;
}

static lpx_t limit_transfer(void *ctx, const juris_transfer_t *t)
{
    vhub_swap_t sw;
    to_swap(t, &sw);
    return vhub_rule_limit(ctx, &sw, 0);
}

static void limit_commit(void *ctx, const juris_transfer_t *t)
{
    vhub_swap_t sw;
    to_swap(t, &sw);
    vhub_limit_commit(ctx, &sw, 0);
}

void juris_limit_plugin(juris_plugin_t *p, const char *id, const char *country, vhub_limit_t *l)
{
    if (!p) return;
    zero(p, sizeof *p);
    p->abi = JURIS_PLUGIN_ABI;
    p->size = (uint32_t) sizeof *p;
    for (unsigned i = 0; id && i < JURIS_ID_LEN && id[i]; i++) p->id[i] = id[i];
    if (country && country[0] && country[1]) {
        p->country[0] = country[0];
        p->country[1] = country[1];
    }
    p->hooks = JURIS_HOOK_TRANSFER | JURIS_HOOK_COMMIT;
    p->ctx = l;
    p->transfer = limit_transfer;
    p->commit = limit_commit;
}
