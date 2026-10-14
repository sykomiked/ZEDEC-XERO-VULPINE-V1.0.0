/* Copyright (c) 2024-2026 Michael Laurence Curzi and 36N9 Genetics, LLC */
/* SPDX-License-Identifier: Apache-2.0 */
/* zp_treaty.c - treaty steps (kernel/src/treaty): Sutra treaties run on a
 * scratch engine.
 *
 * The Playground lets people write '#' comment lines and blank lines in a
 * treaty; they are removed before the strict one-spelling parser sees the
 * text, and a fault is reported at its line and column in what the person
 * wrote. The run uses a fresh engine (zxt_exec's workspace), seeded by the
 * SHA3-256 of the canonical treaty, so the same treaty always gives the
 * same bytes. Nothing touches the owner's books.
 *
 * Outputs: fees, legs (the netted transfers), and per party NET.ID,
 * BALANCE.ID and DIVIDEND.ID (minor units, signed integers).
 */
#include "zp_internal.h"

#include "zxt.h"
#include "zxt_exec.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/* From libzxv (zxv_api.h). Not included: its ZXV_E_* macros collide with
 * the engine's zxv_status_t names that zxt_exec.h brings in. */
int32_t zxv_sha3_256(const uint8_t *data, size_t len, uint8_t out[32]);

static const char *engine_why(int32_t engine_status) {
    switch (engine_status) {
    case ZXV_E_FULL: return "the scratch engine is full";
    case ZXV_E_NO_ROUTE: return "no route between two parties";
    case ZXV_E_FUNDS: return "not enough funds: a claim is larger than what is left after settlement";
    case ZXV_E_LEDGER: return "the ledger refused a posting";
    case ZXV_E_OVERFLOW: return "an amount overflowed";
    default: return "the engine refused the batch";
    }
}

static const char *zxt_name(int32_t st) {
    switch (st) {
    case ZXT_E_ARG: return "ZXT_E_ARG";
    case ZXT_E_MAGIC: return "ZXT_E_MAGIC";
    case ZXT_E_SYNTAX: return "ZXT_E_SYNTAX";
    case ZXT_E_NAME: return "ZXT_E_NAME";
    case ZXT_E_ASSET: return "ZXT_E_ASSET";
    case ZXT_E_OP: return "ZXT_E_OP";
    case ZXT_E_COUNT: return "ZXT_E_COUNT";
    case ZXT_E_NUMBER: return "ZXT_E_NUMBER";
    case ZXT_E_PARTY: return "ZXT_E_PARTY";
    case ZXT_E_WEIGHT: return "ZXT_E_WEIGHT";
    case ZXT_E_CLAIM: return "ZXT_E_CLAIM";
    case ZXT_E_OWE: return "ZXT_E_OWE";
    case ZXT_E_PAD: return "ZXT_E_PAD";
    case ZXT_E_CAP: return "ZXT_E_CAP";
    case ZXT_E_SIZE: return "ZXT_E_SIZE";
    default: return st <= ZXT_E_EXEC ? "ZXT_E_EXEC" : "ZXT_E_UNKNOWN";
    }
}

static const char *zxt_why(int32_t st) {
    switch (st) {
    case ZXT_E_SYNTAX: return "this line is not treaty grammar (TREATY / PARTY / OWE / END, single spaces)";
    case ZXT_E_NAME: return "the treaty name must be 1-30 letters, digits or '-'";
    case ZXT_E_ASSET: return "the asset must be three capital letters, like USD";
    case ZXT_E_OP: return "the operation must be NET or SETL";
    case ZXT_E_COUNT: return "a treaty has 1-16 parties and 0-64 obligations";
    case ZXT_E_NUMBER: return "a number is malformed (no leading zeros, signs or decimals) or exceeds 18 digits";
    case ZXT_E_PARTY: return "a party id is bad, repeated, or not declared";
    case ZXT_E_WEIGHT: return "the weights must be in lowest terms and add up to exactly 1";
    case ZXT_E_CLAIM: return "a party claims more than it deposited";
    case ZXT_E_OWE: return "an obligation is to the party itself, or is not above zero";
    case ZXT_E_SIZE: return "a result would not fit PIC S9(18); refused, never rounded";
    default: return "refused";
    }
}

void zp_lang_treaty(zp_step *s, const char *src, size_t len) {
    char *clean = (char *) malloc(len + 2u);
    size_t *map = (size_t *) malloc((len + 2u) * sizeof *map);
    size_t n = 0, i = 0;
    zxt_treaty_t t;
    uint32_t off = 0;
    int32_t st;
    zxv_engine_t *ws = NULL;
    zxt_result_t r;
    char canon[ZXT_SUTRA_MAX + 1];
    uint32_t cn = 0;
    uint8_t seed[32];
    char buf[160];
    if (!clean || !map) {
        zp_refuse(s, ZP_E_NOMEM, NULL, 0, 0, "out of memory");
        goto done;
    }
    /* Drop comment and blank lines and trailing spaces; keep a map back to
     * the original bytes. */
    while (i < len) {
        size_t e = i, a = i, b;
        while (e < len && src[e] != '\n') e++;
        while (a < e && (src[a] == ' ' || src[a] == '\t')) a++;
        b = e;
        while (b > a && (src[b - 1] == ' ' || src[b - 1] == '\t' || src[b - 1] == '\r')) b--;
        if (a < b && src[a] != '#') {
            for (size_t k = i; k < b; k++) {
                map[n] = k;
                clean[n++] = src[k];
            }
            map[n] = b;
            clean[n++] = '\n';
        }
        i = e + 1u;
    }
    map[n] = len;
    if (n > ZXT_SUTRA_MAX) {
        zp_refuse(s, ZXT_E_COUNT, "ZXT_E_COUNT", 0, 0, "the treaty is longer than any valid treaty");
        goto done;
    }
    st = zxt_parse_sutra(clean, (uint32_t) n, &t, &off);
    if (st != ZXT_OK) {
        uint32_t line, col;
        zp_line_col(src, len, off <= n ? map[off] : len, &line, &col);
        zp_refuse(s, st, zxt_name(st), line, col, zxt_why(st));
        goto done;
    }
    zxt_emit_sutra(&t, canon, sizeof canon - 1u, &cn);
    canon[cn] = '\0';
    zxv_sha3_256((const uint8_t *) canon, cn, seed);
    ws = (zxv_engine_t *) calloc(1, sizeof *ws);
    if (!ws) {
        zp_refuse(s, ZP_E_NOMEM, NULL, 0, 0, "out of memory");
        goto done;
    }
    st = zxt_exec(&t, ws, seed, &r);
    if (st != ZXT_OK) {
        char why[200];
        if (st < ZXT_E_EXEC)
            snprintf(why, sizeof why, "%s; nothing was posted", engine_why(st - ZXT_E_EXEC));
        else
            snprintf(why, sizeof why, "%s; nothing was posted", zxt_why(st));
        zp_refuse(s, st, zxt_name(st), 0, 0, why);
        goto done;
    }
    zp_notef(s, "%s %s: %u parties, %u obligations netted to %u transfer(s)%s", t.name, t.asset,
             (unsigned) t.n_party, (unsigned) t.n_owe, (unsigned) r.n_leg,
             t.op == ZXT_OP_SETL ? ", settled on a scratch engine with the 0.08889% assurance fee" : " (NET: nothing posted)");
    snprintf(buf, sizeof buf, "%lld", (long long) r.fees);
    zp_output(s, "fees", buf);
    {
        zp_buf legs = {0};
        for (uint32_t k = 0; k < r.n_leg; k++) {
            if (k) zp_buf_puts(&legs, ", ");
            zp_buf_printf(&legs, "%s->%s %lld", t.party[r.leg[k].from].id, t.party[r.leg[k].to].id,
                          (long long) r.leg[k].amount);
        }
        zp_output(s, "legs", legs.p ? legs.p : "none");
        zp_buf_free(&legs);
    }
    for (uint32_t k = 0; k < r.n_party; k++) {
        char name[32];
        snprintf(name, sizeof name, "net.%s", t.party[k].id);
        snprintf(buf, sizeof buf, "%lld", (long long) r.net[k]);
        zp_output(s, name, buf);
        snprintf(name, sizeof name, "balance.%s", t.party[k].id);
        snprintf(buf, sizeof buf, "%lld", (long long) r.balance[k]);
        zp_output(s, name, buf);
        if (t.op == ZXT_OP_SETL) {
            snprintf(name, sizeof name, "dividend.%s", t.party[k].id);
            snprintf(buf, sizeof buf, "%lld", (long long) r.dividend[k]);
            zp_output(s, name, buf);
        }
    }
done:
    free(ws);
    free(clean);
    free(map);
}
