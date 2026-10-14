/* Copyright (c) 2024-2026 Michael Laurence Curzi and 36N9 Genetics, LLC
 * SPDX-License-Identifier: Apache-2.0 */
/* zxvd_pay.c — payments in zxvd (zxvd_pay.h P1-P5). */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "zxvd_pay.h"
#include "zxvd_ext.h"
#include "zxvd_rpc.h"
#include "web4_web2.h" /* the request's tokens (zxvd_param_json) */
#include "zxv_api.h"
#include "ident.h" /* P4: id_rp_register / id_rp_verify */

#define ESCROW_PREFIX "escrow: "

/* ===================== shared ===================== */

static bool books(zxvd_call_t *c)
{
    if (zxvd_books_cap()) return true;
    zxvd_fail(c, ZXVD_E_INTERNAL, "the books are not open");
    return false;
}

static bool str_param(zxvd_call_t *c, const char *key, char *out, uint32_t cap)
{
    if (!zxvd_param_str(c, key, out, cap, true)) return false;
    if (out[0]) return true;
    zxvd_fail(c, ZXVD_E_PARAMS, "\"%s\" must not be empty", key);
    return false;
}

/* M3 for a token: a JSON integer or a decimal string. */
static bool tok_amount(const char *js, const w4_jtok_t *t, int64_t *v)
{
    if (t->type == W4_J_NUM) return w4_json_i64(js, t, v) == W4_OK;
    if (t->type != W4_J_STR) return false;
    const char *p = js + t->start, *e = js + t->end;
    bool neg = p < e && *p == '-';
    if (neg) p++;
    uint64_t x = 0;
    if (p == e || e - p > 19) return false;
    for (; p < e; p++) {
        if (*p < '0' || *p > '9') return false;
        x = x * 10u + (uint64_t) (*p - '0');
    }
    if (x > (uint64_t) INT64_MAX) return false;
    *v = neg ? -(int64_t) x : (int64_t) x;
    return true;
}

static int hexval(char c)
{
    if (c >= '0' && c <= '9') return c - '0';
    if (c >= 'a' && c <= 'f') return c - 'a' + 10;
    if (c >= 'A' && c <= 'F') return c - 'A' + 10;
    return -1;
}

/* A hex parameter of exactly `want` bytes (want 0: 1..cap bytes). */
static bool hex_param(zxvd_call_t *c, const char *key, uint8_t *out, uint32_t cap, uint32_t want,
                      uint32_t *len)
{
    static char s[ZXVD_PAY_HEX_MAX + 2];
    if (!zxvd_param_str(c, key, s, sizeof s, true)) return false;
    size_t n = strlen(s);
    bool ok = n && n % 2 == 0 && (want ? n == 2u * want : n <= 2u * cap);
    for (size_t i = 0; ok && i < n; i++) ok = hexval(s[i]) >= 0;
    if (!ok) {
        if (want)
            zxvd_fail(c, ZXVD_E_PARAMS, "\"%s\" must be %u bytes as hex", key, (unsigned) want);
        else
            zxvd_fail(c, ZXVD_E_PARAMS, "\"%s\" must be 1 to %u bytes as hex", key, (unsigned) cap);
        return false;
    }
    for (size_t i = 0; i < n / 2; i++)
        out[i] = (uint8_t) (hexval(s[2 * i]) << 4 | hexval(s[2 * i + 1]));
    *len = (uint32_t) (n / 2);
    return true;
}

static void put_hex(zxvd_buf_t *b, const uint8_t *p, uint32_t n)
{
    static const char X[] = "0123456789abcdef";
    zxvd_buf_raw(b, "\"", 1);
    for (uint32_t i = 0; i < n; i++) {
        char h[2] = {X[p[i] >> 4], X[p[i] & 15u]};
        zxvd_buf_raw(b, h, 2);
    }
    zxvd_buf_raw(b, "\"", 1);
}

/* ===================== P1 ISO 20022 messages ===================== */

static const struct {
    const char *name;
    int32_t field;
} FIELDS[] = {
    {"msg_id", ZXV_FLD_MSG_ID},
    {"cre_dt_tm", ZXV_FLD_CRE_DT_TM},
    {"debtor_name", ZXV_FLD_DEBTOR_NAME},
    {"debtor_acct", ZXV_FLD_DEBTOR_ACCT},
    {"creditor_name", ZXV_FLD_CREDITOR_NAME},
    {"creditor_acct", ZXV_FLD_CREDITOR_ACCT},
    {"end_to_end_id", ZXV_FLD_END_TO_END_ID},
    {"ccy", ZXV_FLD_CCY},
    {"acct_id", ZXV_FLD_ACCT_ID},
};
#define N_FIELDS (sizeof FIELDS / sizeof FIELDS[0])

/* The object parameter `key`, or -1 (absent); false after a failure. */
static bool obj_param(zxvd_call_t *c, const char *key, int32_t want, zxvd_json_t *v, bool *got)
{
    *got = zxvd_param_json(c, key, v);
    if (!*got) return true;
    if (((const w4_jtok_t *) v->tok)[v->at].type == want) return true;
    zxvd_fail(c, ZXVD_E_PARAMS, "\"%s\" must be an %s", key, want == W4_J_OBJ ? "object" : "array");
    return false;
}

static bool msg_fields(zxvd_call_t *c, zxv_msg *m, const zxvd_json_t *v, char ccy[4])
{
    const w4_jtok_t *t = v->tok;
    int32_t k = v->at + 1;
    for (uint32_t i = 0; i < t[v->at].size; i++, k = w4_json_skip(t, v->n, k + 1)) {
        char name[24], val[256];
        uint32_t f = 0;
        if (w4_json_str(v->js, &t[k], name, sizeof name) < 0) name[0] = 0;
        while (f < N_FIELDS && strcmp(name, FIELDS[f].name)) f++;
        if (f == N_FIELDS) {
            zxvd_fail(c, ZXVD_E_PARAMS,
                      "unknown field \"%.23s\": fields are msg_id, cre_dt_tm, debtor_name, "
                      "debtor_acct, creditor_name, creditor_acct, end_to_end_id, ccy, acct_id",
                      name);
            return false;
        }
        int32_t got =
            t[k + 1].type == W4_J_STR ? w4_json_str(v->js, &t[k + 1], val, sizeof val) : -1;
        if (got < 0 || (size_t) got != strlen(val)) {
            zxvd_fail(c, ZXVD_E_PARAMS,
                      "\"fields.%s\" must be a string of at most 255 bytes with no NUL", name);
            return false;
        }
        if (!zxvd_libzxv_ok(c, zxv_msg_set_text(m, FIELDS[f].field, val))) return false;
        if (FIELDS[f].field == ZXV_FLD_CCY) memcpy(ccy, val, 4);
    }
    return true;
}

static bool msg_amounts(zxvd_call_t *c, zxv_msg *m, const zxvd_json_t *v, uint8_t minor)
{
    static const char *const NAME[] = {"settlement", "opening", "closing"};
    const w4_jtok_t *t = v->tok;
    int32_t k = v->at + 1;
    for (uint32_t i = 0; i < t[v->at].size; i++, k = w4_json_skip(t, v->n, k + 1)) {
        int32_t f = 0;
        int64_t units;
        while (f < 3 && !w4_json_str_eq(v->js, &t[k], NAME[f])) f++;
        if (f == 3) {
            zxvd_fail(c, ZXVD_E_PARAMS, "amounts are settlement, opening and closing");
            return false;
        }
        if (!tok_amount(v->js, &t[k + 1], &units)) {
            zxvd_fail(c, ZXVD_E_PARAMS,
                      "\"amounts.%s\" must be an integer or a decimal string of minor units",
                      NAME[f]);
            return false;
        }
        if (!zxvd_libzxv_ok(c, zxv_msg_set_amount(m, f + 1, units, minor))) return false;
    }
    return true;
}

static bool msg_entries(zxvd_call_t *c, zxv_msg *m, const zxvd_json_t *v, uint8_t minor)
{
    const w4_jtok_t *t = v->tok;
    int32_t k = v->at + 1;
    for (uint32_t i = 0; i < t[v->at].size; i++, k = w4_json_skip(t, v->n, k)) {
        int32_t a, d, s;
        int64_t units;
        int32_t dir = -1, st = ZXV_STS_BOOK;
        if (t[k].type != W4_J_OBJ) {
            zxvd_fail(c, ZXVD_E_PARAMS, "each entry must be an object");
            return false;
        }
        a = w4_json_get(v->js, t, v->n, k, "amount_minor");
        d = w4_json_get(v->js, t, v->n, k, "direction");
        s = w4_json_get(v->js, t, v->n, k, "status");
        if (a < 0 || !tok_amount(v->js, &t[a], &units)) {
            zxvd_fail(c, ZXVD_E_PARAMS,
                      "entry %u: \"amount_minor\" must be an integer or a decimal string",
                      (unsigned) i);
            return false;
        }
        if (d >= 0 && w4_json_str_eq(v->js, &t[d], "credit")) dir = ZXV_CRDT;
        if (d >= 0 && w4_json_str_eq(v->js, &t[d], "debit")) dir = ZXV_DBIT;
        if (dir < 0) {
            zxvd_fail(c, ZXVD_E_PARAMS, "entry %u: \"direction\" must be \"credit\" or \"debit\"",
                      (unsigned) i);
            return false;
        }
        if (s >= 0) {
            st = w4_json_str_eq(v->js, &t[s], "booked")    ? ZXV_STS_BOOK
                 : w4_json_str_eq(v->js, &t[s], "pending") ? ZXV_STS_PDNG
                 : w4_json_str_eq(v->js, &t[s], "info")    ? ZXV_STS_INFO
                                                           : -1;
            if (st < 0) {
                zxvd_fail(c, ZXVD_E_PARAMS,
                          "entry %u: \"status\" must be \"booked\", \"pending\" or \"info\"",
                          (unsigned) i);
                return false;
            }
        }
        if (!zxvd_libzxv_ok(c, zxv_msg_add_entry(m, units, minor, dir, st))) return false;
    }
    return true;
}

static void m_render(zxvd_call_t *c)
{
    char kind[16];
    uint32_t tx = 0;
    zxvd_json_t fv, av, ev;
    bool hf, ha, he;
    if (!books(c) || !str_param(c, "kind", kind, sizeof kind)) return;
    int32_t k = !strcmp(kind, "pacs008")   ? ZXV_MX_PACS008
                : !strcmp(kind, "camt053") ? ZXV_MX_CAMT053
                                           : 0;
    if (!k) return zxvd_fail(c, ZXVD_E_PARAMS, "\"kind\" must be \"pacs008\" or \"camt053\"");
    if (!obj_param(c, "fields", W4_J_OBJ, &fv, &hf) ||
        !obj_param(c, "amounts", W4_J_OBJ, &av, &ha) ||
        !obj_param(c, "entries", W4_J_ARR, &ev, &he) || !zxvd_param_u32(c, "tx_count", &tx, false))
        return;
    if (!hf) return zxvd_fail(c, ZXVD_E_PARAMS, "missing object parameter \"fields\"");
    zxv_msg *m = NULL;
    if (!zxvd_libzxv_ok(c, zxv_msg_create_cap(zxvd_books_cap(), k, &m))) return;
    char ccy[4] = "";
    uint8_t minor = 0;
    bool ok = msg_fields(c, m, &fv, ccy);
    if (ok && (ha || he)) {
        if (!ccy[0]) {
            zxvd_fail(c, ZXVD_E_PARAMS, "amounts and entries need \"fields.ccy\"");
            ok = false;
        } else {
            ok = zxvd_libzxv_ok(c, zxv_iso4217_by_alpha(ccy, NULL, &minor, NULL));
        }
    }
    if (ok && ha) ok = msg_amounts(c, m, &av, minor);
    if (ok && he) ok = msg_entries(c, m, &ev, minor);
    zxvd_json_t tv;
    if (ok && zxvd_param_json(c, "tx_count", &tv))
        ok = zxvd_libzxv_ok(c, zxv_msg_set_tx_count(m, tx));
    size_t need = 0;
    char *xml = NULL;
    if (ok) {
        int32_t st = zxv_msg_render(m, NULL, 0, &need);
        if (st == ZXV_E_BUFFER) {
            xml = malloc(need + 1);
            st = xml ? zxv_msg_render(m, xml, need + 1, &need) : ZXV_E_NOMEM;
        }
        ok = zxvd_libzxv_ok(c, st);
    }
    zxv_msg_destroy(m);
    if (ok) {
        zxvd_buf_t *b = zxvd_result(c);
        zxvd_buf_printf(b, "{\"kind\":\"%s\",\"xml\":", kind);
        zxvd_buf_json_str(b, xml);
        zxvd_buf_raw(b, "}", 1);
    }
    free(xml);
}

/* ===================== P2 cards ===================== */

static void m_card(zxvd_call_t *c)
{
    char net[16], pan[32];
    int32_t valid = 0;
    if (!str_param(c, "network", net, sizeof net) ||
        !zxvd_param_str(c, "pan", pan, sizeof pan, true))
        return;
    int32_t n = !strcmp(net, "phoenix")       ? ZXV_NET_PHOENIX
                : !strcmp(net, "dragon")      ? ZXV_NET_DRAGON
                : !strcmp(net, "thunderbird") ? ZXV_NET_THUNDERBIRD
                                              : 0;
    if (!n)
        return zxvd_fail(c, ZXVD_E_PARAMS,
                         "\"network\" must be \"phoenix\", \"dragon\" or \"thunderbird\"");
    if (!zxvd_libzxv_ok(c, zxv_card_check_digit(n, pan, &valid))) return;
    zxvd_buf_printf(zxvd_result(c), "{\"network\":\"%s\",\"valid\":%s}", net,
                    valid ? "true" : "false");
}

/* ===================== P3 netting ===================== */

typedef struct {
    zxv_netting *h; /* NULL: a free slot */
    char id[36], ccy[4];
    bool closed;
    uint32_t n_part;
    char part[64][36];
} cycle_t;

static cycle_t CY[ZXVD_PAY_CYCLES];
/* The cycles' own context: netting touches no account, so the cycles stand
 * apart from the books and outlive a books reopen. */
static zxv_ctx *NCTX;

static void cycle_free(cycle_t *y)
{
    if (y->h) zxv_netting_destroy(y->h);
    memset(y, 0, sizeof *y);
}

static cycle_t *cycle_find(const char *id)
{
    for (uint32_t i = 0; i < ZXVD_PAY_CYCLES; i++)
        if (CY[i].h && !strcmp(CY[i].id, id)) return &CY[i];
    return NULL;
}

/* The "cycle" parameter, of a cycle held. */
static cycle_t *cycle_param(zxvd_call_t *c)
{
    char id[40];
    if (!str_param(c, "cycle", id, sizeof id)) return NULL;
    cycle_t *y = cycle_find(id);
    if (!y) zxvd_fail(c, ZXVD_E_PAY, "no netting cycle \"%s\" is held", id);
    return y;
}

static void put_cycle(zxvd_buf_t *b, const cycle_t *y, bool positions)
{
    zxvd_buf_raw(b, "{\"cycle\":", 9);
    zxvd_buf_json_str(b, y->id);
    zxvd_buf_printf(b, ",\"currency\":\"%s\",\"closed\":%s,\"participants\":[", y->ccy,
                    y->closed ? "true" : "false");
    for (uint32_t i = 0; i < y->n_part; i++) {
        if (i) zxvd_buf_raw(b, ",", 1);
        zxvd_buf_json_str(b, y->part[i]);
    }
    zxvd_buf_raw(b, "]", 1);
    if (positions) {
        int64_t sum = 0;
        zxvd_buf_raw(b, ",\"positions\":[", 14);
        for (uint32_t i = 0; i < y->n_part; i++) {
            char name[64];
            size_t len;
            int64_t net = 0;
            if (zxv_netting_position_at(y->h, i, name, sizeof name, &len, &net) != ZXV_OK)
                name[0] = 0;
            zxvd_buf_raw(b, i ? ",{\"participant\":" : "{\"participant\":", i ? 16 : 15);
            zxvd_buf_json_str(b, name);
            zxvd_buf_printf(b, ",\"net_minor\":\"%lld\"}", (long long) net);
            sum += net;
        }
        zxvd_buf_printf(b, "],\"sum_minor\":\"%lld\"", (long long) sum);
    }
    zxvd_buf_raw(b, "}", 1);
}

static void m_net_open(zxvd_call_t *c)
{
    char id[40], ccy[16];
    if (!str_param(c, "cycle", id, sizeof id) || !str_param(c, "currency", ccy, sizeof ccy)) return;
    if (cycle_find(id))
        return zxvd_fail(c, ZXVD_E_PAY, "a netting cycle \"%s\" is already held", id);
    cycle_t *y = NULL;
    for (uint32_t i = 0; !y && i < ZXVD_PAY_CYCLES; i++)
        if (!CY[i].h) y = &CY[i];
    if (!y)
        return zxvd_fail(c, ZXVD_E_PAY, "at most %u netting cycles are held: drop one first",
                         ZXVD_PAY_CYCLES);
    zxv_netting *h = NULL;
    if (!zxvd_libzxv_ok(c, zxv_netting_open(NCTX, id, ccy, &h))) return;
    y->h = h;
    memcpy(y->id, id, strlen(id) + 1);
    memcpy(y->ccy, ccy, 4);
    put_cycle(zxvd_result(c), y, false);
}

static void m_net_add(zxvd_call_t *c)
{
    char who[40];
    cycle_t *y = cycle_param(c);
    if (!y || !str_param(c, "participant", who, sizeof who)) return;
    if (!zxvd_libzxv_ok(c, zxv_netting_add_participant(y->h, who))) return;
    memcpy(y->part[y->n_part++], who, strlen(who) + 1);
    put_cycle(zxvd_result(c), y, false);
}

static void m_net_submit(zxvd_call_t *c)
{
    char pid[40], from[40], to[40];
    int64_t amount;
    cycle_t *y = cycle_param(c);
    if (!y || !str_param(c, "payment_id", pid, sizeof pid) ||
        !str_param(c, "debtor", from, sizeof from) || !str_param(c, "creditor", to, sizeof to) ||
        !zxvd_param_amount(c, "amount_minor", &amount, true))
        return;
    if (!zxvd_libzxv_ok(c, zxv_netting_submit(y->h, pid, from, to, amount))) return;
    zxvd_buf_t *b = zxvd_result(c);
    zxvd_buf_raw(b, "{\"cycle\":", 9);
    zxvd_buf_json_str(b, y->id);
    zxvd_buf_raw(b, ",\"payment_id\":", 14);
    zxvd_buf_json_str(b, pid);
    zxvd_buf_raw(b, ",\"accepted\":true}", 17);
}

static void m_net_close(zxvd_call_t *c)
{
    cycle_t *y = cycle_param(c);
    if (!y || !zxvd_libzxv_ok(c, zxv_netting_close(y->h))) return;
    y->closed = true;
    put_cycle(zxvd_result(c), y, true);
}

static void m_net_positions(zxvd_call_t *c)
{
    cycle_t *y = cycle_param(c);
    if (!y) return;
    if (!y->closed)
        return zxvd_fail(c, ZXVD_E_PAY, "netting cycle \"%s\" is still open: close it first",
                         y->id);
    put_cycle(zxvd_result(c), y, true);
}

static void m_net_list(zxvd_call_t *c)
{
    zxvd_buf_t *b = zxvd_result(c);
    const char *sep = "";
    zxvd_buf_raw(b, "{\"cycles\":[", 11);
    for (uint32_t i = 0; i < ZXVD_PAY_CYCLES; i++) {
        if (!CY[i].h) continue;
        zxvd_buf_raw(b, sep, strlen(sep));
        put_cycle(b, &CY[i], false);
        sep = ",";
    }
    zxvd_buf_raw(b, "]}", 2);
}

static void m_net_drop(zxvd_call_t *c)
{
    cycle_t *y = cycle_param(c);
    if (!y) return;
    zxvd_buf_t *b = zxvd_result(c);
    zxvd_buf_raw(b, "{\"dropped\":", 11);
    zxvd_buf_json_str(b, y->id);
    zxvd_buf_raw(b, "}", 1);
    cycle_free(y);
}

/* ===================== P4 passkeys ===================== */

static id_rp_cred_t PK[ZXVD_PAY_PASSKEYS];
static uint32_t N_PK;
static uint8_t PK_BUF[ZXVD_PAY_HEX_MAX / 2];

static id_rp_cred_t *pk_find(const uint8_t id[ID_CRED_ID])
{
    for (uint32_t i = 0; i < N_PK; i++)
        if (!memcmp(PK[i].cred_id, id, ID_CRED_ID)) return &PK[i];
    return NULL;
}

static bool rp_param(zxvd_call_t *c, char rp[ID_RP_MAX + 1], bool *uv)
{
    *uv = true;
    return str_param(c, "rp_id", rp, ID_RP_MAX + 1) && zxvd_param_bool(c, "require_uv", uv, false);
}

static void m_pk_register(zxvd_call_t *c)
{
    char rp[ID_RP_MAX + 1];
    bool uv;
    uint32_t len;
    id_rp_cred_t cred;
    if (!rp_param(c, rp, &uv) ||
        !hex_param(c, "authenticator_data", PK_BUF, sizeof PK_BUF, ID_AUTHDATA_MAX, &len))
        return;
    id_status_t st = id_rp_register(&cred, rp, PK_BUF, len, uv);
    if (st == ID_ERR_AUTH)
        return zxvd_fail(c, ZXVD_E_PAY,
                         uv ? "the authenticator data is for another rp_id or lacks user "
                              "verification"
                            : "the authenticator data is for another rp_id");
    if (st != ID_OK)
        return zxvd_fail(c, ZXVD_E_PAY,
                         "the authenticator data is not a device-bound ML-DSA-65 passkey "
                         "registration");
    if (pk_find(cred.cred_id))
        return zxvd_fail(c, ZXVD_E_PAY, "that credential is already registered");
    if (N_PK == ZXVD_PAY_PASSKEYS)
        return zxvd_fail(c, ZXVD_E_PAY, "at most %u passkeys are held", ZXVD_PAY_PASSKEYS);
    PK[N_PK++] = cred;
    zxvd_buf_t *b = zxvd_result(c);
    zxvd_buf_raw(b, "{\"credential_id\":", 17);
    put_hex(b, cred.cred_id, ID_CRED_ID);
    zxvd_buf_printf(b, ",\"sign_count\":%u,\"registered\":%u}", (unsigned) cred.sign_count,
                    (unsigned) N_PK);
}

static void m_pk_verify(zxvd_call_t *c)
{
    char rp[ID_RP_MAX + 1];
    bool uv;
    uint32_t len, n;
    uint8_t id[ID_CRED_ID], auth[ID_AUTHDATA_MIN], cdh[ID_HASH];
    if (!rp_param(c, rp, &uv) || !hex_param(c, "credential_id", id, sizeof id, ID_CRED_ID, &n) ||
        !hex_param(c, "authenticator_data", auth, sizeof auth, ID_AUTHDATA_MIN, &len) ||
        !hex_param(c, "client_data_hash", cdh, sizeof cdh, ID_HASH, &n) ||
        !hex_param(c, "signature", PK_BUF, sizeof PK_BUF, ID_MLDSA65_SIG, &n))
        return;
    id_rp_cred_t *cred = pk_find(id);
    if (!cred) return zxvd_fail(c, ZXVD_E_PAY, "no passkey with that \"credential_id\" is held");
    id_status_t st = id_rp_verify(cred, rp, auth, len, cdh, PK_BUF, uv);
    if (st == ID_ERR_REPLAY)
        return zxvd_fail(c, ZXVD_E_PAY, "replayed: the signature counter did not increase");
    if (st != ID_OK)
        return zxvd_fail(c, ZXVD_E_PAY,
                         "the assertion did not verify (signature, rp_id, user presence or "
                         "user verification)");
    zxvd_buf_t *b = zxvd_result(c);
    zxvd_buf_raw(b, "{\"credential_id\":", 17);
    put_hex(b, id, ID_CRED_ID);
    zxvd_buf_printf(b, ",\"verified\":true,\"sign_count\":%u}", (unsigned) cred->sign_count);
}

/* ===================== P5 escrow ===================== */

typedef struct {
    uint32_t n, from, to, escrow;
    int64_t amount;
    uint64_t entry, closed_entry;
    char ccy[16];
    uint8_t state; /* 0 held, 1 released, 2 refunded */
} hold_t;

typedef struct {
    uint32_t account;
    char ccy[16];
    int64_t balance, held;
} escrow_t;

typedef struct {
    hold_t *h;
    uint32_t n, cap;
    escrow_t *e;
    uint32_t ne;
    uint32_t unmatched, top; /* top: the highest hold number seen */
} view_t;

static const char *const STATE[] = {"held", "released", "refunded"};

static void view_free(view_t *v)
{
    free(v->h);
    free(v->e);
    memset(v, 0, sizeof *v);
}

static hold_t *view_hold(view_t *v, uint32_t n)
{
    for (uint32_t i = 0; i < v->n; i++)
        if (v->h[i].n == n) return &v->h[i];
    return NULL;
}

/* An escrow account: a holder named "escrow: <its currency>". */
static bool is_escrow(const zxv_cap *cap, uint32_t a, char ccy[16])
{
    char name[80];
    size_t l;
    int32_t kind;
    return zxv_ledger_account_name_cap(cap, a, name, sizeof name, &l) == ZXV_OK &&
           zxv_ledger_account_ccy_cap(cap, a, ccy, 16, &l) == ZXV_OK &&
           zxv_ledger_account_kind_cap(cap, a, &kind) == ZXV_OK && kind == ZXV_ACCT_HOLDER &&
           !strncmp(name, ESCROW_PREFIX, sizeof ESCROW_PREFIX - 1) &&
           !strcmp(name + sizeof ESCROW_PREFIX - 1, ccy);
}

/* A number as zxvd writes it: decimal digits, no sign, no zero in front
 * (but "0" itself), at most UINT32_MAX. Its end, or NULL. */
static const char *num_u32(const char *p, uint32_t *v)
{
    uint64_t x = 0;
    const char *s = p;
    while (*p >= '0' && *p <= '9' && x <= UINT32_MAX) x = x * 10u + (uint64_t) (*p++ - '0');
    if (p == s || x > UINT32_MAX || (*s == '0' && p - s > 1)) return NULL;
    *v = (uint32_t) x;
    return p;
}

/* "escrow N <rest>": N (from 1), or 0. */
static uint32_t ref_hold(const char *ref, const char **rest)
{
    uint32_t n = 0;
    const char *p = strncmp(ref, "escrow ", 7) ? NULL : num_u32(ref + 7, &n);
    if (!p || *p != ' ') return 0;
    *rest = p + 1;
    return n;
}

/* One entry of an escrow account into the view. */
static void view_entry(view_t *v, uint32_t acct, const char *ccy, uint64_t id, int64_t amt,
                       uint32_t cp, const char *ref)
{
    const char *rest = "";
    uint32_t n = ref_hold(ref, &rest);
    hold_t *h = n ? view_hold(v, n) : NULL;
    if (n > v->top) v->top = n;
    if (n && amt > 0 && !strncmp(rest, "to ", 3) && !h) {
        uint32_t to = 0;
        const char *e = num_u32(rest + 3, &to);
        if (e && !*e) {
            if (v->n == v->cap) {
                uint32_t nc = v->cap ? v->cap * 2u : 16u;
                hold_t *nh = realloc(v->h, nc * sizeof *nh);
                if (!nh) {
                    v->unmatched++;
                    return;
                }
                v->h = nh;
                v->cap = nc;
            }
            hold_t *x = &v->h[v->n++];
            memset(x, 0, sizeof *x);
            x->n = n;
            x->from = cp;
            x->to = to;
            x->escrow = acct;
            x->amount = amt;
            x->entry = id;
            memcpy(x->ccy, ccy, strlen(ccy) + 1);
            return;
        }
    }
    bool rel = !strcmp(rest, "release"), ref_ = !strcmp(rest, "refund");
    if (h && h->state == 0 && h->escrow == acct && amt == -h->amount && (rel || ref_) &&
        cp == (rel ? h->to : h->from)) {
        h->state = rel ? 1 : 2;
        h->closed_entry = id;
        return;
    }
    v->unmatched++;
}

/* Read every hold back from the escrow accounts' entries. */
static bool view_load(zxvd_call_t *c, view_t *v)
{
    const zxv_cap *cap = zxvd_books_cap();
    uint32_t na = 0;
    memset(v, 0, sizeof *v);
    if (!zxvd_libzxv_ok(c, zxv_ledger_account_count_cap(cap, &na))) return false;
    for (uint32_t a = 0; a < na; a++) {
        char ccy[16];
        uint32_t ne = 0;
        if (!is_escrow(cap, a, ccy)) continue;
        escrow_t *ne_ = realloc(v->e, (v->ne + 1u) * sizeof *ne_);
        if (!ne_) {
            zxvd_fail(c, ZXVD_E_INTERNAL, "out of memory");
            return false;
        }
        v->e = ne_;
        escrow_t *e = &v->e[v->ne++];
        memset(e, 0, sizeof *e);
        e->account = a;
        memcpy(e->ccy, ccy, sizeof e->ccy);
        if (!zxvd_libzxv_ok(c, zxv_ledger_balance_cap(cap, a, NULL, NULL, &e->balance)) ||
            !zxvd_libzxv_ok(c, zxv_ledger_entry_count_cap(cap, a, &ne)))
            return false;
        for (uint32_t i = 0; i < ne; i++) {
            uint64_t id;
            int64_t amt;
            uint32_t cp;
            char ref[64];
            size_t rl;
            if (!zxvd_libzxv_ok(
                    c, zxv_ledger_entry_at_cap(cap, a, i, &id, &amt, &cp, ref, sizeof ref, &rl)))
                return false;
            view_entry(v, a, ccy, id, amt, cp, ref);
        }
    }
    for (uint32_t i = 0; i < v->n; i++)
        for (uint32_t j = 0; j < v->ne; j++)
            if (v->h[i].state == 0 && v->e[j].account == v->h[i].escrow)
                v->e[j].held += v->h[i].amount;
    return true;
}

static void put_hold(zxvd_buf_t *b, const hold_t *h)
{
    zxvd_buf_printf(b,
                    "{\"hold\":%u,\"state\":\"%s\",\"from\":%u,\"to\":%u,\"amount_minor\":"
                    "\"%lld\",\"currency\":",
                    (unsigned) h->n, STATE[h->state], (unsigned) h->from, (unsigned) h->to,
                    (long long) h->amount);
    zxvd_buf_json_str(b, h->ccy);
    zxvd_buf_printf(b, ",\"escrow_account\":%u,\"entry_id\":\"%llu\",\"closed_entry_id\":",
                    (unsigned) h->escrow, (unsigned long long) h->entry);
    if (h->state)
        zxvd_buf_printf(b, "\"%llu\"", (unsigned long long) h->closed_entry);
    else
        zxvd_buf_raw(b, "null", 4);
    zxvd_buf_raw(b, ",\"releases_on\":\"owner\"}", 23);
}

static void m_hold(zxvd_call_t *c)
{
    const zxv_cap *cap = zxvd_books_cap();
    uint32_t from, to, escrow = 0;
    bool have = false;
    int64_t amount;
    char cf[16], ct[16], tmp[16];
    size_t l;
    view_t v;
    if (!books(c) || !zxvd_param_u32(c, "from", &from, true) ||
        !zxvd_param_u32(c, "to", &to, true) || !zxvd_param_amount(c, "amount_minor", &amount, true))
        return;
    if (from == to) return zxvd_fail(c, ZXVD_E_PARAMS, "\"from\" and \"to\" must differ");
    if (!zxvd_libzxv_ok(c, zxv_ledger_account_ccy_cap(cap, from, cf, sizeof cf, &l)) ||
        !zxvd_libzxv_ok(c, zxv_ledger_account_ccy_cap(cap, to, ct, sizeof ct, &l)))
        return;
    if (strcmp(cf, ct))
        return zxvd_fail(c, ZXVD_E_PARAMS,
                         "\"from\" holds %s and \"to\" holds %s: a hold is in one currency", cf,
                         ct);
    if (is_escrow(cap, from, tmp) || is_escrow(cap, to, tmp))
        return zxvd_fail(c, ZXVD_E_PARAMS,
                         "an escrow account is moved only by ledger.release and ledger.refund");
    if (!view_load(c, &v)) return view_free(&v);
    for (uint32_t j = 0; j < v.ne; j++)
        if (!strcmp(v.e[j].ccy, cf)) {
            escrow = v.e[j].account;
            have = true;
        }
    uint32_t n = v.top;
    view_free(&v);
    if (n == UINT32_MAX) return zxvd_fail(c, ZXVD_E_PAY, "no hold numbers are left");
    char name[sizeof ESCROW_PREFIX + 16];
    snprintf(name, sizeof name, ESCROW_PREFIX "%s", cf);
    if (!have && !zxvd_libzxv_ok(c, zxvd_books_account(name, cf, ZXV_ACCT_HOLDER, &escrow))) return;
    /* the next number; one a ledger.post already used elsewhere is skipped */
    int32_t st = ZXV_E_DUPLICATE;
    uint64_t entry = 0;
    for (int tries = 0; tries < 64 && st == ZXV_E_DUPLICATE && n < UINT32_MAX; tries++) {
        char ref[36];
        snprintf(ref, sizeof ref, "escrow %u to %u", (unsigned) ++n, (unsigned) to);
        st = zxvd_books_post(from, escrow, amount, cf, ref, &entry);
    }
    if (!zxvd_libzxv_ok(c, st)) return;
    hold_t h = {n, from, to, escrow, amount, entry, 0, "", 0};
    memcpy(h.ccy, cf, sizeof h.ccy);
    put_hold(zxvd_result(c), &h);
}

static void close_hold(zxvd_call_t *c, bool release)
{
    uint32_t n;
    view_t v;
    if (!books(c) || !zxvd_param_u32(c, "hold", &n, true)) return;
    if (!view_load(c, &v)) return view_free(&v);
    hold_t *h = view_hold(&v, n);
    if (!h) {
        zxvd_fail(c, ZXVD_E_PAY, "no hold %u", (unsigned) n);
    } else if (h->state) {
        zxvd_fail(c, ZXVD_E_PAY, "hold %u is already %s", (unsigned) n, STATE[h->state]);
    } else {
        char ref[36];
        snprintf(ref, sizeof ref, "escrow %u %s", (unsigned) n, release ? "release" : "refund");
        if (zxvd_libzxv_ok(c, zxvd_books_post(h->escrow, release ? h->to : h->from, h->amount,
                                              h->ccy, ref, &h->closed_entry))) {
            h->state = release ? 1 : 2;
            put_hold(zxvd_result(c), h);
        }
    }
    view_free(&v);
}

static void m_release(zxvd_call_t *c)
{
    close_hold(c, true);
}

static void m_refund(zxvd_call_t *c)
{
    close_hold(c, false);
}

static void m_holds(zxvd_call_t *c)
{
    view_t v;
    if (!books(c)) return;
    if (!view_load(c, &v)) return view_free(&v);
    zxvd_buf_t *b = zxvd_result(c);
    zxvd_buf_raw(b, "{\"holds\":[", 10);
    for (uint32_t i = 0; i < v.n; i++) {
        if (i) zxvd_buf_raw(b, ",", 1);
        put_hold(b, &v.h[i]);
    }
    zxvd_buf_raw(b, "],\"escrow_accounts\":[", 21);
    for (uint32_t j = 0; j < v.ne; j++) {
        zxvd_buf_printf(b, "%s{\"account\":%u,\"currency\":", j ? "," : "",
                        (unsigned) v.e[j].account);
        zxvd_buf_json_str(b, v.e[j].ccy);
        zxvd_buf_printf(b, ",\"balance_minor\":\"%lld\",\"held_minor\":\"%lld\",\"consistent\":%s}",
                        (long long) v.e[j].balance, (long long) v.e[j].held,
                        v.e[j].balance == v.e[j].held ? "true" : "false");
    }
    zxvd_buf_printf(b, "],\"unmatched\":%u}", (unsigned) v.unmatched);
    view_free(&v);
}

/* ===================== open and close ===================== */

/* compute only, nothing moves: always served (zxvd_rpc.h X1) */
static const zxvd_method_t METHODS[] = {
    {"iso20022.render", m_render},
    {"card.check", m_card},
};

/* P6: behind switches (zxvd_ext.h X1-X7) */
static const zxvd_group_method_t NETTING[] = {
    {"netting.open", m_net_open, false},           {"netting.add", m_net_add, false},
    {"netting.submit", m_net_submit, false},       {"netting.close", m_net_close, false},
    {"netting.positions", m_net_positions, false}, {"netting.list", m_net_list, false},
    {"netting.drop", m_net_drop, false},
};
static const zxvd_group_method_t PASSKEY[] = {
    {"passkey.register", m_pk_register, false},
    {"passkey.verify", m_pk_verify, false},
};
static const zxvd_group_method_t ESCROW[] = {
    {"ledger.hold", m_hold, false},
    {"ledger.release", m_release, false},
    {"ledger.refund", m_refund, false},
    {"ledger.holds", m_holds, true}, /* a read: it moves nothing */
};
static const zxvd_group_t GROUPS[] = {
    {"netting", true, "multilateral netting cycles (test or licensed use only)", NETTING,
     sizeof NETTING / sizeof NETTING[0], NULL},
    {"passkeys", false, "passkey registration and checks", PASSKEY,
     sizeof PASSKEY / sizeof PASSKEY[0], NULL},
    {"escrow", true, "escrow holds on the owner's books (test or licensed use only)", ESCROW,
     sizeof ESCROW / sizeof ESCROW[0], NULL},
};
#define N_PAY_GROUPS (sizeof GROUPS / sizeof GROUPS[0])

int zxvd_pay_open(void)
{
    zxvd_pay_close();
    if (zxv_ctx_create(&NCTX) != ZXV_OK) {
        fprintf(stderr, "zxvd: no context for the netting cycles\n");
        return -1;
    }
    bool ok = zxvd_rpc_add_methods(METHODS, sizeof METHODS / sizeof METHODS[0]) == 0;
    for (size_t i = 0; ok && i < N_PAY_GROUPS; i++) ok = zxvd_rpc_add_group(&GROUPS[i]) == 0;
    if (!ok) {
        fprintf(stderr, "zxvd: no room for the payment methods\n");
        zxvd_pay_close();
        return -1;
    }
    return 0;
}

void zxvd_pay_close(void)
{
    zxvd_rpc_remove_methods(METHODS);
    for (size_t i = 0; i < N_PAY_GROUPS; i++) zxvd_rpc_remove_group(&GROUPS[i]);
    for (uint32_t i = 0; i < ZXVD_PAY_CYCLES; i++) cycle_free(&CY[i]);
    zxv_ctx_destroy(NCTX);
    NCTX = NULL;
    memset(PK, 0, sizeof PK);
    N_PK = 0;
}
