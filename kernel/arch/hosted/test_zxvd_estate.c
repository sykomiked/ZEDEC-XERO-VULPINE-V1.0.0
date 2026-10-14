/* Copyright (c) 2024-2026 Michael Laurence Curzi and 36N9 Genetics, LLC */
/* SPDX-License-Identifier: Apache-2.0 */
/* test_zxvd_estate.c — grants.*, estate.* and recovery.* (zxvd_estate.c)
 * behind zxvd_rpc.c, on the real grants, estate, faraid and recovery engines
 * with TEST-ONLY Ed25519 keys: exact replies, refusals, the switches, a grant
 * within the pool and one refused beyond it, faraid shares that sum exactly,
 * an estate through its phases, and a recovery with guardian approvals and
 * an old-device veto. The mutation harness's test command for zxvd_estate.c
 * (tests/harness/mutator/targets.json). */
#include "test_zxvd_sandbox.h"

static test_signer_t FU, UW, OW, G0, G1, G2, AT, CT, GW, RT, Q[5], D1, NK, D2;

static void add_groups(void)
{
    zxvd_rpc_groups_reset();
    CHECK(zxvd_rpc_add_group(zxvd_grants_group()) == 0, "add grants");
    CHECK(zxvd_rpc_add_group(zxvd_estate_group()) == 0, "add estate");
}

/* a good-standing recipient object */
#define GOOD(acct, w, subj)                                                                        \
    "{\"account\":" #acct ",\"weight\":" #w ",\"subject\":" #subj                                  \
    ",\"uptime_bps\":9900,\"volume\":20000,\"stake\":\"100000\"}"

static void test_grants(void)
{
    EXPECT(rpc("grants.setup", "\"funnel\":50"),
           ERRR("-32012", "grants is off: start zxvd with --enable grants, or call features.set "
                          "(regulated: test or licensed use only)"));
    EXPECT(rpc("grants.status", "\"now\":1"),
           OKR("{\"set_up\":false,\"keys\":0,\"minted\":false,\"sandbox\":true}"));
    HAS(rpc("features.get", ""), "{\"name\":\"grants\",\"enabled\":false,\"regulated\":true");
    HAS(rpc("features.get", ""), "{\"name\":\"estate\",\"enabled\":false,\"regulated\":false");
    zxvd_rpc_enable("grants");
    EXPECT(rpc("grants.epoch", "\"epoch\":1"),
           ERRR("-32060", "grants is not set up: call grants.setup first"));
    EXPECT(rpc("grants.setup", "\"funnel\":50,\"underwriters\":[1,2,3,4,5,6,7,8,9]"),
           ERRR("-32602", "at most 8 underwriters"));
    EXPECT(rpc("grants.setup", "\"funnel\":50,\"underwriters\":[-1]"),
           ERRR("-32602", "\\\"underwriters\\\"[0] is out of range"));
    EXPECT(rpc("grants.setup", "\"funnel\":50,\"underwriters\":[\"x\"]"),
           ERRR("-32602", "\\\"underwriters\\\"[0] must be an integer"));
    EXPECT(rpc("grants.setup", "\"funnel\":50,\"underwriters\":7"),
           ERRR("-32602", "\\\"underwriters\\\" must be an array"));
    EXPECT(rpc("grants.setup", "\"funnel\":50,\"cap_num\":2,\"cap_den\":1"),
           ERRR("-32060", "GRANT_ERR_ARG: a bad argument (an unusable account, a zero weight or "
                          "price)"));
    EXPECT(rpc("grants.setup", "\"funnel\":50,\"min_uptime_bps\":10001"),
           ERRR("-32060", "GRANT_ERR_ARG: a bad argument (an unusable account, a zero weight or "
                          "price)"));
    EXPECT(rpc("grants.setup", "\"underwriters\":[60]"),
           ERRR("-32602", "missing integer parameter \\\"funnel\\\""));
    CHECK(has(rpc("grants.status", ""), "\"set_up\":false"), "refused setups set nothing up");
    EXPECT(rpc("grants.setup", "\"funnel\":50,\"underwriters\":[60]"),
           OKR("{\"funnel\":50,\"pool\":2,\"reserve\":3,\"issuer\":1,\"underwriters\":1,"
               "\"min_uptime_bps\":9500,\"min_volume\":\"10000\",\"min_stake\":\"100000\","
               "\"cap_num\":\"1\",\"cap_den\":\"1\",\"minted\":false}"));
    EXPECT(rpc("grants.setup", "\"funnel\":50"),
           ERRR("-32060", "SET_UP: grants are set up already (this run)"));
    rpc("grants.key", "\"key\":50,\"public_key\":\"%s\"", pk_hex(&FU));
    EXPECT(rpc("grants.key", "\"key\":60,\"public_key\":\"%s\"", pk_hex(&UW)),
           OKR("{\"key\":60,\"added\":true,\"keys\":2}"));
    EXPECT(rpc("grants.account_open", "\"owner\":7,\"asset\":\"vino\""),
           OKR("{\"account\":4,\"owner\":7,\"asset\":\"vino\"}"));
    EXPECT(rpc("grants.account_open", "\"owner\":21,\"asset\":\"barrel\""),
           OKR("{\"account\":5,\"owner\":21,\"asset\":\"barrel\"}"));
    rpc("grants.account_open", "\"owner\":22,\"asset\":\"barrel\""); /* 6 */
    rpc("grants.account_open", "\"owner\":23,\"asset\":\"barrel\""); /* 7 */
    rpc("grants.account_open", "\"owner\":21,\"asset\":\"vino\"");   /* 8 */
    rpc("grants.account_open", "\"owner\":22,\"asset\":\"vino\"");   /* 9 */
    EXPECT(rpc("grants.fund", "\"account\":5,\"amount_minor\":1"),
           ERRR("-32602", "\\\"account\\\" must be a fundable sandbox account"));
    EXPECT(rpc("grants.fund", "\"account\":4,\"amount_minor\":10000"),
           OKR("{\"account\":4,\"balance_minor\":\"10000\",\"sandbox\":true}"));
    EXPECT(rpc("grants.account", "\"account\":2"),
           OKR("{\"account\":2,\"owner\":4294967280,\"asset\":\"vino\",\"balance_minor\":\"0\","
               "\"credit_minor\":\"0\",\"sandbox\":true}"));

    EXPECT(rpc("grants.epoch", "\"epoch\":1"), OKR("{\"epoch\":\"1\"}"));
    EXPECT(rpc("grants.epoch", "\"epoch\":1"),
           ERRR("-32060", "GRANT_ERR_EPOCH: the epoch or expiry is not above the last"));
    EXPECT(rpc("grants.pool_credit", "\"from\":4,\"amount_minor\":1000,\"now\":5"),
           OKR("{\"pool_minor\":\"1000\",\"inflow_minor\":\"1000\"}"));
    EXPECT(rpc("grants.pool_credit", "\"from\":4,\"amount_minor\":0"),
           ERRR("-32060", "GRANT_ERR_ARG: a bad argument (an unusable account, a zero weight or "
                          "price)"));
    EXPECT(rpc("grants.pool_credit", "\"from\":4,\"amount_minor\":99999"),
           ERRR("-32060", "GRANT_ERR_LEDGER: the ledger refused the posting"));
    EXPECT(rpc("grants.lot_open", "\"lot\":1"),
           OKR("{\"lot\":1,\"label\":\"Virtual Barrel Allocation (Unbacked Synthetic Unit)\","
               "\"status\":\"Pending Physical Underwriter Attestation\"}"));
    EXPECT(rpc("grants.lot_open", "\"lot\":1"),
           ERRR("-32060", "GRANT_ERR_ARG: a bad argument (an unusable account, a zero weight or "
                          "price)"));

    /* G2 */
    EXPECT(rpc("grants.eval", "\"subject\":21,\"uptime_bps\":9500,\"volume\":10000,\"stake\":"
                              "100000"),
           OKR("{\"subject\":21,\"eligible\":true,\"mask\":0,\"reasons\":[],\"epoch\":\"1\"}"));
    EXPECT(rpc("grants.eval",
               "\"subject\":23,\"uptime_bps\":9499,\"dropped_reports\":1,\"routing_reports\":1,"
               "\"failed_challenges\":1,\"fraud_findings\":1,\"volume\":9999,\"stake\":99999"),
           OKR("{\"subject\":23,\"eligible\":false,\"mask\":127,\"reasons\":[\"uptime\","
               "\"dropped\",\"routing\",\"audit\",\"fraud\",\"volume\",\"stake\"],\"epoch\":"
               "\"1\"}"));
    EXPECT(rpc("grants.eval", "\"subject\":23,\"uptime_bps\":10001,\"volume\":10000,\"stake\":"
                              "100000"),
           OKR("{\"subject\":23,\"eligible\":false,\"mask\":128,\"reasons\":[\"invalid\"],"
               "\"epoch\":\"1\"}"));
    EXPECT(rpc("grants.eval", "\"subject\":23,\"uptime_bps\":9500,\"volume\":10000"),
           ERRR("-32602", "missing integer parameter \\\"stake\\\""));

    /* G3: within the pool: 1000 buys 3 barrels at 300, split 1:2 */
    EXPECT(rpc("grants.distribute", "\"lot\":1,\"price\":300,\"recipients\":[]"),
           ERRR("-32602", "\\\"recipients\\\" must hold 1..32 objects"));
    EXPECT(rpc("grants.distribute", "\"lot\":1,\"price\":300"),
           ERRR("-32602", "\\\"recipients\\\" must hold 1..32 objects"));
    EXPECT(rpc("grants.distribute", "\"lot\":1,\"price\":300,\"recipients\":7"),
           ERRR("-32602", "\\\"recipients\\\" must be an array"));
    EXPECT(rpc("grants.distribute", "\"lot\":1,\"price\":300,\"recipients\":[1]"),
           ERRR("-32602", "\\\"recipients\\\"[0] must be an object"));
    EXPECT(rpc("grants.distribute", "\"lot\":1,\"price\":300,\"recipients\":[{\"account\":5}]"),
           ERRR("-32602", "missing integer parameter \\\"weight\\\""));
    EXPECT(rpc("grants.distribute", "\"lot\":2,\"price\":300,\"recipients\":[" GOOD(5, 1, 21) "]"),
           ERRR("-32060", "GRANT_ERR_NOT_FOUND: no such lot or epoch"));
    EXPECT(rpc("grants.distribute",
               "\"lot\":1,\"price\":300,\"recipients\":[" GOOD(
                   5, 1, 21) ","
                             "{\"account\":7,\"weight\":1,\"subject\":23,\"uptime_bps\":"
                             "9000,\"fraud_findings\":1,\"volume\":20000,\"stake\":100000}]"),
           ERRR("-32060", "GRANT_ERR_INELIGIBLE: recipient 1 (subject 23) is not in good standing "
                          "(mask 17); nothing was granted"));
    EXPECT(rpc("grants.distribute", "\"lot\":1,\"price\":300,\"now\":6,\"recipients\":[" GOOD(
                                        5, 1, 21) "," GOOD(6, 2, 22) "]"),
           OKR("{\"barrels\":\"3\",\"cost_minor\":\"900\",\"postings\":1,\"shares\":[{\"account\":"
               "5,\"barrels\":\"1\"},{\"account\":6,\"barrels\":\"2\"}],\"pool_minor\":\"100\","
               "\"label\":\"Virtual Barrel Allocation (Unbacked Synthetic Unit)\"}"));
    EXPECT(rpc("grants.distribute", "\"lot\":1,\"price\":1,\"recipients\":[" GOOD(5, 1, 21) "]"),
           ERRR("-32060", "GRANT_ERR_STATE: this epoch has distributed already, or the lot is "
                          "attested"));
    /* beyond the pool: 100 left cannot buy a 300 barrel, and nothing is minted */
    rpc("grants.epoch", "\"epoch\":2");
    EXPECT(rpc("grants.distribute", "\"lot\":1,\"price\":300,\"recipients\":[" GOOD(5, 1, 21) "]"),
           ERRR("-32060", "GRANT_ERR_EMPTY: the pool cannot pay for one whole barrel: grants are "
                          "never minted"));
    HAS(rpc("grants.status", "\"now\":6"),
        "\"pool_minor\":\"100\",\"reserve_minor\":\"900\",\"barrels_outstanding\":\"3\"");

    /* an appeal signed by the dispute funnel clears the masked bits for one epoch */
    const char *bad = "{\"account\":7,\"weight\":1,\"subject\":23,\"uptime_bps\":9000,"
                      "\"fraud_findings\":1,\"volume\":20000,\"stake\":100000}";
    rpc("grants.pool_credit", "\"from\":4,\"amount_minor\":500");
    EXPECT(
        rpc("grants.appeal", "\"subject\":23,\"epoch\":2,\"mask\":17,\"sig\":\"%s\"", zero_sig()),
        ERRR("-32060", "GRANT_ERR_SIG: the signature does not verify, or the underwriter is not "
                       "listed"));
    char *s = sign_msg(&FU, "grants.message",
                       "\"kind\":\"appeal\",\"subject\":23,\"epoch\":2,"
                       "\"mask\":17");
    EXPECT(rpc("grants.appeal", "\"subject\":23,\"epoch\":2,\"mask\":17,\"sig\":\"%s\"", s),
           OKR("{\"subject\":23,\"mask\":17,\"epoch\":\"2\"}"));
    EXPECT(rpc("grants.appeal", "\"subject\":23,\"epoch\":1,\"mask\":17,\"sig\":\"%s\"", s),
           ERRR("-32060", "GRANT_ERR_EPOCH: the epoch or expiry is not above the last"));
    EXPECT(rpc("grants.distribute", "\"lot\":1,\"price\":300,\"recipients\":[%s]", bad),
           OKR("{\"barrels\":\"2\",\"cost_minor\":\"600\",\"postings\":1,\"shares\":[{\"account\":"
               "7,\"barrels\":\"2\"}],\"pool_minor\":\"0\",\"label\":\"Virtual Barrel Allocation "
               "(Unbacked Synthetic Unit)\"}"));

    /* G5: backing, redemption, dispute */
    EXPECT(rpc("grants.redeem", "\"holder\":5,\"holder_vino\":8,\"lot\":1,\"n\":1,\"price\":300"),
           ERRR("-32060", "GRANT_ERR_UNBACKED: the lot has no underwriter attestation"));
    const char *att = "\"lot\":1,\"count\":5,\"underwriter\":60,\"expiry\":5000";
    s = sign_msg(&UW, "grants.message", "\"kind\":\"attest\",%s", att);
    EXPECT(rpc("grants.attest", "%s,\"now\":5000,\"sig\":\"%s\"", att, s),
           ERRR("-32060", "GRANT_ERR_EXPIRED: the attestation has expired"));
    EXPECT(rpc("grants.attest",
               "\"lot\":1,\"count\":4,\"underwriter\":60,\"expiry\":5000,"
               "\"now\":10,\"sig\":\"%s\"",
               s),
           ERRR("-32060", "GRANT_ERR_ARG: a bad argument (an unusable account, a zero weight or "
                          "price)"));
    EXPECT(rpc("grants.attest",
               "\"lot\":1,\"count\":5,\"underwriter\":50,\"expiry\":5000,"
               "\"now\":10,\"sig\":\"%s\"",
               s),
           ERRR("-32060", "GRANT_ERR_SIG: the signature does not verify, or the underwriter is not "
                          "listed"));
    EXPECT(rpc("grants.attest", "%s,\"now\":10,\"sig\":\"%s\"", att, s),
           OKR("{\"lot\":1,\"backed\":true,\"label\":\"Virtual Barrel Allocation (Underwriter "
               "Attested Unit)\",\"status\":\"Underwriter Signature On Record (Signature "
               "Only)\"}"));
    EXPECT(rpc("grants.redeem", "\"holder\":6,\"holder_vino\":9,\"lot\":1,\"n\":3,\"price\":300,"
                                "\"now\":10"),
           ERRR("-32060",
                "GRANT_ERR_FUNDS: the holder lacks the barrels, or the pool the uncounted "
                "Vino"));
    EXPECT(rpc("grants.redeem", "\"holder\":6,\"holder_vino\":9,\"lot\":1,\"n\":1,\"price\":300,"
                                "\"now\":4999"),
           OKR("{\"lot\":1,\"barrels\":\"1\",\"paid_minor\":\"300\",\"held\":\"1\"}"));
    EXPECT(rpc("grants.redeem", "\"holder\":6,\"holder_vino\":9,\"lot\":1,\"n\":1,\"price\":300,"
                                "\"now\":5000"),
           ERRR("-32060", "GRANT_ERR_EXPIRED: the attestation has expired"));
    HAS(rpc("grants.account", "\"account\":9"), "\"balance_minor\":\"300\"");
    EXPECT(rpc("grants.status", "\"now\":10"),
           OKR("{\"set_up\":true,\"keys\":2,\"epoch\":\"2\",\"inflow_minor\":\"500\",\"weight\":"
               "\"1\",\"rate_defined\":true,\"rate_num\":\"500\",\"rate_den\":\"1\","
               "\"granted_barrels\":\"2\",\"granted_cost_minor\":\"600\","
               "\"total_barrels_granted\":\"5\",\"pool_minor\":\"0\",\"reserve_minor\":\"1200\","
               "\"barrels_outstanding\":\"4\",\"barrels_backed\":\"4\",\"barrels_unbacked\":\"0\","
               "\"lots\":[{\"lot\":1,\"count\":\"4\",\"backed\":true,\"label\":\"Virtual Barrel "
               "Allocation (Underwriter Attested Unit)\",\"status\":\"Underwriter Signature On "
               "Record (Signature Only)\"}],\"check\":true,\"minted\":false,\"sandbox\":true}"));
    EXPECT(rpc("grants.dispute", "\"lot\":1,\"sig\":\"%s\"", s),
           ERRR("-32060", "GRANT_ERR_SIG: the signature does not verify, or the underwriter is not "
                          "listed"));
    s = sign_msg(&FU, "grants.message", "\"kind\":\"dispute\",\"lot\":1");
    EXPECT(rpc("grants.dispute", "\"lot\":1,\"sig\":\"%s\"", s),
           OKR("{\"lot\":1,\"backed\":false,\"disputed\":true}"));
    EXPECT(rpc("grants.redeem", "\"holder\":6,\"holder_vino\":9,\"lot\":1,\"n\":1,\"price\":300,"
                                "\"now\":10"),
           ERRR("-32060", "GRANT_ERR_DISPUTED: the attestation is disputed"));
    EXPECT(rpc("grants.message", "\"kind\":\"dispute\",\"lot\":9"),
           ERRR("-32060", "GRANT_ERR_NOT_FOUND: no such lot or epoch"));
    EXPECT(
        rpc("grants.message", "\"kind\":\"gift\""),
        ERRR("-32602", "\\\"kind\\\" must be \\\"appeal\\\", \\\"attest\\\" or \\\"dispute\\\""));
    HAS(rpc("grants.message", "\"kind\":\"attest\",\"lot\":1,\"count\":5,\"underwriter\":60,"
                              "\"expiry\":5000"),
        "\"message\":\"7a78762f6772616e742d6174746573742f7631010000000500000000000000"
        "3c0000008813000000000000\",\"bytes\":43");
    HAS(rpc("grants.status", "\"now\":10"),
        "\"barrels_backed\":\"0\",\"barrels_unbacked\":\"4\",\"lots\":[{\"lot\":1,\"count\":\"4\","
        "\"backed\":false");
    rpc("features.set", "\"feature\":\"grants\",\"enabled\":false");
    EXPECT(rpc("grants.lot_open", "\"lot\":2"),
           ERRR("-32012", "grants is off: start zxvd with --enable grants, or call features.set "
                          "(regulated: test or licensed use only)"));
    HAS(rpc("grants.status", ""), "\"check\":true");
}

/* a fresh grants group: nothing before setup, the most underwriters and both ends of the range */
static void test_grants_limits(void)
{
    add_groups();
    zxvd_rpc_enable("grants");
    EXPECT(rpc("grants.account_open", "\"owner\":7,\"asset\":\"vino\""),
           ERRR("-32060", "grants is not set up: call grants.setup first"));
    EXPECT(rpc("grants.fund", "\"account\":4,\"amount_minor\":1"),
           ERRR("-32060", "grants is not set up: call grants.setup first"));
    EXPECT(rpc("grants.account", "\"account\":4"),
           ERRR("-32060", "grants is not set up: call grants.setup first"));
    HAS(rpc("grants.setup", "\"funnel\":50,\"underwriters\":[0,1,2,3,4,5,6,4294967295]"),
        "\"underwriters\":8,");
    rpc("grants.lot_open", "\"lot\":1");
    rpc("grants.lot_open", "\"lot\":2");
    HAS(rpc("grants.status", "\"now\":1"), "},{\"lot\":2,\"count\":");
}

static void test_faraid(void)
{
    zxvd_rpc_enable("estate");
    /* Minbariyya: wife, father, mother, two daughters (awl): 3/27, 4/27, 4/27, 16/27 */
    EXPECT(rpc("estate.faraid", "\"wives\":1,\"father\":true,\"mother\":true,\"daughters\":2,"
                                "\"amount_minor\":2700"),
           OKR("{\"classes\":[{\"class\":\"wives\",\"persons\":1,\"num\":\"1\",\"den\":\"9\","
               "\"each_minor\":[\"300\"]},{\"class\":\"father\",\"persons\":1,\"num\":\"4\","
               "\"den\":\"27\",\"each_minor\":[\"400\"]},{\"class\":\"mother\",\"persons\":1,"
               "\"num\":\"4\",\"den\":\"27\",\"each_minor\":[\"400\"]},{\"class\":\"daughters\","
               "\"persons\":2,\"num\":\"16\",\"den\":\"27\",\"each_minor\":[\"800\",\"800\"]}],"
               "\"unallocated\":{\"num\":\"0\",\"den\":\"1\"},\"awl\":true,\"radd\":false,"
               "\"amount_minor\":\"2700\",\"advisory\":true,\"ruling\":false,\"notice\":\"a "
               "computation of one common majority treatment, not a ruling: a qualified scholar "
               "or court decides\"}"));
    /* husband, a son and a daughter: 1/4, 1/2, 1/4; 101 leaves one unit to the larger
     * remainder; the parts sum to 101 exactly */
    EXPECT(rpc("estate.faraid", "\"husband\":true,\"sons\":1,\"daughters\":1,\"amount_minor\":"
                                "\"101\""),
           OKR("{\"classes\":[{\"class\":\"husband\",\"persons\":1,\"num\":\"1\",\"den\":\"4\","
               "\"each_minor\":[\"25\"]},{\"class\":\"sons\",\"persons\":1,\"num\":\"1\",\"den\":"
               "\"2\",\"each_minor\":[\"51\"]},{\"class\":\"daughters\",\"persons\":1,\"num\":"
               "\"1\",\"den\":\"4\",\"each_minor\":[\"25\"]}],\"unallocated\":{\"num\":\"0\","
               "\"den\":\"1\"},\"awl\":false,\"radd\":false,\"amount_minor\":\"101\","
               "\"advisory\":true,\"ruling\":false,\"notice\":\"a computation of one common "
               "majority treatment, not a ruling: a qualified scholar or court decides\"}"));
    /* radd: mother and one daughter: 1/4, 3/4 (no amount) */
    HAS(rpc("estate.faraid", "\"mother\":true,\"daughters\":1"),
        "{\"classes\":[{\"class\":\"mother\",\"persons\":1,\"num\":\"1\",\"den\":\"4\"},{"
        "\"class\":\"daughters\",\"persons\":1,\"num\":\"3\",\"den\":\"4\"}],\"unallocated\":{"
        "\"num\":\"0\",\"den\":\"1\"},\"awl\":false,\"radd\":true,");
    /* the spouse alone: half is unallocated, and no amount can be split */
    HAS(rpc("estate.faraid", "\"husband\":true"), "\"unallocated\":{\"num\":\"1\",\"den\":\"2\"}");
    EXPECT(rpc("estate.faraid", "\"husband\":true,\"amount_minor\":10"),
           ERRR("-32060", "EST_ERR_UNSUPPORTED: relatives or a residue this computation does not "
                          "model: ask a qualified scholar"));
    EXPECT(rpc("estate.faraid", "\"sons\":1,\"others\":true"),
           ERRR("-32060", "EST_ERR_UNSUPPORTED: relatives or a residue this computation does not "
                          "model: ask a qualified scholar"));
    EXPECT(rpc("estate.faraid", "\"husband\":true,\"wives\":1"),
           ERRR("-32060", "EST_ERR_ARG: a bad argument"));
    EXPECT(rpc("estate.faraid", ""), ERRR("-32060", "EST_ERR_ARG: a bad argument"));
    EXPECT(rpc("estate.faraid", "\"husband\":1"),
           ERRR("-32602", "\\\"husband\\\" must be true or false"));
    EXPECT(rpc("estate.faraid", "\"sons\":12,\"daughters\":12,\"amount_minor\":10"),
           ERRR("-32060", "EST_ERR_FULL: too many heirs"));
    EXPECT(rpc("estate.faraid", "\"sons\":1,\"amount_minor\":-1"),
           ERRR("-32602", "\\\"amount_minor\\\" must not be negative"));
}

static char keys3[200], keys5[330];

static void estate_setup(void)
{
    snprintf(keys3, sizeof keys3, "%s%s%s", pk_hex(&G0), pk_hex(&G1), pk_hex(&G2));
    rpc("estate.setup",
        "\"owner\":\"%s\",\"guardians\":\"%s\",\"k\":2,\"attestors\":\"%s\",\"court\":\"%s\","
        "\"gateway\":\"%s\",\"wait\":100,\"release\":50",
        pk_hex(&OW), keys3, pk_hex(&AT), pk_hex(&CT), pk_hex(&GW));
}

static void test_estate(void)
{
    EXPECT(rpc("estate.report", ""),
           ERRR("-32060", "the estate is not set up: call estate.setup first"));
    EXPECT(rpc("estate.status", "\"now\":1"), OKR("{\"set_up\":false,\"moves_money\":false}"));
    EXPECT(rpc("estate.setup",
               "\"owner\":\"%s\",\"guardians\":\"00\",\"k\":1,\"attestors\":\"%s\","
               "\"court\":\"%s\",\"gateway\":\"%s\"",
               pk_hex(&OW), pk_hex(&AT), pk_hex(&CT), pk_hex(&GW)),
           ERRR("-32602", "\\\"guardians\\\" must be 1..5 keys of 32 bytes, back to back"));
    snprintf(keys3, sizeof keys3, "%s%s%s", pk_hex(&G0), pk_hex(&G1), pk_hex(&G2));
    EXPECT(rpc("estate.setup",
               "\"owner\":\"%s\",\"guardians\":\"%s\",\"k\":4,\"attestors\":\"%s\","
               "\"court\":\"%s\",\"gateway\":\"%s\"",
               pk_hex(&OW), keys3, pk_hex(&AT), pk_hex(&CT), pk_hex(&GW)),
           ERRR("-32060", "EST_ERR_ARG: a bad argument"));
    snprintf(keys3, sizeof keys3, "%s%s%s", pk_hex(&G0), pk_hex(&G1), pk_hex(&G2));
    EXPECT(rpc("estate.setup",
               "\"owner\":\"%s\",\"guardians\":\"%s\",\"k\":2,\"attestors\":\"%s\",\"court\":"
               "\"%s\",\"gateway\":\"%s\",\"wait\":100,\"release\":50",
               pk_hex(&OW), keys3, pk_hex(&AT), pk_hex(&CT), pk_hex(&GW)),
           OKR("{\"guardians\":3,\"k\":2,\"attestors\":1,\"wait\":\"100\",\"release\":\"50\","
               "\"phase\":\"alive\"}"));
    EXPECT(rpc("estate.setup", "\"owner\":\"%s\"", pk_hex(&OW)),
           ERRR("-32060", "SET_UP: the estate is set up already (this run)"));

    /* E4: the will, while alive */
    EXPECT(rpc("estate.manifest", "\"enclave\":\"common\",\"heirs\":[{\"id\":1,\"num\":1,\"den\":"
                                  "2},{\"id\":2,\"num\":1,\"den\":3}]"),
           ERRR("-32060", "EST_ERR_SUM: the shares do not sum to exactly 1"));
    EXPECT(rpc("estate.manifest", "\"enclave\":\"common\",\"heirs\":[]"),
           ERRR("-32602", "\\\"heirs\\\" must hold 1..16 objects"));
    EXPECT(rpc("estate.manifest", "\"enclave\":\"common\",\"heirs\":[{\"id\":1,\"num\":1}]"),
           ERRR("-32602", "missing integer parameter \\\"den\\\""));
    EXPECT(rpc("estate.manifest", "\"enclave\":\"trust\""),
           ERRR("-32602", "\\\"enclave\\\" must be \\\"common\\\", \\\"faraid\\\" or "
                          "\\\"forced\\\""));
    HAS(rpc("estate.manifest", "\"enclave\":\"common\",\"heirs\":[{\"id\":1,\"num\":1,\"den\":2},"
                               "{\"id\":2,\"num\":1,\"den\":3},{\"id\":3,\"num\":\"1\",\"den\":"
                               "6}]"),
        "{\"enclave\":\"common\",\"advisory\":false,\"heirs\":[{\"id\":1,\"num\":\"3\",\"den\":"
        "\"6\"},{\"id\":2,\"num\":\"2\",\"den\":\"6\"},{\"id\":3,\"num\":\"1\",\"den\":\"6\"}],"
        "\"digest\":\"");

    /* E1: report, approvals, the wait, then the living veto */
    EXPECT(rpc("estate.message", "\"kind\":\"report\""),
           ERRR("-32602", "missing integer parameter \\\"bond_minor\\\""));
    char *s = sign_msg(&AT, "estate.message", "\"kind\":\"report\",\"bond_minor\":500");
    EXPECT(rpc("estate.report", "\"attestor\":0,\"bond_minor\":501,\"sig\":\"%s\"", s),
           ERRR("-32060", "EST_ERR_SIG: the signature does not verify"));
    EXPECT(rpc("estate.report", "\"attestor\":0,\"bond_minor\":500,\"now\":1000,\"sig\":\"%s\"", s),
           OKR("{\"phase\":\"reported\",\"epoch\":\"0\"}"));
    EXPECT(rpc("estate.report", "\"attestor\":0,\"bond_minor\":500,\"sig\":\"%s\"", s),
           ERRR("-32060", "EST_ERR_STATE: not allowed in this phase (or bond or contest state)"));
    s = sign_msg(&G0, "estate.message", "\"kind\":\"approve\"");
    EXPECT(rpc("estate.approve", "\"guardian\":1,\"sig\":\"%s\"", s),
           ERRR("-32060", "EST_ERR_SIG: the signature does not verify"));
    EXPECT(rpc("estate.approve", "\"guardian\":0,\"sig\":\"%s\"", s),
           OKR("{\"approvals\":1,\"k\":2}"));
    EXPECT(rpc("estate.approve", "\"guardian\":0,\"sig\":\"%s\"", s),
           ERRR("-32060", "EST_DUP: already recorded: nothing changed"));
    EXPECT(rpc("estate.confirm", "\"now\":2000"),
           ERRR("-32060", "EST_ERR_QUORUM: fewer guardian approvals than k"));
    s = sign_msg(&G1, "estate.message", "\"kind\":\"approve\"");
    EXPECT(rpc("estate.approve", "\"guardian\":1,\"sig\":\"%s\"", s),
           OKR("{\"approvals\":2,\"k\":2}"));
    EXPECT(rpc("estate.confirm", "\"now\":1099"),
           ERRR("-32060", "EST_ERR_TIME: the waiting period is not over"));
    EXPECT(rpc("estate.confirm", "\"now\":1100"), OKR("{\"phase\":\"confirmed\",\"epoch\":\"0\"}"));
    s = sign_msg(&OW, "estate.message", "\"kind\":\"veto\"");
    EXPECT(rpc("estate.veto", "\"sig\":\"%s\"", s), OKR("{\"phase\":\"alive\",\"epoch\":\"1\"}"));
    HAS(rpc("estate.status", "\"now\":1100"),
        "{\"set_up\":true,\"phase\":\"alive\",\"epoch\":\"1\",\"bond_minor\":\"500\",\"bond\":"
        "\"refunded\",\"approvals\":0,\"k\":2,\"enclave\":\"common\",\"advisory\":false");
    EXPECT(rpc("estate.veto", "\"sig\":\"%s\"", s),
           ERRR("-32060", "EST_ERR_STATE: not allowed in this phase (or bond or contest state)"));

    /* again, through to distribution (the old signatures no longer work) */
    char *old = sign_msg(&AT, "estate.message", "\"kind\":\"report\",\"bond_minor\":500");
    rpc("estate.report", "\"attestor\":0,\"bond_minor\":500,\"now\":3000,\"sig\":\"%s\"", old);
    rpc("estate.approve", "\"guardian\":0,\"sig\":\"%s\"",
        sign_msg(&G0, "estate.message", "\"kind\":\"approve\""));
    rpc("estate.approve", "\"guardian\":2,\"sig\":\"%s\"",
        sign_msg(&G2, "estate.message", "\"kind\":\"approve\""));
    EXPECT(rpc("estate.distribute", "\"amount_minor\":1"),
           ERRR("-32060", "EST_ERR_STATE: not allowed in this phase (or bond or contest state)"));
    EXPECT(rpc("estate.confirm", "\"now\":3100"), OKR("{\"phase\":\"confirmed\",\"epoch\":\"1\"}"));
    EXPECT(rpc("estate.rule", "\"sig\":\"%s\"", zero_sig()),
           ERRR("-32060", "EST_ERR_STATE: not allowed in this phase (or bond or contest state)"));
    EXPECT(rpc("estate.begin", ""), OKR("{\"phase\":\"distributing\",\"epoch\":\"1\"}"));
    HAS(rpc("estate.status", "\"now\":3149"), "\"key_release\":false");
    HAS(rpc("estate.status", "\"now\":3150"), "\"key_release\":true");
    /* E2a: guardian 0 contests heir index 0 (id 1) */
    s = sign_msg(&G0, "estate.message", "\"kind\":\"contest\",\"heir\":0");
    EXPECT(rpc("estate.contest", "\"heir\":0,\"who\":1,\"sig\":\"%s\"", s),
           ERRR("-32060", "EST_ERR_SIG: the signature does not verify"));
    EXPECT(rpc("estate.contest", "\"heir\":0,\"who\":0,\"sig\":\"%s\"", s),
           OKR("{\"heir\":0,\"contested\":true}"));
    EXPECT(rpc("estate.contest", "\"heir\":0,\"who\":0,\"sig\":\"%s\"", s),
           ERRR("-32060", "EST_DUP: already recorded: nothing changed"));
    EXPECT(rpc("estate.message", "\"kind\":\"contest\",\"heir\":3"),
           ERRR("-32060", "EST_ERR_ARG: a bad argument"));
    /* 1000: 500 (escrowed), 333, 166 + the unit left to the larger remainder: 167 */
    EXPECT(rpc("estate.distribute", "\"amount_minor\":1000"),
           OKR("{\"parts\":[{\"id\":1,\"minor\":\"0\",\"escrowed\":true},{\"id\":2,\"minor\":"
               "\"333\",\"escrowed\":false},{\"id\":3,\"minor\":\"167\",\"escrowed\":false}],"
               "\"to_escrow_minor\":\"500\",\"moved\":false}"));
    EXPECT(rpc("estate.stream", "\"amount_minor\":7"),
           OKR("{\"parts\":[{\"id\":1,\"minor\":\"0\",\"escrowed\":true},{\"id\":2,\"minor\":"
               "\"2\",\"escrowed\":false},{\"id\":3,\"minor\":\"1\",\"escrowed\":false}],"
               "\"to_escrow_minor\":\"3\",\"held_minor\":\"1\",\"moved\":false}"));
    s = sign_msg(&CT, "estate.message", "\"kind\":\"resolve\",\"heir\":0");
    EXPECT(rpc("estate.resolve", "\"heir\":0,\"sig\":\"%s\"", zero_sig()),
           ERRR("-32060", "EST_ERR_SIG: the signature does not verify"));
    EXPECT(rpc("estate.resolve", "\"heir\":0,\"sig\":\"%s\"", s),
           OKR("{\"heir\":0,\"released_minor\":\"503\"}"));
    EXPECT(rpc("estate.resolve", "\"heir\":0,\"sig\":\"%s\"", s),
           ERRR("-32060", "EST_ERR_STATE: not allowed in this phase (or bond or contest state)"));
    EXPECT(rpc("estate.close", ""), OKR("{\"phase\":\"closed\",\"epoch\":\"1\"}"));
    EXPECT(rpc("estate.close", ""),
           ERRR("-32060", "EST_ERR_STATE: not allowed in this phase (or bond or contest state)"));
    EXPECT(rpc("estate.stream", "\"amount_minor\":5"),
           OKR("{\"parts\":[{\"id\":1,\"minor\":\"3\",\"escrowed\":false},{\"id\":2,\"minor\":"
               "\"2\",\"escrowed\":false},{\"id\":3,\"minor\":\"1\",\"escrowed\":false}],"
               "\"to_escrow_minor\":\"0\",\"held_minor\":\"0\",\"moved\":false}"));
    s = sign_msg(&CT, "estate.message", "\"kind\":\"fraud\"");
    EXPECT(rpc("estate.fraud", "\"who\":255,\"sig\":\"%s\"", s),
           ERRR("-32060", "EST_ERR_STATE: not allowed in this phase (or bond or contest state)"));
    HAS(rpc("estate.status", "\"now\":1"),
        "\"phase\":\"closed\",\"epoch\":\"1\",\"bond_minor\":\"500\",\"bond\":\"refunded\","
        "\"approvals\":2,\"k\":2,\"enclave\":\"common\",\"advisory\":false,\"heirs\":[{\"id\":1,"
        "\"num\":\"3\",\"den\":\"6\",\"contested\":false,\"escrow_minor\":\"0\","
        "\"streamed_minor\":\"6\"}");
    HAS(rpc("estate.status", "\"now\":1"),
        "\"escrow_minor\":\"0\",\"stream_total_minor\":\"12\",\"stream_held_minor\":\"0\","
        "\"key_release\":false,\"digest\":\"");
    EXPECT(rpc("estate.message", "\"kind\":\"will\""),
           ERRR("-32602", "\\\"kind\\\" must be report, approve, veto, ruling, fraud, contest or "
                          "resolve"));
}

/* faraid and forced heirship need the gateway's ruling before distribution */
static void test_estate_ruled(void)
{
    add_groups();
    zxvd_rpc_enable("estate");
    estate_setup();
    EXPECT(rpc("estate.manifest", "\"enclave\":\"faraid\",\"husband\":true,\"sons\":1,\"ids\":[7]"),
           ERRR("-32602", "\\\"ids\\\" must hold 2 ids"));
    EXPECT(rpc("estate.manifest", "\"enclave\":\"faraid\",\"husband\":true,\"sons\":1,\"ids\":"
                                  "[7,-8]"),
           ERRR("-32602", "\\\"ids\\\"[1] is out of range"));
    EXPECT(rpc("estate.manifest", "\"enclave\":\"faraid\",\"sons\":17,\"ids\":[7]"),
           ERRR("-32060", "EST_ERR_FULL: too many heirs"));
    EXPECT(rpc("estate.manifest", "\"enclave\":\"faraid\",\"husband\":true,\"ids\":[7]"),
           ERRR("-32060", "EST_ERR_UNSUPPORTED: relatives or a residue this computation does not "
                          "model: ask a qualified scholar"));
    HAS(rpc("estate.manifest", "\"enclave\":\"faraid\",\"husband\":true,\"sons\":1,\"daughters\":"
                               "1,\"ids\":[7,8,9]"),
        "{\"enclave\":\"faraid\",\"advisory\":true,\"heirs\":[{\"id\":7,\"num\":\"1\",\"den\":"
        "\"4\"},{\"id\":8,\"num\":\"2\",\"den\":\"4\"},{\"id\":9,\"num\":\"1\",\"den\":\"4\"}]");
    /* E5: France, two children and a gift of 1/2 cut to the disposable 1/3 */
    EXPECT(rpc("estate.manifest", "\"enclave\":\"forced\",\"jurisdiction\":\"XX\",\"children\":"
                                  "[1],\"will_den\":2"),
           ERRR("-32060", "EST_ERR_UNKNOWN: no forced-heirship row for that jurisdiction"));
    EXPECT(
        rpc("estate.manifest", "\"enclave\":\"forced\",\"jurisdiction\":\"FRAN\",\"will_den\":2"),
        ERRR("-32602", "\\\"jurisdiction\\\" must be a string of at most 3 bytes"));
    EXPECT(rpc("estate.manifest", "\"enclave\":\"forced\",\"jurisdiction\":\"F\",\"will_den\":2"),
           ERRR("-32602", "\\\"jurisdiction\\\" must be two letters"));
    EXPECT(rpc("estate.manifest", "\"enclave\":\"forced\",\"jurisdiction\":\"FR\",\"children\":"
                                  "[1,2],\"gifts\":[{\"num\":1}],\"will_den\":2"),
           ERRR("-32602", "missing integer parameter \\\"id\\\""));
    EXPECT(rpc("estate.manifest", "\"enclave\":\"forced\",\"jurisdiction\":\"FR\",\"children\":"
                                  "[1,2],\"gifts\":[{\"id\":5,\"num\":3}],\"will_den\":2"),
           ERRR("-32060", "EST_ERR_SUM: the shares do not sum to exactly 1"));
    HAS(rpc("estate.manifest", "\"enclave\":\"forced\",\"jurisdiction\":\"FR\",\"children\":[1,2],"
                               "\"gifts\":[{\"id\":5,\"num\":1}],\"will_den\":2"),
        "{\"enclave\":\"forced\",\"advisory\":true,\"heirs\":[{\"id\":1,\"num\":\"1\",\"den\":"
        "\"3\"},{\"id\":2,\"num\":\"1\",\"den\":\"3\"},{\"id\":5,\"num\":\"1\",\"den\":\"3\"}]");
    rpc("estate.report", "\"attestor\":0,\"bond_minor\":9,\"now\":0,\"sig\":\"%s\"",
        sign_msg(&AT, "estate.message", "\"kind\":\"report\",\"bond_minor\":9"));
    rpc("estate.approve", "\"guardian\":0,\"sig\":\"%s\"",
        sign_msg(&G0, "estate.message", "\"kind\":\"approve\""));
    rpc("estate.approve", "\"guardian\":1,\"sig\":\"%s\"",
        sign_msg(&G1, "estate.message", "\"kind\":\"approve\""));
    /* E1: a guardian finds fraud: the bond is forfeited */
    char *s = sign_msg(&G2, "estate.message", "\"kind\":\"fraud\"");
    EXPECT(rpc("estate.fraud", "\"who\":7,\"sig\":\"%s\"", s),
           ERRR("-32060", "EST_ERR_ARG: a bad argument"));
    EXPECT(rpc("estate.fraud", "\"who\":2,\"sig\":\"%s\"", s), OKR("{\"bond\":\"forfeited\"}"));
    EXPECT(rpc("estate.confirm", "\"now\":100"), OKR("{\"phase\":\"confirmed\",\"epoch\":\"0\"}"));
    EXPECT(rpc("estate.begin", ""),
           ERRR("-32060", "EST_ERR_STATE: not allowed in this phase (or bond or contest state)"));
    EXPECT(rpc("estate.rule", "\"sig\":\"%s\"", s),
           ERRR("-32060", "EST_ERR_SIG: the signature does not verify"));
    s = sign_msg(&GW, "estate.message", "\"kind\":\"ruling\"");
    EXPECT(rpc("estate.rule", "\"sig\":\"%s\"", s), OKR("{\"phase\":\"ruled\",\"epoch\":\"0\"}"));
    HAS(rpc("estate.status", ""), "\"bond\":\"forfeited\",\"approvals\":2,\"k\":2,\"enclave\":"
                                  "\"forced\",\"advisory\":false");
    EXPECT(rpc("estate.begin", ""), OKR("{\"phase\":\"distributing\",\"epoch\":\"0\"}"));
    EXPECT(rpc("estate.distribute", "\"amount_minor\":\"100\""),
           OKR("{\"parts\":[{\"id\":1,\"minor\":\"34\",\"escrowed\":false},{\"id\":2,\"minor\":"
               "\"33\",\"escrowed\":false},{\"id\":5,\"minor\":\"33\",\"escrowed\":false}],"
               "\"to_escrow_minor\":\"0\",\"moved\":false}"));
    EXPECT(rpc("estate.manifest", "\"enclave\":\"forced\",\"jurisdiction\":\"FR\",\"children\":"
                                  "[1],\"will_den\":2"),
           ERRR("-32060", "EST_ERR_STATE: not allowed in this phase (or bond or contest state)"));
    EXPECT(rpc("estate.manifest", "\"enclave\":\"common\",\"heirs\":[{\"id\":1,\"num\":1,\"den\":"
                                  "1}]"),
           ERRR("-32060", "EST_ERR_STATE: not allowed in this phase (or bond or contest state)"));
    HAS(rpc("estate.message", "\"kind\":\"ruling\""), "\"bytes\":95,\"scheme\":\"ed25519\"}");
}

static void test_recovery(void)
{
    EXPECT(rpc("recovery.claim", ""),
           ERRR("-32060", "recovery is not set up: call recovery.setup first"));
    EXPECT(rpc("recovery.status", "\"now\":1"),
           OKR("{\"set_up\":false,\"notices_delivered\":false}"));
    keys5[0] = 0;
    for (int i = 0; i < 4; i++) strcat(keys5, pk_hex(&Q[i]));
    EXPECT(rpc("recovery.setup",
               "\"root\":\"%s\",\"guardians\":\"%s\",\"gateway\":\"%s\","
               "\"first_device\":\"%s\"",
               pk_hex(&RT), keys5, pk_hex(&GW), pk_hex(&D1)),
           ERRR("-32602", "\\\"guardians\\\" must be exactly 5 keys"));
    strcat(keys5, pk_hex(&Q[4]));
    EXPECT(rpc("recovery.setup",
               "\"root\":\"%s\",\"guardians\":\"%s\",\"gateway\":\"%s\","
               "\"first_device\":\"%s\",\"timelock\":0",
               pk_hex(&RT), keys5, pk_hex(&GW), pk_hex(&D1)),
           ERRR("-32060", "REC_ERR_ARG: a bad argument"));
    EXPECT(rpc("recovery.setup",
               "\"root\":\"%s\",\"guardians\":\"%s\",\"gateway\":\"%s\","
               "\"first_device\":\"%s\"",
               pk_hex(&RT), keys5, pk_hex(&GW), pk_hex(&D1)),
           OKR("{\"timelock\":\"259200\",\"rate_window\":\"2592000\",\"rate_max\":3,\"state\":"
               "\"idle\"}"));
    EXPECT(rpc("recovery.setup", "\"root\":\"%s\"", pk_hex(&RT)),
           ERRR("-32060", "SET_UP: recovery is set up already (this run)"));
    /* a second device, signed by the root anchor */
    char *s =
        sign_msg(&RT, "recovery.message", "\"kind\":\"device_add\",\"key\":\"%s\"", pk_hex(&D2));
    EXPECT(rpc("recovery.device_add", "\"key\":\"%s\",\"sig\":\"%s\"", pk_hex(&D2), zero_sig()),
           ERRR("-32060", "REC_ERR_SIG: the signature does not verify"));
    EXPECT(rpc("recovery.device_add", "\"key\":\"%s\",\"sig\":\"%s\"", pk_hex(&D2), s),
           OKR("{\"active\":true}"));
    EXPECT(rpc("recovery.device_add", "\"key\":\"%s\",\"sig\":\"%s\"", pk_hex(&D2), s),
           ERRR("-32060", "REC_DUP: already recorded: nothing changed"));
    s = sign_msg(&RT, "recovery.message", "\"kind\":\"device_revoke\",\"key\":\"%s\"", pk_hex(&D2));
    EXPECT(rpc("recovery.device_revoke", "\"key\":\"%s\",\"sig\":\"%s\"", pk_hex(&D2), s),
           OKR("{\"active\":false}"));
    EXPECT(rpc("recovery.device_revoke", "\"key\":\"%s\",\"sig\":\"%s\"", pk_hex(&D2), s),
           ERRR("-32060", "REC_ERR_DEVICE: not an active device key"));

    /* R1-R4: a claim, three approvals, the old device vetoes, a fourth approval and 14 days */
    EXPECT(rpc("recovery.finalize", "\"now\":1"),
           ERRR("-32060", "REC_ERR_STATE: no pending claim, or not thawing"));
    EXPECT(rpc("recovery.claim", "\"new_key\":\"%s\",\"now\":1000", pk_hex(&D1)),
           ERRR("-32060", "REC_ERR_ARG: a bad argument"));
    EXPECT(rpc("recovery.claim", "\"new_key\":\"%s\",\"now\":1000", pk_hex(&NK)),
           OKR("{\"state\":\"pending\",\"claim\":\"1\",\"approvals\":0,\"vetoed\":false}"));
    EXPECT(rpc("recovery.claim", "\"new_key\":\"%s\",\"now\":1000", pk_hex(&NK)),
           ERRR("-32060", "REC_ERR_BUSY: a claim is already open"));
    HAS(rpc("recovery.status", "\"now\":1000,\"liquid_minor\":100"),
        "\"state\":\"pending\",\"claim\":\"1\",\"approvals\":0,\"needed\":3,\"vetoed\":false,"
        "\"devices\":1,\"can_view\":true,\"can_spend\":false,\"gateway_verified\":false,"
        "\"opened\":\"1000\",\"vbill_spendable_minor\":\"0\",\"liquid_allowance_minor\":\"0\","
        "\"notices\":\"5\"");
    for (uint32_t g = 0; g < 3; g++) {
        s = sign_msg(&Q[g], "recovery.message", "\"kind\":\"approve\"");
        HAS(rpc("recovery.approve", "\"guardian\":%u,\"sig\":\"%s\"", g, s), "\"approvals\"");
    }
    EXPECT(rpc("recovery.approve", "\"guardian\":2,\"sig\":\"%s\"", s),
           ERRR("-32060", "REC_DUP: already recorded: nothing changed"));
    EXPECT(rpc("recovery.approve", "\"guardian\":5,\"sig\":\"%s\"", s),
           ERRR("-32060", "REC_ERR_GUARDIAN: no such guardian"));
    EXPECT(rpc("recovery.approve", "\"guardian\":3,\"sig\":\"%s\"", s),
           ERRR("-32060", "REC_ERR_SIG: the signature does not verify"));
    s = sign_msg(&D1, "recovery.message", "\"kind\":\"veto\"");
    EXPECT(rpc("recovery.veto", "\"device\":\"%s\",\"sig\":\"%s\"", pk_hex(&NK), s),
           ERRR("-32060", "REC_ERR_DEVICE: not an active device key"));
    EXPECT(rpc("recovery.veto", "\"device\":\"%s\",\"sig\":\"%s\"", pk_hex(&D1), s),
           OKR("{\"state\":\"pending\",\"claim\":\"1\",\"approvals\":3,\"vetoed\":true}"));
    EXPECT(rpc("recovery.finalize", "\"now\":1000000"),
           ERRR("-32060", "REC_ERR_QUORUM: not enough guardian approvals (3, or 4 after a veto)"));
    s = sign_msg(&Q[3], "recovery.message", "\"kind\":\"approve\"");
    EXPECT(rpc("recovery.approve", "\"guardian\":3,\"sig\":\"%s\"", s),
           OKR("{\"state\":\"pending\",\"claim\":\"1\",\"approvals\":4,\"vetoed\":true}"));
    EXPECT(rpc("recovery.finalize", "\"now\":1210599"),
           ERRR("-32060", "REC_ERR_TIME: the time lock is not over (14 days after a veto)"));
    EXPECT(rpc("recovery.gateway", "\"sig\":\"%s\"", s),
           ERRR("-32060", "REC_ERR_STATE: no pending claim, or not thawing"));
    EXPECT(rpc("recovery.finalize", "\"now\":1210600"),
           OKR("{\"state\":\"thawing\",\"claim\":\"1\",\"approvals\":4,\"vetoed\":true}"));
    /* R5: 5% a day of liquid funds for 72 hours, V-Bills after 7 days */
    HAS(rpc("recovery.status", "\"now\":1210600,\"liquid_minor\":10000,\"vbill_minor\":50"),
        "\"vbill_spendable_minor\":\"0\",\"liquid_allowance_minor\":\"500\"");
    HAS(rpc("recovery.status", "\"now\":1297000,\"liquid_minor\":10000,\"vbill_minor\":50"),
        "\"devices\":1,\"can_view\":true,\"can_spend\":true,\"gateway_verified\":false,"
        "\"opened\":\"1000\",\"vbill_spendable_minor\":\"0\",\"liquid_allowance_minor\":\"1000\"");
    HAS(rpc("recovery.status", "\"now\":1815400,\"liquid_minor\":10000,\"vbill_minor\":50"),
        "\"vbill_spendable_minor\":\"50\",\"liquid_allowance_minor\":\"10000\"");
    s = sign_msg(&GW, "recovery.message", "\"kind\":\"gateway\"");
    EXPECT(rpc("recovery.gateway", "\"sig\":\"%s\"", s), OKR("{\"gateway_verified\":true}"));
    HAS(rpc("recovery.status", "\"now\":1210600,\"liquid_minor\":10000"),
        "\"gateway_verified\":true,\"opened\":\"1000\",\"vbill_spendable_minor\":\"0\","
        "\"liquid_allowance_minor\":\"10000\"");
    /* the new device cancels a later claim outright */
    EXPECT(rpc("recovery.claim", "\"new_key\":\"%s\",\"now\":1210601", pk_hex(&D2)),
           OKR("{\"state\":\"pending\",\"claim\":\"2\",\"approvals\":0,\"vetoed\":false}"));
    s = sign_msg(&NK, "recovery.message", "\"kind\":\"cancel\"");
    EXPECT(rpc("recovery.cancel", "\"device\":\"%s\",\"sig\":\"%s\"", pk_hex(&D1), s),
           ERRR("-32060", "REC_ERR_DEVICE: not an active device key"));
    EXPECT(rpc("recovery.cancel", "\"device\":\"%s\",\"sig\":\"%s\"", pk_hex(&NK), s),
           OKR("{\"state\":\"thawing\",\"claim\":\"2\",\"approvals\":0,\"vetoed\":false}"));
    EXPECT(rpc("recovery.cancel", "\"device\":\"%s\",\"sig\":\"%s\"", pk_hex(&NK), s),
           ERRR("-32060", "REC_ERR_STATE: no pending claim, or not thawing"));
    /* R1: three claims in 30 days at most (the cancelled ones count) */
    rpc("recovery.claim", "\"new_key\":\"%s\",\"now\":1210602", pk_hex(&D2));
    rpc("recovery.cancel", "\"device\":\"%s\",\"sig\":\"%s\"", pk_hex(&NK),
        sign_msg(&NK, "recovery.message", "\"kind\":\"cancel\""));
    EXPECT(rpc("recovery.claim", "\"new_key\":\"%s\",\"now\":1210603", pk_hex(&D2)),
           ERRR("-32060", "REC_ERR_RATE: too many claims in the window"));
    EXPECT(rpc("recovery.message", "\"kind\":\"steal\""),
           ERRR("-32602", "\\\"kind\\\" must be approve, veto, cancel, gateway, device_add or "
                          "device_revoke"));
    EXPECT(rpc("recovery.message", "\"kind\":\"device_add\""),
           ERRR("-32602", "missing hex parameter \\\"key\\\""));
    EXPECT(rpc("recovery.status", "\"now\":1210603,\"vbill_minor\":-1"),
           ERRR("-32602", "\\\"vbill_minor\\\" must not be negative"));
    HAS(rpc("recovery.status", "\"now\":1210603"),
        "\"notices\":\"55\",\"notices_delivered\":false}");
    rpc("features.set", "\"feature\":\"estate\",\"enabled\":false");
    EXPECT(rpc("recovery.claim", ""),
           ERRR("-32012", "estate is off: start zxvd with --enable estate, or call features.set"));
    HAS(rpc("recovery.status", ""), "\"state\":\"thawing\"");
}

int main(void)
{
    test_signer_init(&FU, 50);
    test_signer_init(&UW, 60);
    test_signer_init(&OW, 70);
    test_signer_init(&G0, 71);
    test_signer_init(&G1, 72);
    test_signer_init(&G2, 73);
    test_signer_init(&AT, 74);
    test_signer_init(&CT, 75);
    test_signer_init(&GW, 76);
    test_signer_init(&RT, 77);
    for (int i = 0; i < 5; i++) test_signer_init(&Q[i], (uint8_t) (80 + i));
    test_signer_init(&D1, 90);
    test_signer_init(&NK, 91);
    test_signer_init(&D2, 92);
    add_groups();
    test_grants();
    test_faraid();
    test_estate();
    test_recovery();
    test_estate_ruled();
    test_grants_limits();
    zxvd_rpc_groups_reset();
    return finish("test_zxvd_estate");
}
