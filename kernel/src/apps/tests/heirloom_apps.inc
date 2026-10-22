/* Copyright (c) 2024-2026 Michael Laurence Curzi and 36N9 Genetics, LLC */
/* SPDX-License-Identifier: Apache-2.0 */
/* Heirloom's model against an estate engine. zxvd's estate group
 * (zxvd_estate.c) is not linked into this in-process zxvd, so estate.* and
 * recovery.* are answered on a link of Heirloom's own (HRL_LINK) by a small
 * engine written here from estate.h E1-E3 and recovery.h R1-R5, and every
 * other request (ledger.*, for Coffer) goes to the real dispatcher. Every
 * signature is a real Ed25519 signature made with throwaway keys this test
 * generates (libzxv zxv_ed25519_keygen), checked by the engine; nothing
 * comes from build_system/keys/. The faraid answers are zxvd's own
 * (test_zxvd_estate.c). */
#include "heirloom_model.h"
#include "zxv_api.h"

enum { HE_ALIVE, HE_REPORTED, HE_CONFIRMED, HE_RULED, HE_DISTRIBUTING, HE_CLOSED };

static heirloom_t HRL;
static zxm_link_t HRL_LINK;
static int hrl_sent;              /* estate.* and recovery.* requests answered */
static bool hrl_bad_manifest;     /* the engine keeps a share it was not sent */
static bool hrl_bad_faraid;       /* the faraid shares pass one whole */
static bool hrl_bad_split;        /* estate.distribute answers a part the preview did not say */
static bool hrl_spend_while_open; /* recovery.status lets an open claim spend */
static zxv_ctx *HRL_CTX;
static zxv_cap *HRL_CAP;

typedef struct {
    uint8_t pk[32], sk[32];
} hrl_key_t;

static hrl_key_t HK_OWNER, HK_G[3], HK_AT, HK_CT, HK_GW, HK_RT, HK_Q[5], HK_D1, HK_NEW;

static struct {
    bool set_up;
    uint8_t owner[32], g[5][32], at[32];
    uint32_t ng, k, approved;
    int64_t wait, release, epoch, reported_at, confirmed_at;
    int phase;
    uint32_t n, id[HEIRLOOM_HEIRS];
    int64_t num[HEIRLOOM_HEIRS], den;
    bool contested[HEIRLOOM_HEIRS];
    bool rec_set_up;
    int rec_state; /* 0 idle, 1 pending, 2 thawing */
    int64_t claim, opened;
} HE;

static void hrl_hex(const uint8_t *p, size_t n, char *out)
{
    for (size_t i = 0; i < n; i++) snprintf(out + 2 * i, 3, "%02x", p[i]);
}

static bool hrl_unhex(const char *s, uint8_t *p, size_t n)
{
    if (strlen(s) != 2 * n) return false;
    for (size_t i = 0; i < n; i++) {
        unsigned v = 0;
        for (int k = 0; k < 2; k++) {
            char c = s[2 * i + (size_t) k];
            unsigned d = c >= '0' && c <= '9'   ? (unsigned) (c - '0')
                         : c >= 'a' && c <= 'f' ? (unsigned) (c - 'a' + 10)
                                                : 16u;
            if (d > 15u) return false;
            v = v * 16u + d;
        }
        p[i] = (uint8_t) v;
    }
    return true;
}

/* the params of a request, from "params": on */
static const char *hrl_params(const char *req)
{
    const char *p = strstr(req, "\"params\":");
    return p ? p + 9 : "";
}

/* the integer after "key": in the params, quoted or not; -1 when absent */
static int64_t hrl_num(const char *from, const char *key)
{
    char k[48];
    snprintf(k, sizeof k, "\"%s\":", key);
    const char *p = strstr(from, k);
    if (!p) return -1;
    p += strlen(k);
    if (*p == '"') p++;
    if (*p < '0' || *p > '9') return -1;
    int64_t v = 0;
    for (; *p >= '0' && *p <= '9'; p++) v = v * 10 + (*p - '0');
    return v;
}

/* the string after "key": into out (cap bytes); false when absent */
static bool hrl_str(const char *from, const char *key, char *out, size_t cap)
{
    char k[48];
    snprintf(k, sizeof k, "\"%s\":\"", key);
    const char *p = strstr(from, k);
    if (!p) return false;
    p += strlen(k);
    const char *e = strchr(p, '"');
    if (!e || (size_t) (e - p) >= cap) return false;
    memcpy(out, p, (size_t) (e - p));
    out[e - p] = 0;
    return true;
}

static bool hrl_true(const char *from, const char *key)
{
    char k[48];
    snprintf(k, sizeof k, "\"%s\":true", key);
    return strstr(from, k) != NULL;
}

static int32_t hrl_ok(char *resp, uint32_t cap, uint32_t id, const char *result)
{
    return snprintf(resp, cap, "{\"jsonrpc\":\"2.0\",\"id\":%u,\"result\":%s}", id, result);
}

static int32_t hrl_err(char *resp, uint32_t cap, uint32_t id, const char *why)
{
    return snprintf(resp, cap,
                    "{\"jsonrpc\":\"2.0\",\"id\":%u,\"error\":{\"code\":-32060,"
                    "\"message\":\"%s\"}}",
                    id, why);
}

/* the bytes a signer signs: kind, the epoch and a value (est_msg's role) */
static size_t hrl_msg(const char *kind, int64_t value, uint8_t *m, size_t cap)
{
    int n = snprintf((char *) m, cap, "heirloom-test|%s|%lld|%lld", kind, (long long) HE.epoch,
                     (long long) value);
    return n > 0 && (size_t) n < cap ? (size_t) n : 0;
}

static bool hrl_verify(const uint8_t pk[32], const char *kind, int64_t value, const char *params)
{
    char sh[HEIRLOOM_SIG_HEX + 1];
    uint8_t sig[64], m[96];
    size_t n = hrl_msg(kind, value, m, sizeof m);
    return n && hrl_str(params, "sig", sh, sizeof sh) && hrl_unhex(sh, sig, 64) &&
           zxv_ed25519_verify(HRL_CAP, pk, m, n, sig) == ZXV_OK;
}

/* a signature by key k over what heirloom_message answers, as 128 hex digits */
static void hrl_sign(const hrl_key_t *k, const char *msg_hex, char *out)
{
    uint8_t m[HEIRLOOM_MSG_HEX / 2], sig[64];
    size_t n = strlen(msg_hex) / 2;
    if (!hrl_unhex(msg_hex, m, n)) n = 0;
    if (zxv_ed25519_sign(HRL_CAP, k->sk, m, n, sig) != ZXV_OK) memset(sig, 0, sizeof sig);
    hrl_hex(sig, 64, out);
}

static const char *hrl_phase_name(int p)
{
    static const char *const N[] = {"alive", "reported",     "confirmed",
                                    "ruled", "distributing", "closed"};
    return p >= 0 && p < 6 ? N[p] : "?";
}

static int32_t hrl_phase(char *resp, uint32_t cap, uint32_t id)
{
    char o[96];
    snprintf(o, sizeof o, "{\"phase\":\"%s\",\"epoch\":\"%lld\"}", hrl_phase_name(HE.phase),
             (long long) HE.epoch);
    return hrl_ok(resp, cap, id, o);
}

static uint32_t hrl_approvals(void)
{
    uint32_t n = 0;
    for (uint32_t x = HE.approved; x; x &= x - 1) n++;
    return n;
}

static int64_t hrl_gcd(int64_t a, int64_t b)
{
    while (b) {
        int64_t t = a % b;
        a = b;
        b = t;
    }
    return a;
}

/* estate.manifest (common law, E4): the heirs over their least common denominator */
static int32_t hrl_manifest(const char *p, char *resp, uint32_t cap, uint32_t id)
{
    if (HE.phase != HE_ALIVE) return hrl_err(resp, cap, id, "EST_ERR_STATE");
    uint32_t n = 0, ids[HEIRLOOM_HEIRS];
    int64_t num[HEIRLOOM_HEIRS], den[HEIRLOOM_HEIRS], l = 1;
    for (const char *q = strstr(p, "{\"id\":"); q && n < HEIRLOOM_HEIRS;
         q = strstr(q + 1, "{\"id\":")) {
        ids[n] = (uint32_t) hrl_num(q, "id");
        num[n] = hrl_num(q, "num");
        den[n] = hrl_num(q, "den");
        if (num[n] < 0 || den[n] < 1) return hrl_err(resp, cap, id, "EST_ERR_ARG");
        l = l / hrl_gcd(l, den[n]) * den[n];
        n++;
    }
    int64_t sum = 0;
    for (uint32_t i = 0; i < n; i++) sum += num[i] * (l / den[i]);
    if (n == 0 || sum != l) return hrl_err(resp, cap, id, "EST_ERR_SUM: not exactly 1");
    HE.n = n;
    HE.den = l;
    char o[1400];
    int k = snprintf(o, sizeof o, "{\"enclave\":\"common\",\"advisory\":false,\"heirs\":[");
    for (uint32_t i = 0; i < n; i++) {
        HE.id[i] = ids[i];
        HE.num[i] = num[i] * (l / den[i]);
        HE.contested[i] = false;
        int64_t kept = HE.num[i] + (hrl_bad_manifest && i == 0 ? 1 : 0);
        k += snprintf(o + k, sizeof o - (size_t) k,
                      "%s{\"id\":%u,\"num\":\"%lld\",\"den\":\"%lld\"}", i ? "," : "",
                      (unsigned) ids[i], (long long) kept, (long long) l);
    }
    snprintf(o + k, sizeof o - (size_t) k, "],\"digest\":\"00\"}");
    return hrl_ok(resp, cap, id, o);
}

static int32_t hrl_status(const char *p, char *resp, uint32_t cap, uint32_t id)
{
    int64_t now = hrl_num(p, "now");
    if (!HE.set_up) return hrl_ok(resp, cap, id, "{\"set_up\":false,\"moves_money\":false}");
    char o[2400];
    int k = snprintf(o, sizeof o,
                     "{\"set_up\":true,\"phase\":\"%s\",\"epoch\":\"%lld\",\"bond_minor\":\"0\","
                     "\"bond\":\"none\",\"approvals\":%u,\"k\":%u,\"enclave\":\"common\","
                     "\"advisory\":false,\"heirs\":[",
                     hrl_phase_name(HE.phase), (long long) HE.epoch, hrl_approvals(), HE.k);
    for (uint32_t i = 0; i < HE.n; i++)
        k += snprintf(o + k, sizeof o - (size_t) k,
                      "%s{\"id\":%u,\"num\":\"%lld\",\"den\":\"%lld\",\"contested\":%s,"
                      "\"escrow_minor\":\"0\",\"streamed_minor\":\"0\"}",
                      i ? "," : "", (unsigned) HE.id[i], (long long) HE.num[i], (long long) HE.den,
                      HE.contested[i] ? "true" : "false");
    bool rel = (HE.phase == HE_DISTRIBUTING || HE.phase == HE_CLOSED) &&
               now >= HE.confirmed_at + HE.release;
    snprintf(o + k, sizeof o - (size_t) k,
             "],\"escrow_minor\":\"0\",\"key_release\":%s,\"digest\":\"00\",\"moves_money\":false}",
             rel ? "true" : "false");
    return hrl_ok(resp, cap, id, o);
}

/* estate.distribute (E2): floor(amount * num / den), the rest one each by largest remainder,
 * ties to the lower index; a contested heir's part to escrow */
static int32_t hrl_distribute(const char *p, char *resp, uint32_t cap, uint32_t id)
{
    if (HE.phase != HE_DISTRIBUTING && HE.phase != HE_CLOSED)
        return hrl_err(resp, cap, id, "EST_ERR_STATE");
    int64_t amount = hrl_num(p, "amount_minor"), part[HEIRLOOM_HEIRS], rem[HEIRLOOM_HEIRS], left;
    if (amount < 0) return hrl_err(resp, cap, id, "EST_ERR_ARG");
    left = amount;
    for (uint32_t i = 0; i < HE.n; i++) {
        part[i] = amount * HE.num[i] / HE.den;
        rem[i] = amount * HE.num[i] % HE.den;
        left -= part[i];
    }
    for (; left > 0; left--) {
        uint32_t best = 0;
        for (uint32_t i = 1; i < HE.n; i++)
            if (rem[i] > rem[best]) best = i;
        part[best]++;
        rem[best] = -1;
    }
    if (hrl_bad_split) part[HE.n - 1]++;
    char o[1400];
    int64_t escrow = 0;
    int k = snprintf(o, sizeof o, "{\"parts\":[");
    for (uint32_t i = 0; i < HE.n; i++) {
        if (HE.contested[i]) escrow += part[i];
        k += snprintf(o + k, sizeof o - (size_t) k,
                      "%s{\"id\":%u,\"minor\":\"%lld\",\"escrowed\":%s}", i ? "," : "",
                      (unsigned) HE.id[i], (long long) (HE.contested[i] ? 0 : part[i]),
                      HE.contested[i] ? "true" : "false");
    }
    snprintf(o + k, sizeof o - (size_t) k, "],\"to_escrow_minor\":\"%lld\",\"moved\":false}",
             (long long) escrow);
    return hrl_ok(resp, cap, id, o);
}

static int32_t hrl_estate(const char *m, const char *p, char *resp, uint32_t cap, uint32_t id)
{
    char s[700];
    if (!strcmp(m, "estate.setup")) {
        if (HE.set_up) return hrl_err(resp, cap, id, "SET_UP: the estate is set up already");
        size_t len;
        if (!hrl_str(p, "owner", s, sizeof s) || !hrl_unhex(s, HE.owner, 32) ||
            !hrl_str(p, "guardians", s, sizeof s) || (len = strlen(s)) % 64 || len == 0 ||
            len > 5 * 64)
            return hrl_err(resp, cap, id, "EST_ERR_ARG");
        HE.ng = (uint32_t) (len / 64);
        for (uint32_t i = 0; i < HE.ng; i++) {
            char one[65];
            memcpy(one, s + 64 * i, 64);
            one[64] = 0;
            hrl_unhex(one, HE.g[i], 32);
        }
        HE.k = (uint32_t) hrl_num(p, "k");
        if (HE.k < 1 || HE.k > HE.ng || !hrl_str(p, "attestors", s, sizeof s) ||
            !hrl_unhex(s, HE.at, 32))
            return hrl_err(resp, cap, id, "EST_ERR_ARG");
        HE.wait = hrl_num(p, "wait");
        HE.release = hrl_num(p, "release");
        HE.set_up = true;
        snprintf(s, sizeof s,
                 "{\"guardians\":%u,\"k\":%u,\"attestors\":1,\"wait\":\"%lld\",\"release\":"
                 "\"%lld\",\"phase\":\"alive\"}",
                 HE.ng, HE.k, (long long) HE.wait, (long long) HE.release);
        return hrl_ok(resp, cap, id, s);
    }
    if (!strcmp(m, "estate.faraid")) {
        /* zxvd's own answers (test_zxvd_estate.c): husband, a son and a daughter */
        if (!hrl_true(p, "husband") || hrl_num(p, "sons") != 1 || hrl_num(p, "daughters") != 1)
            return hrl_err(resp, cap, id, "EST_ERR_UNSUPPORTED");
        bool amt = hrl_num(p, "amount_minor") == 101;
        snprintf(s, sizeof s,
                 "{\"classes\":[{\"class\":\"husband\",\"persons\":1,\"num\":\"1\",\"den\":\"4\""
                 "%s},{\"class\":\"sons\",\"persons\":1,\"num\":\"1\",\"den\":\"2\"%s},{\"class\":"
                 "\"daughters\",\"persons\":1,\"num\":\"1\",\"den\":\"4\"%s}],\"unallocated\":{"
                 "\"num\":\"%s\",\"den\":\"1\"},\"awl\":false,\"radd\":false,\"advisory\":true,"
                 "\"ruling\":false,\"notice\":\"a computation of one common majority treatment, "
                 "not a ruling\"}",
                 amt ? ",\"each_minor\":[\"25\"]" : "", amt ? ",\"each_minor\":[\"51\"]" : "",
                 amt ? ",\"each_minor\":[\"25\"]" : "", hrl_bad_faraid ? "1" : "0");
        return hrl_ok(resp, cap, id, s);
    }
    if (!strcmp(m, "estate.status")) return hrl_status(p, resp, cap, id);
    if (!HE.set_up) return hrl_err(resp, cap, id, "the estate is not set up");
    if (!strcmp(m, "estate.manifest")) return hrl_manifest(p, resp, cap, id);
    if (!strcmp(m, "estate.message")) {
        uint8_t b[96];
        char kind[16], hx[200];
        if (!hrl_str(p, "kind", kind, sizeof kind)) return hrl_err(resp, cap, id, "kind");
        int64_t v = !strcmp(kind, "report")    ? hrl_num(p, "bond_minor")
                    : !strcmp(kind, "contest") ? hrl_num(p, "heir")
                                               : 0;
        size_t n = hrl_msg(kind, v, b, sizeof b);
        hrl_hex(b, n, hx);
        snprintf(s, sizeof s, "{\"message\":\"%s\",\"bytes\":%u,\"scheme\":\"ed25519\"}", hx,
                 (unsigned) n);
        return hrl_ok(resp, cap, id, s);
    }
    if (!strcmp(m, "estate.report")) {
        int64_t bond = hrl_num(p, "bond_minor");
        if (HE.phase != HE_ALIVE) return hrl_err(resp, cap, id, "EST_ERR_STATE");
        if (!hrl_verify(HE.at, "report", bond, p)) return hrl_err(resp, cap, id, "EST_ERR_SIG");
        HE.phase = HE_REPORTED;
        HE.approved = 0;
        HE.reported_at = hrl_num(p, "now");
        return hrl_phase(resp, cap, id);
    }
    if (!strcmp(m, "estate.approve")) {
        int64_t g = hrl_num(p, "guardian");
        if (HE.phase != HE_REPORTED) return hrl_err(resp, cap, id, "EST_ERR_STATE");
        if (g < 0 || g >= (int64_t) HE.ng || !hrl_verify(HE.g[g], "approve", 0, p))
            return hrl_err(resp, cap, id, "EST_ERR_SIG");
        if (HE.approved & (1u << g)) return hrl_err(resp, cap, id, "EST_DUP");
        HE.approved |= 1u << g;
        snprintf(s, sizeof s, "{\"approvals\":%u,\"k\":%u}", hrl_approvals(), HE.k);
        return hrl_ok(resp, cap, id, s);
    }
    if (!strcmp(m, "estate.confirm")) {
        int64_t now = hrl_num(p, "now");
        if (HE.phase != HE_REPORTED) return hrl_err(resp, cap, id, "EST_ERR_STATE");
        if (hrl_approvals() < HE.k) return hrl_err(resp, cap, id, "EST_ERR_QUORUM");
        if (now < HE.reported_at + HE.wait) return hrl_err(resp, cap, id, "EST_ERR_TIME");
        HE.phase = HE_CONFIRMED;
        HE.confirmed_at = now;
        return hrl_phase(resp, cap, id);
    }
    if (!strcmp(m, "estate.veto")) {
        if (HE.phase != HE_REPORTED && HE.phase != HE_CONFIRMED)
            return hrl_err(resp, cap, id, "EST_ERR_STATE");
        if (!hrl_verify(HE.owner, "veto", 0, p)) return hrl_err(resp, cap, id, "EST_ERR_SIG");
        HE.phase = HE_ALIVE;
        HE.approved = 0;
        HE.epoch++;
        return hrl_phase(resp, cap, id);
    }
    if (!strcmp(m, "estate.begin")) {
        if (HE.phase != HE_CONFIRMED) return hrl_err(resp, cap, id, "EST_ERR_STATE");
        HE.phase = HE_DISTRIBUTING;
        return hrl_phase(resp, cap, id);
    }
    if (!strcmp(m, "estate.contest")) {
        int64_t h = hrl_num(p, "heir");
        if (h < 0 || h >= (int64_t) HE.n || !hrl_verify(HE.g[0], "contest", h, p))
            return hrl_err(resp, cap, id, "EST_ERR_SIG");
        HE.contested[h] = true;
        return hrl_ok(resp, cap, id, "{\"contested\":true}");
    }
    if (!strcmp(m, "estate.distribute")) return hrl_distribute(p, resp, cap, id);
    return hrl_err(resp, cap, id, "no such method here");
}

static int32_t hrl_recovery(const char *m, const char *p, char *resp, uint32_t cap, uint32_t id)
{
    char s[512];
    static const char *const S[] = {"idle", "pending", "thawing"};
    if (!strcmp(m, "recovery.setup")) {
        if (HE.rec_set_up) return hrl_err(resp, cap, id, "SET_UP");
        if (!hrl_str(p, "guardians", s, sizeof s) || strlen(s) != 5 * 64)
            return hrl_err(resp, cap, id, "\\\"guardians\\\" must be exactly 5 keys");
        HE.rec_set_up = true;
        return hrl_ok(resp, cap, id,
                      "{\"timelock\":\"259200\",\"rate_window\":\"2592000\",\"rate_max\":3,"
                      "\"state\":\"idle\"}");
    }
    if (!strcmp(m, "recovery.status")) {
        if (!HE.rec_set_up)
            return hrl_ok(resp, cap, id, "{\"set_up\":false,\"notices_delivered\":false}");
        bool spend = HE.rec_state != 1 || hrl_spend_while_open;
        snprintf(s, sizeof s,
                 "{\"set_up\":true,\"state\":\"%s\",\"claim\":\"%lld\",\"approvals\":0,"
                 "\"needed\":3,\"vetoed\":false,\"devices\":1,\"can_view\":true,\"can_spend\":%s,"
                 "\"gateway_verified\":false,\"opened\":\"%lld\",\"vbill_spendable_minor\":\"0\","
                 "\"liquid_allowance_minor\":\"0\",\"notices\":\"%lld\",\"notices_delivered\":"
                 "false}",
                 S[HE.rec_state], (long long) HE.claim, spend ? "true" : "false",
                 (long long) HE.opened, (long long) (HE.claim * 5));
        return hrl_ok(resp, cap, id, s);
    }
    if (!HE.rec_set_up) return hrl_err(resp, cap, id, "recovery is not set up");
    if (!strcmp(m, "recovery.claim")) {
        if (HE.rec_state == 1) return hrl_err(resp, cap, id, "REC_ERR_BUSY");
        HE.rec_state = 1;
        HE.claim++;
        HE.opened = hrl_num(p, "now");
        snprintf(s, sizeof s,
                 "{\"state\":\"pending\",\"claim\":\"%lld\",\"approvals\":0,\"vetoed\":false}",
                 (long long) HE.claim);
        return hrl_ok(resp, cap, id, s);
    }
    return hrl_err(resp, cap, id, "no such method here");
}

/* estate.* and recovery.* here; everything else to the real dispatcher */
static int32_t hrl_route(void *ctx, const char *req, uint32_t len, char *resp, uint32_t cap)
{
    static char r[ZXM_REQ_MAX + 1];
    char m[48];
    if (len > ZXM_REQ_MAX) return -1;
    memcpy(r, req, len);
    r[len] = 0;
    bool est = hrl_str(r, "method", m, sizeof m) && !strncmp(m, "estate.", 7);
    bool rec = !est && hrl_str(r, "method", m, sizeof m) && !strncmp(m, "recovery.", 9);
    if (!est && !rec) return inproc(ctx, req, len, resp, cap);
    hrl_sent++;
    uint32_t id = (uint32_t) hrl_num(r, "id");
    const char *p = hrl_params(r);
    return est ? hrl_estate(m, p, resp, cap, id) : hrl_recovery(m, p, resp, cap, id);
}

static void hrl_keygen(hrl_key_t *k)
{
    if (zxv_ed25519_keygen(HRL_CAP, k->pk, k->sk) != ZXV_OK) memset(k, 0, sizeof *k);
}

static char *hrl_pk(const hrl_key_t *k)
{
    static char b[8][65];
    static int i;
    char *o = b[i++ & 7];
    hrl_hex(k->pk, 32, o);
    return o;
}

/* a test step the model does not take: a raw request on Heirloom's link */
static int hrl_raw(const char *method, const char *params)
{
    zxm_begin(&HRL_LINK, method);
    zxm_p_raw(&HRL_LINK, params, (uint32_t) strlen(params));
    return zxm_call(&HRL_LINK);
}

/* the signature of key k over the estate message of kind (value: bond or heir) */
static void hrl_signed(const hrl_key_t *k, const char *kind, int64_t value, char *sig)
{
    char msg[HEIRLOOM_MSG_HEX + 1];
    if (heirloom_message(&HRL, kind, value, msg, sizeof msg) != ZXM_OK) msg[0] = 0;
    hrl_sign(k, msg, sig);
}

APPS_TEST(heirloom)
{
    /* the rules (HR1 - HR3) */
    CHECK(heirloom_share_gap(1, 2, 1, 2) == 0 && heirloom_share_gap(1, 2, 1, 3) == -1 &&
              heirloom_share_gap(2, 3, 1, 2) == 1 &&
              heirloom_share_gap(HEIRLOOM_NINE, HEIRLOOM_NINE, HEIRLOOM_NINE, HEIRLOOM_NINE) ==
                  INT64_C(999999998000000001) &&
              heirloom_share_gap(3, 2, 1, 2) == HEIRLOOM_REFUSED &&
              heirloom_share_gap(-1, 2, 1, 2) == HEIRLOOM_REFUSED &&
              heirloom_share_gap(1, 0, 1, 2) == HEIRLOOM_REFUSED &&
              heirloom_share_gap(1, HEIRLOOM_NINE + 1, 1, 2) == HEIRLOOM_REFUSED,
          "HR1: the gap from one whole, its edges");
    {
        static const int64_t n3[3] = {1, 1, 1}, d3[3] = {2, 3, 6}, d7[3] = {2, 3, 7},
                             over[3] = {1, 2, 1}, one[1] = {1}, d1[1] = {1};
        int64_t g = 9;
        CHECK(heirloom_manifest_gap(n3, d3, 3, &g) == 0 && g == 0 &&
                  heirloom_manifest_gap(n3, d7, 3, &g) == 0 && g < 0 &&
                  heirloom_manifest_gap(one, d1, 1, &g) == 0 && g == 0 &&
                  heirloom_manifest_gap(over, d3, 3, &g) == HEIRLOOM_REFUSED &&
                  heirloom_manifest_gap(n3, d3, 0, &g) == HEIRLOOM_REFUSED &&
                  heirloom_manifest_gap(n3, d3, HEIRLOOM_HEIRS + 1, &g) == HEIRLOOM_REFUSED,
              "HR2: whole, short, a lone heir, past a whole, no heirs");
    }
    CHECK(heirloom_release_due(1, 2, 0, 0, 0) && !heirloom_release_due(1, 2, -1, 0, 0) &&
              !heirloom_release_due(1, 2, 0, -1, 0) && !heirloom_release_due(1, 2, 0, 0, 1) &&
              !heirloom_release_due(0, 2, 0, 9, 0) && !heirloom_release_due(1, 0, 0, 9, 0) &&
              !heirloom_release_due(1, 6, 0, 9, 0) && heirloom_release_due_n(1, 5, 0, 1, 0) == 1 &&
              heirloom_release_due_n(1, 2, 5, 1, 0) == 0,
          "HR3: due only with a report, k signatures, the wait over and no veto");

    /* throwaway keys, made here */
    CHECK(zxv_ctx_create(&HRL_CTX) == ZXV_OK &&
              zxv_cap_create(HRL_CTX, 1, NULL, ZXV_CAP_CRYPTO_SIGN, &HRL_CAP) == ZXV_OK,
          "a signing capability for the test's own keys");
    hrl_keygen(&HK_OWNER);
    for (int i = 0; i < 3; i++) hrl_keygen(&HK_G[i]);
    hrl_keygen(&HK_AT);
    hrl_keygen(&HK_CT);
    hrl_keygen(&HK_GW);
    hrl_keygen(&HK_RT);
    for (int i = 0; i < 5; i++) hrl_keygen(&HK_Q[i]);
    hrl_keygen(&HK_D1);
    hrl_keygen(&HK_NEW);

    memset(&HE, 0, sizeof HE);
    zxm_link_init(&HRL_LINK, hrl_route, NULL);
    heirloom_init(&HRL, &HRL_LINK);
    CHECK(heirloom_refresh(&HRL, 1) == ZXM_OK && !HRL.set_up && HRL.n == 0, "no estate yet");

    /* estate.setup: three guardians, two to confirm, a wait of 100 s */
    char guardians[3 * 64 + 1];
    snprintf(guardians, sizeof guardians, "%s%s%s", hrl_pk(&HK_G[0]), hrl_pk(&HK_G[1]),
             hrl_pk(&HK_G[2]));
    heirloom_setup_t st = {
        hrl_pk(&HK_OWNER), guardians, hrl_pk(&HK_AT), hrl_pk(&HK_CT), hrl_pk(&HK_GW), 4, 100, 50};
    int before = hrl_sent;
    CHECK(heirloom_setup(&HRL, &st) == ZXM_E_ARG && hrl_sent == before && !HRL.set_up,
          "a quorum past the guardians is refused before anything is sent");
    st.k = 0;
    CHECK(heirloom_setup(&HRL, &st) == ZXM_E_ARG && hrl_sent == before,
          "a quorum of 0 (time alone) is refused");
    st.k = 2;
    CHECK(heirloom_setup(&HRL, &st) == ZXM_OK && HRL.set_up && HRL.guardians == 3 && HRL.k == 2 &&
              HRL.wait == 100 && !strcmp(HRL.phase, "alive"),
          "set up: %s", HRL_LINK.err);

    /* HM1: a will that is not whole is never sent */
    static const uint32_t ids[3] = {1, 2, 3};
    static const int64_t num[3] = {1, 1, 1}, den[3] = {2, 3, 6}, den7[3] = {2, 3, 7},
                         den5[3] = {2, 3, 5};
    before = hrl_sent;
    CHECK(heirloom_set_will(&HRL, ids, num, den7, 3) == ZXM_E_ARG && hrl_sent == before &&
              strstr(HRL.notice, "short") && HRL.n == 0,
          "1/2 + 1/3 + 1/7 is short: nothing sent (%s)", HRL.notice);
    CHECK(heirloom_set_will(&HRL, ids, num, den5, 3) == ZXM_E_ARG && hrl_sent == before &&
              strstr(HRL.notice, "pass"),
          "1/2 + 1/3 + 1/5 passes one whole: nothing sent (%s)", HRL.notice);
    CHECK(heirloom_set_will(&HRL, ids, num, den, 3) == ZXM_OK && HRL.n == 3 &&
              HRL.heir[0].num == 3 && HRL.heir[1].num == 2 && HRL.heir[2].num == 1 &&
              HRL.heir[2].den == 6,
          "1/2 + 1/3 + 1/6 is whole: kept over 6: %s", HRL_LINK.err);
    hrl_bad_manifest = true;
    CHECK(heirloom_set_will(&HRL, ids, num, den, 3) == ZXM_E_PROTOCOL && HRL.heir[0].num == 3,
          "an engine that keeps another share is refused, the copy kept");
    hrl_bad_manifest = false;
    CHECK(heirloom_set_will(&HRL, ids, num, den, 3) == ZXM_OK, "the will again");

    /* HM2: faraid is advice, and whole */
    heirloom_family_t fam = {true, false, false, 0, 1, 1};
    CHECK(heirloom_faraid(&HRL, &fam, 101) == ZXM_OK && HRL.ncls == 3 && HRL.cls[1].num == 1 &&
              HRL.cls[1].den == 2 && HRL.cls[1].each == 51 && HRL.cls[0].each == 25 &&
              strstr(HRL.notice, "not a ruling"),
          "faraid: 1/4, 1/2, 1/4 of 101 is 25, 51, 25: %s", HRL_LINK.err);
    CHECK(heirloom_faraid(&HRL, &fam, -1) == ZXM_OK && HRL.cls[0].each == -1, "faraid shares only");
    hrl_bad_faraid = true;
    CHECK(heirloom_faraid(&HRL, &fam, 101) == ZXM_E_PROTOCOL,
          "faraid shares that pass one whole are refused");
    hrl_bad_faraid = false;
    fam.sons = 2;
    CHECK(heirloom_faraid(&HRL, &fam, 10) == ZXM_E_REMOTE, "the engine's refusal is passed on");

    /* HM4: no report from here, nothing due */
    CHECK(heirloom_refresh(&HRL, 5000) == ZXM_OK && !HRL.release_due && !HRL.filed &&
              strstr(HRL.notice, "No death report"),
          "nothing is due on time alone");
    before = hrl_sent;
    CHECK(heirloom_confirm(&HRL, 5000) == ZXM_E_STATE && hrl_sent == before,
          "no confirm without a report");

    /* HM3: a report signed by the attestor, approvals by guardians */
    char sig[HEIRLOOM_SIG_HEX + 1];
    hrl_signed(&HK_G[0], "report", 500, sig);
    CHECK(heirloom_report(&HRL, 0, 500, 1000, sig) == ZXM_E_REMOTE && !HRL.filed,
          "a report not signed by the attestor is refused");
    CHECK(heirloom_report(&HRL, 0, 500, 1000, "zz") == ZXM_E_ARG, "a signature that is no hex");
    hrl_signed(&HK_AT, "report", 500, sig);
    CHECK(heirloom_report(&HRL, 0, 500, 1000, sig) == ZXM_OK && HRL.filed &&
              HRL.reported_at == 1000 && !strcmp(HRL.phase, "reported"),
          "a signed report: %s", HRL_LINK.err);
    hrl_signed(&HK_G[0], "approve", 0, sig);
    CHECK(heirloom_approve(&HRL, 0, sig) == ZXM_OK && HRL.approvals == 1, "one guardian");
    CHECK(heirloom_refresh(&HRL, 9000) == ZXM_OK && !HRL.release_due && strstr(HRL.notice, "more"),
          "one of two: not due however long it waits (%s)", HRL.notice);
    before = hrl_sent;
    CHECK(heirloom_confirm(&HRL, 9000) == ZXM_E_STATE && hrl_sent == before,
          "no confirm before the quorum");
    CHECK(heirloom_approve(&HRL, 1, sig) == ZXM_E_REMOTE && HRL.approvals == 1,
          "guardian 1 cannot sign with guardian 0's signature");
    hrl_signed(&HK_G[1], "approve", 0, sig);
    CHECK(heirloom_approve(&HRL, 1, sig) == ZXM_OK && HRL.approvals == 2, "two guardians");
    CHECK(heirloom_refresh(&HRL, 1099) == ZXM_OK && !HRL.release_due &&
              strstr(HRL.notice, "waiting period"),
          "one second before the wait ends: not due");
    CHECK(heirloom_refresh(&HRL, 1100) == ZXM_OK && HRL.release_due, "due as the wait ends");

    /* the living veto */
    hrl_signed(&HK_G[2], "veto", 0, sig);
    CHECK(heirloom_veto(&HRL, sig) == ZXM_E_REMOTE && !strcmp(HRL.phase, "reported"),
          "only the owner vetoes");
    hrl_signed(&HK_OWNER, "veto", 0, sig);
    CHECK(heirloom_veto(&HRL, sig) == ZXM_OK && !strcmp(HRL.phase, "alive") && HRL.epoch == 1 &&
              HRL.approvals == 0,
          "the owner's signature cancels the report");
    CHECK(heirloom_refresh(&HRL, 99999) == ZXM_OK && !HRL.release_due &&
              strstr(HRL.notice, "cancelled"),
          "after a veto nothing is due (%s)", HRL.notice);

    /* again, through to the split */
    hrl_signed(&HK_AT, "report", 500, sig);
    CHECK(heirloom_report(&HRL, 0, 500, 3000, sig) == ZXM_OK && HRL.report_epoch == 1, "reported");
    hrl_signed(&HK_G[0], "approve", 0, sig);
    heirloom_approve(&HRL, 0, sig);
    hrl_signed(&HK_G[2], "approve", 0, sig);
    CHECK(heirloom_approve(&HRL, 2, sig) == ZXM_OK && heirloom_refresh(&HRL, 3100) == ZXM_OK &&
              HRL.release_due,
          "two guardians and the wait: due");
    CHECK(heirloom_confirm(&HRL, 3100) == ZXM_OK && !strcmp(HRL.phase, "confirmed"),
          "confirmed: %s", HRL_LINK.err);

    /* HM5: a preview before distribution reports only */
    before = hrl_sent;
    CHECK(heirloom_preview(&HRL, 1000) == ZXM_OK && hrl_sent == before && !HRL.compared &&
              HRL.preview[0] == 500 && HRL.preview[1] == 333 && HRL.preview[2] == 167,
          "1000 previewed as 500, 333, 167; nothing asked");
    CHECK(heirloom_preview(&HRL, -1) == ZXM_E_ARG &&
              heirloom_preview(&HRL, CROWN_NINE + 1) == ZXM_E_ARG,
          "an amount outside 0 .. 9 digits");
    CHECK(hrl_raw("estate.begin", "{}") == ZXM_OK, "distributing");
    hrl_signed(&HK_G[0], "contest", 0, sig);
    char cp[200];
    snprintf(cp, sizeof cp, "{\"heir\":0,\"who\":0,\"sig\":\"%s\"}", sig);
    CHECK(hrl_raw("estate.contest", cp) == ZXM_OK, "heir 1 contested");
    CHECK(heirloom_refresh(&HRL, 3200) == ZXM_OK && !strcmp(HRL.phase, "distributing") &&
              HRL.heir[0].contested && HRL.key_release,
          "distributing, heir 1 contested, the key released after its delay");
    CHECK(heirloom_preview(&HRL, 1000) == ZXM_OK && HRL.compared && !HRL.mismatch &&
              HRL.engine[0] == 0 && HRL.engine[1] == 333 && HRL.engine[2] == 167 &&
              HRL.to_escrow == 500,
          "the engine's split matches the preview, 500 in escrow: %s", HRL_LINK.err);
    hrl_bad_split = true;
    CHECK(heirloom_preview(&HRL, 1000) == ZXM_E_PROTOCOL && HRL.mismatch,
          "an engine part the preview did not say is a mismatch");
    hrl_bad_split = false;

    /* the amount from the books: what is held in USD, through Coffer (ledger.read) */
    char root[256], scratch[512];
    CHECK(zxf_root(root, sizeof root) == 0, "the test's folder");
    snprintf(scratch, sizeof scratch, "%s/heirloom-scratch.books", root);
    zxvd_rpc_books_close();
    CHECK(zxvd_rpc_books_file(scratch) == 0 && zxvd_rpc_books_open() == 0, "scratch books open");
    coffer_t *cf = &HRL.books;
    uint32_t employer = 0, cash = 0, savings = 0;
    uint64_t entry = 0;
    CHECK(coffer_enable(cf, "USD") == ZXM_OK &&
              coffer_open_account(cf, "employer", "USD", true, &employer) == ZXM_OK &&
              coffer_open_account(cf, "cash", "USD", false, &cash) == ZXM_OK &&
              coffer_open_account(cf, "savings", "USD", false, &savings) == ZXM_OK,
          "accounts: %s", HRL_LINK.err);
    CHECK(coffer_propose(cf, employer, cash, 60000, "hl cash") == ZXM_OK &&
              coffer_confirm(cf, &entry) == ZXM_OK &&
              coffer_propose(cf, employer, savings, 40000, "hl save") == ZXM_OK &&
              coffer_confirm(cf, &entry) == ZXM_OK,
          "money in: %s", HRL_LINK.err);
    CHECK(heirloom_preview_holdings(&HRL, "USD") == ZXM_OK && HRL.amount == 100000 &&
              HRL.preview[0] == 50000 && HRL.preview[1] == 33333 && HRL.preview[2] == 16667 &&
              HRL.compared && !HRL.mismatch,
          "100000 held: 50000, 33333, 16667, as the engine says: %s", HRL_LINK.err);
    zxvd_rpc_books_close();

    /* HM6: recovery guardians */
    char five[5 * 64 + 1] = "";
    for (int i = 0; i < 5; i++) strcat(five, hrl_pk(&HK_Q[i]));
    CHECK(heirloom_recovery_status(&HRL, 1, 0, 0) == ZXM_OK && !HRL.rec_set_up, "no recovery yet");
    CHECK(heirloom_recovery_claim(&HRL, hrl_pk(&HK_NEW), 1000) == ZXM_E_STATE,
          "no claim before recovery is set up");
    CHECK(heirloom_recovery_setup(&HRL, hrl_pk(&HK_RT), guardians, hrl_pk(&HK_GW),
                                  hrl_pk(&HK_D1)) == ZXM_E_ARG,
          "three guardians are not five");
    CHECK(heirloom_recovery_setup(&HRL, hrl_pk(&HK_RT), five, hrl_pk(&HK_GW), hrl_pk(&HK_D1)) ==
                  ZXM_OK &&
              HRL.rec_set_up && HRL.timelock == 259200,
          "recovery set up: %s", HRL_LINK.err);
    CHECK(heirloom_recovery_claim(&HRL, hrl_pk(&HK_NEW), 1000) == ZXM_OK && HRL.claim == 1 &&
              !strcmp(HRL.rec_state, "pending"),
          "a claim opened");
    CHECK(heirloom_recovery_claim(&HRL, hrl_pk(&HK_NEW), 1001) == ZXM_E_REMOTE,
          "one claim at a time");
    CHECK(heirloom_recovery_status(&HRL, 2000, 100, 50) == ZXM_OK && HRL.can_view &&
              !HRL.can_spend && HRL.rec_needed == 3 && strstr(HRL.notice, "view-only"),
          "view-only while a claim is open");
    hrl_spend_while_open = true;
    CHECK(heirloom_recovery_status(&HRL, 2000, 100, 50) == ZXM_E_PROTOCOL && !HRL.can_spend,
          "an open claim that may spend is refused");
    hrl_spend_while_open = false;

    zxv_cap_destroy(HRL_CAP);
    zxv_ctx_destroy(HRL_CTX);
}
