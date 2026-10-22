/* Copyright (c) 2024-2026 Michael Laurence Curzi and 36N9 Genetics, LLC */
/* SPDX-License-Identifier: Apache-2.0 */
/* test_juris_cp.c — juris_cp.h JC1-JC7 with real ML-DSA-87 keys: the
 * gateway verify hook, signed plug-in manifests registered through
 * juris_host_register, and capctl, taxhook and the KYC gate answering
 * through the jurisdiction host. */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "juris_cp.h"
#include "../taxhook/taxhook_sample.h"
#include "../cbank/cb_ccy.h"
#include "../robin_debanks/sha256.h"

static int pass, fail;
#define CHECK(c, ...)                                                                              \
    do {                                                                                           \
        if (c)                                                                                     \
            pass++;                                                                                \
        else {                                                                                     \
            fail++;                                                                                \
            printf("FAIL %s:%d: ", __FILE__, __LINE__);                                            \
            printf(__VA_ARGS__);                                                                   \
            printf("\n");                                                                          \
        }                                                                                          \
    } while (0)

#define T0  1767225600ull /* 2026-01-01T00:00:00Z, day 20454 */
#define DAY 86400ull
#define EXP 20482u /* T0 + 28 days, a multiple of 7 */
#define KES 404u

static uint64_t NOW = T0;
static uint64_t clk(void *ctx)
{
    return ctx ? *(const uint64_t *) ctx : 0;
}

static bool ccy_map(const char *alpha, uint16_t *num)
{
    const cb_ccy_t *c = cb_ccy_by_alpha(alpha);
    if (!c) return false;
    *num = c->num;
    return true;
}

static bool ccy_zero(const char *alpha, uint16_t *num)
{
    (void) alpha;
    *num = 0;
    return true;
}

typedef struct {
    uint8_t pk[JURIS_GW_PK_LEN], sk[JURIS_GW_SK_LEN];
} keys_t;

static keys_t GW, GW2, CPS, ISS, HOLD;

static void keygen(keys_t *k, uint8_t s)
{
    uint8_t seed[32];
    memset(seed, s, sizeof seed);
    pqm_mldsa87_keygen(seed, k->pk, k->sk);
}

static lpx_t tag(uint8_t b)
{
    return lpx_make(b, LPX_P_NEUTRAL, LPX_PH_POTENTIAL);
}

static uint8_t base_of(lpx_t t)
{
    return lpx_base(t);
}

/* ---- JC1 ---- */
static void test_gateway(void)
{
    juris_gw_t g;
    memset(&g, 0x5A, sizeof g);
    juris_gw_init(&g);
    CHECK(!g.set && g.pk[0] == 0 && g.pk[JURIS_GW_PK_LEN - 1] == 0, "init clears the key");
    juris_gw_init(NULL);
    uint8_t msg[40], sig[JURIS_GW_SIG_LEN + 1];
    memset(msg, 7, sizeof msg);
    juris_gw_sign(GW.sk, msg, sizeof msg, NULL, sig);
    CHECK(!juris_gw_verify(&g, msg, sizeof msg, sig, JURIS_GW_SIG_LEN),
          "JC1: no key set refuses every signature");
    CHECK(!juris_gw_set(&g, GW.pk, JURIS_GW_PK_LEN - 1) && !g.set, "short key refused");
    CHECK(!juris_gw_set(&g, GW.pk, JURIS_GW_PK_LEN + 1) && !g.set, "long key refused");
    CHECK(!juris_gw_set(&g, NULL, JURIS_GW_PK_LEN) && !juris_gw_set(NULL, GW.pk, JURIS_GW_PK_LEN),
          "NULL refused");
    CHECK(juris_gw_set(&g, GW.pk, JURIS_GW_PK_LEN) && g.set && !memcmp(g.pk, GW.pk, sizeof g.pk),
          "a full key is taken");
    CHECK(juris_gw_verify(&g, msg, sizeof msg, sig, JURIS_GW_SIG_LEN), "a gateway signature");
    CHECK(!juris_gw_verify(&g, msg, sizeof msg, sig, JURIS_GW_SIG_LEN - 1),
          "a truncated signature is refused");
    CHECK(!juris_gw_verify(&g, msg, sizeof msg, sig, JURIS_GW_SIG_LEN + 1),
          "a signature with a trailing byte is refused");
    CHECK(!juris_gw_verify(NULL, msg, sizeof msg, sig, JURIS_GW_SIG_LEN) &&
              !juris_gw_verify(&g, NULL, sizeof msg, sig, JURIS_GW_SIG_LEN) &&
              !juris_gw_verify(&g, msg, sizeof msg, NULL, JURIS_GW_SIG_LEN),
          "NULL context, message or signature refused");
    msg[3] ^= 1;
    CHECK(!juris_gw_verify(&g, msg, sizeof msg, sig, JURIS_GW_SIG_LEN), "a changed message");
    msg[3] ^= 1;
    CHECK(!juris_gw_verify(&g, msg, sizeof msg - 1, sig, JURIS_GW_SIG_LEN), "a shorter message");
    uint8_t sig2[JURIS_GW_SIG_LEN];
    cpk_sign(GW.sk, msg, sizeof msg, NULL, sig2);
    CHECK(!juris_gw_verify(&g, msg, sizeof msg, sig2, JURIS_GW_SIG_LEN),
          "JC1: the same key under cpk's ZXV-COMPLIANCE context is refused");
    CHECK(!cpk_verify_pk(GW.pk, JURIS_GW_PK_LEN, msg, sizeof msg, sig, JURIS_GW_SIG_LEN),
          "and a gateway signature is no compliance signature");
    juris_gw_sign(GW2.sk, msg, sizeof msg, NULL, sig2);
    CHECK(!juris_gw_verify(&g, msg, sizeof msg, sig2, JURIS_GW_SIG_LEN), "another key's");
    uint8_t id[32], want[32];
    juris_gw_key_id(GW.pk, id);
    sha256(GW.pk, JURIS_GW_PK_LEN, want);
    CHECK(!memcmp(id, want, 32), "the key id is SHA-256 of the public key");
    CHECK(!juris_gw_set(&g, GW2.pk, 3) && !memcmp(g.pk, GW.pk, sizeof g.pk),
          "a refused key leaves the old one");
}

/* ---- capctl ---- */
static capctl_t CC;
static capctl_tally_t TALLY[4];
static cpk_ring_t RING;
static uint8_t M[CAPCTL_MANIFEST_MAX], MSIG[CPK_SIG_LEN];
static size_t MLEN;

static void profile(capctl_profile_t *p, const char *cc, uint32_t version)
{
    memset(p, 0, sizeof *p);
    p->hdr.kind = CP_KIND_CAPCTL;
    memcpy(p->hdr.country, cc, 2);
    p->hdr.version = version;
    p->hdr.not_before = T0;
    p->hdr.not_after = T0 + 100 * DAY;
    cpk_key_id(CPS.pk, p->hdr.key_id);
    p->ccy = KES;
    p->flags = CAPCTL_F_OUTBOUND | CAPCTL_F_INBOUND | CAPCTL_F_DEFAULT_ALLOW;
    p->per_tx_limit = 1000;
    p->report_single = 500;
    p->period[0].window_days = 7;
    p->period[0].amount = 1500;
    p->n_period = 1;
}

static capctl_status_t install_profile(const capctl_profile_t *p)
{
    MLEN = capctl_encode(p, M, sizeof M);
    uint8_t msg[CP_MSG_LEN];
    cp_manifest_msg(M, MLEN, msg);
    cpk_sign(CPS.sk, msg, sizeof msg, NULL, MSIG);
    return capctl_install(&CC, M, MLEN, MSIG, sizeof MSIG, NOW);
}

static juris_transfer_t xfer(const char *from, const char *to, const char *ccy, uint64_t amount)
{
    juris_transfer_t t;
    memset(&t, 0, sizeof t);
    snprintf(t.from_country, sizeof t.from_country, "%s", from);
    snprintf(t.to_country, sizeof t.to_country, "%s", to);
    snprintf(t.ccy, sizeof t.ccy, "%s", ccy);
    t.amount = amount;
    t.sender = 11;
    t.receiver = 22;
    t.epoch = 1;
    return t;
}

static uint8_t GSIG[JURIS_GW_SIG_LEN];

/* sign p's J3 manifest over image with key k, version v */
static void gw_manifest(const juris_plugin_t *p, uint64_t v, const uint8_t *image, size_t n,
                        const keys_t *k, juris_manifest_t *m)
{
    CHECK(juris_cp_manifest(p, v, image, n, GSIG, JURIS_GW_SIG_LEN, m), "manifest filled");
    uint8_t msg[JURIS_MANIFEST_MSG];
    juris_manifest_msg(m, msg);
    juris_gw_sign(k->sk, msg, sizeof msg, NULL, GSIG);
}

static juris_settings_t settings(const char *cc)
{
    juris_settings_t s;
    juris_settings_default(&s);
    memcpy(s.country, cc, 3);
    s.in_effect = true;
    return s;
}

static void test_manifest(void)
{
    juris_plugin_t p;
    juris_cp_capctl_t a;
    capctl_init(&CC, cpk_verify, &RING, TALLY, 4);
    CHECK(juris_cp_capctl_plugin(&p, &a, &CC, "KE", clk, &NOW, ccy_map), "capctl plug-in");
    juris_manifest_t m;
    memset(&m, 0x33, sizeof m);
    uint8_t img[3] = {1, 2, 3}, h[32];
    CHECK(juris_cp_manifest(&p, 9, img, 3, GSIG, 77, &m), "JC2: fill");
    sha256(img, 3, h);
    CHECK(!memcmp(m.id, p.id, JURIS_ID_LEN) && m.country[0] == 'K' && m.country[1] == 'E' &&
              m.country[2] == 0 && m.hooks == p.hooks && m.version == 9 &&
              !memcmp(m.image_hash, h, 32) && m.sig == GSIG && m.sig_len == 77,
          "JC2: id, country, hooks, version, image hash and signature");
    CHECK(juris_cp_manifest(&p, 1, NULL, 0, NULL, 0, &m), "an empty image");
    sha256(NULL, 0, h);
    CHECK(!memcmp(m.image_hash, h, 32), "hashes as empty");
    CHECK(!juris_cp_manifest(NULL, 1, img, 3, GSIG, 1, &m) &&
              !juris_cp_manifest(&p, 1, img, 3, GSIG, 1, NULL) &&
              !juris_cp_manifest(&p, 1, NULL, 3, GSIG, 1, &m),
          "JC2: bad arguments refused");
}

static void test_capctl_register(void)
{
    juris_plugin_t p;
    juris_cp_capctl_t a;
    juris_gw_t g;
    juris_gw_init(&g);
    juris_gw_set(&g, GW.pk, JURIS_GW_PK_LEN);
    NOW = T0 + DAY;
    capctl_init(&CC, cpk_verify, &RING, TALLY, 4);
    capctl_enable(&CC, true);
    CHECK(juris_cp_capctl_plugin(&p, &a, &CC, "KE", clk, &NOW, ccy_map), "JC3: plug-in");
    CHECK(!strcmp(p.id, "capctl/KE") && p.country[0] == 'K' && p.country[1] == 'E' &&
              p.country[2] == 0 && p.hooks == (JURIS_HOOK_TRANSFER | JURIS_HOOK_COMMIT) &&
              p.abi == JURIS_PLUGIN_ABI && p.size == sizeof p && p.ctx == &a && p.transfer &&
              p.commit && !p.tax_rate && !p.market && !p.standard,
          "JC3: id capctl/KE, transfer + commit hooks");
    CHECK(CC.n_req == 1 && CC.req[0].country[0] == 'K' && CC.req[0].min_version == 0,
          "JC3: the plug-in's country now requires a profile");
    capctl_profile_t pr;
    profile(&pr, "KE", 2);
    CHECK(install_profile(&pr) == CAPCTL_OK, "a signed KE profile installs");

    juris_settings_t s = settings("KE");
    static lpx_qrec_t q[8];
    juris_host_t h;
    juris_manifest_t m;
    /* no key: refused */
    juris_host_init(&h, &s, juris_gw_verify, NULL, q, 8);
    gw_manifest(&p, 2, M, MLEN, &GW, &m);
    CHECK(juris_host_register(&h, &p, &m, M, MLEN) == JURIS_E_SIG, "no gateway key: refused");
    juris_gw_t none;
    juris_gw_init(&none);
    juris_host_init(&h, &s, juris_gw_verify, &none, q, 8);
    CHECK(juris_host_register(&h, &p, &m, M, MLEN) == JURIS_E_SIG && h.n == 0,
          "an unset gateway key refuses");
    juris_host_init(&h, &s, juris_gw_verify, &g, q, 8);
    /* wrong key */
    gw_manifest(&p, 2, M, MLEN, &GW2, &m);
    CHECK(juris_host_register(&h, &p, &m, M, MLEN) == JURIS_E_SIG, "another key's manifest");
    /* right key, compliance context */
    {
        uint8_t msg[JURIS_MANIFEST_MSG];
        juris_cp_manifest(&p, 2, M, MLEN, GSIG, JURIS_GW_SIG_LEN, &m);
        juris_manifest_msg(&m, msg);
        cpk_sign(GW.sk, msg, sizeof msg, NULL, GSIG);
        CHECK(juris_host_register(&h, &p, &m, M, MLEN) == JURIS_E_SIG,
              "the gateway key under ZXV-COMPLIANCE");
    }
    gw_manifest(&p, 2, M, MLEN, &GW, &m);
    m.sig_len = JURIS_GW_SIG_LEN - 1;
    CHECK(juris_host_register(&h, &p, &m, M, MLEN) == JURIS_E_SIG, "a truncated signature");
    m.sig_len = JURIS_GW_SIG_LEN;
    CHECK(juris_host_register(&h, &p, &m, M, MLEN - 1) == JURIS_E_HASH,
          "a different image (the manifest cut by a byte)");
    /* a signed hook mask that differs from the plug-in's */
    {
        juris_plugin_t q2 = p;
        q2.hooks = JURIS_HOOK_TRANSFER;
        juris_manifest_t m2;
        gw_manifest(&q2, 2, M, MLEN, &GW, &m2);
        CHECK(juris_host_register(&h, &p, &m2, M, MLEN) == JURIS_E_HOOKS, "J3: hook mask");
    }
    gw_manifest(&p, 2, M, MLEN, &GW, &m);
    CHECK(juris_host_register(&h, &p, &m, M, MLEN) == JURIS_OK && h.n == 1,
          "JC1/J3: registered with the gateway's signature over the compliance manifest");
    CHECK(juris_host_register(&h, &p, &m, M, MLEN) == JURIS_E_VERSION, "J3: the same version");
    gw_manifest(&p, 1, M, MLEN, &GW, &m);
    CHECK(juris_host_register(&h, &p, &m, M, MLEN) == JURIS_E_VERSION, "J3: a rollback");
    /* J4: another country's plug-in */
    {
        juris_plugin_t pn;
        juris_cp_capctl_t an;
        static capctl_t ng; /* its own instance: a plug-in requires its country */
        capctl_init(&ng, cpk_verify, &RING, NULL, 0);
        CHECK(juris_cp_capctl_plugin(&pn, &an, &ng, "NG", clk, &NOW, ccy_map), "an NG plug-in");
        juris_manifest_t mn;
        gw_manifest(&pn, 1, M, MLEN, &GW, &mn);
        CHECK(juris_host_register(&h, &pn, &mn, M, MLEN) == JURIS_E_COUNTRY && h.n == 1,
              "J4: only the settings' country registers");
    }

    /* decisions through the host */
    juris_transfer_t t = xfer("KE", "NG", "KES", 100);
    lpx_t r = juris_check_transfer(&h, &t);
    capctl_result_t rep;
    CHECK(r == tag(LPX_B_TRUE) && lpx_action(r) == LPX_ACT_PROCEED, "JC3: ALLOW is TRUE (proceed)");
    CHECK(a.have && a.last.decision == CAPCTL_ALLOW && !juris_cp_capctl_report(&a, &rep),
          "no report for a plain allow");
    t.amount = 600;
    r = juris_check_transfer(&h, &t);
    CHECK(r == tag(LPX_B_TRUE) && juris_cp_capctl_report(&a, &rep) &&
              rep.decision == CAPCTL_ALLOW_WITH_REPORT && rep.n_reports == 1 &&
              rep.report[0].amount == 600 && rep.report[0].person == 11 &&
              rep.report[0].dir == CAPCTL_DIR_OUT && rep.report[0].profile_version == 2,
          "JC3: ALLOW_WITH_REPORT is TRUE and the report is kept for the caller");
    CHECK(!juris_cp_capctl_report(NULL, &rep) && !juris_cp_capctl_report(&a, NULL),
          "report: NULL refused");
    t.amount = 1001;
    r = juris_check_transfer(&h, &t);
    CHECK(base_of(r) == LPX_B_FALSE && a.last.reason == CAPCTL_R_PER_TX_LIMIT &&
              !juris_cp_capctl_report(&a, &rep),
          "JC3: DENY is FALSE (per-transaction limit)");
    t.amount = 1000;
    CHECK(juris_check_transfer(&h, &t) == tag(LPX_B_TRUE), "at the limit");
    t = xfer("NG", "KE", "KES", 700);
    CHECK(juris_check_transfer(&h, &t) == tag(LPX_B_TRUE) && juris_cp_capctl_report(&a, &rep) &&
              rep.report[0].dir == CAPCTL_DIR_IN && rep.report[0].person == 22,
          "inbound to KE is checked by KE's profile");
    t = xfer("KE", "KE", "XXQ", 5);
    CHECK(juris_check_transfer(&h, &t) == tag(LPX_B_FALSE), "JC4: unknown ccy, even domestic");
    t = xfer("KE", "KE", "KES", 5000);
    CHECK(juris_check_transfer(&h, &t) == tag(LPX_B_TRUE) && a.last.reason == CAPCTL_R_DOMESTIC,
          "domestic is allowed");
    /* JC4 mapping */
    static const char *const unk[][2] = {{"", "NG"},   {"KE", ""},   {"ke", "NG"}, {"KE", "N"},
                                         {"K@", "NG"}, {"KE", "[G"}, {"1E", "NG"}, {"KE", "N1"}};
    for (unsigned i = 0; i < sizeof unk / sizeof unk[0]; i++) {
        t = xfer(unk[i][0], unk[i][1], "KES", 10);
        a.have = true;
        r = juris_check_transfer(&h, &t);
        CHECK(base_of(r) == LPX_B_NEITHER && lpx_action(r) != LPX_ACT_PROCEED && !a.have,
              "JC4: country \"%s\" -> \"%s\" waits", unk[i][0], unk[i][1]);
    }
    t = xfer("KE", "NG", "KES", 10);
    t.from_country[2] = 'X';
    CHECK(base_of(juris_check_transfer(&h, &t)) == LPX_B_NEITHER, "JC4: a 3-letter from");
    t = xfer("KE", "NG", "KES", 10);
    t.to_country[2] = 'X';
    CHECK(base_of(juris_check_transfer(&h, &t)) == LPX_B_NEITHER, "JC4: a 3-letter to");
    t = xfer("KE", "NG", "VFV", 10);
    CHECK(juris_check_transfer(&h, &t) == tag(LPX_B_FALSE), "JC4: VFV has no ISO 4217 number");
    t = xfer("KE", "NG", "USD", 10);
    CHECK(juris_check_transfer(&h, &t) == tag(LPX_B_FALSE) && a.last.reason == CAPCTL_R_FX_REQUIRED,
          "JC4: another currency without a base amount is denied");
    a.ccy = NULL;
    t = xfer("KE", "NG", "KES", 10);
    CHECK(juris_check_transfer(&h, &t) == tag(LPX_B_FALSE), "JC4: no mapper is FALSE");
    a.ccy = ccy_zero;
    CHECK(juris_check_transfer(&h, &t) == tag(LPX_B_FALSE), "JC4: numeric 0 is FALSE");
    a.ccy = ccy_map;
    /* commit feeds the rolling window */
    t = xfer("KE", "NG", "KES", 900);
    CHECK(juris_check_transfer(&h, &t) == tag(LPX_B_TRUE), "900 allowed");
    juris_commit_transfer(&h, &t);
    CHECK(capctl_used(&CC, "KE", CAPCTL_DIR_OUT, 11, 7, NOW) == 900,
          "JC3: the commit hook recorded the transfer");
    t.amount = 700;
    CHECK(juris_check_transfer(&h, &t) == tag(LPX_B_FALSE) &&
              a.last.reason == CAPCTL_R_PERIOD_LIMIT,
          "JC3: the 7-day limit now denies 700");
    t.amount = 600;
    CHECK(juris_check_transfer(&h, &t) == tag(LPX_B_TRUE), "and allows 600");
    juris_transfer_t u = xfer("", "NG", "KES", 50);
    juris_commit_transfer(&h, &u);
    u = xfer("KE", "NG", "VFV", 50);
    juris_commit_transfer(&h, &u);
    CHECK(capctl_used(&CC, "KE", CAPCTL_DIR_OUT, 11, 7, NOW) == 900,
          "unmappable transfers commit nothing");
    p.commit(NULL, &t);
    p.commit(&a, NULL);
    CHECK(capctl_used(&CC, "KE", CAPCTL_DIR_OUT, 11, 7, NOW) == 900, "NULL commits nothing");
    CHECK(p.transfer(NULL, &t) == tag(LPX_B_NEITHER) && p.transfer(&a, NULL) == tag(LPX_B_NEITHER),
          "JC7: NULL is NEITHER");
    /* off */
    capctl_enable(&CC, false);
    a.have = true;
    r = juris_check_transfer(&h, &t);
    CHECK(base_of(r) == LPX_B_NEITHER && lpx_action(r) == LPX_ACT_WAIT && !a.have,
          "JC3: capctl off -> NEITHER, the host waits");
    capctl_enable(&CC, true);
    /* expired at evaluation */
    NOW = T0 + 101 * DAY;
    r = juris_check_transfer(&h, &t);
    CHECK(base_of(r) == LPX_B_FALSE && a.last.reason == CAPCTL_R_PROFILE_EXPIRED,
          "C4: an expired profile denies");
    NOW = T0 + DAY;
    /* required but missing: a fresh capctl with no profile */
    capctl_init(&CC, cpk_verify, &RING, TALLY, 4);
    capctl_enable(&CC, true);
    CHECK(juris_cp_capctl_plugin(&p, &a, &CC, "KE", clk, &NOW, ccy_map), "again");
    t = xfer("KE", "NG", "KES", 10);
    CHECK(juris_check_transfer(&h, &t) == tag(LPX_B_FALSE) &&
              a.last.reason == CAPCTL_R_PROFILE_MISSING,
          "JC3: required but missing fails closed");
    a.clock = NULL;
    profile(&pr, "KE", 3);
    CHECK(install_profile(&pr) == CAPCTL_OK, "install v3");
    CHECK(juris_check_transfer(&h, &t) == tag(LPX_B_FALSE) &&
              a.last.reason == CAPCTL_R_PROFILE_EXPIRED,
          "no clock is time 0: before the window, refused");
    a.clock = clk;
    CHECK(juris_check_transfer(&h, &t) == tag(LPX_B_TRUE), "with the clock back");
    a.cctx = NULL;
    CHECK(juris_check_transfer(&h, &t) == tag(LPX_B_FALSE), "the clock's context is passed");
    a.cctx = &NOW;
    /* fill refusals */
    juris_plugin_t z;
    memset(&z, 0x77, sizeof z);
    CHECK(!juris_cp_capctl_plugin(&z, &a, &CC, "ke", clk, &NOW, ccy_map) && z.abi == 0 &&
              z.transfer == NULL,
          "lowercase country refused, p zeroed");
    CHECK(!juris_cp_capctl_plugin(&z, NULL, &CC, "KE", clk, &NOW, ccy_map) &&
              !juris_cp_capctl_plugin(&z, &a, NULL, "KE", clk, &NOW, ccy_map) &&
              !juris_cp_capctl_plugin(&z, &a, &CC, NULL, clk, &NOW, ccy_map) &&
              !juris_cp_capctl_plugin(NULL, &a, &CC, "KE", clk, &NOW, ccy_map) &&
              !juris_cp_capctl_plugin(&z, &a, &CC, "K", clk, &NOW, ccy_map) &&
              !juris_cp_capctl_plugin(&z, &a, &CC, "@E", clk, &NOW, ccy_map) &&
              !juris_cp_capctl_plugin(&z, &a, &CC, "K[", clk, &NOW, ccy_map),
          "bad arguments refused");
    CC.n_req = CAPCTL_MAX_REQ;
    CHECK(!juris_cp_capctl_plugin(&z, &a, &CC, "TZ", clk, &NOW, ccy_map) && z.abi == 0,
          "capctl_require full: refused");
}

/* ---- taxhook ---- */
static taxhook_t TH;
static uint8_t TM[TAXHOOK_MANIFEST_MAX], TSIG[CPK_SIG_LEN];
static size_t TLEN;

static taxhook_status_t install_table(taxhook_table_t *t, uint64_t nb, uint64_t na)
{
    t->hdr.not_before = nb;
    t->hdr.not_after = na;
    cpk_key_id(CPS.pk, t->hdr.key_id);
    TLEN = taxhook_encode(t, TM, sizeof TM);
    uint8_t msg[CP_MSG_LEN];
    cp_manifest_msg(TM, TLEN, msg);
    cpk_sign(CPS.sk, msg, sizeof msg, NULL, TSIG);
    return taxhook_install(&TH, TM, TLEN, TSIG, sizeof TSIG, NOW);
}

static void test_ymd(void)
{
    CHECK(juris_cp_ymd(0) == 19700101u, "1970-01-01");
    CHECK(juris_cp_ymd(T0) == 20260101u && juris_cp_ymd(T0 - 1) == 20251231u, "2026-01-01");
    CHECK(juris_cp_ymd(951782400ull) == 20000229u && juris_cp_ymd(951868800ull) == 20000301u,
          "2000-02-29, then March");
    CHECK(juris_cp_ymd(4107542400ull) == 21000301u && juris_cp_ymd(4107456000ull) == 21000228u,
          "2100 is not a leap year");
    CHECK(juris_cp_ymd(1593561600ull) == 20200701u, "2020-07-01");
    CHECK(juris_cp_ymd(1709164800ull) == 20240229u, "2024-02-29");
    CHECK(juris_cp_ymd(253402300799ull) == 99991231u && juris_cp_ymd(253402300800ull) == 0,
          "9999-12-31 is the last day");
    CHECK(juris_cp_ymd(UINT64_MAX) == 0, "far future");
    /* every day from 1970-01-01 to 9999-12-31 against a plain day counter */
    uint32_t y = 1970, mo = 1, dd = 1, bad = 0;
    static const uint8_t mlen[12] = {31, 28, 31, 30, 31, 30, 31, 31, 30, 31, 30, 31};
    for (uint64_t day = 0; day <= 2932896u; day++) {
        uint64_t at = day * DAY + (day % 3u) * 43199u;
        if (juris_cp_ymd(at) != y * 10000u + mo * 100u + dd) bad++;
        bool leap = (y % 4u == 0 && y % 100u != 0) || y % 400u == 0;
        if (++dd > mlen[mo - 1] + (mo == 2 && leap ? 1u : 0u)) {
            dd = 1;
            if (++mo > 12) {
                mo = 1;
                y++;
            }
        }
    }
    CHECK(bad == 0 && y == 10000 && mo == 1 && dd == 1, "every day to 9999 (%u wrong)", bad);
}

static void test_tax(void)
{
    juris_gw_t g;
    juris_gw_init(&g);
    juris_gw_set(&g, GW.pk, JURIS_GW_PK_LEN);
    NOW = T0;
    taxhook_init(&TH, cpk_verify, &RING);
    taxhook_enable(&TH, true);
    taxhook_table_t de;
    taxhook_sample_de_vat(&de);
    CHECK(install_table(&de, 1577836800ull /* 2020 */, T0 + 365 * DAY) == TAXHOOK_OK,
          "DE VAT sample table installs");
    juris_plugin_t p;
    juris_cp_tax_t a;
    static const char nat[CP_REGION_LEN] = {0};
    CHECK(juris_cp_tax_plugin(&p, &a, &TH, "DE", nat, TAXHOOK_VAT, clk, &NOW), "JC5: plug-in");
    CHECK(!memcmp(p.id, "taxhook/DE//1", 14) && p.hooks == JURIS_HOOK_TAX && p.tax_rate &&
              !p.transfer && !p.commit && p.ctx == &a && p.country[0] == 'D' && p.country[2] == 0,
          "JC5: id taxhook/DE//1, tax hook only: %s", p.id);
    juris_settings_t s = settings("DE");
    static lpx_qrec_t q[8];
    juris_host_t h;
    juris_host_init(&h, &s, juris_gw_verify, &g, q, 8);
    juris_manifest_t m;
    gw_manifest(&p, 1, TM, TLEN, &GW, &m);
    CHECK(juris_host_register(&h, &p, &m, TM, TLEN) == JURIS_OK, "registered");
    uint64_t n = 1, d = 1;
    CHECK(juris_host_tax_rate(&h, JURIS_TAX_VAT, &n, &d) == JURIS_OK && n == 190000 && d == 1000000,
          "JC5: DE standard VAT 19%% in 2026: %llu/%llu", (unsigned long long) n,
          (unsigned long long) d);
    NOW = 1596240000ull; /* 2020-08-01 */
    CHECK(juris_host_tax_rate(&h, JURIS_TAX_VAT, &n, &d) == JURIS_OK && n == 160000,
          "JC5: the rate in force on the day (16%% in 2020-08)");
    NOW = 1609459199ull; /* 2020-12-31 23:59:59 */
    CHECK(juris_host_tax_rate(&h, JURIS_TAX_VAT, &n, &d) == JURIS_OK && n == 160000,
          "last second of 2020");
    NOW = 1609459200ull;
    CHECK(juris_host_tax_rate(&h, JURIS_TAX_VAT, &n, &d) == JURIS_OK && n == 190000,
          "first second of 2021");
    NOW = T0;
    CHECK(juris_host_tax_rate(&h, JURIS_TAX_INCOME, &n, &d) == JURIS_E_NO_RULE &&
              juris_host_tax_rate(&h, JURIS_TAX_WITHHOLDING, &n, &d) == JURIS_E_NO_RULE,
          "JC5: income and withholding: this table does not know");
    uint64_t pn = 5, pd = 5;
    CHECK(p.tax_rate(&a, JURIS_TAX_INCOME, &pn, &pd) == tag(LPX_B_NEITHER) && pn == 0 && pd == 0,
          "NEITHER for income, outputs zeroed");
    CHECK(p.tax_rate(&a, JURIS_TAX_VAT, &pn, &pd) == tag(LPX_B_TRUE) && pn == 190000 &&
              pd == TAXHOOK_PPM,
          "TRUE with the rate");
    CHECK(p.tax_rate(NULL, JURIS_TAX_VAT, &pn, &pd) == tag(LPX_B_NEITHER) && pn == 0 &&
              p.tax_rate(&a, JURIS_TAX_VAT, NULL, &pd) == tag(LPX_B_NEITHER) && pd == 0 &&
              p.tax_rate(&a, JURIS_TAX_VAT, &pn, NULL) == tag(LPX_B_NEITHER),
          "JC7: NULL is NEITHER");
    taxhook_enable(&TH, false);
    CHECK(p.tax_rate(&a, JURIS_TAX_VAT, &pn, &pd) == tag(LPX_B_NEITHER) &&
              juris_host_tax_rate(&h, JURIS_TAX_VAT, &n, &d) == JURIS_E_NO_RULE,
          "JC5: taxhook off is NEITHER");
    taxhook_enable(&TH, true);
    NOW = T0 + 366 * DAY;
    CHECK(p.tax_rate(&a, JURIS_TAX_VAT, &pn, &pd) == tag(LPX_B_FALSE) &&
              juris_host_tax_rate(&h, JURIS_TAX_VAT, &n, &d) == JURIS_E_NO_RULE,
          "JC5: an expired table is FALSE");
    NOW = 1577836800ull + 10; /* 2020-01-01: in the window, 19% in force */
    CHECK(p.tax_rate(&a, JURIS_TAX_VAT, &pn, &pd) == tag(LPX_B_TRUE) && pn == 190000, "2020-01-01");
    a.clock = NULL; /* time 0: before the window */
    CHECK(p.tax_rate(&a, JURIS_TAX_VAT, &pn, &pd) == tag(LPX_B_FALSE), "no clock: FALSE");
    a.clock = clk;
    a.cctx = NULL;
    CHECK(p.tax_rate(&a, JURIS_TAX_VAT, &pn, &pd) == tag(LPX_B_FALSE), "clock context passed");
    a.cctx = &NOW;
    NOW = T0;
    /* a second table: a regional VAT table answers NEITHER, so the national one decides */
    taxhook_table_t by;
    taxhook_sample_de_vat(&by);
    memcpy(by.hdr.region, "BY", 2);
    by.rate[0].rate_ppm = 1;
    CHECK(install_table(&by, 1577836800ull, T0 + 365 * DAY) == TAXHOOK_OK, "DE-BY table");
    juris_plugin_t pb;
    juris_cp_tax_t ab;
    static const char byr[CP_REGION_LEN] = {'B', 'Y'};
    CHECK(juris_cp_tax_plugin(&pb, &ab, &TH, "DE", byr, TAXHOOK_VAT, clk, &NOW) &&
              !strcmp(pb.id, "taxhook/DE/BY/1"),
          "regional plug-in id %s", pb.id);
    gw_manifest(&pb, 1, TM, TLEN, &GW, &m);
    CHECK(juris_host_register(&h, &pb, &m, TM, TLEN) == JURIS_OK, "registered");
    CHECK(pb.tax_rate(&ab, JURIS_TAX_VAT, &pn, &pd) == tag(LPX_B_NEITHER),
          "JC5: a regional table does not answer the national question");
    CHECK(juris_host_tax_rate(&h, JURIS_TAX_VAT, &n, &d) == JURIS_OK && n == 190000,
          "the national table decides");
    /* a GST table answers; a sales-tax table does not */
    taxhook_table_t gst;
    taxhook_sample_de_vat(&gst);
    gst.kind = TAXHOOK_GST;
    CHECK(install_table(&gst, 1577836800ull, T0 + 365 * DAY) == TAXHOOK_OK, "GST table");
    juris_plugin_t pg;
    juris_cp_tax_t ag;
    CHECK(juris_cp_tax_plugin(&pg, &ag, &TH, "DE", nat, TAXHOOK_GST, clk, &NOW) &&
              !strcmp(pg.id, "taxhook/DE//2"),
          "GST plug-in");
    CHECK(pg.tax_rate(&ag, JURIS_TAX_VAT, &pn, &pd) == tag(LPX_B_TRUE) && pn == 190000,
          "JC5: GST answers the VAT question");
    juris_plugin_t ps;
    juris_cp_tax_t as;
    CHECK(juris_cp_tax_plugin(&ps, &as, &TH, "DE", nat, TAXHOOK_SALES, clk, &NOW) &&
              !strcmp(ps.id, "taxhook/DE//3"),
          "sales plug-in");
    CHECK(ps.tax_rate(&as, JURIS_TAX_VAT, &pn, &pd) == tag(LPX_B_NEITHER),
          "JC5: a sales-tax table does not answer VAT");
    /* a VAT plug-in with no table installed for it: FALSE */
    juris_plugin_t pf;
    juris_cp_tax_t af;
    CHECK(juris_cp_tax_plugin(&pf, &af, &TH, "FR", nat, TAXHOOK_VAT, clk, &NOW), "FR plug-in");
    CHECK(pf.tax_rate(&af, JURIS_TAX_VAT, &pn, &pd) == tag(LPX_B_FALSE), "JC5: no table: FALSE");
    /* a table with no STANDARD rate in force: FALSE, and it outvotes the good one */
    NOW = 1167609600ull - DAY; /* 2006-12-31: before DE's first entry */
    taxhook_init(&TH, cpk_verify, &RING);
    taxhook_enable(&TH, true);
    taxhook_sample_de_vat(&de);
    CHECK(install_table(&de, 0, T0 + 365 * DAY) == TAXHOOK_OK, "DE from time 0");
    CHECK(p.tax_rate(&a, JURIS_TAX_VAT, &pn, &pd) == tag(LPX_B_FALSE) && pn == 0,
          "JC5: no rate in force on the day: FALSE, no guess");
    NOW = T0;
    /* fill refusals */
    juris_plugin_t z;
    memset(&z, 0x77, sizeof z);
    static const char badr[][CP_REGION_LEN] = {{'b', 'y'}, {0, 'Y'}, {'B', 0, 'Y'}, {'-'}};
    for (unsigned i = 0; i < 4; i++)
        CHECK(!juris_cp_tax_plugin(&z, &a, &TH, "DE", badr[i], TAXHOOK_VAT, clk, &NOW) &&
                  z.abi == 0,
              "bad region %u refused", i);
    static const char full[CP_REGION_LEN] = {'A', 'Z', '0', '9', 'Q', 'R'};
    CHECK(juris_cp_tax_plugin(&z, &a, &TH, "DE", full, TAXHOOK_VAT, clk, &NOW) &&
              !strcmp(z.id, "taxhook/DE/AZ09QR/1"),
          "a six-character region");
    /* JC4 letters: A and Z are letters, the bytes beside them are not */
    static const char *okcc[] = {"AA", "ZZ", "AZ", "ZA"};
    for (unsigned i = 0; i < 4; i++)
        CHECK(juris_cp_tax_plugin(&z, &a, &TH, okcc[i], nat, TAXHOOK_VAT, clk, &NOW) &&
                  z.country[0] == okcc[i][0] && z.country[1] == okcc[i][1],
              "country %s accepted", okcc[i]);
    static const char *badcc[] = {"@A", "[A", "A@B", "A[B", "`A", "A{"};
    for (unsigned i = 0; i < 6; i++)
        CHECK(!juris_cp_tax_plugin(&z, &a, &TH, badcc[i], nat, TAXHOOK_VAT, clk, &NOW) &&
                  z.abi == 0,
              "country %s refused", badcc[i]);
    static const char r1[CP_REGION_LEN] = {'B'};
    CHECK(juris_cp_tax_plugin(&z, &a, &TH, "AZ", r1, TAXHOOK_SALES, clk, &NOW) &&
              !memcmp(z.id, "taxhook/AZ/B/3", 15),
          "id taxhook/AZ/B/3, NUL padded: %s", z.id);
    {
        uint64_t rn = 1, rd = 1;
        CHECK(juris_cp_tax_plugin(&z, &a, &TH, "DE", r1, TAXHOOK_VAT, clk, &NOW) &&
                  z.tax_rate(z.ctx, JURIS_TAX_VAT, &rn, &rd) == tag(LPX_B_NEITHER) && rn == 0 &&
                  rd == 0,
              "JC5: a one-letter region is regional: NEITHER");
    }
    CHECK(!juris_cp_tax_plugin(&z, &a, &TH, "DE", nat, 0, clk, &NOW) &&
              !juris_cp_tax_plugin(&z, &a, &TH, "DE", nat, 4, clk, &NOW) &&
              !juris_cp_tax_plugin(&z, &a, &TH, "de", nat, 1, clk, &NOW) &&
              !juris_cp_tax_plugin(&z, &a, &TH, "D", nat, 1, clk, &NOW) &&
              !juris_cp_tax_plugin(&z, &a, &TH, NULL, nat, 1, clk, &NOW) &&
              !juris_cp_tax_plugin(&z, &a, &TH, "DE", NULL, 1, clk, &NOW) &&
              !juris_cp_tax_plugin(&z, &a, NULL, "DE", nat, 1, clk, &NOW) &&
              !juris_cp_tax_plugin(&z, NULL, &TH, "DE", nat, 1, clk, &NOW) &&
              !juris_cp_tax_plugin(NULL, &a, &TH, "DE", nat, 1, clk, &NOW),
          "bad arguments refused");
}

/* ---- the KYC gate ---- */
static void sign_with(void *ctx, const uint8_t *msg, size_t len, uint8_t sig[KYC_SIG_LEN])
{
    cpk_sign(((const keys_t *) ctx)->sk, msg, len, NULL, sig);
}

static const kyc_pred_t OVER18 = {KYC_PRED_AGE_OVER, 18, {0, 0}};
static const kyc_pred_t OVER21 = {KYC_PRED_AGE_OVER, 21, {0, 0}};
static kyc_verifier_t V;
static kyc_md_verifier_t MD;
static kyc_backend_t BE;
static kyc_spent_t SPENT[16];
static cpk_ring_t IRING;
static uint8_t TOK[3][KYC_MD_TOKEN_LEN];

static void present(int which, const kyc_pred_t *pred, kyc_request_t *req,
                    uint8_t out[KYC_MD_PRESENT_LEN])
{
    uint8_t rnd[32];
    memset(rnd, 0x40 + which, sizeof rnd);
    CHECK(kyc_request_new(&V, pred, rnd, NOW, req) == KYC_OK, "challenge");
    kyc_md_present(TOK[which], req, sign_with, &HOLD, out);
}

static void test_kyc(void)
{
    NOW = T0;
    cpk_init(&IRING);
    cpk_add(&IRING, ISS.pk, 1u << CP_KIND_KYC_TOKEN, "DE");
    kyc_md_verifier_init(&MD, cpk_verify, &IRING, cpk_verify_pk, SPENT, 16);
    MD.require_revlist = false;
    kyc_md_backend(&BE, &MD);
    uint8_t vid[KYC_ID_LEN];
    memset(vid, 9, sizeof vid);
    kyc_verifier_init(&V, vid, &BE);
    kyc_verifier_enable(&V, true);
    kyc_md_issuer_t is;
    memset(&is, 0, sizeof is);
    cpk_key_id(ISS.pk, is.issuer_id);
    memcpy(is.issuer_country, "DE", 2);
    is.sign = sign_with;
    is.sctx = &ISS;
    uint8_t seed[32];
    memset(seed, 3, sizeof seed);
    for (uint32_t i = 0; i < 3; i++)
        CHECK(kyc_md_issue(&is, seed, i, &OVER18, EXP, HOLD.pk, TOK[i]) == KYC_OK, "token %u", i);

    juris_kyc_gate_t g;
    juris_kyc_entry_t store[2];
    CHECK(!juris_kyc_init(&g, NULL, &OVER18, "KE", clk, &NOW, store, 2) &&
              !juris_kyc_init(&g, &V, NULL, "KE", clk, &NOW, store, 2) &&
              !juris_kyc_init(&g, &V, &(kyc_pred_t){KYC_PRED_AGE_OVER, 0, {0, 0}}, "KE", clk, &NOW,
                              store, 2) &&
              !juris_kyc_init(&g, &V, &OVER18, NULL, clk, &NOW, store, 2) &&
              !juris_kyc_init(&g, &V, &OVER18, "kE", clk, &NOW, store, 2) &&
              !juris_kyc_init(&g, &V, &OVER18, "KE", clk, &NOW, NULL, 2) &&
              !juris_kyc_init(NULL, &V, &OVER18, "KE", clk, &NOW, store, 2),
          "JC6: bad arguments refused");
    juris_plugin_t p;
    memset(&p, 0x77, sizeof p);
    CHECK(!juris_kyc_plugin(&p, &g) && p.abi == 0, "a refused gate makes no plug-in");
    CHECK(!juris_kyc_plugin(&p, NULL) && !juris_kyc_plugin(NULL, &g), "NULL refused");
    CHECK(juris_kyc_init(&g, &V, &OVER18, "KE", clk, &NOW, NULL, 0), "no store, cap 0");
    CHECK(juris_kyc_init(&g, &V, &OVER18, "KE", clk, &NOW, store, 1) && !g.enabled && g.n == 0 &&
              g.cap == 1,
          "JC6: off by default");
    CHECK(juris_kyc_plugin(&p, &g) && !strcmp(p.id, "kycgate/KE") &&
              p.hooks == JURIS_HOOK_TRANSFER && p.transfer && !p.commit && p.ctx == &g &&
              p.country[0] == 'K' && p.country[1] == 'E',
          "JC6: transfer plug-in kycgate/KE");
    juris_transfer_t t = xfer("KE", "NG", "KES", 10);
    t.sender = 7;
    t.receiver = 8;
    CHECK(p.transfer(&g, &t) == tag(LPX_B_NEITHER), "JC6: off is NEITHER");
    juris_kyc_enable(&g, true, false);
    CHECK(p.transfer(&g, &t) == tag(LPX_B_FALSE), "JC6: on, sender not admitted: FALSE");

    kyc_request_t req;
    uint8_t pr[KYC_MD_PRESENT_LEN];
    present(0, &OVER21, &req, pr);
    CHECK(juris_kyc_admit(&g, &req, pr, sizeof pr, NOW, 7) == KYC_E_PREDICATE && g.n == 0,
          "JC6: another predicate refused");
    present(0, &OVER18, &req, pr);
    CHECK(juris_kyc_admit(&g, &req, pr, sizeof pr - 1, NOW, 7) == KYC_E_DECODE && g.n == 0,
          "a cut proof refused");
    present(0, &OVER18, &req, pr);
    CHECK(juris_kyc_admit(&g, &req, pr, sizeof pr, NOW, 7) == KYC_OK && g.n == 1 &&
              store[0].account == 7 && store[0].expiry_day == EXP,
          "JC6: admitted until the token's expiry day");
    CHECK(juris_kyc_admit(&g, &req, pr, sizeof pr, NOW, 7) == KYC_E_CHALLENGE,
          "a replayed presentation refused");
    CHECK(p.transfer(&g, &t) == tag(LPX_B_TRUE), "JC6: sender admitted: TRUE");
    juris_kyc_enable(&g, true, true);
    CHECK(p.transfer(&g, &t) == tag(LPX_B_FALSE), "JC6: receiver required and missing: FALSE");
    /* full: room checked before the proof is verified, so the token is not spent */
    present(1, &OVER18, &req, pr);
    CHECK(juris_kyc_admit(&g, &req, pr, sizeof pr, NOW, 8) == KYC_E_FULL && g.n == 1,
          "JC6: full gate refuses a new account");
    juris_kyc_entry_t store2[2];
    juris_kyc_gate_t g2;
    juris_kyc_init(&g2, &V, &OVER18, "KE", clk, &NOW, store2, 2);
    CHECK(juris_kyc_admit(&g2, &req, pr, sizeof pr, NOW, 8) == KYC_OK,
          "and the same proof still works elsewhere: nothing was spent");
    /* only the first n entries count; every one of them is searched */
    store2[1].account = 11;
    store2[1].expiry_day = EXP;
    juris_kyc_enable(&g2, true, false);
    juris_transfer_t t2 = t;
    t2.sender = 11;
    CHECK(g2.n == 1 && p.transfer(&g2, &t2) == tag(LPX_B_FALSE),
          "a stale store entry past n is not an admission");
    g2.n = 2;
    CHECK(p.transfer(&g2, &t2) == tag(LPX_B_TRUE), "the second entry is found");
    t2.sender = 8;
    CHECK(p.transfer(&g2, &t2) == tag(LPX_B_TRUE) && store2[0].expiry_day == EXP,
          "the first entry is found, its expiry the token's");
    /* an existing account renews in place, even when full */
    present(2, &OVER18, &req, pr);
    CHECK(juris_kyc_admit(&g, &req, pr, sizeof pr, NOW, 7) == KYC_OK && g.n == 1,
          "a second admission of the same account takes no room");
    store[0].expiry_day = EXP + 7;
    present(1, &OVER18, &req, pr); /* TOK[1] is spent at V now */
    CHECK(juris_kyc_admit(&g, &req, pr, sizeof pr, NOW, 7) == KYC_E_SPENT &&
              store[0].expiry_day == EXP + 7,
          "a spent token refused, entry unchanged");
    store[0].expiry_day = EXP + 7;
    memset(seed, 4, sizeof seed);
    CHECK(kyc_md_issue(&is, seed, 0, &OVER18, EXP, HOLD.pk, TOK[0]) == KYC_OK, "fresh token");
    present(0, &OVER18, &req, pr);
    CHECK(juris_kyc_admit(&g, &req, pr, sizeof pr, NOW, 7) == KYC_OK &&
              store[0].expiry_day == EXP + 7,
          "a shorter token never shortens an admission");
    store[0].expiry_day = EXP - 7;
    memset(seed, 5, sizeof seed);
    CHECK(kyc_md_issue(&is, seed, 0, &OVER18, EXP, HOLD.pk, TOK[0]) == KYC_OK, "fresh token");
    present(0, &OVER18, &req, pr);
    CHECK(juris_kyc_admit(&g, &req, pr, sizeof pr, NOW, 7) == KYC_OK && store[0].expiry_day == EXP,
          "a longer token extends an admission");
    juris_kyc_enable(&g, true, false);
    NOW = (uint64_t) EXP * DAY + DAY - 1;
    CHECK(p.transfer(&g, &t) == tag(LPX_B_TRUE), "the last second of the expiry day");
    NOW = (uint64_t) (EXP + 1) * DAY;
    CHECK(p.transfer(&g, &t) == tag(LPX_B_FALSE), "JC6: the day after expiry: FALSE");
    g.clock = NULL;
    CHECK(p.transfer(&g, &t) == tag(LPX_B_TRUE), "no clock is day 0");
    g.clock = clk;
    NOW = T0;
    juris_kyc_enable(&g, true, true);
    t.receiver = 7;
    CHECK(p.transfer(&g, &t) == tag(LPX_B_TRUE), "both admitted (the same account)");
    CHECK(p.transfer(NULL, &t) == tag(LPX_B_NEITHER) && p.transfer(&g, NULL) == tag(LPX_B_NEITHER),
          "JC7: NULL is NEITHER");
    CHECK(juris_kyc_admit(NULL, &req, pr, 1, NOW, 1) == KYC_E_ARG &&
              juris_kyc_admit(&g, NULL, pr, 1, NOW, 1) == KYC_E_ARG &&
              juris_kyc_admit(&g, &req, NULL, 1, NOW, 1) == KYC_E_ARG,
          "admit: NULL refused");
    /* the predicate must match in every field, both country letters included */
    static const kyc_pred_t RES_DE = {KYC_PRED_RESIDENT, 0, {'D', 'E'}};
    static const kyc_pred_t RES_x[] = {{KYC_PRED_RESIDENT, 0, {'E', 'E'}},
                                       {KYC_PRED_RESIDENT, 0, {'D', 'D'}},
                                       {KYC_PRED_RESIDENT, 0, {'E', 'D'}},
                                       {KYC_PRED_RESIDENT, 0, {'D', 'F'}},
                                       {KYC_PRED_RESIDENT, 0, {'C', 'E'}}};
    juris_kyc_gate_t g3;
    juris_kyc_entry_t store3[1];
    CHECK(juris_kyc_init(&g3, &V, &RES_DE, "DE", clk, &NOW, store3, 1) && g3.country[0] == 'D' &&
              g3.country[1] == 'E',
          "a residence gate");
    for (unsigned i = 0; i < 5; i++) {
        present(1, &RES_x[i], &req, pr);
        CHECK(juris_kyc_admit(&g3, &req, pr, sizeof pr, NOW, 7) == KYC_E_PREDICATE && g3.n == 0,
              "resident %c%c is not resident DE", RES_x[i].country[0], RES_x[i].country[1]);
    }
    memset(seed, 6, sizeof seed);
    CHECK(kyc_md_issue(&is, seed, 1, &RES_DE, EXP, HOLD.pk, TOK[1]) == KYC_OK, "DE token");
    present(1, &RES_DE, &req, pr);
    CHECK(juris_kyc_admit(&g3, &req, pr, sizeof pr, NOW, 7) == KYC_OK && g3.n == 1,
          "resident DE admitted");
    juris_kyc_enable(NULL, true, true);
    juris_kyc_enable(&g, false, false);
    CHECK(!g.enabled && !g.need_receiver, "enable switches both");

    /* through the host, with a gateway signature over whatever image the
     * deployment names (here the issuer key) */
    juris_gw_t gw;
    juris_gw_init(&gw);
    juris_gw_set(&gw, GW.pk, JURIS_GW_PK_LEN);
    juris_settings_t s = settings("KE");
    static lpx_qrec_t q[8];
    juris_host_t h;
    juris_host_init(&h, &s, juris_gw_verify, &gw, q, 8);
    juris_manifest_t m;
    gw_manifest(&p, 1, ISS.pk, JURIS_GW_PK_LEN, &GW, &m);
    CHECK(juris_host_register(&h, &p, &m, ISS.pk, JURIS_GW_PK_LEN) == JURIS_OK, "registered");
    juris_kyc_enable(&g, true, false);
    t.sender = 7;
    CHECK(juris_check_transfer(&h, &t) == tag(LPX_B_TRUE), "the host proceeds");
    t.sender = 9;
    CHECK(base_of(juris_check_transfer(&h, &t)) == LPX_B_FALSE &&
              lpx_action(h.last_tag) != LPX_ACT_PROCEED,
          "the host refuses an account never admitted");
}

int main(void)
{
    keygen(&GW, 1);
    keygen(&GW2, 2);
    keygen(&CPS, 3);
    keygen(&ISS, 4);
    keygen(&HOLD, 5);
    cpk_init(&RING);
    cpk_add(&RING, CPS.pk, (1u << CP_KIND_CAPCTL) | (1u << CP_KIND_TAXTABLE), "**");
    test_gateway();
    test_manifest();
    test_capctl_register();
    test_ymd();
    test_tax();
    test_kyc();
    if (fail) printf("test_juris_cp: %d of %d checks failed\n", fail, pass + fail);
    printf("%d checks\n", pass + fail);
    return fail ? 1 : 0;
}
