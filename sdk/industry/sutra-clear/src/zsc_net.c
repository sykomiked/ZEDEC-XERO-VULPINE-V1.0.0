/* Copyright (c) 2024-2026 Michael Laurence Curzi and 36N9 Genetics, LLC */
/* SPDX-License-Identifier: Apache-2.0 */
/* zsc_net.c — Sutra-Clear's netting backends (the ABI 1.5 treaty engine,
 * or zxv_netting_* where the library has it) and the ledger journal reader
 * behind the "ZSJ1" export and the journal digest. Integer code only
 * (R11). */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "zsc_internal.h"

#define TREATY_PARTIES 16u
#define TREATY_OWES    64u

static int add_ok(int64_t a, int64_t b, int64_t *out) {
    return !__builtin_add_overflow(a, b, out);
}

/* Run one batch through zxv_treaty_run (OP_NET) and add its positions. */
static int32_t run_batch(const char *ccy, uint32_t batch, const uint32_t *gidx, uint32_t n_local,
                         const zsc_oblig *obs, const uint32_t *lfrom, const uint32_t *lto,
                         size_t n, int64_t *acc) {
    char text[ZXV_TREATY_SUTRA_MAX + 64];
    size_t o = 0;
    int k;
    uint32_t off = 0, parties = 0, legs = 0;
    int32_t fault = 0, st;
    int64_t fees = 0, sum = 0;
    zxv_treaty *t = NULL;
    zxv_treaty_result *r = NULL;
    k = snprintf(text, sizeof text, "TREATY N%u %s NET\n", batch, ccy);
    o = (size_t) k;
    for (uint32_t i = 0; i < n_local; i++) {
        k = snprintf(text + o, sizeof text - o, "PARTY P%u 0 0 %s\n", i + 1, i == 0 ? "1/1" : "0/1");
        if (k < 0 || (size_t) k >= sizeof text - o) return ZXV_E_INTERNAL;
        o += (size_t) k;
    }
    for (size_t j = 0; j < n; j++) {
        k = snprintf(text + o, sizeof text - o, "OWE P%u P%u %lld\n", lfrom[j] + 1, lto[j] + 1,
                     (long long) obs[j].amount);
        if (k < 0 || (size_t) k >= sizeof text - o) return ZXV_E_INTERNAL;
        o += (size_t) k;
    }
    k = snprintf(text + o, sizeof text - o, "END\n");
    if (k < 0 || (size_t) k >= sizeof text - o) return ZXV_E_INTERNAL;
    st = zxv_treaty_create(&t);
    if (st != ZXV_OK) return st;
    st = zxv_treaty_parse_sutra(t, text, &off, &fault);
    if (st != ZXV_OK) {
        zind_set_error("zsc netting: the treaty engine refused batch %u (fault %d at %u)", batch,
                       (int) fault, off);
        zxv_treaty_destroy(t);
        return st;
    }
    st = zxv_treaty_run(t, &r);
    zxv_treaty_destroy(t);
    if (st != ZXV_OK) {
        zind_set_error("zsc netting: zxv_treaty_run refused batch %u (%s)", batch,
                       zxv_status_name(st));
        return st;
    }
    st = zxv_treaty_result_summary(r, &parties, &legs, &fees);
    if (st == ZXV_OK && (parties != n_local || fees != 0)) st = ZXV_E_INTERNAL;
    for (uint32_t i = 0; st == ZXV_OK && i < n_local; i++) {
        int64_t net = 0, bal = 0, div = 0;
        st = zxv_treaty_result_party(r, i, &net, &bal, &div);
        if (st != ZXV_OK) break;
        if (!add_ok(sum, net, &sum) || !add_ok(acc[gidx[i]], net, &acc[gidx[i]])) {
            zind_set_error("zsc netting: a net position overflows int64");
            st = ZXV_E_RANGE;
        }
    }
    if (st == ZXV_OK && sum != 0) {
        zind_set_error("zsc netting: batch %u positions sum to %lld, not 0", batch,
                       (long long) sum);
        st = ZXV_E_INTERNAL;
    }
    zxv_treaty_result_destroy(r);
    return st;
}

int32_t zsc_net_treaty(const char *ccy, uint32_t n_parts, const zsc_oblig *obs, size_t n,
                       int64_t *pos) {
    int64_t *acc, *direct;
    int32_t *local; /* global -> local index in the current batch, or -1 */
    uint32_t gidx[TREATY_PARTIES], lfrom[TREATY_OWES], lto[TREATY_OWES], n_local = 0, batch = 0;
    size_t start = 0, cnt = 0;
    int32_t st = ZXV_OK;
    if (!ccy || (!obs && n) || !pos || n_parts == 0) return ZXV_E_ARG;
    acc = (int64_t *) calloc(n_parts, sizeof *acc);
    direct = (int64_t *) calloc(n_parts, sizeof *direct);
    local = (int32_t *) malloc(n_parts * sizeof *local);
    if (!acc || !direct || !local) {
        st = ZXV_E_NOMEM;
        goto out;
    }
    for (uint32_t i = 0; i < n_parts; i++) local[i] = -1;
    for (size_t j = 0; j <= n && st == ZXV_OK; j++) {
        int need = 0;
        if (j < n) {
            const zsc_oblig *b = &obs[j];
            if (b->from >= n_parts || b->to >= n_parts || b->from == b->to || b->amount <= 0 ||
                b->amount > ZXV_AMOUNT_MAX) {
                st = ZXV_E_ARG;
                break;
            }
            need = (local[b->from] < 0) + (local[b->to] < 0);
            if (!add_ok(direct[b->to], b->amount, &direct[b->to]) ||
                !add_ok(direct[b->from], -b->amount, &direct[b->from])) {
                zind_set_error("zsc netting: a participant's gross position overflows int64");
                st = ZXV_E_RANGE;
                break;
            }
        }
        if (cnt > 0 && (j == n || cnt == TREATY_OWES || n_local + (uint32_t) need > TREATY_PARTIES)) {
            st = run_batch(ccy, ++batch, gidx, n_local, obs + start, lfrom, lto, cnt, acc);
            for (uint32_t i = 0; i < n_local; i++) local[gidx[i]] = -1;
            n_local = 0;
            cnt = 0;
            start = j;
        }
        if (j == n || st != ZXV_OK) break;
        {
            const zsc_oblig *b = &obs[j];
            if (local[b->from] < 0) {
                local[b->from] = (int32_t) n_local;
                gidx[n_local++] = b->from;
            }
            if (local[b->to] < 0) {
                local[b->to] = (int32_t) n_local;
                gidx[n_local++] = b->to;
            }
            lfrom[cnt] = (uint32_t) local[b->from];
            lto[cnt] = (uint32_t) local[b->to];
            cnt++;
        }
    }
    if (st == ZXV_OK) {
        for (uint32_t i = 0; i < n_parts; i++)
            if (acc[i] != direct[i]) {
                zind_set_error("zsc netting: the treaty engine and the plain sum disagree for "
                               "participant %u (%lld vs %lld)",
                               i, (long long) acc[i], (long long) direct[i]);
                st = ZXV_E_INTERNAL;
                break;
            }
    }
    if (st == ZXV_OK) memcpy(pos, acc, n_parts * sizeof *pos);
out:
    free(acc);
    free(direct);
    free(local);
    return st;
}

int32_t zsc_net_zxv(zxv_ctx *ctx, const char *cycle_id, const char *ccy, uint32_t n_parts,
                    const char (*codes)[12], const zsc_oblig *obs, size_t n, int64_t *pos) {
    zxv_netting *c = NULL;
    int64_t *acc;
    uint32_t cnt = 0;
    int32_t st;
    if (!ctx || !cycle_id || !ccy || !codes || (!obs && n) || !pos || n_parts == 0)
        return ZXV_E_ARG;
    acc = (int64_t *) calloc(n_parts, sizeof *acc);
    if (!acc) return ZXV_E_NOMEM;
    st = zxv_netting_open(ctx, cycle_id, ccy, &c);
    for (uint32_t i = 0; st == ZXV_OK && i < n_parts; i++)
        st = zxv_netting_add_participant(c, codes[i]);
    for (size_t j = 0; st == ZXV_OK && j < n; j++) {
        char pid[24];
        snprintf(pid, sizeof pid, "%s-%zu", ccy, j);
        st = zxv_netting_submit(c, pid, codes[obs[j].from], codes[obs[j].to], obs[j].amount);
    }
    if (st == ZXV_OK) st = zxv_netting_close(c);
    if (st == ZXV_OK) st = zxv_netting_position_count(c, &cnt);
    for (uint32_t i = 0; st == ZXV_OK && i < cnt; i++) {
        char name[32];
        size_t nl = 0;
        int64_t net = 0;
        uint32_t k;
        st = zxv_netting_position_at(c, i, name, sizeof name, &nl, &net);
        if (st != ZXV_OK) break;
        for (k = 0; k < n_parts && strcmp(codes[k], name) != 0; k++) {
        }
        if (k == n_parts) st = ZXV_E_INTERNAL;
        else acc[k] = net;
    }
    if (st == ZXV_OK) memcpy(pos, acc, n_parts * sizeof *pos);
    zxv_netting_destroy(c);
    free(acc);
    return st;
}

/* ===== journal ===== */

static int cmp_post(const void *a, const void *b) {
    const zsc_jpost *x = (const zsc_jpost *) a, *y = (const zsc_jpost *) b;
    return x->id < y->id ? -1 : x->id > y->id;
}

void zsc_journal_free(zsc_journal *j) {
    if (!j) return;
    free(j->acct);
    free(j->post);
    memset(j, 0, sizeof *j);
}

int32_t zsc_journal_read(const zxv_cap *cap, zsc_journal *out) {
    zsc_journal j;
    uint32_t na = 0;
    size_t alloc = 0;
    int32_t st;
    if (!cap || !out) return ZXV_E_ARG;
    memset(&j, 0, sizeof j);
    st = zxv_ledger_account_count_cap(cap, &na);
    if (st != ZXV_OK) return st;
    j.acct = (zsc_jacct *) calloc(na ? na : 1, sizeof *j.acct);
    if (!j.acct) return ZXV_E_NOMEM;
    j.n_acct = na;
    for (uint32_t a = 0; a < na && st == ZXV_OK; a++) {
        size_t l = 0;
        uint32_t ne = 0;
        st = zxv_ledger_account_name_cap(cap, a, j.acct[a].name, sizeof j.acct[a].name, &l);
        if (st == ZXV_OK)
            st = zxv_ledger_account_ccy_cap(cap, a, j.acct[a].ccy, sizeof j.acct[a].ccy, &l);
        if (st == ZXV_OK) st = zxv_ledger_account_kind_cap(cap, a, &j.acct[a].kind);
        if (st == ZXV_OK) st = zxv_ledger_entry_count_cap(cap, a, &ne);
        for (uint32_t e = 0; st == ZXV_OK && e < ne; e++) {
            uint64_t id = 0;
            int64_t amt = 0;
            uint32_t cp = 0;
            char ref[40];
            st = zxv_ledger_entry_at_cap(cap, a, e, &id, &amt, &cp, ref, sizeof ref, &l);
            if (st != ZXV_OK || amt >= 0) continue; /* each posting once: its sending side */
            if (j.n_post == alloc) {
                size_t na2 = alloc ? alloc * 2 : 64;
                zsc_jpost *np = (zsc_jpost *) realloc(j.post, na2 * sizeof *np);
                if (!np) {
                    st = ZXV_E_NOMEM;
                    break;
                }
                j.post = np;
                alloc = na2;
            }
            j.post[j.n_post].id = id;
            j.post[j.n_post].from = a;
            j.post[j.n_post].to = cp;
            j.post[j.n_post].amount = -amt;
            memcpy(j.post[j.n_post].ref, ref, sizeof ref);
            j.n_post++;
        }
    }
    if (st != ZXV_OK) {
        zsc_journal_free(&j);
        return st;
    }
    if (j.n_post > 1) qsort(j.post, j.n_post, sizeof *j.post, cmp_post);
    *out = j;
    return ZXV_OK;
}

int32_t zsc_journal_digest(const zsc_journal *j, uint64_t upto, uint8_t out[32]) {
    zsc_dbuf d;
    int32_t st;
    memset(&d, 0, sizeof d);
    zsc_db_bytes(&d, "ZSJP1", 5);
    for (size_t i = 0; i < j->n_post && j->post[i].id <= upto; i++) {
        const zsc_jpost *p = &j->post[i];
        if (p->from >= j->n_acct || p->to >= j->n_acct) {
            zsc_db_free(&d);
            return ZXV_E_INTERNAL;
        }
        zsc_db_u64(&d, p->id);
        zsc_db_str(&d, j->acct[p->from].name);
        zsc_db_str(&d, j->acct[p->to].name);
        zsc_db_str(&d, j->acct[p->from].ccy);
        zsc_db_u64(&d, (uint64_t) p->amount);
        zsc_db_str(&d, p->ref);
    }
    st = d.bad ? ZXV_E_NOMEM : zind_subject("sutra-clear/journal", d.buf, d.len, out);
    zsc_db_free(&d);
    return st;
}

int32_t zsc_journal_export(const zxv_cap *cap, uint8_t *buf, size_t cap_len, size_t *out_len) {
    zsc_journal j;
    zsc_dbuf d;
    int32_t st;
    if (!cap || !out_len) return ZXV_E_ARG;
    st = zsc_journal_read(cap, &j);
    if (st != ZXV_OK) return st;
    memset(&d, 0, sizeof d);
    zsc_db_bytes(&d, "ZSJ1", 4);
    zsc_db_u32(&d, j.n_acct);
    for (uint32_t a = 0; a < j.n_acct; a++) {
        zsc_db_u32(&d, a);
        zsc_db_str(&d, j.acct[a].name);
        zsc_db_str(&d, j.acct[a].ccy);
        zsc_db_u8(&d, (uint8_t) j.acct[a].kind);
    }
    zsc_db_u64(&d, (uint64_t) j.n_post);
    for (size_t i = 0; i < j.n_post; i++) {
        zsc_db_u64(&d, j.post[i].id);
        zsc_db_u32(&d, j.post[i].from);
        zsc_db_u32(&d, j.post[i].to);
        zsc_db_u64(&d, (uint64_t) j.post[i].amount);
        zsc_db_str(&d, j.post[i].ref);
    }
    zsc_journal_free(&j);
    if (d.bad) {
        zsc_db_free(&d);
        return ZXV_E_NOMEM;
    }
    *out_len = d.len;
    if (!buf || cap_len < d.len) st = ZXV_E_BUFFER;
    else memcpy(buf, d.buf, d.len);
    zsc_db_free(&d);
    return st;
}
