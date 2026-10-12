/* Copyright (c) 2024-2026 Michael Laurence Curzi and 36N9 Genetics, LLC */
/* SPDX-License-Identifier: Apache-2.0 */
/* test_aipi_zwasm.c — AIPI end to end on real Wasm: functions published by
 * CID, fetched and run in the Z-Wasm sandbox, checked by their own inverse
 * export, certified, held inside their Z-Contract, fuel-bounded, offline
 * from the local cache. */
#include "aipi_mods.h"

static int failures = 0, passes = 0;
#define CHECK(c)                                                                 \
    do {                                                                         \
        if (c) passes++;                                                         \
        else {                                                                   \
            failures++;                                                          \
            fprintf(stderr, "FAIL %s:%d: %s\n", __FILE__, __LINE__, #c);         \
        }                                                                        \
    } while (0)

static zbudget_t B;

static void make_fn(aipi_fn_t *fn, const ipfsn_cid_t *mod, const char *entry, int verify)
{
    memset(fn, 0, sizeof *fn);
    fn->intent = AIPI_INTENT_FN;
    fn->verify = (uint8_t) verify;
    fn->privacy = AIPI_PRIV_PUBLIC;
    fn->determinism = AIPI_DET_DETERMINISTIC;
    fn->fuel_max = 100000;
    fn->module = *mod;
    strcpy(fn->entry, entry);
}

static void test_pack(void)
{
    int64_t a[9] = {0, -1, INT64_MIN, INT64_MAX, 42, 0, 0, 0, 0}, b[9];
    uint8_t buf[80];
    uint32_t len = 0, n = 0;
    CHECK(aipi_zw_pack(a, 5, buf, sizeof buf, &len) == AIPI_OK && len == 40);
    CHECK(buf[8] == 0xFF && buf[16 + 7] == 0x80 && buf[32] == 42);
    CHECK(aipi_zw_unpack(buf, len, b, &n) == AIPI_OK && n == 5 && !memcmp(a, b, 40));
    CHECK(aipi_zw_unpack(buf, 7, b, &n) == AIPI_ERR_MALFORMED);
    CHECK(aipi_zw_unpack(buf, 72, b, &n) == AIPI_ERR_MALFORMED);
    CHECK(aipi_zw_pack(a, 9, buf, sizeof buf, &len) == AIPI_ERR_SPACE);
    CHECK(aipi_zw_pack(a, 2, buf, 15, &len) == AIPI_ERR_SPACE);
    CHECK(aipi_zw_unpack(buf, 0, b, &n) == AIPI_OK && n == 0);
}

static void test_local_and_contract(void)
{
    tnode_t *r = tn_new("requester", 1, &B);
    wb m;
    ipfsn_cid_t mod, con, f;
    aipi_fn_t fn;
    uint8_t in[8], out[16];
    uint32_t job, len;
    const aipi_job_t *j;
    aipi_zw_stats_t st;

    mod_halves(&m);
    CHECK(aipi_put_blob(&r->a, m.b, (uint32_t) m.n, IPFSN_VIS_PUBLIC, &mod) == AIPI_OK);
    CHECK(aipi_put_blob(&r->a, (const uint8_t *) HALVES_ZC, sizeof HALVES_ZC - 1, IPFSN_VIS_PUBLIC,
                        &con) == AIPI_OK);
    make_fn(&fn, &mod, "half", AIPI_VERIFY_CHECKER);
    strcpy(fn.checker, "half_ok");
    fn.has_contract = true;
    fn.contract = con;
    CHECK(aipi_publish_fn(&r->a, &fn, &f) == AIPI_OK);

    /* offline: runs from the local cache in the sandbox */
    r->a.online = false;
    CHECK(aipi_execute(&r->a, &f, i64in(41, in), 8, false, 0, 0, 100, &job) == AIPI_OK);
    j = aipi_job(&r->a, job);
    CHECK(j->route == AIPI_ROUTE_LOCAL && j->status == AIPI_JOB_DONE);
    CHECK(aipi_result(&r->a, job, out, sizeof out, &len) == AIPI_OK && len == 8 &&
          i64out(out) == 20);
    CHECK(lpx_base(j->tag) == LPX_B_TRUE && lpx_phase(j->tag) == LPX_PH_MANIFEST);
    aipi_release(&r->a, job);

    /* outside the contract: refused before the code runs */
    aipi_zw_stats(r->zw, &st);
    {
        uint64_t before = st.refused_contract, fuel = st.fuel_used;
        CHECK(aipi_execute(&r->a, &f, i64in(2000000, in), 8, false, 0, 0, 100, &job) ==
              AIPI_ERR_EXEC);
        j = aipi_job(&r->a, job);
        CHECK(j && j->status == AIPI_JOB_FAILED && lpx_base(j->tag) == LPX_B_FALSE);
        aipi_zw_stats(r->zw, &st);
        CHECK(st.refused_contract == before + 1 && st.fuel_used == fuel);
        aipi_release(&r->a, job);
    }
    /* bad input shape */
    CHECK(aipi_execute(&r->a, &f, in, 7, false, 0, 0, 100, &job) == AIPI_ERR_MALFORMED);
    aipi_release(&r->a, job);

    /* a runaway function stops at its fuel limit */
    {
        aipi_fn_t s;
        ipfsn_cid_t sf;
        make_fn(&s, &mod, "spin", AIPI_VERIFY_LOCAL);
        s.fuel_max = 5000;
        CHECK(aipi_publish_fn(&r->a, &s, &sf) == AIPI_OK);
        aipi_zw_stats(r->zw, &st);
        {
            uint64_t trapped = st.trapped, fuel = st.fuel_used;
            CHECK(aipi_execute(&r->a, &sf, i64in(1, in), 8, false, 0, 0, 100, &job) ==
                  AIPI_ERR_EXEC);
            aipi_zw_stats(r->zw, &st);
            CHECK(st.trapped == trapped + 1);
            CHECK(st.fuel_used - fuel >= 5000 && st.fuel_used - fuel <= 5100);
        }
        aipi_release(&r->a, job);
    }
    /* float code never runs */
    {
        aipi_fn_t fl;
        ipfsn_cid_t fm, ff;
        wb fmod;
        mod_floaty(&fmod);
        CHECK(aipi_put_blob(&r->a, fmod.b, (uint32_t) fmod.n, IPFSN_VIS_PUBLIC, &fm) == AIPI_OK);
        make_fn(&fl, &fm, "half", AIPI_VERIFY_LOCAL);
        CHECK(aipi_publish_fn(&r->a, &fl, &ff) == AIPI_OK);
        aipi_zw_stats(r->zw, &st);
        {
            uint64_t refused = st.refused_module;
            CHECK(aipi_execute(&r->a, &ff, i64in(8, in), 8, false, 0, 0, 100, &job) ==
                  AIPI_ERR_EXEC);
            aipi_zw_stats(r->zw, &st);
            CHECK(st.refused_module == refused + 1);
        }
        aipi_release(&r->a, job);
    }
    tn_free(r);
}

static void test_mesh_inverse_check(void)
{
    tnode_t *r = tn_new("requester", 1, &B), *e = tn_new("executor", 2, &B);
    wb m;
    ipfsn_cid_t mod, con, f, call;
    aipi_fn_t fn;
    uint8_t in[8], out[16], claim[AIPI_CLAIM_ENC_MAX], forged[8], v = 0;
    uint32_t job, len;
    int cl;
    const aipi_job_t *j;
    aipi_zw_stats_t st;

    /* only the executor publishes; the requester knows just the CID */
    mod_halves(&m);
    aipi_put_blob(&e->a, m.b, (uint32_t) m.n, IPFSN_VIS_PUBLIC, &mod);
    aipi_put_blob(&e->a, (const uint8_t *) HALVES_ZC, sizeof HALVES_ZC - 1, IPFSN_VIS_PUBLIC, &con);
    make_fn(&fn, &mod, "half", AIPI_VERIFY_CHECKER);
    strcpy(fn.checker, "half_ok");
    fn.has_contract = true;
    fn.contract = con;
    CHECK(aipi_publish_fn(&e->a, &fn, &f) == AIPI_OK);
    r->peer = e;
    r->balance = 100;
    tn_paid_total = 0;

    /* the descriptor and the checker's code come by CID from the peer */
    CHECK(aipi_execute(&r->a, &f, i64in(1001, in), 8, false, 10, 0, 100, &job) == AIPI_OK);
    j = aipi_job(&r->a, job);
    CHECK(j->route == AIPI_ROUTE_MESH && r->broadcasts == 1 && r->escrow == 10);
    CHECK(ipfsn_bs_has(&r->bs, &mod) && ipfsn_bs_has(&r->bs, &con));
    aipi_call_cid(&f, in, 8, &call);

    /* a forged answer fails half_ok in the requester's own sandbox */
    {
        int64_t w = 501;
        aipi_zw_pack(&w, 1, forged, 8, &len);
    }
    cl = aipi_claim_encode(&call, (const uint8_t *) "FFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFF", forged, 8,
                           0, 0, claim, sizeof claim);
    CHECK(aipi_submit(&r->a, job, claim, (uint32_t) cl, &v) == AIPI_OK && v == AIPI_V_REJECTED);
    CHECK(lpx_base(j->tag) == LPX_B_BOTH && r->escrow == 10 && tn_paid_total == 0);
    /* an answer outside the contract is refused before the checker runs */
    aipi_zw_stats(r->zw, &st);
    {
        uint64_t checks_fuel = st.fuel_used, refused = st.refused_contract;
        int64_t w = 600000;
        aipi_zw_pack(&w, 1, forged, 8, &len);
        cl = aipi_claim_encode(&call, (const uint8_t *) "GGGGGGGGGGGGGGGGGGGGGGGGGGGGGGGG", forged,
                               8, 0, 0, claim, sizeof claim);
        CHECK(aipi_submit(&r->a, job, claim, (uint32_t) cl, &v) == AIPI_OK &&
              v == AIPI_V_REJECTED);
        aipi_zw_stats(r->zw, &st);
        CHECK(st.refused_contract == refused + 1 && st.fuel_used == checks_fuel);
    }
    /* a wrongly sized answer is not a result */
    cl = aipi_claim_encode(&call, (const uint8_t *) "HHHHHHHHHHHHHHHHHHHHHHHHHHHHHHHH", forged, 4,
                           0, 0, claim, sizeof claim);
    CHECK(aipi_submit(&r->a, job, claim, (uint32_t) cl, &v) == AIPI_OK && v == AIPI_V_REJECTED);

    /* the executor really runs the Wasm; its answer verifies and is paid */
    cl = aipi_serve(&e->a, r->call, r->call_len, claim, sizeof claim);
    CHECK(cl > 0);
    CHECK(aipi_submit(&r->a, job, claim, (uint32_t) cl, &v) == AIPI_OK && v == AIPI_V_ACCEPTED);
    CHECK(aipi_result(&r->a, job, out, sizeof out, &len) == AIPI_OK && i64out(out) == 500);
    CHECK(tn_paid_total == 10 && r->escrow == 0 && r->balance == 90);
    aipi_zw_stats(e->zw, &st);
    CHECK(st.runs == 1);
    aipi_release(&r->a, job);

    /* the executor goes away: the requester now has the code and runs it */
    r->peer = NULL;
    r->a.online = false;
    CHECK(aipi_execute(&r->a, &f, i64in(77, in), 8, false, 10, 0, 100, &job) == AIPI_OK);
    CHECK(aipi_job(&r->a, job)->route == AIPI_ROUTE_LOCAL);
    CHECK(aipi_result(&r->a, job, out, sizeof out, &len) == AIPI_OK && i64out(out) == 38);
    CHECK(r->balance == 90);
    aipi_release(&r->a, job);
    tn_free(r);
    tn_free(e);
}

static void test_mesh_certificate(void)
{
    tnode_t *r = tn_new("requester", 1, &B), *e = tn_new("executor", 2, &B);
    wb m;
    ipfsn_cid_t mod, f, call;
    aipi_fn_t fn;
    uint8_t in[8], out[16], claim[AIPI_CLAIM_ENC_MAX], one[8], c[8], v = 0;
    uint32_t job, len;
    int cl;

    mod_factors(&m);
    aipi_put_blob(&e->a, m.b, (uint32_t) m.n, IPFSN_VIS_PUBLIC, &mod);
    make_fn(&fn, &mod, "is_composite", AIPI_VERIFY_CERT);
    strcpy(fn.checker, "composite_ok");
    strcpy(fn.certifier, "factor_of");
    fn.intent = AIPI_INTENT_SETTLE_LEDGER; /* any intent; the codec follows it */
    CHECK(aipi_publish_fn(&e->a, &fn, &f) == AIPI_OK && f.codec == AIPI_MC_SETTLE_LEDGER);
    r->peer = e;
    r->balance = 50;
    tn_paid_total = 0;

    CHECK(aipi_execute(&r->a, &f, i64in(8633, in), 8, false, 5, 0, 100, &job) == AIPI_OK);
    aipi_call_cid(&f, in, 8, &call);
    i64in(1, one);
    /* "composite" with a certificate that is not a factor: refused */
    cl = aipi_claim_encode(&call, (const uint8_t *) "11111111111111111111111111111111", one, 8,
                           i64in(7, c), 8, claim, sizeof claim);
    CHECK(aipi_submit(&r->a, job, claim, (uint32_t) cl, &v) == AIPI_OK && v == AIPI_V_REJECTED);
    /* certificate 0 makes the checker trap (n % 0): not accepted either */
    cl = aipi_claim_encode(&call, (const uint8_t *) "22222222222222222222222222222222", one, 8,
                           i64in(0, c), 8, claim, sizeof claim);
    CHECK(aipi_submit(&r->a, job, claim, (uint32_t) cl, &v) == AIPI_OK && v == AIPI_V_REJECTED);
    /* the trivial factor n itself: refused (c < n) */
    cl = aipi_claim_encode(&call, (const uint8_t *) "33333333333333333333333333333333", one, 8,
                           i64in(8633, c), 8, claim, sizeof claim);
    CHECK(aipi_submit(&r->a, job, claim, (uint32_t) cl, &v) == AIPI_OK && v == AIPI_V_REJECTED);
    CHECK(tn_paid_total == 0);
    /* the executor's certificate: 8633 = 89 * 97 */
    cl = aipi_serve(&e->a, r->call, r->call_len, claim, sizeof claim);
    CHECK(cl > 0);
    {
        aipi_claim_t parsed;
        CHECK(aipi_claim_decode(claim, (uint32_t) cl, &parsed) == AIPI_OK);
        CHECK(parsed.cert_len == 8 && i64out(parsed.cert) == 89 && i64out(parsed.out) == 1);
    }
    CHECK(aipi_submit(&r->a, job, claim, (uint32_t) cl, &v) == AIPI_OK && v == AIPI_V_ACCEPTED);
    CHECK(aipi_result(&r->a, job, out, sizeof out, &len) == AIPI_OK && i64out(out) == 1);
    CHECK(tn_paid_total == 5 && r->balance == 45);
    aipi_release(&r->a, job);

    /* a prime: "composite" is false and there is no certificate for it, so
     * under CERT the call can only expire, refunded. Honest limit: CERT
     * proves only the answers a certificate exists for. */
    CHECK(aipi_execute(&r->a, &f, i64in(8627, in), 8, false, 5, 0, 100, &job) == AIPI_OK);
    cl = aipi_serve(&e->a, r->call, r->call_len, claim, sizeof claim);
    CHECK(cl > 0 && aipi_submit(&r->a, job, claim, (uint32_t) cl, &v) == AIPI_OK &&
          v == AIPI_V_REJECTED);
    CHECK(aipi_tick(&r->a, 100) == 1 && r->balance == 45 && r->escrow == 0);
    aipi_release(&r->a, job);
    tn_free(r);
    tn_free(e);
}

int main(void)
{
    zbudget_init(&B);
    test_pack();
    test_local_and_contract();
    test_mesh_inverse_check();
    test_mesh_certificate();
    zbudget_fini(&B);
    printf("%d passed, %d failed\n", passes, failures);
    return failures ? 1 : 0;
}
