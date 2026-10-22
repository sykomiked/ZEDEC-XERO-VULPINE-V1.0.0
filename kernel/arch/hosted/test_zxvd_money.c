/* Copyright (c) 2024-2026 Michael Laurence Curzi and 36N9 Genetics, LLC */
/* SPDX-License-Identifier: Apache-2.0 */
/* test_zxvd_money.c — mint.*, qnote.*, fx.*, vhub.*, capmkt.*, market.*,
 * royalty.*, cred.* and dispute.* (zxvd_money.c) behind zxvd_rpc.c, on the
 * real engines and sandbox ledgers, with TEST-ONLY Ed25519 keys and a real
 * ECVRF key: every method, the feature switches (regulated ones say so),
 * bad parameters, signature and replay refusals that change nothing, and
 * every engine's own audit ("check") after every call that changes state.
 * Key ids equal the owner ids they sign for. */
#include "test_zxvd_sandbox.h"
#include "zxvd_sandbox.h"
#include "vrf.h"

#define NOW 1000000u

static test_signer_t K1, K2, K3, K4, K5, K6, K7;
static char LAST[1 << 17];

/* every set-up group's audit holds */
static void audit(void)
{
    static const char *const S[] = {"mint.status",    "qnote.status",  "fx.status",
                                    "vhub.status",    "capmkt.status", "market.status",
                                    "royalty.status", "cred.status",   "dispute.status"};
    for (size_t i = 0; i < sizeof S / sizeof S[0]; i++) {
        rpc(S[i], "\"now\":%u", NOW);
        CHECK(has(OUT.p, "\"result\":") && !has(OUT.p, "\"check\":false"), "%s: %s", S[i], OUT.p);
    }
}

/* a call that may change state: the reply, then every audit */
static const char *call(const char *method, const char *fmt, ...)
{
    static char params[15000];
    va_list ap;
    va_start(ap, fmt);
    vsnprintf(params, sizeof params, fmt, ap);
    va_end(ap);
    rpc(method, "%s", params);
    snprintf(LAST, sizeof LAST, "%s", OUT.p ? OUT.p : "");
    audit();
    return LAST;
}

/* the status reply, copied */
static char *snap(const char *status)
{
    rpc(status, "\"now\":%u", NOW);
    const char *src = OUT.p ? OUT.p : "";
    size_t n = strlen(src) + 1;
    char *c = malloc(n);
    if (!c) {
        printf("FAIL: out of memory\n");
        exit(1);
    }
    memcpy(c, src, n);
    return c;
}

/* a refused call leaves the group's status exactly as it was */
#define UNCHANGED(status, got, want)                                                               \
    do {                                                                                           \
        char *b_ = snap(status);                                                                   \
        EXPECT(got, want);                                                                         \
        char *a_ = snap(status);                                                                   \
        CHECK(!strcmp(a_, b_), "%s changed by a refusal:\n    before %s\n    after  %s", status,   \
              b_, a_);                                                                             \
        free(a_);                                                                                  \
        free(b_);                                                                                  \
    } while (0)

static unsigned num_after(const char *got, const char *key)
{
    const char *p = strstr(got, key);
    return p ? (unsigned) strtoul(p + strlen(key), NULL, 10) : 9999u;
}

static unsigned acct_of(const char *got)
{
    return num_after(got, "\"account\":");
}

/* a 32-byte hex id made of one repeated byte */
static const char *id32(unsigned b)
{
    static char r[4][65];
    static int k;
    char *o = r[k++ & 3];
    for (int i = 0; i < 32; i++) snprintf(o + 2 * i, 3, "%02x", b & 0xFF);
    return o;
}

/* the 64 hex chars after `key` in the last reply */
static const char *hex_after(const char *got, const char *key)
{
    static char r[4][161];
    static int k;
    char *o = r[k++ & 3];
    const char *p = strstr(got, key);
    o[0] = 0;
    if (p) {
        p += strlen(key) + 1;
        size_t n = 0;
        while (p[n] && p[n] != '"' && n < 160) n++;
        memcpy(o, p, n);
        o[n] = 0;
    }
    return o;
}

static const char *msg_hex(void)
{
    return hex_after(OUT.p ? OUT.p : "", "\"message\":");
}

#define OFF(f, reg) f " is off: start zxvd with --enable " f ", or call features.set" reg

#define REG " (regulated: test or licensed use only)"

static void test_switches(void)
{
    EXPECT(rpc("mint.issue", ""), ERRR("-32012", OFF("mint", REG)));
    EXPECT(rpc("qnote.bid", ""), ERRR("-32012", OFF("qnote", REG)));
    EXPECT(rpc("fx.order", ""), ERRR("-32012", OFF("fx", REG)));
    EXPECT(rpc("vhub.swap", ""), ERRR("-32012", OFF("vhub", REG)));
    EXPECT(rpc("capmkt.bid", ""), ERRR("-32012", OFF("capmkt", REG)));
    EXPECT(rpc("market.pay", ""), ERRR("-32012", OFF("market", REG)));
    EXPECT(rpc("royalty.pay", ""), ERRR("-32012", OFF("royalty", "")));
    EXPECT(rpc("cred.fulfil", ""), ERRR("-32012", OFF("cred", "")));
    EXPECT(rpc("dispute.open", ""), ERRR("-32012", OFF("dispute", "")));
    EXPECT(rpc("mint.setup", ""), ERRR("-32012", OFF("mint", REG)));
    /* status and messages answer while off */
    EXPECT(rpc("mint.status", ""), OKR("{\"set_up\":false,\"keys\":0,\"sandbox\":true}"));
    EXPECT(rpc("qnote.status", ""), OKR("{\"set_up\":false,\"sandbox\":true}"));
    EXPECT(rpc("fx.status", ""),
           OKR("{\"set_up\":false,\"keys\":0,\"leverage\":0,\"sandbox\":true}"));
    EXPECT(rpc("vhub.status", ""), OKR("{\"set_up\":false,\"keys\":0,\"sandbox\":true}"));
    EXPECT(rpc("capmkt.status", ""), OKR("{\"set_up\":false,\"keys\":0,\"sandbox\":true}"));
    EXPECT(rpc("market.status", ""), OKR("{\"set_up\":false,\"keys\":0,\"sandbox\":true}"));
    EXPECT(rpc("royalty.status", ""), OKR("{\"set_up\":false,\"keys\":0,\"sandbox\":true}"));
    EXPECT(rpc("cred.status", ""),
           OKR("{\"set_up\":false,\"credit_lines\":false,\"sandbox\":true}"));
    EXPECT(rpc("dispute.status", ""), OKR("{\"set_up\":false,\"keys\":0,\"sandbox\":true}"));
    EXPECT(rpc("mint.message", "\"kind\":\"claim\""),
           ERRR("-32060", "mint is not set up: call mint.setup first"));
    EXPECT(rpc("capmkt.quote", "\"resource\":\"compute\",\"tenor\":\"hour\""),
           ERRR("-32060", "capmkt is not set up: call capmkt.setup first"));
    EXPECT(rpc("cred.card", "\"subject\":1"),
           ERRR("-32060", "cred is not set up: call cred.setup first"));
    const char *fg = rpc("features.get", "");
    HAS(fg, "{\"name\":\"mint\",\"enabled\":false,\"regulated\":true");
    HAS(fg, "{\"name\":\"qnote\",\"enabled\":false,\"regulated\":true");
    HAS(fg, "{\"name\":\"fx\",\"enabled\":false,\"regulated\":true");
    HAS(fg, "{\"name\":\"vhub\",\"enabled\":false,\"regulated\":true");
    HAS(fg, "{\"name\":\"capmkt\",\"enabled\":false,\"regulated\":true");
    HAS(fg, "{\"name\":\"market\",\"enabled\":false,\"regulated\":true");
    HAS(fg, "{\"name\":\"royalty\",\"enabled\":false,\"regulated\":false");
    HAS(fg, "{\"name\":\"cred\",\"enabled\":false,\"regulated\":false");
    HAS(fg, "{\"name\":\"dispute\",\"enabled\":false,\"regulated\":false");
    const char *d = rpc("rpc.discover", "");
    HAS(d, "\"mint.issue\"");
    HAS(d, "\"dispute.select\"");
    CHECK(zxvd_rpc_enable("mint,qnote,fx,vhub,capmkt,market,royalty,cred,dispute") == 0,
          "enable all");
}

/* ===================== mint ===================== */

static const char *claim(unsigned id, unsigned juris, unsigned qty, unsigned expiry)
{
    static char b[2][512];
    static int k;
    char *o = b[k++ & 1];
    snprintf(o, 512,
             "\"attestor\":1,\"id\":\"%s\",\"capacity\":\"ewhr\",\"juris\":%u,\"qty\":%u,"
             "\"expiry\":%u",
             id32(id), juris, qty, expiry);
    return o;
}

static void test_mint(void)
{
    EXPECT(rpc("mint.juris", "\"code\":840,\"cap_minor\":1"),
           ERRR("-32060", "mint is not set up: call mint.setup first"));
    EXPECT(rpc("mint.setup", "\"vbill_backing\":0"),
           ERRR("-32602", "\\\"vbill_backing\\\" must be above 0"));
    EXPECT(rpc("mint.setup", "\"vbill_backing\":-1"),
           ERRR("-32602", "\\\"vbill_backing\\\" must not be negative"));
    EXPECT(call("mint.setup", ""),
           OKR("{\"vino_issuer\":0,\"vbill_issuer\":1,\"vbill_backing\":\"100\","
               "\"exclusive\":true,\"jurisdictions\":0}"));
    EXPECT(rpc("mint.setup", ""), ERRR("-32060", "SET_UP: mint is set up already (this run)"));
    EXPECT(rpc("mint.key", "\"key\":1,\"public_key\":\"%s\"", pk_hex(&K1)),
           OKR("{\"key\":1,\"added\":true,\"keys\":1}"));
    unsigned hv = acct_of(call("mint.account_open", "\"owner\":5,\"asset\":\"vino\""));
    unsigned hb = acct_of(call("mint.account_open", "\"owner\":5,\"asset\":\"vbill\""));
    CHECK(hv == 2 && hb == 3, "holder accounts %u %u", hv, hb);

    /* M1: no jurisdiction yet: fail closed */
    char *s =
        sign_msg(&K1, "mint.message", "\"kind\":\"claim\",%s", claim(1, 840, 1000, NOW + 100));
    char issue[1024];
    snprintf(issue, sizeof issue, "%s,\"sig\":\"%s\",\"target\":\"vino\",\"holder\":%u,\"now\":%u",
             claim(1, 840, 1000, NOW + 100), s, hv, NOW);
    UNCHANGED("mint.status", rpc("mint.issue", "%s", issue),
              ERRR("-32060", "MINT_ERR_UNKNOWN: an unknown or disabled attestor, or no claim "
                             "with that id"));
    EXPECT(call("mint.attestor", "\"id\":1"), OKR("{\"id\":1,\"enabled\":true,\"haircut_bps\":0}"));
    UNCHANGED("mint.status", rpc("mint.issue", "%s", issue),
              ERRR("-32060", "MINT_ERR_JURIS: the jurisdiction is unknown or not enabled "
                             "(minting starts off)"));
    EXPECT(call("mint.juris", "\"code\":840,\"haircut_bps\":100,\"cap_minor\":5000"),
           OKR("{\"code\":840,\"enabled\":true,\"haircut_bps\":100,\"cap_minor\":\"5000\","
               "\"used_minor\":\"0\"}"));
    EXPECT(rpc("mint.juris", "\"code\":840,\"haircut_bps\":10001,\"cap_minor\":5000"),
           ERRR("-32060", "MINT_ERR_ARG: a bad argument (a zero quantity, a holder that is not "
                          "of the target's asset or is an issuer)"));
    EXPECT(rpc("mint.juris", "\"code\":840"),
           ERRR("-32602", "missing integer parameter \\\"cap_minor\\\""));
    UNCHANGED("mint.status", rpc("mint.issue", "%s", issue),
              ERRR("-32060", "MINT_ERR_PRICE: no unit value is set for that capacity"));
    EXPECT(rpc("mint.unit", "\"capacity\":\"oil\",\"num\":3,\"den\":1"),
           ERRR("-32602", "\\\"capacity\\\" must be \\\"ewhr\\\", \\\"compute\\\" or \\\"kwh\\\""));
    EXPECT(call("mint.unit", "\"capacity\":\"ewhr\",\"num\":3,\"den\":1"),
           OKR("{\"capacity\":\"ewhr\",\"num\":\"3\",\"den\":\"1\"}"));
    /* M4: a wrong signature, a tampered quantity, a truncated signature */
    char bad[1024];
    snprintf(bad, sizeof bad, "%s,\"sig\":\"%s\",\"target\":\"vino\",\"holder\":%u,\"now\":%u",
             claim(1, 840, 1001, NOW + 100), s, hv, NOW);
    UNCHANGED("mint.status", rpc("mint.issue", "%s", bad),
              ERRR("-32060", "MINT_ERR_SIG: the attestor's signature does not verify"));
    snprintf(bad, sizeof bad, "%s,\"sig\":\"%s\",\"target\":\"vino\",\"holder\":%u,\"now\":%u",
             claim(1, 840, 1000, NOW + 100), zero_sig(), hv, NOW);
    UNCHANGED("mint.status", rpc("mint.issue", "%s", bad),
              ERRR("-32060", "MINT_ERR_SIG: the attestor's signature does not verify"));
    snprintf(bad, sizeof bad, "%s,\"sig\":\"%.126s\",\"target\":\"vino\",\"holder\":%u",
             claim(1, 840, 1000, NOW + 100), s, hv);
    EXPECT(rpc("mint.issue", "%s", bad), ERRR("-32602", "\\\"sig\\\" must be 64 bytes of hex"));
    /* M5/M6: 1000 x 3 = 3000, less 1% = 2970 Vino in Fibonacci coins */
    HAS(call("mint.issue", "%s", issue),
        "\"state\":\"live\",\"attestor\":1,\"capacity\":\"ewhr\",\"juris\":840,\"qty\":\"1000\"");
    HAS(LAST, "\"gross_minor\":\"3000\",\"net_minor\":\"2970\",\"units\":\"2970\","
              "\"face_minor\":\"2970\",\"remainder_minor\":\"0\",\"coins\":{\"top\":\"0\","
              "\"rungs\":[");
    HAS(rpc("mint.account", "\"account\":%u", hv), "\"balance_minor\":\"2970\"");
    /* M9: the same claim again is a replay */
    UNCHANGED("mint.status", rpc("mint.issue", "%s", issue),
              ERRR("-32060", "MINT_ERR_DUP: a claim with that id is already held (replay)"));
    /* M4: expired; M7: the cap */
    s = sign_msg(&K1, "mint.message", "\"kind\":\"claim\",%s", claim(2, 840, 1000, NOW));
    UNCHANGED("mint.status",
              rpc("mint.issue", "%s,\"sig\":\"%s\",\"target\":\"vino\",\"holder\":%u,\"now\":%u",
                  claim(2, 840, 1000, NOW), s, hv, NOW),
              ERRR("-32060", "MINT_ERR_EXPIRED: the claim has expired"));
    s = sign_msg(&K1, "mint.message", "\"kind\":\"claim\",%s", claim(3, 840, 1000, NOW + 9));
    UNCHANGED("mint.status",
              rpc("mint.issue", "%s,\"sig\":\"%s\",\"target\":\"vino\",\"holder\":%u,\"now\":%u",
                  claim(3, 840, 1000, NOW + 9), s, hv, NOW),
              ERRR("-32060", "MINT_ERR_CAP: the jurisdiction's cap for this epoch would be "
                             "passed"));
    /* a V-Bill mint: 300 x 3 = 900, 891 net, 8 V-Bills of 100, 91 not minted */
    s = sign_msg(&K1, "mint.message", "\"kind\":\"claim\",%s", claim(4, 840, 300, NOW + 9));
    HAS(call("mint.issue", "%s,\"sig\":\"%s\",\"target\":\"vbill\",\"holder\":%u,\"now\":%u",
             claim(4, 840, 300, NOW + 9), s, hb, NOW),
        "\"target\":\"vbill\",\"holder\":3,\"gross_minor\":\"900\",\"net_minor\":\"891\","
        "\"units\":\"8\",\"face_minor\":\"800\",\"remainder_minor\":\"91\"}");
    EXPECT(rpc("mint.issue", "%s,\"sig\":\"%s\",\"target\":\"gold\",\"holder\":%u",
               claim(4, 840, 300, NOW + 9), s, hb),
           ERRR("-32602", "\\\"target\\\" must be \\\"vino\\\" or \\\"vbill\\\""));
    HAS(rpc("mint.status", "\"now\":%u", NOW),
        "\"backing_minor\":\"3770\",\"vino\":{\"issuer\":0,\"issued\":\"2970\"");
    /* M8 */
    EXPECT(call("mint.quarantine", "\"id\":\"%s\"", id32(1)),
           OKR("{\"id\":\""
               "0101010101010101010101010101010101010101010101010101010101010101"
               "\",\"face_minor\":\"2970\",\"state\":\"quarantined\",\"moved\":false}"));
    EXPECT(rpc("mint.quarantine", "\"id\":\"%s\"", id32(1)),
           ERRR("-32060", "MINT_ERR_STATE: wrong state (a cap below this epoch's use, or a claim "
                          "not live)"));
    EXPECT(rpc("mint.quarantine", "\"id\":\"%s\"", id32(9)),
           ERRR("-32060", "MINT_ERR_UNKNOWN: an unknown or disabled attestor, or no claim with "
                          "that id"));
    HAS(rpc("mint.status", "\"now\":%u", NOW), "\"backing_minor\":\"800\"");
    HAS(rpc("mint.account", "\"account\":%u", hv), "\"balance_minor\":\"2970\"");
    UNCHANGED("mint.status", rpc("mint.retire", "\"id\":\"%s\",\"from\":%u", id32(1), hb),
              ERRR("-32060", "MINT_ERR_ARG: a bad argument (a zero quantity, a holder that is not "
                             "of the target's asset or is an issuer)"));
    HAS(call("mint.retire", "\"id\":\"%s\",\"from\":%u", id32(1), hv), "\"state\":\"retired\"");
    HAS(rpc("mint.account", "\"account\":%u", hv), "\"balance_minor\":\"0\"");
    EXPECT(rpc("mint.juris", "\"code\":840,\"cap_minor\":100"),
           ERRR("-32060", "MINT_ERR_STATE: wrong state (a cap below this epoch's use, or a claim "
                          "not live)"));
    EXPECT(call("mint.epoch", ""), OKR("{\"epoch\":\"1\"}"));
    HAS(call("mint.juris", "\"code\":840,\"cap_minor\":100"), "\"used_minor\":\"0\"}");
    EXPECT(rpc("mint.message", "\"kind\":\"other\""),
           ERRR("-32602", "\\\"kind\\\" must be \\\"claim\\\""));
    HAS(rpc("mint.message", "\"kind\":\"claim\",%s", claim(1, 840, 1, 2)),
        "\"bytes\":74,\"scheme\":\"ed25519\"}");
    HAS(rpc("mint.status", "\"now\":%u", NOW), "\"check\":true,\"sandbox\":true}");
}

/* ===================== qnote ===================== */

static void test_qnote(void)
{
    EXPECT(rpc("qnote.lot_open", ""),
           ERRR("-32060", "qnote is not set up: call qnote.setup first"));
    EXPECT(call("qnote.setup", ""), OKR("{\"reserve\":1,\"escrow\":2,\"lots_max\":8}"));
    EXPECT(rpc("qnote.setup", ""), ERRR("-32060", "SET_UP: qnote is set up already (this run)"));
    unsigned b1 = acct_of(call("qnote.account_open", "\"owner\":1,\"asset\":\"fiat\""));
    unsigned b2 = acct_of(call("qnote.account_open", "\"owner\":2,\"asset\":\"fiat\""));
    unsigned sv = acct_of(call("qnote.account_open", "\"owner\":3,\"asset\":\"fiat\""));
    call("qnote.fund", "\"account\":%u,\"amount_minor\":100000", b1);
    call("qnote.fund", "\"account\":%u,\"amount_minor\":100000", b2);
    call("qnote.fund", "\"account\":%u,\"amount_minor\":100000", sv);
    /* Q1: face 1050, unit 100: 10 units, 50 dust; ladder 100 .. 60 by 10 */
    const char *lot = "\"claim\":\"%s\",\"face_minor\":1050,\"unit_face_minor\":100,"
                      "\"p_start\":100,\"p_step\":10,\"p_min\":60";
    HAS(call("qnote.lot_open", lot, id32(7)), "{\"lot\":0,\"state\":\"open\",\"asset\":\"QN0\"");
    HAS(LAST, "\"units\":\"10\",\"dust_minor\":\"50\"");
    UNCHANGED("qnote.status", rpc("qnote.lot_open", lot, id32(7)),
              ERRR("-32060", "QNOTE_ERR_DUP: that claim already has a lot"));
    UNCHANGED("qnote.status",
              rpc("qnote.lot_open",
                  "\"claim\":\"%s\",\"face_minor\":1050,\"unit_face_minor\":100,"
                  "\"p_start\":101,\"p_step\":10,\"p_min\":60",
                  id32(8)),
              ERRR("-32060", "QNOTE_ERR_ARG: a bad argument (an unusable account, a price "
                             "outside the ladder, a zero quantity)"));
    EXPECT(rpc("qnote.bid", "\"lot\":5"), ERRR("-32602", "no lot 5"));
    unsigned q1 = acct_of(call("qnote.lot_account", "\"lot\":0,\"owner\":1"));
    unsigned q2 = acct_of(call("qnote.lot_account", "\"lot\":0,\"owner\":2"));
    /* Q2: bids lock qty x limit */
    EXPECT(call("qnote.bid",
                "\"lot\":0,\"fiat_account\":%u,\"qnote_account\":%u,\"qty\":6,\"limit\":90,"
                "\"now\":%u",
                b1, q1, NOW),
           OKR("{\"lot\":0,\"bid\":0,\"locked_minor\":\"540\"}"));
    UNCHANGED("qnote.status",
              rpc("qnote.bid",
                  "\"lot\":0,\"fiat_account\":%u,\"qnote_account\":%u,\"qty\":6,\"limit\":110", b2,
                  q2),
              ERRR("-32060", "QNOTE_ERR_ARG: a bad argument (an unusable account, a price "
                             "outside the ladder, a zero quantity)"));
    HAS(call("qnote.bid",
             "\"lot\":0,\"fiat_account\":%u,\"qnote_account\":%u,\"qty\":8,\"limit\":70,"
             "\"now\":%u",
             b2, q2, NOW),
        "\"locked_minor\":\"560\"");
    /* Q3: D(90) = 6, D(80) = 6, D(70) = 14 >= 10: price 70, b1 6 in full, b2 4 */
    EXPECT(rpc("qnote.settle", "\"lot\":0"),
           ERRR("-32060", "QNOTE_ERR_STATE: wrong lot state for this step"));
    HAS(call("qnote.clear", "\"lot\":0"), "\"state\":\"cleared\"");
    HAS(LAST, "\"price\":\"70\",\"sold\":\"10\",\"unsold\":\"0\"");
    HAS(call("qnote.settle", "\"lot\":0,\"now\":%u", NOW), "\"state\":\"settled\"");
    HAS(LAST, "\"proceeds_minor\":\"700\",\"loss_minor\":\"350\"");
    HAS(rpc("qnote.account", "\"account\":%u", q1), "\"asset\":\"QN0\",\"balance_minor\":\"6\"");
    HAS(rpc("qnote.account", "\"account\":%u", b1),
        "\"asset\":\"fiat\",\"balance_minor\":\"99580\"");
    /* Q5: 1000 salvaged; holders are senior up to 10 x 100 */
    HAS(call("qnote.recover", "\"lot\":0,\"from\":%u,\"amount_minor\":1000,\"now\":%u", sv, NOW),
        "\"paid_minor\":\"1000\",\"retired\":\"10\"");
    HAS(rpc("qnote.account", "\"account\":%u", b1), "\"balance_minor\":\"100180\"");
    UNCHANGED("qnote.status",
              rpc("qnote.recover", "\"lot\":0,\"from\":%u,\"amount_minor\":0,\"now\":%u", sv, NOW),
              ERRR("-32060", "QNOTE_ERR_ARG: a bad argument (an unusable account, a price "
                             "outside the ladder, a zero quantity)"));
    HAS(call("qnote.close", "\"lot\":0,\"now\":%u", NOW), "\"state\":\"closed\"");
    EXPECT(rpc("qnote.close", "\"lot\":0,\"now\":%u", NOW),
           ERRR("-32060", "QNOTE_ERR_STATE: wrong lot state for this step"));
    /* a second lot takes the next prepared asset */
    HAS(call("qnote.lot_open", lot, id32(8)), "{\"lot\":1,\"state\":\"open\",\"asset\":\"QN1\"");
    HAS(rpc("qnote.status", ""), "\"reserve_in_minor\":\"700\"");
    HAS(rpc("qnote.status", ""), "\"check\":true,\"sandbox\":true}");
}

/* ===================== fx ===================== */

static const char *fx_terms(const char *side, unsigned b, unsigned q, unsigned qty, unsigned lim,
                            unsigned nonce)
{
    static char r[2][256];
    static int k;
    char *o = r[k++ & 1];
    snprintf(o, 256,
             "\"side\":\"%s\",\"base_account\":%u,\"quote_account\":%u,\"qty\":%u,\"limit\":%u,"
             "\"nonce\":%u",
             side, b, q, qty, lim, nonce);
    return o;
}

static void test_fx(void)
{
    EXPECT(rpc("fx.setup", "\"band_bps\":-5"), ERRR("-32602", "\\\"band_bps\\\" is out of range"));
    UNCHANGED("fx.status", rpc("fx.setup", "\"band_bps\":10000"),
              ERRR("-32060", "FX_ERR_ARG: a bad argument (an account of another asset or owner, "
                             "an escrow or fee account, a zero quantity or limit)"));
    HAS(call("fx.setup", ""),
        "\"base\":\"xta\",\"quote\":\"xtb\",\"base_escrow\":2,\"quote_escrow\":3,"
        "\"fee_account\":4,\"scale\":\"100\",\"band_bps\":500,\"max_orders\":48,"
        "\"enabled\":false,\"leverage\":0}");
    rpc("fx.key", "\"key\":21,\"public_key\":\"%s\"", pk_hex(&K2));
    rpc("fx.key", "\"key\":22,\"public_key\":\"%s\"", pk_hex(&K3));
    unsigned sb = acct_of(call("fx.account_open", "\"owner\":21,\"asset\":\"xta\""));
    unsigned sq = acct_of(call("fx.account_open", "\"owner\":21,\"asset\":\"xtb\""));
    unsigned bb = acct_of(call("fx.account_open", "\"owner\":22,\"asset\":\"xta\""));
    unsigned bq = acct_of(call("fx.account_open", "\"owner\":22,\"asset\":\"xtb\""));
    call("fx.fund", "\"account\":%u,\"amount_minor\":10000", sb);
    call("fx.fund", "\"account\":%u,\"amount_minor\":10000", bq);
    /* F1: off until fx.config */
    UNCHANGED(
        "fx.status", rpc("fx.open", ""),
        ERRR("-32060", "FX_ERR_DISABLED: the batch market is off until fx.config turns it on"));
    EXPECT(call("fx.config", "\"enabled\":true"),
           OKR("{\"enabled\":true,\"band_bps\":500,\"max_orders\":48,\"leverage\":0}"));
    EXPECT(call("fx.open", ""), OKR("{\"epoch\":\"1\",\"state\":\"open\"}"));
    EXPECT(rpc("fx.config", "\"enabled\":false"),
           ERRR("-32060", "FX_ERR_STATE: out of order for the epoch"));
    char *s =
        sign_msg(&K2, "fx.message", "\"kind\":\"order\",%s", fx_terms("sell", sb, sq, 1000, 50, 1));
    HAS(call("fx.order", "%s,\"sig\":\"%s\",\"now\":%u", fx_terms("sell", sb, sq, 1000, 50, 1), s,
             NOW),
        "{\"order\":0,\"side\":\"sell\",\"owner\":21,\"locked_minor\":\"1000\"");
    /* F4: a replayed (owner, nonce) and a forged order change nothing */
    UNCHANGED("fx.status",
              rpc("fx.order", "%s,\"sig\":\"%s\",\"now\":%u", fx_terms("sell", sb, sq, 1000, 50, 1),
                  s, NOW),
              ERRR("-32060", "FX_ERR_DUP: this owner already used that nonce in this epoch "
                             "(replay)"));
    UNCHANGED("fx.status",
              rpc("fx.order", "%s,\"sig\":\"%s\",\"now\":%u", fx_terms("sell", sb, sq, 1000, 49, 2),
                  s, NOW),
              ERRR("-32060", "FX_ERR_SIG: the owner's signature does not verify"));
    EXPECT(rpc("fx.order", "%s,\"sig\":\"%s\"", fx_terms("hold", sb, sq, 1, 1, 3), s),
           ERRR("-32602", "\\\"side\\\" must be \\\"sell\\\" or \\\"buy\\\""));
    s = sign_msg(&K3, "fx.message", "\"kind\":\"order\",%s", fx_terms("buy", bb, bq, 600, 60, 1));
    HAS(call("fx.order", "%s,\"sig\":\"%s\",\"now\":%u", fx_terms("buy", bb, bq, 600, 60, 1), s,
             NOW),
        "\"side\":\"buy\",\"owner\":22,\"locked_minor\":\"360\"");
    /* F5: V = 600 at 50 and 60, |S-D| tied, midpoint 55 tied, the lower: 50 */
    HAS(call("fx.clear", ""),
        "\"price\":\"50\",\"ref_before\":\"0\",\"supply\":\"1000\",\"demand\":\"600\","
        "\"volume\":\"600\",\"gross_minor\":\"300\",\"fee_minor\":\"0\",\"fills\":1");
    HAS(rpc("fx.net", ""), "\"balanced\":true}");
    HAS(call("fx.settle", "\"now\":%u", NOW), "\"volume\":\"600\"");
    HAS(rpc("fx.account", "\"account\":%u", sb), "\"balance_minor\":\"9400\"");
    HAS(rpc("fx.account", "\"account\":%u", sq), "\"balance_minor\":\"300\"");
    HAS(rpc("fx.account", "\"account\":%u", bb), "\"balance_minor\":\"600\"");
    HAS(rpc("fx.account", "\"account\":%u", bq), "\"balance_minor\":\"9700\"");
    /* the old epoch's signature does not carry over */
    EXPECT(call("fx.open", ""), OKR("{\"epoch\":\"2\",\"state\":\"open\"}"));
    UNCHANGED("fx.status",
              rpc("fx.order", "%s,\"sig\":\"%s\",\"now\":%u", fx_terms("buy", bb, bq, 600, 60, 1),
                  s, NOW),
              ERRR("-32060", "FX_ERR_SIG: the owner's signature does not verify"));
    s = sign_msg(&K3, "fx.message", "\"kind\":\"order\",%s", fx_terms("buy", bb, bq, 10, 60, 1));
    call("fx.order", "%s,\"sig\":\"%s\",\"now\":%u", fx_terms("buy", bb, bq, 10, 60, 1), s, NOW);
    EXPECT(call("fx.cancel", "\"now\":%u", NOW),
           OKR("{\"epoch\":\"2\",\"state\":\"settled\",\"cancelled\":true}"));
    HAS(rpc("fx.account", "\"account\":%u", bq), "\"balance_minor\":\"9700\"");
    EXPECT(rpc("fx.message", "\"kind\":\"swap\""),
           ERRR("-32602", "\\\"kind\\\" must be \\\"order\\\""));
    HAS(rpc("fx.status", ""), "\"check\":true,\"sandbox\":true}");
}

/* ===================== vhub ===================== */

static void test_vhub(void)
{
    EXPECT(rpc("vhub.setup", "\"issuer_xta\":31"),
           ERRR("-32602", "missing integer parameter \\\"issuer_xtb\\\""));
    HAS(call("vhub.setup", "\"issuer_xta\":31,\"issuer_xtb\":32"),
        "{\"currencies\":[{\"ccy\":\"xta\",\"issuer_key\":31,\"pool\":3,\"vbill_float\":4},"
        "{\"ccy\":\"xtb\",\"issuer_key\":32,\"pool\":5,\"vbill_float\":6}]");
    rpc("vhub.key", "\"key\":31,\"public_key\":\"%s\"", pk_hex(&K4));
    rpc("vhub.key", "\"key\":32,\"public_key\":\"%s\"", pk_hex(&K5));
    unsigned snd = acct_of(call("vhub.account_open", "\"owner\":41,\"asset\":\"xta\""));
    unsigned rcv = acct_of(call("vhub.account_open", "\"owner\":42,\"asset\":\"xtb\""));
    call("vhub.fund", "\"account\":%u,\"amount_minor\":10000", snd);
    HAS(call("vhub.back", "\"ccy\":\"xta\",\"leg\":\"float\",\"amount_minor\":1000"),
        "\"ccy\":\"xta\",\"leg\":\"float\",\"account\":4,\"balance_minor\":\"1000\"");
    HAS(call("vhub.back", "\"ccy\":\"xtb\",\"leg\":\"pool\",\"amount_minor\":100000"),
        "\"account\":5,\"balance_minor\":\"100000\"");
    EXPECT(rpc("vhub.back", "\"ccy\":\"xtz\",\"leg\":\"pool\",\"amount_minor\":1"),
           ERRR("-32602", "\\\"ccy\\\" must be \\\"xta\\\" or \\\"xtb\\\""));
    EXPECT(rpc("vhub.back", "\"ccy\":\"xta\",\"leg\":\"side\",\"amount_minor\":1"),
           ERRR("-32602", "\\\"leg\\\" must be \\\"float\\\" or \\\"pool\\\""));
    /* H1: pegs signed by each currency's issuer */
    char *s = sign_msg(&K4, "vhub.message",
                       "\"kind\":\"peg\",\"ccy\":\"xta\",\"num\":100,\"den\":1,\"epoch\":1");
    UNCHANGED(
        "vhub.status",
        rpc("vhub.peg", "\"ccy\":\"xtb\",\"num\":100,\"den\":1,\"epoch\":1,\"sig\":\"%s\"", s),
        ERRR("-32060", "VHUB_ERR_SIG: the issuer's signature does not verify"));
    EXPECT(call("vhub.peg", "\"ccy\":\"xta\",\"num\":100,\"den\":1,\"epoch\":1,\"sig\":\"%s\"", s),
           OKR("{\"ccy\":\"xta\",\"num\":\"100\",\"den\":\"1\",\"epoch\":\"1\"}"));
    UNCHANGED(
        "vhub.status",
        rpc("vhub.peg", "\"ccy\":\"xta\",\"num\":100,\"den\":1,\"epoch\":1,\"sig\":\"%s\"", s),
        ERRR("-32060", "VHUB_ERR_EPOCH: the peg epoch is not above the last (replay)"));
    s = sign_msg(&K5, "vhub.message",
                 "\"kind\":\"peg\",\"ccy\":\"xtb\",\"num\":50,\"den\":1,\"epoch\":1");
    call("vhub.peg", "\"ccy\":\"xtb\",\"num\":50,\"den\":1,\"epoch\":1,\"sig\":\"%s\"", s);
    /* H5: no rule yet: fails closed */
    const char *sw = "\"from\":\"xta\",\"to\":\"xtb\",\"sender\":%u,\"receiver\":%u,"
                     "\"amount_minor\":%u,\"epoch\":1,\"now\":%u";
    UNCHANGED("vhub.status", rpc("vhub.swap", sw, snd, rcv, 1000u, NOW),
              ERRR("-32060", "VHUB_WAIT: a rule has not decided, or a currency has no rule (fails "
                             "closed)"));
    HAS(call("vhub.rule", "\"ccy\":\"xta\",\"limit_minor\":5000"),
        "\"ccy\":\"xta\",\"rule\":\"limit\",\"limit_minor\":\"5000\",\"rules\":1}");
    call("vhub.rule", "\"ccy\":\"xtb\",\"limit_minor\":1000000");
    /* H2: 1000 xta = 10 V-Bills = 500 xtb, in one posting */
    EXPECT(call("vhub.swap", sw, snd, rcv, 1000u, NOW),
           OKR("{\"from\":\"xta\",\"to\":\"xtb\",\"sender\":7,\"receiver\":8,\"amount_minor\":"
               "\"1000\",\"vbills\":\"10\",\"out_minor\":\"500\"}"));
    HAS(rpc("vhub.account", "\"account\":%u", rcv), "\"balance_minor\":\"500\"");
    UNCHANGED("vhub.status", rpc("vhub.swap", sw, snd, rcv, 150u, NOW),
              ERRR("-32060", "VHUB_ERR_INEXACT: the amount does not convert exactly: see "
                             "vhub.quote"));
    EXPECT(rpc("vhub.quote", "\"from\":\"xta\",\"to\":\"xtb\",\"amount_minor\":150"),
           OKR("{\"from\":\"xta\",\"to\":\"xtb\",\"exact_minor\":\"100\"}"));
    /* H6: 1000 + 4100 > 5000 for this sender in epoch 1 */
    UNCHANGED("vhub.status", rpc("vhub.swap", sw, snd, rcv, 4100u, NOW),
              ERRR("-32060", "VHUB_REFUSED: a jurisdiction rule refused the swap"));
    /* the receiver holds xtb, not xta: the ledger refuses, nothing moves */
    UNCHANGED("vhub.status", rpc("vhub.swap", sw, rcv, snd, 100u, NOW),
              ERRR("-32060", "VHUB_ERR_LEDGER: the ledger refused (funds or backing)"));
    /* the xta pool (account 3) is the hub's own: never a party */
    UNCHANGED("vhub.status", rpc("vhub.swap", sw, 3u, rcv, 100u, NOW),
              ERRR("-32060", "VHUB_ERR_ARG: a bad argument (the same currency twice, a hub account "
                             "as a party, an amount past 2^59, a peg not set)"));
    HAS(rpc("vhub.status", ""),
        "\"pool_minor\":\"1000\",\"float_vbills\":\"990\",\"rules\":1},{\"ccy\":\"xtb\"");
    HAS(rpc("vhub.status", ""), "\"posted\":\"1\",\"quarantined\":0,\"check\":true");
}

/* ===================== capmkt ===================== */

static void test_capmkt(void)
{
    EXPECT(rpc("capmkt.setup", "\"share_num\":0"),
           ERRR("-32602", "\\\"share_num\\\" must be 1..\\\"share_den\\\""));
    EXPECT(call("capmkt.setup", ""),
           OKR("{\"share_num\":8,\"share_den\":21,\"fee\":\"0.08889%\"}"));
    EXPECT(rpc("capmkt.setup", ""), ERRR("-32060", "SET_UP: capmkt is set up already (this run)"));
    rpc("capmkt.key", "\"key\":51,\"public_key\":\"%s\"", pk_hex(&K6));
    HAS(call("capmkt.deposit", "\"account\":51,\"amount_minor\":100000"),
        "{\"account\":51,\"available\":\"100000\"");
    call("capmkt.deposit", "\"account\":52,\"amount_minor\":1");
    UNCHANGED("capmkt.status", rpc("capmkt.withdraw", "\"account\":52,\"amount_minor\":2"),
              ERRR("-32060", "CM_ERR_FUNDS: not enough deposit for the bid lock"));
    const char *mk = "\"resource\":\"compute\",\"tenor\":\"hour\",\"region\":0";
    EXPECT(call("capmkt.ask", "\"account\":52,%s,\"qty\":10,\"price\":5,\"expires\":%u", mk,
                NOW + 600),
           OKR("{\"order\":1,\"side\":\"ask\",\"qty\":\"10\",\"price\":\"5\",\"locked\":\"0\"}"));
    EXPECT(call("capmkt.bid", "\"account\":51,%s,\"qty\":10,\"limit\":7,\"expires\":%u", mk,
                NOW + 600),
           OKR("{\"order\":2,\"side\":\"bid\",\"qty\":\"10\",\"price\":\"7\",\"locked\":\"70\"}"));
    EXPECT(rpc("capmkt.bid", "\"account\":51,\"resource\":\"gold\",\"tenor\":\"hour\""),
           ERRR("-32602", "\\\"resource\\\" must be \\\"compute\\\", \\\"inference\\\", "
                          "\\\"storage\\\" or \\\"bandwidth\\\""));
    /* 52 has an ask on this market: it may not bid against itself */
    UNCHANGED(
        "capmkt.status",
        rpc("capmkt.bid", "\"account\":52,%s,\"qty\":10,\"limit\":7,\"expires\":%u", mk, NOW + 600),
        ERRR("-32060", "CM_ERR_SELF: bid and ask from the same account"));
    call("capmkt.deposit", "\"account\":53,\"amount_minor\":69");
    UNCHANGED(
        "capmkt.status",
        rpc("capmkt.bid", "\"account\":53,%s,\"qty\":10,\"limit\":7,\"expires\":%u", mk, NOW + 600),
        ERRR("-32060", "CM_ERR_FUNDS: not enough deposit for the bid lock"));
    HAS(rpc("capmkt.quote", "%s,\"now\":%u", mk, NOW),
        "\"best_bid\":\"7\",\"best_ask\":\"5\",\"bid_units\":\"10\",\"ask_units\":\"10\","
        "\"providers\":1,\"concentrated\":true}");
    /* one price for the round: the midpoint of [5, 7] */
    HAS(call("capmkt.clear", "%s,\"now\":%u", mk, NOW),
        "\"price\":\"6\",\"volume\":\"10\",\"lo\":\"5\",\"hi\":\"7\",\"providers\":1");
    HAS(LAST, "\"contracts\":[{\"contract\":");
    unsigned con = num_after(LAST, "\"contracts\":[{\"contract\":");
    HAS(LAST, "\"buyer\":51,\"provider\":52,\"qty\":\"10\",\"delivered\":\"0\",\"price\":\"6\","
              "\"escrow\":\"60\"");
    HAS(rpc("capmkt.account", "\"account\":51"), "\"available\":\"99940\",\"locked\":\"0\","
                                                 "\"escrow\":\"60\"");
    /* pay on delivery, against the buyer's signed proof */
    char *s = sign_msg(&K6, "capmkt.message",
                       "\"kind\":\"proof\",\"contract\":%u,\"units\":4,\"at\":%u", con, NOW + 10);
    UNCHANGED("capmkt.status",
              rpc("capmkt.deliver", "\"contract\":%u,\"units\":5,\"at\":%u,\"sig\":\"%s\"", con,
                  NOW + 10, s),
              ERRR("-32060", "CM_ERR_PROOF: the proof chain or the buyer's signature is refused"));
    HAS(call("capmkt.deliver", "\"contract\":%u,\"units\":4,\"at\":%u,\"sig\":\"%s\"", con,
             NOW + 10, s),
        "\"delivered\":\"4\",\"price\":\"6\",\"escrow\":\"36\",\"paid\":\"24\",\"fee\":\"0\"");
    /* the same proof again is a replay: the chain has moved on */
    UNCHANGED("capmkt.status",
              rpc("capmkt.deliver", "\"contract\":%u,\"units\":4,\"at\":%u,\"sig\":\"%s\"", con,
                  NOW + 10, s),
              ERRR("-32060", "CM_ERR_PROOF: the proof chain or the buyer's signature is refused"));
    s = sign_msg(&K6, "capmkt.message", "\"kind\":\"proof\",\"contract\":%u,\"units\":1,\"at\":%u",
                 con, NOW + 7200);
    UNCHANGED("capmkt.status",
              rpc("capmkt.deliver", "\"contract\":%u,\"units\":1,\"at\":%u,\"sig\":\"%s\"", con,
                  NOW + 7200, s),
              ERRR("-32060", "CM_ERR_LATE: delivered after the window: not paid, no penalty"));
    EXPECT(rpc("capmkt.deliver", "\"contract\":99,\"units\":1,\"at\":1,\"sig\":\"%s\"", s),
           ERRR("-32060", "CM_ERR_UNKNOWN: no such account, order or contract"));
    EXPECT(call("capmkt.expire", "\"now\":%u", NOW + 7200), OKR("{\"closed\":1}"));
    HAS(rpc("capmkt.account", "\"account\":51"), "\"available\":\"99976\",\"locked\":\"0\","
                                                 "\"escrow\":\"0\"");
    HAS(rpc("capmkt.account", "\"account\":52"), "\"available\":\"25\"");
    EXPECT(call("capmkt.withdraw", "\"account\":52,\"amount_minor\":25"),
           OKR("{\"account\":52,\"available\":\"0\",\"locked\":\"0\",\"escrow\":\"0\",\"earned\":"
               "\"24\",\"delivered\":\"4\",\"missed\":\"6\",\"sandbox\":true}"));
    unsigned o =
        num_after(call("capmkt.bid", "\"account\":51,%s,\"qty\":1,\"limit\":3,\"expires\":%u", mk,
                       NOW + 9000),
                  "\"order\":");
    UNCHANGED("capmkt.status", rpc("capmkt.cancel", "\"account\":52,\"order\":%u", o),
              ERRR("-32060", "CM_ERR_ARG: a bad argument (a zero quantity or price, a delivery "
                             "outside the window, an order the account does not own)"));
    UNCHANGED("capmkt.status", rpc("capmkt.cancel", "\"account\":51,\"order\":99"),
              ERRR("-32060", "CM_ERR_UNKNOWN: no such account, order or contract"));
    EXPECT(call("capmkt.cancel", "\"account\":51,\"order\":%u", o),
           OKR("{\"order\":3,\"cancelled\":true}"));
    HAS(rpc("capmkt.status", ""), "\"check\":true,\"sandbox\":true}");
}

/* ===================== market ===================== */

#define CID "bafybeigdyrzt5sfp7udm7hu76uh7y26nf3efuylqabf3oclgtqy55fbzdi"

static void test_market(void)
{
    HAS(call("market.setup", "\"now\":%u", NOW),
        "{\"currency\":\"VFV\",\"asset\":\"vino\",\"escrow\":1,\"commons\":2,\"tax\":3");
    rpc("market.key", "\"key\":61,\"public_key\":\"%s\"", pk_hex(&K1));
    rpc("market.key", "\"key\":62,\"public_key\":\"%s\"", pk_hex(&K2));
    rpc("market.key", "\"key\":63,\"public_key\":\"%s\"", pk_hex(&K3));
    unsigned sa = acct_of(call("market.account_open", "\"owner\":61,\"asset\":\"vino\""));
    unsigned ba = acct_of(call("market.account_open", "\"owner\":62,\"asset\":\"vino\""));
    call("market.fund", "\"account\":%u,\"amount_minor\":3000", ba);
    const char *store = "\"owner\":61,\"name\":\"Shop\",\"region\":\"XA\",\"version\":1,"
                        "\"arbiter\":63";
    char *s = sign_msg(&K1, "market.message", "\"kind\":\"store\",%s", store);
    EXPECT(
        rpc("market.store", "%s,\"sig\":\"%s\"", store, zero_sig()),
        ERRR("-32060",
             "MK_ERR_AUTH: the wrong party, a signature that does not verify, or a delivered CID "
             "that is not the listed one"));
    EXPECT(call("market.store", "%s,\"sig\":\"%s\"", store, s), OKR("{\"store\":1,\"version\":1}"));
    /* a replayed or rolled-back update */
    UNCHANGED("market.status", rpc("market.store", "%s,\"handle\":1,\"sig\":\"%s\"", store, s),
              ERRR("-32060", "MK_ERR_DUPLICATE: a version not above the last (replay or "
                             "rollback)"));
    EXPECT(rpc("market.store", "\"owner\":0,\"name\":\"x\",\"region\":\"XA\",\"version\":1"),
           ERRR("-32602", "\\\"owner\\\" must not be 0"));
    const char *lst = "\"store\":1,\"type\":\"digital\",\"title\":\"song\",\"price_minor\":1000,"
                      "\"stock\":10,\"cid\":\"" CID "\",\"version\":1";
    s = sign_msg(&K1, "market.message", "\"kind\":\"listing\",%s", lst);
    EXPECT(
        rpc("market.listing", "%s,\"sig\":\"%s\"", lst, sign_last(&K2)),
        ERRR("-32060",
             "MK_ERR_AUTH: the wrong party, a signature that does not verify, or a delivered CID "
             "that is not the listed one"));
    EXPECT(call("market.listing", "%s,\"sig\":\"%s\"", lst, s),
           OKR("{\"listing\":1,\"version\":1,\"available\":\"unlimited\"}"));
    EXPECT(rpc("market.listing", "\"store\":1,\"type\":\"rental\""),
           ERRR("-32602",
                "\\\"type\\\" must be \\\"physical\\\", \\\"digital\\\" or \\\"service\\\""));
    /* an order, paid into escrow, fulfilled by the CID, confirmed */
    HAS(call("market.order", "\"buyer\":62,\"listing\":1,\"qty\":2,\"region\":\"XA\""),
        "{\"order\":1,\"state\":\"pending-payment\",\"store\":1,\"buyer\":62,\"seller\":61,"
        "\"total_minor\":\"2000\"");
    UNCHANGED(
        "market.status", rpc("market.pay", "\"order\":1,\"buyer\":61"),
        ERRR("-32060",
             "MK_ERR_AUTH: the wrong party, a signature that does not verify, or a delivered CID "
             "that is not the listed one"));
    HAS(call("market.pay", "\"order\":1,\"buyer\":62"), "\"state\":\"paid\"");
    HAS(LAST, "\"held_minor\":\"2000\"");
    UNCHANGED("market.status", rpc("market.pay", "\"order\":1,\"buyer\":62"),
              ERRR("-32060", "MK_ERR_STATE: not allowed in the order's state"));
    HAS(rpc("market.account", "\"account\":%u", ba), "\"balance_minor\":\"1000\"");
    UNCHANGED("market.status", rpc("market.fulfil", "\"order\":1,\"seller\":61,\"proof\":\"bafy\""),
              ERRR("-32060", "MK_ERR_AUTH: the wrong party, a signature that does not verify, or a "
                             "delivered CID that is not the listed one"));
    HAS(call("market.fulfil", "\"order\":1,\"seller\":61,\"proof\":\"" CID "\""),
        "\"state\":\"fulfilled\"");
    HAS(call("market.confirm", "\"order\":1,\"buyer\":62"), "\"state\":\"completed\"");
    HAS(LAST, "\"held_minor\":\"0\",\"to_seller_minor\":\"1999\",\"to_commons_minor\":\"1\"");
    HAS(rpc("market.account", "\"account\":%u", sa), "\"balance_minor\":\"1999\"");
    /* a second order the buyer cannot pay: the settlement is refused whole */
    HAS(call("market.order", "\"buyer\":62,\"listing\":1,\"qty\":2,\"region\":\"XA\""),
        "{\"order\":2,\"state\":\"pending-payment\"");
    /* the engine counts the refused settlement ("refused"); nothing else moves */
    EXPECT(rpc("market.pay", "\"order\":2,\"buyer\":62"),
           ERRR("-32060", "MK_ERR_SETTLE: the ledger refused the settlement; nothing changed"));
    audit();
    HAS(rpc("market.status", ""), "\"escrow_minor\":\"0\",\"commons_minor\":\"1\","
                                  "\"postings\":\"2\",\"refused\":\"1\"");
    HAS(rpc("market.account", "\"account\":%u", ba), "\"balance_minor\":\"1000\"");
    HAS(rpc("market.status", ""), "{\"order\":2,\"state\":\"pending-payment\"");
    HAS(call("market.cancel", "\"order\":2,\"actor\":62"), "\"state\":\"cancelled\"");
    /* a dispute ruled by the store's arbiter */
    HAS(call("market.order", "\"buyer\":62,\"listing\":1,\"qty\":1,\"region\":\"XA\""),
        "{\"order\":3,");
    call("market.pay", "\"order\":3,\"buyer\":62");
    HAS(call("market.dispute", "\"order\":3,\"actor\":62"), "\"state\":\"disputed\"");
    s = sign_msg(&K3, "market.message", "\"kind\":\"ruling\",\"order\":3,\"buyer_award\":400");
    UNCHANGED(
        "market.status",
        rpc("market.rule", "\"order\":3,\"mediator\":63,\"buyer_award\":401,\"sig\":\"%s\"", s),
        ERRR("-32060",
             "MK_ERR_AUTH: the wrong party, a signature that does not verify, or a delivered CID "
             "that is not the listed one"));
    HAS(call("market.rule", "\"order\":3,\"mediator\":63,\"buyer_award\":400,\"sig\":\"%s\"", s),
        "\"state\":\"resolved\"");
    HAS(LAST, "\"held_minor\":\"0\",\"to_seller_minor\":\"600\",\"to_commons_minor\":\"0\","
              "\"to_buyer_minor\":\"400\"");
    UNCHANGED(
        "market.status",
        rpc("market.rule", "\"order\":3,\"mediator\":63,\"buyer_award\":400,\"sig\":\"%s\"", s),
        ERRR("-32060", "MK_ERR_STATE: not allowed in the order's state"));
    EXPECT(rpc("market.refund", "\"order\":1,\"seller\":61,\"line\":8,\"qty\":1"),
           ERRR("-32602", "\\\"line\\\" is out of range"));
    HAS(call("market.refund", "\"order\":1,\"seller\":61,\"line\":0,\"qty\":1"),
        "\"to_buyer_minor\":\"1000\"");
    EXPECT(rpc("market.tick", "\"now\":5"), ERRR("-32602", "\\\"now\\\" must not go back"));
    EXPECT(call("market.tick", "\"now\":%u", NOW + 1), OKR("{\"now\":\"1000001\"}"));
    EXPECT(rpc("market.message", "\"kind\":\"ruling\",\"order\":77,\"buyer_award\":1"),
           ERRR("-32060", "MK_ERR_NOT_FOUND: no such store, listing or order"));
    HAS(rpc("market.status", ""), "\"stores\":1,\"listings\":1,");
    HAS(rpc("market.status", ""), "\"check\":true,\"sandbox\":true}");
}

/* ===================== royalty ===================== */

/* sign_last reads at most 256 message bytes; a manifest is ROY_MANIFEST_MSG
   (265): sign the whole "message" of the last reply */
static char *sign_long(const test_signer_t *k)
{
    static char b[4][129];
    static int r;
    char *o = b[r++ & 3];
    uint8_t m[1024], sig[64];
    size_t n = 0;
    const char *p = OUT.p ? strstr(OUT.p, "\"message\":\"") : NULL;
    if (p)
        for (p += 11; *p != '"' && n < sizeof m; p += 2) {
            unsigned v;
            if (sscanf(p, "%2x", &v) != 1) break;
            m[n++] = (uint8_t) v;
        }
    CHECK(n > 256, "sign_long: a %u-byte message", (unsigned) n);
    test_signer_sign(k, m, (uint32_t) n, sig);
    hex_of(sig, 64, o);
    return o;
}

static void test_royalty(void)
{
    HAS(call("royalty.setup", ""), "{\"fee_account\":1,\"asset\":\"vino\",\"advances\":false}");
    rpc("royalty.key", "\"key\":71,\"public_key\":\"%s\"", pk_hex(&K4));
    rpc("royalty.key", "\"key\":72,\"public_key\":\"%s\"", pk_hex(&K5));
    unsigned a = acct_of(call("royalty.account_open", "\"owner\":71,\"asset\":\"vino\""));
    unsigned b = acct_of(call("royalty.account_open", "\"owner\":72,\"asset\":\"vino\""));
    unsigned payer = acct_of(call("royalty.account_open", "\"owner\":73,\"asset\":\"vino\""));
    unsigned buyer = acct_of(call("royalty.account_open", "\"owner\":74,\"asset\":\"vino\""));
    unsigned give = acct_of(call("royalty.account_open", "\"owner\":75,\"asset\":\"vino\""));
    call("royalty.fund", "\"account\":%u,\"amount_minor\":100000", buyer);
    char man[512];
    snprintf(man, sizeof man,
             "\"work\":\"%s\",\"version\":1,\"owners\":[{\"id\":71,\"account\":%u,\"weight\":1},"
             "{\"id\":72,\"account\":%u,\"weight\":1}],\"terms\":\"percent\",\"num\":10,"
             "\"den\":100",
             id32(0xA1), a, b);
    rpc("royalty.message", "\"kind\":\"manifest\",%s", man);
    char *s = sign_long(&K4);
    char *s5 = sign_long(&K5); /* 72 is not the first owner */
    UNCHANGED("royalty.status", rpc("royalty.register", "%s,\"sig\":\"%s\"", man, s5),
              ERRR("-32060", "ROY_ERR_SIG: a signature does not verify"));
    HAS(call("royalty.register", "%s,\"sig\":\"%s\"", man, s), "\"version\":\"1\",\"depth\":0");
    UNCHANGED("royalty.status", rpc("royalty.register", "%s,\"sig\":\"%s\"", man, s),
              ERRR("-32060", "ROY_ERR_DUP: the work is registered already, or the receipt is "
                             "used (replay)"));
    EXPECT(rpc("royalty.register", "\"work\":\"%s\",\"version\":1,\"owners\":[]", id32(0xA2)),
           ERRR("-32602", "\\\"owners\\\" must hold 1..8 objects"));
    EXPECT(rpc("royalty.register",
               "\"work\":\"%s\",\"version\":1,\"owners\":[{\"id\":1,\"account\":1,\"weight\":1}],"
               "\"terms\":\"loan\"",
               id32(0xA2)),
           ERRR("-32602", "\\\"terms\\\" must be \\\"percent\\\", \\\"flat\\\" or \\\"zxv_hw\\\""));
    /* R5: a realised sale, then the royalty from it */
    const char *sale =
        call("royalty.sale", "\"from\":%u,\"to\":%u,\"amount_minor\":10000,\"now\":%u", buyer,
             payer, NOW);
    char rcpt[65];
    snprintf(rcpt, sizeof rcpt, "%s", hex_after(sale, "\"receipt\":"));
    rpc("royalty.quote", "\"work\":\"%s\",\"gross_minor\":10000", id32(0xA1));
    HAS(OUT.p, "{\"gross_minor\":\"10000\",\"royalty_minor\":\"1000\",\"fee_minor\":\"0\"");
    HAS(OUT.p, "\"shares\":[{\"level\":0,\"owner\":71,\"account\":2,\"gross_minor\":\"500\"");
    char dg[65];
    snprintf(dg, sizeof dg, "%s", hex_after(OUT.p, "\"digest\":"));
    UNCHANGED("royalty.status",
              rpc("royalty.pay",
                  "\"work\":\"%s\",\"payer\":%u,\"gross_minor\":10000,\"receipt\":\"%s\","
                  "\"digest\":\"%s\"",
                  id32(0xA1), payer, rcpt, id32(0)),
              ERRR("-32060", "ROY_ERR_STALE_QUOTE: the quote changed: ask royalty.quote again"));
    UNCHANGED("royalty.status",
              rpc("royalty.pay",
                  "\"work\":\"%s\",\"payer\":%u,\"gross_minor\":10000,\"receipt\":\"%s\","
                  "\"digest\":\"%s\"",
                  id32(0xA1), payer, id32(0x55), dg),
              ERRR("-32060", "ROY_ERR_UNREALISED: no realised sale to the payer for this receipt"));
    HAS(call("royalty.pay",
             "\"work\":\"%s\",\"payer\":%u,\"gross_minor\":10000,\"receipt\":\"%s\","
             "\"digest\":\"%s\",\"now\":%u",
             id32(0xA1), payer, rcpt, dg, NOW),
        "{\"posted\":true,\"quote\":{\"gross_minor\":\"10000\",\"royalty_minor\":\"1000\"");
    HAS(rpc("royalty.account", "\"account\":%u", a), "\"balance_minor\":\"500\"");
    UNCHANGED("royalty.status",
              rpc("royalty.pay",
                  "\"work\":\"%s\",\"payer\":%u,\"gross_minor\":10000,\"receipt\":\"%s\","
                  "\"digest\":\"%s\"",
                  id32(0xA1), payer, rcpt, dg),
              ERRR("-32060", "ROY_ERR_DUP: the work is registered already, or the receipt is used "
                             "(replay)"));
    /* R8: owner 71 gives 10% of its share, opt-in, signed */
    char split[256];
    snprintf(split, sizeof split,
             "\"work\":\"%s\",\"owner\":71,\"giving_account\":%u,\"giving_bps\":1000,\"seq\":1",
             id32(0xA1), give);
    s = sign_msg(&K4, "royalty.message", "\"kind\":\"split\",%s", split);
    HAS(call("royalty.split", "%s,\"sig\":\"%s\"", split, s),
        "{\"id\":71,\"account\":2,\"weight\":\"1\",\"giving_bps\":1000");
    UNCHANGED("royalty.status", rpc("royalty.split", "%s,\"sig\":\"%s\"", split, s),
              ERRR("-32060", "ROY_ERR_VERSION: the version or sequence is not above the last "
                             "(replay)"));
    EXPECT(rpc("royalty.split", "\"work\":\"%s\",\"owner\":71,\"giving_bps\":10001,\"seq\":2",
               id32(0xA1)),
           ERRR("-32602", "\\\"giving_bps\\\" and \\\"invest_bps\\\" must be at most 10000"));
    /* R4: a new version needs every current owner */
    snprintf(man, sizeof man,
             "\"work\":\"%s\",\"version\":2,\"owners\":[{\"id\":71,\"account\":%u,\"weight\":3},"
             "{\"id\":72,\"account\":%u,\"weight\":1}],\"terms\":\"flat\",\"flat_minor\":100",
             id32(0xA1), a, b);
    rpc("royalty.message", "\"kind\":\"manifest\",%s", man);
    char s1c[129];
    snprintf(s1c, sizeof s1c, "%s", sign_long(&K4));
    char *s2 = sign_long(&K5);
    UNCHANGED("royalty.status", rpc("royalty.update", "%s,\"sigs\":[{\"sig\":\"%s\"}]", man, s1c),
              ERRR("-32060", "ROY_ERR_ARG: a bad argument (bad terms, owners or accounts, or not "
                             "one signature per current owner)"));
    UNCHANGED(
        "royalty.status",
        rpc("royalty.update", "%s,\"sigs\":[{\"sig\":\"%s\"},{\"sig\":\"%s\"}]", man, s1c, s1c),
        ERRR("-32060", "ROY_ERR_SIG: a signature does not verify"));
    HAS(call("royalty.update", "%s,\"sigs\":[{\"sig\":\"%s\"},{\"sig\":\"%s\"}]", man, s1c, s2),
        "\"version\":\"2\"");
    EXPECT(rpc("royalty.message", "\"kind\":\"lease\""),
           ERRR("-32602", "\\\"kind\\\" must be \\\"manifest\\\" or \\\"split\\\""));
    HAS(rpc("royalty.status", ""), "\"receipts_used\":2,");
    HAS(rpc("royalty.status", ""), "\"check\":true,\"sandbox\":true}");
}

/* ===================== cred ===================== */

static void test_cred(void)
{
    EXPECT(rpc("cred.setup", "\"decay_den\":0"), ERRR("-32060", "CRED_ERR_ARG: a bad argument"));
    EXPECT(rpc("cred.setup", "\"fail_base\":0"), ERRR("-32060", "CRED_ERR_ARG: a bad argument"));
    EXPECT(rpc("cred.setup", "\"fail_cap\":21"), ERRR("-32060", "CRED_ERR_ARG: a bad argument"));
    /* C3 with base 1: the first failure costs 1 */
    HAS(call("cred.setup", "\"fail_base\":1"),
        "\"fail_base\":\"1\",\"fail_cap\":10,\"credit_lines\":false}");
    EXPECT(rpc("cred.setup", ""), ERRR("-32060", "SET_UP: cred is set up already (this run)"));
    EXPECT(call("cred.subject", "\"id\":1,\"pretrusted\":true"),
           OKR("{\"id\":1,\"pretrusted\":true,\"subjects\":1}"));
    call("cred.subject", "\"id\":2");
    call("cred.subject", "\"id\":3");
    UNCHANGED("cred.status", rpc("cred.subject", "\"id\":2"),
              ERRR("-32060", "CRED_ERR_DUP: that receipt or ruling was seen already (replay), or "
                             "the subject exists"));
    /* C2: floor(log2(1001)) + 1 = 10 */
    EXPECT(call("cred.fulfil", "\"rater\":1,\"ratee\":2,\"value_minor\":1000,\"receipt\":\"%s\"",
                id32(0x11)),
           OKR("{\"rater\":1,\"ratee\":2,\"gain\":\"10\",\"local\":\"10\",\"computed\":false}"));
    UNCHANGED("cred.status",
              rpc("cred.fulfil", "\"rater\":1,\"ratee\":2,\"value_minor\":1000,\"receipt\":\"%s\"",
                  id32(0x11)),
              ERRR("-32060", "CRED_ERR_DUP: that receipt or ruling was seen already (replay), or "
                             "the subject exists"));
    UNCHANGED("cred.status",
              rpc("cred.fulfil", "\"rater\":2,\"ratee\":2,\"value_minor\":1,\"receipt\":\"%s\"",
                  id32(0x12)),
              ERRR("-32060", "CRED_ERR_SELF: a subject cannot rate itself"));
    UNCHANGED("cred.status",
              rpc("cred.fulfil", "\"rater\":1,\"ratee\":9,\"value_minor\":1,\"receipt\":\"%s\"",
                  id32(0x12)),
              ERRR("-32060", "CRED_ERR_UNKNOWN: no such subject"));
    call("cred.fulfil", "\"rater\":2,\"ratee\":3,\"value_minor\":5,\"receipt\":\"%s\"", id32(0x13));
    EXPECT(rpc("cred.card", "\"subject\":2"),
           ERRR("-32060", "CRED_ERR_STATE: trust is not computed for this subject set: call "
                          "cred.compute"));
    HAS(call("cred.compute", ""), "\"computed\":true}");
    HAS(rpc("cred.card", "\"subject\":2,\"bond\":0,\"uptime_bps\":9000"), "{\"card\":{\"tier\":");
    HAS(OUT.p, "\"sandbox input\"");
    HAS(OUT.p, "\"credit_line\":\"0\"},\"sandbox\":true}");
    EXPECT(call("cred.fail", "\"rater\":1,\"ratee\":2,\"ruling\":\"%s\"", id32(0x21)),
           OKR("{\"rater\":1,\"ratee\":2,\"local\":\"9\",\"computed\":false}"));
    UNCHANGED("cred.status",
              rpc("cred.fail", "\"rater\":1,\"ratee\":2,\"ruling\":\"%s\"", id32(0x21)),
              ERRR("-32060", "CRED_ERR_DUP: that receipt or ruling was seen already (replay), or "
                             "the subject exists"));
    EXPECT(call("cred.decay", ""), OKR("{\"epoch\":\"1\"}"));
    HAS(rpc("cred.status", ""), "{\"id\":2,\"pretrusted\":false,\"fails\":0");
    HAS(rpc("cred.status", ""), "\"check\":true,\"sandbox\":true}");
}

/* ===================== dispute ===================== */

static uint8_t VSK[VRF_SK_LEN], VPK[VRF_PK_LEN];

static void hex_in(const char *h, uint8_t *out, size_t n)
{
    for (size_t i = 0; i < n; i++) {
        unsigned v = 0;
        sscanf(h + 2 * i, "%2x", &v);
        out[i] = (uint8_t) v;
    }
}

/* the VRF holder's proof and output for the case's seed */
static void vrf_for(unsigned id, char pi_hex[161], char out_hex[65])
{
    uint8_t seed[32], pi[VRF_PROOF_LEN], out[32];
    rpc("dispute.message", "\"kind\":\"seed\",\"case\":%u", id);
    hex_in(msg_hex(), seed, 32);
    CHECK(vrf_dsp_out(VSK, seed, pi, out), "vrf prove");
    hex_of(pi, sizeof pi, pi_hex);
    hex_of(out, sizeof out, out_hex);
}

static void test_dispute(void)
{
    uint8_t vseed[32];
    char vpk[65];
    memset(vseed, 0x5A, sizeof vseed);
    vrf_keypair(vseed, VPK, VSK);
    hex_of(VPK, sizeof VPK, vpk);
    EXPECT(rpc("dispute.setup", "\"gateway\":99,\"vrf_public_key\":\"%s\"", id32(0xFF)),
           ERRR("-32602", "\\\"vrf_public_key\\\" is not a curve point"));
    EXPECT(rpc("dispute.setup", "\"gateway\":99,\"vrf_public_key\":\"%s\",\"jury\":[1,1]", vpk),
           ERRR("-32602", "\\\"jury\\\" must hold 3 sizes"));
    UNCHANGED(
        "dispute.status",
        rpc("dispute.setup", "\"gateway\":99,\"vrf_public_key\":\"%s\",\"jury\":[2,2,2]", vpk),
        ERRR("-32060", "DSP_ERR_ARG: a bad argument (an unusable account, the same party "
                       "twice)"));
    HAS(call("dispute.setup",
             "\"gateway\":99,\"vrf_public_key\":\"%s\",\"base_bond\":100,\"juror_bond\":50,"
             "\"commit_ticks\":10,\"reveal_ticks\":10,\"appeal_ticks\":10,\"jury\":[1,1,1]",
             vpk),
        "{\"gateway\":99,\"escrow\":1,\"reserve\":2,\"base_bond_minor\":\"100\","
        "\"juror_bond_minor\":\"50\",\"jury\":[1,1,1]");
    rpc("dispute.key", "\"key\":81,\"public_key\":\"%s\"", pk_hex(&K1));
    rpc("dispute.key", "\"key\":82,\"public_key\":\"%s\"", pk_hex(&K2));
    rpc("dispute.key", "\"key\":91,\"public_key\":\"%s\"", pk_hex(&K3));
    rpc("dispute.key", "\"key\":99,\"public_key\":\"%s\"", pk_hex(&K7));
    unsigned ba = acct_of(call("dispute.account_open", "\"owner\":81,\"asset\":\"vino\""));
    unsigned sa = acct_of(call("dispute.account_open", "\"owner\":82,\"asset\":\"vino\""));
    unsigned ja = acct_of(call("dispute.account_open", "\"owner\":91,\"asset\":\"vino\""));
    call("dispute.fund", "\"account\":%u,\"amount_minor\":10000", ba);
    call("dispute.fund", "\"account\":%u,\"amount_minor\":10000", sa);
    call("dispute.fund", "\"account\":%u,\"amount_minor\":1000", ja);
    EXPECT(call("dispute.juror", "\"id\":91,\"account\":%u,\"stake\":100", ja),
           OKR("{\"id\":91,\"removed\":false,\"pool\":1}"));
    UNCHANGED("dispute.status", rpc("dispute.juror", "\"id\":91,\"account\":%u,\"stake\":100", ja),
              ERRR("-32060", "DSP_ERR_DUP: already done (replay), the order is open already, or "
                             "the juror exists"));
    char open[512];
    snprintf(open, sizeof open,
             "\"order\":\"%s\",\"terms\":\"%s\",\"buyer\":81,\"seller\":82,\"buyer_account\":%u,"
             "\"seller_account\":%u,\"escrow_minor\":5000,\"now\":100",
             id32(0x0D), id32(0x0E), ba, sa);
    HAS(call("dispute.open", "%s", open), "{\"case\":1,\"state\":\"proofs\",\"level\":1");
    UNCHANGED("dispute.status", rpc("dispute.open", "%s", open),
              ERRR("-32060", "DSP_ERR_DUP: already done (replay), the order is open already, or "
                             "the juror exists"));
    /* D2: a signed proof, undecided without a predicate */
    char *s = sign_msg(&K1, "dispute.message",
                       "\"kind\":\"proof\",\"case\":1,\"party\":\"buyer\",\"proof_kind\":1,"
                       "\"hash\":\"%s\"",
                       id32(0x33));
    UNCHANGED("dispute.status",
              rpc("dispute.proof",
                  "\"case\":1,\"party\":\"seller\",\"proof_kind\":1,\"hash\":\"%s\",\"sig\":\"%s\"",
                  id32(0x33), s),
              ERRR("-32060", "DSP_ERR_SIG: the signature does not verify"));
    HAS(call("dispute.proof",
             "\"case\":1,\"party\":\"buyer\",\"proof_kind\":1,\"hash\":\"%s\",\"sig\":\"%s\"",
             id32(0x33), s),
        "{\"result\":\"pending\",\"case\":{\"case\":1,\"state\":\"proofs\"");
    EXPECT(rpc("dispute.proof", "\"case\":7"),
           ERRR("-32060", "DSP_ERR_UNKNOWN: no such case or juror"));
    UNCHANGED("dispute.status",
              rpc("dispute.gateway", "\"case\":1,\"award_bps\":1,\"sig\":\"%s\"", zero_sig()),
              ERRR("-32060", "DSP_ERR_STATE: out of order for the case"));
    /* D3: the buyer locks the 1x appeal bond */
    HAS(call("dispute.escalate", "\"case\":1,\"party\":\"buyer\",\"now\":100"),
        "\"state\":\"select\",\"level\":2,\"round\":1");
    HAS(LAST, "\"appeal_bond_minor\":\"100\",\"locked_minor\":\"100\"");
    /* D4: the jury drawn by ECVRF; a proof for another seed is refused */
    char pi[161], out[65];
    vrf_for(1, pi, out);
    char badpi[161];
    snprintf(badpi, sizeof badpi, "%s", pi);
    badpi[100] = badpi[100] == '0' ? '1' : '0';
    UNCHANGED(
        "dispute.status",
        rpc("dispute.select", "\"case\":1,\"proof\":\"%s\",\"out\":\"%s\",\"now\":100", badpi, out),
        ERRR("-32060", "DSP_ERR_VRF: the VRF proof does not verify for this case's seed"));
    EXPECT(rpc("dispute.select", "\"case\":1,\"proof\":\"00\",\"out\":\"%s\"", out),
           ERRR("-32602", "\\\"proof\\\" must be 80 bytes of hex"));
    HAS(call("dispute.select", "\"case\":1,\"proof\":\"%s\",\"out\":\"%s\",\"now\":100", pi, out),
        "\"state\":\"vote\",\"level\":2,\"round\":1,\"order\":");
    HAS(LAST, "\"jury\":[91],\"commit_deadline\":\"110\"");
    /* D5: commit, a replayed commit, reveal */
    rpc("dispute.message",
        "\"kind\":\"vote\",\"case\":1,\"juror\":91,\"vote\":\"buyer\",\"salt\":\"%s\"", id32(0x44));
    char cm[65];
    snprintf(cm, sizeof cm, "%s", msg_hex());
    s = sign_msg(&K3, "dispute.message",
                 "\"kind\":\"commit\",\"case\":1,\"juror\":91,\"commit\":\"%s\"", cm);
    HAS(call("dispute.commit",
             "\"case\":1,\"juror\":91,\"commit\":\"%s\",\"sig\":\"%s\",\"now\":101", cm, s),
        "{\"result\":\"ok\"");
    UNCHANGED("dispute.status",
              rpc("dispute.commit",
                  "\"case\":1,\"juror\":91,\"commit\":\"%s\",\"sig\":\"%s\",\"now\":101", cm, s),
              ERRR("-32060", "DSP_ERR_DUP: already done (replay), the order is open already, or "
                             "the juror exists"));
    UNCHANGED("dispute.status",
              rpc("dispute.reveal",
                  "\"case\":1,\"juror\":91,\"vote\":\"seller\",\"salt\":\"%s\",\"now\":102",
                  id32(0x44)),
              ERRR("-32060", "DSP_ERR_COMMIT: the reveal does not match the commitment"));
    HAS(call("dispute.reveal",
             "\"case\":1,\"juror\":91,\"vote\":\"buyer\",\"salt\":\"%s\",\"now\":102", id32(0x44)),
        "{\"result\":\"ok\"");
    /* D6: the buyer wins the round; its appeal bond comes back */
    HAS(call("dispute.tally", "\"case\":1,\"now\":103"), "\"state\":\"appeal\"");
    HAS(rpc("dispute.account", "\"account\":%u", ba), "\"balance_minor\":\"10000\"");
    UNCHANGED("dispute.status", rpc("dispute.finalize", "\"case\":1,\"now\":104"),
              ERRR("-32060", "DSP_ERR_DEADLINE: too early or too late"));
    HAS(call("dispute.finalize", "\"case\":1,\"now\":200"), "\"state\":\"final\"");
    HAS(rpc("dispute.ruling", "\"case\":1"),
        "{\"case\":1,\"award_bps\":10000,\"level\":2,\"round\":1,\"loser\":\"seller\","
        "\"loser_id\":82,\"buyer_minor\":\"5000\",\"seller_minor\":\"0\",\"digest\":\"");
    EXPECT(call("dispute.release", "\"case\":1"), OKR("{\"case\":1,\"released\":true}"));
    EXPECT(rpc("dispute.ruling", "\"case\":1"),
           ERRR("-32060", "DSP_ERR_UNKNOWN: no such case or juror"));
    EXPECT(rpc("dispute.message", "\"kind\":\"x\",\"case\":1"),
           ERRR("-32060", "DSP_ERR_UNKNOWN: no such case or juror"));
    EXPECT(call("dispute.juror", "\"id\":91,\"remove\":true"),
           OKR("{\"id\":91,\"removed\":true,\"pool\":1}"));
    HAS(rpc("dispute.status", ""), "\"slashed_minor\":\"0\"");
    HAS(rpc("dispute.status", ""), "\"check\":true,\"sandbox\":true}");
}

int main(void)
{
    test_signer_init(&K1, 1);
    test_signer_init(&K2, 2);
    test_signer_init(&K3, 3);
    test_signer_init(&K4, 4);
    test_signer_init(&K5, 5);
    test_signer_init(&K6, 6);
    test_signer_init(&K7, 7);
    zxvd_rpc_groups_reset();
    const zxvd_group_t *(*const G[])(void) = {
        zxvd_mint_group,    zxvd_qnote_group,  zxvd_fx_group,
        zxvd_vhub_group,    zxvd_capmkt_group, zxvd_market_group,
        zxvd_royalty_group, zxvd_cred_group,   zxvd_dispute_group};
    for (size_t i = 0; i < sizeof G / sizeof G[0]; i++)
        CHECK(zxvd_rpc_add_group(G[i]()) == 0, "add group %zu", i);
    test_switches();
    test_mint();
    test_qnote();
    test_fx();
    test_vhub();
    test_capmkt();
    test_market();
    test_royalty();
    test_cred();
    test_dispute();
    /* a reset forgets every group's state */
    zxvd_rpc_groups_reset();
    for (size_t i = 0; i < sizeof G / sizeof G[0]; i++) zxvd_rpc_add_group(G[i]());
    EXPECT(rpc("mint.status", ""), OKR("{\"set_up\":false,\"keys\":0,\"sandbox\":true}"));
    EXPECT(rpc("capmkt.status", ""), OKR("{\"set_up\":false,\"keys\":0,\"sandbox\":true}"));
    EXPECT(rpc("cred.status", ""),
           OKR("{\"set_up\":false,\"credit_lines\":false,\"sandbox\":true}"));
    EXPECT(rpc("dispute.status", ""), OKR("{\"set_up\":false,\"keys\":0,\"sandbox\":true}"));
    zxvd_rpc_groups_reset();
    return finish("test_zxvd_money");
}
