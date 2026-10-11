/* Copyright (c) 2024-2026 Michael Laurence Curzi and 36N9 Genetics, LLC */
/* SPDX-License-Identifier: Apache-2.0 */
/* test_zxt_exec.c — running treaties on the engine, the result record and
 * the CALL 'ZXV_ENGINE' entry point. */
#include "zxt_exec.h"

#include <stdio.h>
#include <string.h>

static int g_pass, g_fail;
#define CHECK(c, ...)                                                                              \
    do {                                                                                           \
        if (c) {                                                                                   \
            g_pass++;                                                                              \
        } else {                                                                                   \
            g_fail++;                                                                              \
            printf("FAIL %s:%d: ", __FILE__, __LINE__);                                            \
            printf(__VA_ARGS__);                                                                   \
            printf("\n");                                                                          \
        }                                                                                          \
    } while (0)

static zxv_engine_t ws;
static const uint8_t SEED[32] = {1};

static const char TRI[] = "TREATY CORRIDOR-7 USD SETL\n"
                          "PARTY ACME 5000000 4000 1/3\n"
                          "PARTY BETA 3000000 3000 1/7\n"
                          "PARTY GAMMA 900000 0 11/21\n"
                          "OWE ACME BETA 1200000\n"
                          "OWE BETA GAMMA 700000\n"
                          "OWE GAMMA ACME 100000\n"
                          "END\n";

static zxt_treaty_t parse(const char *s)
{
    zxt_treaty_t t;
    zxt_status_t st = zxt_parse_sutra(s, (uint32_t) strlen(s), &t, NULL);
    CHECK(st == ZXT_OK, "fixture parses (%d)", st);
    return t;
}

static bool result_zero_but_status(const zxt_result_t *r, int32_t st)
{
    zxt_result_t z;
    memset(&z, 0, sizeof z);
    z.status = st;
    return memcmp(&z, r, sizeof z) == 0;
}

/* The dividend rule, computed a second way (128-bit arithmetic). */
static void ref_share(const zxt_treaty_t *t, uint64_t total, int64_t *out)
{
    unsigned __int128 rem[ZXT_MAX_PARTIES];
    uint64_t given = 0;
    bool got[ZXT_MAX_PARTIES] = {0};
    for (uint32_t i = 0; i < t->n_party; i++) {
        unsigned __int128 p = (unsigned __int128) total * t->party[i].w_num;
        out[i] = (int64_t) (p / t->party[i].w_den);
        rem[i] = p % t->party[i].w_den;
        given += (uint64_t) out[i];
    }
    while (given < total) {
        int best = -1;
        for (uint32_t i = 0; i < t->n_party; i++)
            if (!got[i] &&
                (best < 0 || rem[i] * t->party[best].w_den > rem[best] * t->party[i].w_den))
                best = (int) i;
        got[best] = true;
        out[best]++;
        given++;
    }
}

static void test_setl(void)
{
    zxt_treaty_t t = parse(TRI);
    zxt_result_t r, r2;
    CHECK(zxt_exec(&t, &ws, SEED, &r) == ZXT_OK && r.status == ZXT_OK, "setl runs");
    CHECK(r.n_party == 3 && r.n_leg == 2 && r.where == 0, "counts %u %u", r.n_party, r.n_leg);
    CHECK(r.net[0] == -1100000 && r.net[1] == 500000 && r.net[2] == 600000, "net positions");
    CHECK(r.leg[0].from == 0 && r.leg[0].to == 2 && r.leg[0].amount == 600000, "leg 0");
    CHECK(r.leg[1].from == 0 && r.leg[1].to == 1 && r.leg[1].amount == 500000, "leg 1");
    /* 0.08889% of 1 100 000 = 977.79: 977 charged, the rest carried */
    CHECK(r.fees == 977, "fees %lld", (long long) r.fees);
    CHECK(r.balance[0] == 5000000 - 1100000 - 977 - 4000, "acme balance %lld",
          (long long) r.balance[0]);
    CHECK(r.balance[1] == 3000000 + 500000 - 3000, "beta balance");
    CHECK(r.balance[2] == 900000 + 600000, "gamma balance");
    /* 244 into the dividend bucket: 81.33, 34.86, 127.81 -> 81, 35, 128 */
    uint64_t div = zxv_engine_bucket(&ws, PAY_ASSURE_VBILL_DIVIDEND);
    CHECK(div == 244, "dividend bucket %llu", (unsigned long long) div);
    CHECK(r.dividend[0] == 81 && r.dividend[1] == 35 && r.dividend[2] == 128, "dividend shares");
    int64_t ref[ZXT_MAX_PARTIES];
    ref_share(&t, div, ref);
    CHECK(memcmp(ref, r.dividend, 3 * sizeof ref[0]) == 0, "dividend == reference");
    for (uint32_t i = 3; i < ZXT_MAX_PARTIES; i++)
        CHECK(r.net[i] == 0 && r.balance[i] == 0 && r.dividend[i] == 0, "unused party %u", i);
    CHECK(zxv_engine_check(&ws), "engine invariants hold after the run");
    /* conservation: deposits = balances + claims + fees */
    int64_t lhs = 5000000 + 3000000 + 900000, rhs = r.fees + 4000 + 3000;
    for (int i = 0; i < 3; i++) rhs += r.balance[i];
    CHECK(lhs == rhs, "conservation %lld vs %lld", (long long) lhs, (long long) rhs);
    /* E3 determinism */
    memset(&r2, 0x77, sizeof r2);
    CHECK(zxt_exec(&t, &ws, SEED, &r2) == ZXT_OK && memcmp(&r, &r2, sizeof r) == 0,
          "deterministic");
}

static void test_net(void)
{
    zxt_treaty_t t = parse(TRI);
    zxt_result_t r;
    t.op = ZXT_OP_NET;
    CHECK(zxt_exec(&t, &ws, SEED, &r) == ZXT_OK, "net runs");
    CHECK(r.fees == 0 && r.n_leg == 2 && r.leg[0].amount == 600000, "net legs, no fee");
    CHECK(r.balance[0] == 5000000 && r.balance[1] == 3000000 && r.balance[2] == 900000,
          "net posts nothing, claims stay");
    CHECK(r.dividend[0] == 0 && r.dividend[1] == 0 && r.dividend[2] == 0, "no dividend");
    CHECK(r.net[2] == 600000, "net position reported");

    /* a cycle nets to nothing: no legs, no fee even under SETL */
    t = parse("TREATY C USD SETL\nPARTY A 10 10 1/2\nPARTY B 10 10 1/2\n"
              "OWE A B 7\nOWE B A 7\nEND\n");
    CHECK(zxt_exec(&t, &ws, SEED, &r) == ZXT_OK && r.n_leg == 0 && r.fees == 0, "cycle");
    CHECK(r.balance[0] == 0 && r.balance[1] == 0 && r.net[0] == 0, "claims paid out in full");
    /* no obligations, a zero deposit and a zero claim */
    t = parse("TREATY Z USD SETL\nPARTY A 0 0 1/1\nEND\n");
    CHECK(zxt_exec(&t, &ws, SEED, &r) == ZXT_OK && r.balance[0] == 0 && r.n_party == 1, "empty");
}

static void test_fail_closed(void)
{
    zxt_treaty_t t = parse(TRI);
    zxt_result_t r;
    /* a claim larger than what is left after settlement */
    t.party[0].claim = 4000000;
    memset(&r, 0x55, sizeof r);
    CHECK(zxt_exec(&t, &ws, SEED, &r) == ZXT_E_EXEC + ZXV_E_FUNDS &&
              result_zero_but_status(&r, ZXT_E_EXEC + ZXV_E_FUNDS),
          "claim beyond balance fails closed (%d)", r.status);
    t.party[0].claim = 5000000 - 1100000 - 977; /* exactly what is left */
    CHECK(zxt_exec(&t, &ws, SEED, &r) == ZXT_OK && r.balance[0] == 0, "claim of the exact rest");
    t.party[0].claim = 5000000 - 1100000 - 976;
    CHECK(zxt_exec(&t, &ws, SEED, &r) == ZXT_E_EXEC + ZXV_E_FUNDS, "claim one past the rest");

    /* a payer that cannot cover its legs */
    t = parse("TREATY P USD SETL\nPARTY A 100 0 1/2\nPARTY B 0 0 1/2\nOWE A B 100\nEND\n");
    CHECK(zxt_exec(&t, &ws, SEED, &r) == ZXT_E_EXEC + ZXV_E_FUNDS &&
              result_zero_but_status(&r, r.status),
          "uncovered fee fails closed");
    t.op = ZXT_OP_NET;
    CHECK(zxt_exec(&t, &ws, SEED, &r) == ZXT_OK && r.balance[0] == 100, "same treaty nets");

    /* a net position past PIC S9(18): ZXT_E_SIZE */
    t = parse("TREATY S USD NET\nPARTY A 0 0 1/3\nPARTY B 0 0 1/3\nPARTY C 0 0 1/3\n"
              "OWE A C 600000000000000000\nOWE B C 400000000000000000\nEND\n");
    CHECK(zxt_exec(&t, &ws, SEED, &r) == ZXT_E_SIZE && result_zero_but_status(&r, ZXT_E_SIZE),
          "net size error (%d)", r.status);
    t.owe[1].amount = 399999999999999999;
    t.owe[0].amount = 600000000000000000;
    /* (it fits PIC S9(18), so it reaches the engine, whose 2^59 line limit
     * refuses it) */
    CHECK(zxt_exec(&t, &ws, SEED, &r) == ZXT_E_EXEC + ZXV_E_ARG,
          "net exactly at the PIC limit is not a size error (%d)", r.status);
    t.owe[0].from = 2;
    t.owe[0].to = 0;
    t.owe[1].from = 2;
    t.owe[1].to = 1;
    t.owe[1].amount = 400000000000000000;
    CHECK(zxt_exec(&t, &ws, SEED, &r) == ZXT_E_SIZE, "payer side size error");
    /* a deposit above the ledger's per-line limit (2^59): engine refuses */
    t = parse("TREATY L USD NET\nPARTY A 576460752303423489 0 1/1\nEND\n");
    CHECK(zxt_exec(&t, &ws, SEED, &r) == ZXT_E_EXEC + ZXV_E_ARG, "deposit over 2^59");
    t = parse("TREATY L USD NET\nPARTY A 576460752303423488 0 1/1\nEND\n");
    CHECK(zxt_exec(&t, &ws, SEED, &r) == ZXT_OK && r.balance[0] == 576460752303423488LL,
          "deposit of 2^59");
    /* an invalid treaty never reaches the engine */
    t = parse(TRI);
    t.party[1].claim = t.party[1].deposit + 1;
    CHECK(zxt_exec(&t, &ws, SEED, &r) == ZXT_E_CLAIM && result_zero_but_status(&r, ZXT_E_CLAIM),
          "validate first");
    CHECK(zxt_exec(NULL, &ws, SEED, &r) == ZXT_E_ARG && r.status == ZXT_E_ARG, "NULL treaty");
    CHECK(zxt_exec(&t, NULL, SEED, &r) == ZXT_E_ARG, "NULL workspace");
    CHECK(zxt_exec(&t, &ws, NULL, &r) == ZXT_E_ARG, "NULL seed");
    CHECK(zxt_exec(&t, &ws, SEED, NULL) == ZXT_E_ARG, "NULL result");
    /* a bad asset name reaches zxv_engine_init only through a hand-built AST,
     * which validate refuses first */
    t = parse(TRI);
    t.asset[0] = '1';
    CHECK(zxt_exec(&t, &ws, SEED, &r) == ZXT_E_ASSET, "asset checked before the engine");
}

static void test_dividend_rule(void)
{
    /* sixteen parties with unequal weights; amounts big enough that the
     * dividend bucket holds a large, awkward number */
    char buf[4096];
    int n = sprintf(buf, "TREATY W USD SETL\n");
    /* weights: 1/2, 1/4, ..., 1/2^15, 1/2^15 */
    for (int i = 0; i < 16; i++)
        n += sprintf(buf + n, "PARTY P%02d 100000000000000 0 1/%u\n", i,
                     1u << (i < 15 ? i + 1 : 15));
    for (int j = 0; j < 15; j++)
        n += sprintf(buf + n, "OWE P%02d P%02d %llu\n", j, j + 1,
                     12345678901ULL * (unsigned) (j + 3));
    strcpy(buf + n, "END\n");
    zxt_treaty_t t = parse(buf);
    zxt_result_t r;
    CHECK(zxt_exec(&t, &ws, SEED, &r) == ZXT_OK, "16-party setl");
    uint64_t div = zxv_engine_bucket(&ws, PAY_ASSURE_VBILL_DIVIDEND);
    int64_t ref[ZXT_MAX_PARTIES], sum = 0;
    ref_share(&t, div, ref);
    CHECK(memcmp(ref, r.dividend, sizeof ref) == 0, "16-party dividend == reference");
    for (int i = 0; i < 16; i++) sum += r.dividend[i];
    CHECK(sum == (int64_t) div && div > 1000, "dividend conserved (%lld of %llu)", (long long) sum,
          (unsigned long long) div);

    /* ties go to the lowest index: three equal thirds of a bucket that is
     * 2 mod 3 give the extra units to parties 0 and 1 */
    t = parse("TREATY T USD SETL\nPARTY A 100000000 0 1/3\nPARTY B 100000000 0 1/3\n"
              "PARTY C 100000000 0 1/3\nOWE A B 3000000\nEND\n");
    for (int k = 0; k < 64; k++, t.owe[0].amount += 1000) {
        CHECK(zxt_exec(&t, &ws, SEED, &r) == ZXT_OK, "thirds run");
        div = zxv_engine_bucket(&ws, PAY_ASSURE_VBILL_DIVIDEND);
        if (div % 3 == 2) break;
    }
    CHECK(div % 3 == 2, "bucket %llu is 2 mod 3", (unsigned long long) div);
    CHECK(r.dividend[0] == (int64_t) (div / 3 + 1) && r.dividend[1] == (int64_t) (div / 3 + 1) &&
              r.dividend[2] == (int64_t) (div / 3),
          "ties to the lowest index: %lld %lld %lld", (long long) r.dividend[0],
          (long long) r.dividend[1], (long long) r.dividend[2]);
    /* near the ledger's 2^59 line limit with 9-digit weights: the dividend
     * times the weight needs more than 64 bits, so the 128-bit share
     * arithmetic is exercised in full */
    t = parse("TREATY H USD SETL\nPARTY A 576460752303423488 0 999999936/999999937\n"
              "PARTY B 0 0 1/999999937\n"
              "OWE A B 500000000000000000\nEND\n");
    CHECK(zxt_exec(&t, &ws, SEED, &r) == ZXT_OK, "huge setl (%d)", r.status);
    div = zxv_engine_bucket(&ws, PAY_ASSURE_VBILL_DIVIDEND);
    ref_share(&t, div, ref);
    CHECK(div > (1ULL << 40) && memcmp(ref, r.dividend, 2 * sizeof ref[0]) == 0,
          "huge dividend %llu == reference (%lld %lld vs %lld %lld)", (unsigned long long) div,
          (long long) r.dividend[0], (long long) r.dividend[1], (long long) ref[0],
          (long long) ref[1]);
    t.party[0].w_num = 1;
    t.party[0].w_den = 999999937;
    t.party[1].w_num = 999999936;
    t.party[1].w_den = 999999937;
    CHECK(zxt_exec(&t, &ws, SEED, &r) == ZXT_OK, "huge setl, weights swapped");
    ref_share(&t, zxv_engine_bucket(&ws, PAY_ASSURE_VBILL_DIVIDEND), ref);
    CHECK(memcmp(ref, r.dividend, 2 * sizeof ref[0]) == 0, "swapped == reference");
    /* a zero weight gets nothing even with remainder units about */
    t = parse("TREATY T USD SETL\nPARTY A 100000000 0 0/1\nPARTY B 100000000 0 1/3\n"
              "PARTY C 100000000 0 2/3\nOWE A B 3000000\nEND\n");
    CHECK(zxt_exec(&t, &ws, SEED, &r) == ZXT_OK && r.dividend[0] == 0, "zero weight");
    ref_share(&t, zxv_engine_bucket(&ws, PAY_ASSURE_VBILL_DIVIDEND), ref);
    CHECK(memcmp(ref, r.dividend, 3 * sizeof ref[0]) == 0, "zero weight == reference");
}

static void test_result_record(void)
{
    zxt_treaty_t t = parse(TRI);
    zxt_result_t r;
    static uint8_t rec[ZXT_RES_LEN + 8];
    CHECK(ZXT_RES_LEN == 2422u, "result length");
    zxt_exec(&t, &ws, SEED, &r);
    CHECK(zxt_emit_result(&r, rec, ZXT_RES_LEN) == ZXT_OK, "emit result");
    CHECK(memcmp(rec, "ZXR1+00000000003002+000000000000000977", 38) == 0, "result head: %.38s",
          rec);
    CHECK(memcmp(rec + 38, "-000000000001100000+000000000003895023+000000000000000081", 57) == 0,
          "party 0: %.57s", rec + 38);
    CHECK(memcmp(rec + 38 + 2 * 57, "+000000000000600000+000000000001500000+000000000000000128",
                 57) == 0,
          "party 2");
    CHECK(memcmp(rec + 38 + 3 * 57, "+000000000000000000+000000000000000000+000000000000000000",
                 57) == 0,
          "unused party");
    const uint8_t *legs = rec + 38 + 16 * 57;
    CHECK(memcmp(legs, "0103+000000000000600000", 23) == 0 &&
              memcmp(legs + 23, "0102+000000000000500000", 23) == 0 &&
              memcmp(legs + 46, "0000+000000000000000000", 23) == 0 &&
              memcmp(legs + 63 * 23, "0000+000000000000000000", 23) == 0,
          "legs");
    CHECK(zxt_emit_result(&r, rec, ZXT_RES_LEN - 1u) == ZXT_E_CAP, "cap");
    CHECK(zxt_emit_result(NULL, rec, ZXT_RES_LEN) == ZXT_E_ARG, "NULL r");
    CHECK(zxt_emit_result(&r, NULL, ZXT_RES_LEN) == ZXT_E_ARG, "NULL rec");
#define BAD(mut)                                                                                   \
    do {                                                                                           \
        zxt_result_t b_ = r;                                                                       \
        mut;                                                                                       \
        CHECK(zxt_emit_result(&b_, rec, ZXT_RES_LEN) == ZXT_E_SIZE, "emit refuses %s", #mut);      \
    } while (0)
    BAD(b_.status = 1);
    BAD(b_.status = -10000);
    BAD(b_.where = 100000);
    BAD(b_.n_party = 17);
    BAD(b_.n_leg = 65);
    BAD(b_.fees = ZXT_PIC18_MAX + 1);
    BAD(b_.net[15] = -ZXT_PIC18_MAX - 1);
    BAD(b_.balance[15] = ZXT_PIC18_MAX + 1);
    BAD(b_.dividend[15] = ZXT_PIC18_MAX + 1);
    BAD(b_.leg[63].amount = ZXT_PIC18_MAX + 1);
#undef BAD
    zxt_result_t e = r;
    e.status = -9999;
    e.where = 99999;
    e.n_party = 16;
    e.n_leg = 64;
    CHECK(zxt_emit_result(&e, rec, ZXT_RES_LEN) == ZXT_OK &&
              memcmp(rec, "ZXR1-99999999916064", 19) == 0,
          "widest header: %.19s", rec);
}

static void test_cobol_entry(void)
{
    zxt_treaty_t t = parse(TRI);
    static uint8_t trec[ZXT_REC_LEN], res[ZXT_RES_LEN], want[ZXT_RES_LEN];
    zxt_result_t r;
    uint8_t seed[32];
    zxt_emit_record(&t, trec, sizeof trec);
    memcpy(seed, trec + 4, 32);
    zxt_exec(&t, &ws, seed, &r);
    zxt_emit_result(&r, want, sizeof want);
    CHECK(ZXV_ENGINE((const char *) trec, (char *) res) == ZXT_OK &&
              memcmp(res, want, sizeof res) == 0,
          "ZXV_ENGINE == parse + exec + emit");
    /* a refused record: status and offset, nothing else */
    memcpy(trec + 46 + 27, "+000000000005000001", 19); /* ACME claim > deposit */
    CHECK(ZXV_ENGINE((const char *) trec, (char *) res) == ZXT_E_CLAIM, "refused record");
    CHECK(memcmp(res, "ZXR1-001100046000", 17) == 0, "refusal head: %.19s", res);
    CHECK(memcmp(res + 19, "+000000000000000000", 19) == 0 &&
              memcmp(res + 38, "+000000000000000000+000000000000000000+000000000000000000", 57) ==
                  0,
          "refusal carries no numbers");
    CHECK(ZXV_ENGINE(NULL, (char *) res) == ZXT_E_ARG &&
              ZXV_ENGINE((const char *) trec, NULL) == ZXT_E_ARG,
          "NULL records");
}

int main(void)
{
    test_setl();
    test_net();
    test_fail_closed();
    test_dividend_rule();
    test_result_record();
    test_cobol_entry();
    printf("%s: %d passed, %d failed\n", g_fail ? "FAIL" : "PASS", g_pass, g_fail);
    printf("%d checks\n", g_pass + g_fail);
    return g_fail ? 1 : 0;
}
