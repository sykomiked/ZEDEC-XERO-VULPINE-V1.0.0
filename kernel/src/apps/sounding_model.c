/* Copyright (c) 2024-2026 Michael Laurence Curzi and 36N9 Genetics, LLC */
/* SPDX-License-Identifier: Apache-2.0 */
/* sounding_model.c — Sounding (see sounding_model.h). */
#include <string.h>

#include "sounding_model.h"

void sounding_init(sounding_t *s, zxm_link_t *link)
{
    memset(s, 0, sizeof *s);
    s->link = link;
    s->rpc_rate = -1;
}

const sounding_if_t *sounding_find(const sounding_t *s, const char *name)
{
    for (uint32_t i = 0; i < s->n; i++)
        if (!strcmp(s->ifs[i].name, name)) return &s->ifs[i];
    return NULL;
}

/* a counter that may be null: false = absent or not a count */
static bool opt_u64(const zxm_link_t *l, int32_t o, const char *k, uint64_t *v, bool *known)
{
    int32_t t = zxm_get(l, o, k);
    if (t < 0) return false;
    if (l->tok[t].type == W4_J_NULL) {
        *known = false;
        *v = 0;
        return true;
    }
    return w4_json_u64(l->resp, &l->tok[t], v) == W4_OK;
}

static int64_t rate(uint64_t now, uint64_t was, uint64_t dt_ms)
{
    if (!dt_ms || now < was) return -1; /* a counter reset: no rate this time */
    return (int64_t) ((now - was) * 1000u / dt_ms);
}

int sounding_refresh(sounding_t *s)
{
    zxm_link_t *l = s->link;
    zxm_begin(l, "sys.net");
    int rc = zxm_call(l);
    if (rc) return rc;
    static sounding_t n;
    uint64_t now, req, err, bin, bout;
    bool sup;
    int32_t arr = zxm_get(l, l->result, "interfaces"), r = zxm_get(l, l->result, "rpc");
    if (zxm_get_u64(l, l->result, "time_ms", &now) ||
        zxm_get_bool(l, l->result, "supported", &sup) || arr < 0 || l->tok[arr].type != W4_J_ARR ||
        r < 0 || l->tok[r].type != W4_J_OBJ || zxm_get_u64(l, r, "requests", &req) ||
        zxm_get_u64(l, r, "errors", &err) || zxm_get_u64(l, r, "bytes_in", &bin) ||
        zxm_get_u64(l, r, "bytes_out", &bout))
        return ZXM_E_PROTOCOL;
    n = *s;
    uint64_t dt = s->time_ms && now > s->time_ms ? now - s->time_ms : 0;
    n.supported = sup;
    n.time_ms = now;
    n.n = 0;
    int64_t all_rx = 0, all_tx = 0;
    bool any = false, rated = dt != 0;
    uint32_t len = zxm_len(l, arr);
    for (uint32_t i = 0; i < len && n.n < SOUNDING_IFS; i++) {
        int32_t o = zxm_at(l, arr, i), a;
        sounding_if_t *f = &n.ifs[n.n];
        memset(f, 0, sizeof *f);
        bool known = true;
        uint64_t rxp, txp;
        if (o < 0 || zxm_get_str(l, o, "name", f->name, sizeof f->name) ||
            zxm_get_bool(l, o, "up", &f->up) || zxm_get_bool(l, o, "loopback", &f->loopback) ||
            (a = zxm_get(l, o, "addresses")) < 0 || l->tok[a].type != W4_J_ARR ||
            !opt_u64(l, o, "rx_bytes", &f->rx_bytes, &known) ||
            !opt_u64(l, o, "tx_bytes", &f->tx_bytes, &known) ||
            !opt_u64(l, o, "rx_packets", &rxp, &known) ||
            !opt_u64(l, o, "tx_packets", &txp, &known))
            return ZXM_E_PROTOCOL;
        f->counters = known;
        for (uint32_t k = 0; k < zxm_len(l, a) && f->naddr < SOUNDING_ADDRS; k++) {
            int32_t e = zxm_at(l, a, k);
            if (e < 0 || l->tok[e].type != W4_J_STR) return ZXM_E_PROTOCOL;
            if (w4_json_str(l->resp, &l->tok[e], f->addr[f->naddr], sizeof f->addr[0]) >= 0)
                f->naddr++;
        }
        const sounding_if_t *was = sounding_find(s, f->name);
        f->rx_rate = f->tx_rate = -1;
        if (was && was->counters && f->counters && dt) {
            f->rx_rate = rate(f->rx_bytes, was->rx_bytes, dt);
            f->tx_rate = rate(f->tx_bytes, was->tx_bytes, dt);
        }
        if (!f->loopback && f->counters) {
            any = true;
            if (f->rx_rate < 0 || f->tx_rate < 0)
                rated = false;
            else {
                all_rx += f->rx_rate;
                all_tx += f->tx_rate;
            }
        }
        n.n++;
    }
    n.rpc_rate = s->time_ms && dt ? rate(req, s->rpc_requests, dt) : -1;
    n.rpc_requests = req;
    n.rpc_errors = err;
    n.rpc_bytes_in = bin;
    n.rpc_bytes_out = bout;
    sounding_sample_t h = {now, any && rated ? all_rx : -1, any && rated ? all_tx : -1};
    if (n.nhist == SOUNDING_SAMPLES) {
        n.first = (n.first + 1) % SOUNDING_SAMPLES;
        n.nhist--;
    }
    n.hist[(n.first + n.nhist) % SOUNDING_SAMPLES] = h;
    n.nhist++;
    *s = n;
    return ZXM_OK;
}

uint32_t sounding_history_len(const sounding_t *s)
{
    return s->nhist;
}

const sounding_sample_t *sounding_history(const sounding_t *s, uint32_t i)
{
    return i < s->nhist ? &s->hist[(s->first + i) % SOUNDING_SAMPLES] : NULL;
}
