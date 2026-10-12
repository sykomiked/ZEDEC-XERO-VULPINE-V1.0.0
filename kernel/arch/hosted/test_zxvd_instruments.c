/* Copyright (c) 2024-2026 Michael Laurence Curzi and 36N9 Genetics, LLC */
/* SPDX-License-Identifier: Apache-2.0 */
/* test_zxvd_instruments.c — bonds.* and derivatives.* (zxvd_instruments.c)
 * behind zxvd_rpc.c, on the real vbond and vderiv engines and sandbox
 * ledgers, with TEST-ONLY Ed25519 keys: exact replies, refusals, the
 * feature switches and full lifecycles. The mutation harness's test command
 * for zxvd_instruments.c and zxvd_sandbox.c (tests/harness/mutator/targets.json).
 * Key ids equal the ledger owner ids they sign for. */
#include "test_zxvd_sandbox.h"
#include "zxvd_sandbox.h"

static test_signer_t GW, AL, BO, WR, BU, LO, SH;

#define OFF_MSG(f)                                                                                 \
    f " is off: start zxvd with --enable " f ", or call features.set (regulated: test or "         \
      "licensed use only)"

static void test_switches(void)
{
    EXPECT(rpc("bonds.issue", ""), ERRR("-32012", OFF_MSG("bonds")));
    EXPECT(rpc("derivatives.push", ""), ERRR("-32012", OFF_MSG("derivatives")));
    EXPECT(rpc("bonds.status", ""), OKR("{\"set_up\":false,\"keys\":0,\"sandbox\":true}"));
    EXPECT(rpc("derivatives.status", ""),
           OKR("{\"set_up\":false,\"keys\":0,\"leverage\":false,\"sandbox\":true}"));
    EXPECT(rpc("bonds.account", "\"account\":0"),
           ERRR("-32060", "bonds is not set up: call bonds.setup first"));
    EXPECT(rpc("bonds.message", "\"kind\":\"switch\",\"epoch\":1"),
           ERRR("-32060", "bonds is not set up: call bonds.setup first"));
    HAS(rpc("features.get", ""),
        "{\"name\":\"bonds\",\"enabled\":false,\"regulated\":true,\"groups\":[{\"note\":\"V-Bonds");
    HAS(rpc("features.get", ""), "{\"name\":\"derivatives\",\"enabled\":false,\"regulated\":true");
    CHECK(zxvd_rpc_enable("bonds,derivatives") == 0, "enable both");
}

static void test_params(void)
{
    EXPECT(rpc("bonds.account_open", "\"owner\":1,\"asset\":\"vino\""),
           ERRR("-32060", "bonds is not set up: call bonds.setup first"));
    EXPECT(rpc("bonds.fund", "\"account\":3,\"amount_minor\":1"),
           ERRR("-32060", "bonds is not set up: call bonds.setup first"));
    /* the most terms and the highest index pass the reader; the engine refuses the weights */
    EXPECT(rpc("bonds.setup", "\"juris\":840,\"gateway\":900,\"terms\":[1,2,3,4,5,6,7,90],"
                              "\"weights\":[1,1,1,1,1,1,9,8]"),
           ERRR("-32060", "VBOND_ERR_ARG: a bad argument (an account that is not the owner's, a "
                          "zero amount)"));
    EXPECT(rpc("bonds.setup", "\"gateway\":900"),
           ERRR("-32602", "missing integer parameter \\\"juris\\\""));
    EXPECT(rpc("bonds.setup", "\"juris\":-1,\"gateway\":900"),
           ERRR("-32602", "\\\"juris\\\" is out of range"));
    EXPECT(rpc("bonds.setup", "\"juris\":840,\"gateway\":900,\"share_num\":-1"),
           ERRR("-32602", "\\\"share_num\\\" must not be negative"));
    EXPECT(rpc("bonds.setup", "\"juris\":840,\"gateway\":900,\"share_num\":\"1.5\""),
           ERRR("-32602", "\\\"share_num\\\" must be an integer or a decimal string (no fraction "
                          "or exponent)"));
    EXPECT(rpc("bonds.setup", "\"juris\":840,\"gateway\":900,\"terms\":[]"),
           ERRR("-32602", "\\\"terms\\\" must hold 1..8 indices, \\\"weights\\\" as many"));
    EXPECT(rpc("bonds.setup", "\"juris\":840,\"gateway\":900,\"terms\":[1,2,3,4,5,6,7,8,9]"),
           ERRR("-32602", "\\\"terms\\\" must hold 1..8 indices, \\\"weights\\\" as many"));
    EXPECT(rpc("bonds.setup", "\"juris\":840,\"gateway\":900,\"terms\":[13],\"weights\":[1,2]"),
           ERRR("-32602", "\\\"terms\\\" must hold 1..8 indices, \\\"weights\\\" as many"));
    EXPECT(rpc("bonds.setup", "\"juris\":840,\"gateway\":900,\"weights\":[1]"),
           ERRR("-32602", "\\\"terms\\\" must hold 1..8 indices, \\\"weights\\\" as many"));
    EXPECT(rpc("bonds.setup", "\"juris\":840,\"gateway\":900,\"terms\":[91]"),
           ERRR("-32602", "\\\"terms\\\"[0] is out of range"));
    EXPECT(rpc("bonds.setup", "\"juris\":840,\"gateway\":900,\"terms\":[-1]"),
           ERRR("-32602", "\\\"terms\\\"[0] is out of range"));
    EXPECT(rpc("bonds.setup", "\"juris\":840,\"gateway\":900,\"terms\":[\"x\"]"),
           ERRR("-32602", "\\\"terms\\\"[0] must be an integer"));
    EXPECT(rpc("bonds.setup", "\"juris\":840,\"gateway\":900,\"terms\":5"),
           ERRR("-32602", "\\\"terms\\\" must be an array"));
    EXPECT(rpc("bonds.setup", "\"juris\":840,\"gateway\":900,\"terms\":[13],\"weights\":[\"x\"]"),
           ERRR("-32602", "\\\"weights\\\"[0] must be an integer"));
    EXPECT(rpc("bonds.setup", "\"juris\":840,\"gateway\":900,\"weights\":5"),
           ERRR("-32602", "\\\"weights\\\" must be an array"));
    /* the engine's own checks: falling weights, k = 0, share above 1 */
    EXPECT(rpc("bonds.setup", "\"juris\":840,\"gateway\":900,\"terms\":[13,17],\"weights\":[5,4]"),
           ERRR("-32060", "VBOND_ERR_ARG: a bad argument (an account that is not the owner's, a "
                          "zero amount)"));
    EXPECT(rpc("bonds.setup", "\"juris\":840,\"gateway\":900,\"terms\":[0]"),
           ERRR("-32060", "VBOND_ERR_ARG: a bad argument (an account that is not the owner's, a "
                          "zero amount)"));
    EXPECT(rpc("bonds.setup", "\"juris\":840,\"gateway\":900,\"share_num\":3,\"share_den\":2"),
           ERRR("-32060", "VBOND_ERR_ARG: a bad argument (an account that is not the owner's, a "
                          "zero amount)"));
    EXPECT(rpc("bonds.setup", "\"juris\":840,\"gateway\":900,\"terms\":[13],\"weights\":[0]"),
           ERRR("-32060", "VBOND_ERR_ARG: a bad argument (an account that is not the owner's, a "
                          "zero amount)"));
    CHECK(has(rpc("bonds.status", ""), "\"set_up\":false"), "a refused setup sets nothing up");
}

static void test_keys(void)
{
    EXPECT(rpc("bonds.key", "\"key\":900,\"public_key\":\"%s\"", pk_hex(&GW)),
           OKR("{\"key\":900,\"added\":true,\"keys\":1}"));
    EXPECT(rpc("bonds.key", "\"key\":900,\"public_key\":\"%s\"", pk_hex(&GW)),
           OKR("{\"key\":900,\"added\":false,\"keys\":1}"));
    EXPECT(rpc("bonds.key", "\"key\":900,\"public_key\":\"%s\"", pk_hex(&AL)),
           ERRR("-32060", "KEY_TAKEN: this key id has another public key"));
    EXPECT(rpc("bonds.key", "\"key\":1,\"public_key\":\"00\""),
           ERRR("-32602", "\\\"public_key\\\" must be 32 bytes of hex"));
    EXPECT(rpc("bonds.key", "\"public_key\":\"00\""),
           ERRR("-32602", "missing integer parameter \\\"key\\\""));
    EXPECT(rpc("bonds.key", "\"key\":1"),
           ERRR("-32602", "missing hex parameter \\\"public_key\\\""));
    rpc("bonds.key", "\"key\":1,\"public_key\":\"%s\"", pk_hex(&AL));
    EXPECT(rpc("bonds.key", "\"key\":2,\"public_key\":\"%s\"", pk_hex(&BO)),
           OKR("{\"key\":2,\"added\":true,\"keys\":3}"));
    /* the table fills at 32 keys */
    for (uint32_t i = 3; i < 32; i++)
        rpc("bonds.key", "\"key\":%u,\"public_key\":\"%s\"", 100 + i, pk_hex(&AL));
    EXPECT(rpc("bonds.key", "\"key\":7777,\"public_key\":\"%s\"", pk_hex(&AL)),
           ERRR("-32060", "KEYS_FULL: the key table is full"));
    EXPECT(rpc("bonds.key", "\"key\":131,\"public_key\":\"%s\"", pk_hex(&AL)),
           OKR("{\"key\":131,\"added\":false,\"keys\":32}"));
}

static void test_bonds(void)
{
    EXPECT(rpc("bonds.setup", "\"juris\":840,\"gateway\":900"),
           OKR("{\"juris\":840,\"gateway\":900,\"lock_account\":2,\"terms\":[{\"k\":13,\"ticks\":"
               "\"233\",\"weight\":\"233\"},{\"k\":17,\"ticks\":\"1597\",\"weight\":\"1597\"},{"
               "\"k\":21,\"ticks\":\"10946\",\"weight\":\"10946\"}],\"share_num\":\"1\","
               "\"share_den\":\"2\",\"issuing_enabled\":false}"));
    EXPECT(rpc("bonds.setup", "\"juris\":840,\"gateway\":900"),
           ERRR("-32060", "SET_UP: bonds are set up already (this run)"));
    /* accounts: 0, 1 the issuers, 2 the lock */
    EXPECT(rpc("bonds.account_open", "\"owner\":1,\"asset\":\"vbill\""),
           OKR("{\"account\":3,\"owner\":1,\"asset\":\"vbill\"}"));
    rpc("bonds.account_open", "\"owner\":1,\"asset\":\"vino\"");  /* 4 alice payee */
    rpc("bonds.account_open", "\"owner\":2,\"asset\":\"vino\"");  /* 5 bob payee */
    rpc("bonds.account_open", "\"owner\":2,\"asset\":\"vbill\""); /* 6 bob vbill */
    rpc("bonds.account_open", "\"owner\":5,\"asset\":\"vino\"");  /* 7 revenue source */
    EXPECT(rpc("bonds.account_open", "\"owner\":6,\"asset\":\"vino\""),
           OKR("{\"account\":8,\"owner\":6,\"asset\":\"vino\"}"));
    EXPECT(rpc("bonds.account_open", "\"owner\":6,\"asset\":\"gold\""),
           ERRR("-32602", "unknown asset \\\"gold\\\""));
    EXPECT(rpc("bonds.account_open", "\"owner\":4294967040,\"asset\":\"vino\""),
           ERRR("-32602", "\\\"owner\\\" must be below 4294967040 (the rest are zxvd's own)"));
    CHECK(
        has(rpc("bonds.account_open", "\"owner\":4294967039,\"asset\":\"vino\""), "\"account\":9"),
        "the last user owner id is fine");
    EXPECT(rpc("bonds.fund", "\"account\":3,\"amount_minor\":\"1000\""),
           OKR("{\"account\":3,\"balance_minor\":\"1000\",\"sandbox\":true}"));
    EXPECT(rpc("bonds.fund", "\"account\":2,\"amount_minor\":1"),
           ERRR("-32602", "\\\"account\\\" must be a fundable sandbox account"));
    EXPECT(rpc("bonds.fund", "\"account\":0,\"amount_minor\":1"),
           ERRR("-32602", "\\\"account\\\" must be a fundable sandbox account"));
    EXPECT(rpc("bonds.fund", "\"account\":99,\"amount_minor\":1"),
           ERRR("-32602", "\\\"account\\\" must be a fundable sandbox account"));
    EXPECT(rpc("bonds.fund", "\"account\":3,\"amount_minor\":0"),
           ERRR("-32060", "PAY_ERR: the sandbox ledger refused the amount"));
    rpc("bonds.fund", "\"account\":7,\"amount_minor\":10000");
    EXPECT(rpc("bonds.account", "\"account\":3"),
           OKR("{\"account\":3,\"owner\":1,\"asset\":\"vbill\",\"balance_minor\":\"1000\","
               "\"credit_minor\":\"0\",\"sandbox\":true}"));
    EXPECT(rpc("bonds.account", "\"account\":1"),
           OKR("{\"account\":1,\"owner\":4294967281,\"asset\":\"vbill\",\"balance_minor\":\"0\","
               "\"credit_minor\":\"1000\",\"sandbox\":true}"));
    EXPECT(rpc("bonds.account", "\"account\":99"), ERRR("-32602", "no account 99"));

    /* B0: off until the gateway signs */
    const char *issue = "\"owner\":1,\"vbill_account\":3,\"payee\":4,\"qty\":400,\"k\":13,"
                        "\"now\":1000";
    EXPECT(rpc("bonds.issue", "%s", issue),
           ERRR("-32060", "VBOND_ERR_DISABLED: issuing is off until a signed jurisdiction "
                          "manifest turns it on (bonds.switch)"));
    EXPECT(rpc("bonds.switch", "\"epoch\":1,\"sig\":\"%s\"", zero_sig()),
           ERRR("-32060", "VBOND_ERR_SIG: the signature does not verify"));
    EXPECT(rpc("bonds.switch", "\"epoch\":1,\"sig\":\"00\""),
           ERRR("-32602", "\\\"sig\\\" must be 64 bytes of hex"));
    char *s = sign_msg(&GW, "bonds.message", "\"kind\":\"switch\",\"epoch\":1");
    EXPECT(rpc("bonds.switch", "\"epoch\":1,\"sig\":\"%s\"", s),
           OKR("{\"issuing_enabled\":true,\"epoch\":\"1\"}"));
    EXPECT(rpc("bonds.switch", "\"epoch\":1,\"sig\":\"%s\"", s),
           ERRR("-32060", "VBOND_ERR_EPOCH: the manifest epoch is not above the last"));
    EXPECT(rpc("bonds.message", "\"kind\":\"swap\""),
           ERRR("-32602", "\\\"kind\\\" must be \\\"switch\\\" or \\\"transfer\\\""));
    HAS(rpc("bonds.message", "\"kind\":\"switch\",\"epoch\":2,\"enable\":false"),
        "\"message\":\"7a78762f76626f6e642d73772f763148030000000200000000000000\",\"bytes\":28,"
        "\"scheme\":\"ed25519\"}");

    /* B1 */
    EXPECT(rpc("bonds.issue", "%s", issue),
           OKR("{\"serial\":\"1\",\"owner\":1,\"payee\":4,\"qty\":\"400\",\"k\":13,\"start\":"
               "\"1000\",\"maturity\":\"1233\",\"nonce\":\"0\",\"last_paid_minor\":\"0\"}"));
    HAS(rpc("bonds.issue", "\"owner\":1,\"vbill_account\":3,\"payee\":4,\"qty\":100,\"k\":17,"
                           "\"now\":1000"),
        "{\"serial\":\"2\",\"owner\":1,\"payee\":4,\"qty\":\"100\",\"k\":17,\"start\":\"1000\","
        "\"maturity\":\"2597\"");
    EXPECT(rpc("bonds.issue", "\"owner\":1,\"vbill_account\":3,\"payee\":4,\"qty\":1,\"k\":14"),
           ERRR("-32060", "VBOND_ERR_TERM: the term is not one of the allowed Fibonacci indices"));
    EXPECT(rpc("bonds.issue", "\"owner\":1,\"vbill_account\":3,\"payee\":4,\"qty\":501,\"k\":13"),
           ERRR("-32060", "VBOND_ERR_FUNDS: the payer does not hold enough"));
    EXPECT(rpc("bonds.issue", "\"owner\":2,\"vbill_account\":3,\"payee\":4,\"qty\":1,\"k\":13"),
           ERRR("-32060", "VBOND_ERR_ARG: a bad argument (an account that is not the owner's, a "
                          "zero amount)"));
    EXPECT(rpc("bonds.issue", "\"owner\":1,\"vbill_account\":3,\"payee\":4,\"qty\":1"),
           ERRR("-32602", "missing integer parameter \\\"k\\\""));

    /* B2: alice hands bond 1 to bob */
    EXPECT(rpc("bonds.message", "\"kind\":\"transfer\",\"serial\":9,\"to\":2,\"payee\":5"),
           ERRR("-32060", "VBOND_ERR_NOT_FOUND: no live bond has that serial"));
    s = sign_msg(&AL, "bonds.message", "\"kind\":\"transfer\",\"serial\":1,\"to\":2,\"payee\":5");
    EXPECT(rpc("bonds.transfer", "\"serial\":1,\"from\":2,\"to\":2,\"payee\":5,\"sig\":\"%s\"", s),
           ERRR("-32060", "VBOND_ERR_OWNER: not the bond's current owner"));
    EXPECT(rpc("bonds.transfer", "\"serial\":1,\"from\":1,\"to\":2,\"payee\":5,\"sig\":\"%s\"",
               sign_last(&BO)),
           ERRR("-32060", "VBOND_ERR_SIG: the signature does not verify"));
    EXPECT(rpc("bonds.transfer", "\"serial\":1,\"from\":1,\"to\":2,\"payee\":5,\"sig\":\"%s\"", s),
           OKR("{\"serial\":\"1\",\"owner\":2,\"payee\":5,\"qty\":\"400\",\"k\":13,\"start\":"
               "\"1000\",\"maturity\":\"1233\",\"nonce\":\"1\",\"last_paid_minor\":\"0\"}"));
    EXPECT(rpc("bonds.transfer", "\"serial\":1,\"from\":2,\"to\":2,\"payee\":5,\"sig\":\"%s\"", s),
           ERRR("-32060", "VBOND_ERR_SIG: the signature does not verify"));

    /* B4: realised revenue, then the whole pool to the live bonds */
    EXPECT(rpc("bonds.distribute", "\"pool\":8,\"now\":1000"),
           ERRR("-32060", "VBOND_ERR_EMPTY: the pool is empty: nothing to pay"));
    EXPECT(rpc("bonds.credit", "\"src\":7,\"pool\":8,\"amount_minor\":1000,\"now\":1000"),
           OKR("{\"credited_minor\":\"1000\",\"pool_minor\":\"1000\",\"premium\":false}"));
    EXPECT(rpc("bonds.credit", "\"src\":7,\"pool\":8,\"price\":150,\"floor\":100,\"qty\":7"),
           OKR("{\"credited_minor\":\"175\",\"pool_minor\":\"1175\",\"premium\":true}"));
    EXPECT(rpc("bonds.credit", "\"src\":7,\"pool\":8,\"price\":90,\"floor\":100,\"qty\":7"),
           OKR("{\"credited_minor\":\"0\",\"pool_minor\":\"1175\",\"premium\":true}"));
    EXPECT(rpc("bonds.credit", "\"src\":7,\"pool\":8,\"price\":150,\"qty\":7"),
           ERRR("-32602", "missing integer parameter \\\"floor\\\""));
    EXPECT(rpc("bonds.credit", "\"src\":7,\"pool\":5,\"amount_minor\":1"),
           ERRR("-32060", "VBOND_ERR_ARG: a bad argument (an account that is not the owner's, a "
                          "zero amount)"));
    EXPECT(rpc("bonds.credit", "\"src\":7,\"pool\":8,\"amount_minor\":99999"),
           ERRR("-32060", "VBOND_ERR_FUNDS: the payer does not hold enough"));
    /* U = 400*233 + 100*1597 = 252900; 1175*93200/252900 = 433.0 r 5100, 1175*159700/252900
     * = 741.97: 433 + 741 = 1174, the unit left to the larger remainder (bond 2) */
    EXPECT(rpc("bonds.distribute", "\"pool\":8,\"now\":999"),
           ERRR("-32060", "VBOND_ERR_NOHOLD: no live bond to pay"));
    EXPECT(rpc("bonds.distribute", "\"pool\":8,\"now\":1000"),
           OKR("{\"pool_minor\":\"1175\",\"units\":\"252900\",\"paid_minor\":\"1175\",\"payees\":"
               "2,\"postings\":1,\"paid\":[{\"serial\":\"1\",\"payee\":5,\"minor\":\"433\"},{"
               "\"serial\":\"2\",\"payee\":4,\"minor\":\"742\"}]}"));
    HAS(rpc("bonds.account", "\"account\":5"), "\"balance_minor\":\"433\"");

    /* B3: no early exit; only the owner */
    const char *red = "\"serial\":1,\"owner\":2,\"vbill_account\":6";
    EXPECT(rpc("bonds.redeem", "%s,\"now\":1232", red),
           ERRR("-32060", "VBOND_ERR_IMMATURE: the bond has not matured (no early redemption)"));
    EXPECT(rpc("bonds.redeem", "\"serial\":1,\"owner\":1,\"vbill_account\":3,\"now\":1233"),
           ERRR("-32060", "VBOND_ERR_OWNER: not the bond's current owner"));
    EXPECT(rpc("bonds.redeem", "%s,\"now\":1233", red),
           OKR("{\"serial\":\"1\",\"returned_qty\":\"400\",\"vbill_account\":6}"));
    EXPECT(rpc("bonds.redeem", "%s,\"now\":1233", red),
           ERRR("-32060", "VBOND_ERR_NOT_FOUND: no live bond has that serial"));
    HAS(rpc("bonds.account", "\"account\":6"), "\"balance_minor\":\"400\"");

    /* X4 / X6: off answers -32012, the state stays */
    rpc("features.set", "\"feature\":\"bonds\",\"enabled\":false");
    EXPECT(rpc("bonds.redeem", "%s", red), ERRR("-32012", OFF_MSG("bonds")));
    EXPECT(rpc("bonds.status", ""),
           OKR("{\"set_up\":true,\"keys\":32,\"issuing_enabled\":true,\"juris\":840,\"gateway\":"
               "900,\"switch_epoch\":\"1\",\"locked_qty\":\"100\",\"postings\":\"6\",\"bonds\":[{"
               "\"serial\":\"2\",\"owner\":1,\"payee\":4,\"qty\":\"100\",\"k\":17,\"start\":"
               "\"1000\",\"maturity\":\"2597\",\"nonce\":\"0\",\"last_paid_minor\":\"742\"}],"
               "\"check\":true,\"sandbox\":true}"));
    rpc("features.set", "\"feature\":\"bonds\"");
    /* the gateway turns issuing off again; leaving still works */
    s = sign_msg(&GW, "bonds.message", "\"kind\":\"switch\",\"epoch\":2,\"enable\":false");
    EXPECT(rpc("bonds.switch", "\"epoch\":2,\"enable\":false,\"sig\":\"%s\"", s),
           OKR("{\"issuing_enabled\":false,\"epoch\":\"2\"}"));
    HAS(rpc("bonds.issue", "%s", issue), "VBOND_ERR_DISABLED");
    EXPECT(rpc("bonds.redeem", "\"serial\":2,\"owner\":1,\"vbill_account\":3,\"now\":2597"),
           OKR("{\"serial\":\"2\",\"returned_qty\":\"100\",\"vbill_account\":3}"));
    HAS(rpc("bonds.status", ""), "\"locked_qty\":\"0\",\"postings\":\"7\",\"bonds\":[],"
                                 "\"check\":true");
}

static void test_bond_terms(void)
{
    /* custom terms on a fresh group */
    zxvd_rpc_groups_reset();
    zxvd_rpc_add_group(zxvd_bonds_group());
    zxvd_rpc_enable("bonds");
    EXPECT(rpc("bonds.setup",
               "\"juris\":1,\"gateway\":2,\"terms\":[5,8],\"weights\":[1,3],\"share_num\":"
               "\"2\",\"share_den\":3"),
           OKR("{\"juris\":1,\"gateway\":2,\"lock_account\":2,\"terms\":[{\"k\":5,\"ticks\":\"5\","
               "\"weight\":\"1\"},{\"k\":8,\"ticks\":\"21\",\"weight\":\"3\"}],\"share_num\":"
               "\"2\",\"share_den\":\"3\",\"issuing_enabled\":false}"));
    EXPECT(rpc("bonds.setup", "\"juris\":1,\"gateway\":2,\"terms\":[13]"),
           ERRR("-32060", "SET_UP: bonds are set up already (this run)"));
    EXPECT(rpc("bonds.status", ""),
           OKR("{\"set_up\":true,\"keys\":0,\"issuing_enabled\":false,\"juris\":1,\"gateway\":2,"
               "\"switch_epoch\":\"0\",\"locked_qty\":\"0\",\"postings\":\"0\",\"bonds\":[],"
               "\"check\":true,\"sandbox\":true}"));
}

static void test_derivatives(void)
{
    zxvd_rpc_groups_reset();
    zxvd_rpc_add_group(zxvd_bonds_group());
    zxvd_rpc_add_group(zxvd_derivatives_group());
    zxvd_rpc_enable("derivatives");
    EXPECT(rpc("bonds.status", ""), OKR("{\"set_up\":false,\"keys\":0,\"sandbox\":true}"));
    EXPECT(rpc("derivatives.account_open", "\"owner\":1,\"asset\":\"vino\""),
           ERRR("-32060", "derivatives is not set up: call derivatives.setup first"));
    EXPECT(rpc("derivatives.fund", "\"account\":5,\"amount_minor\":1"),
           ERRR("-32060", "derivatives is not set up: call derivatives.setup first"));
    EXPECT(rpc("derivatives.account", "\"account\":5"),
           ERRR("-32060", "derivatives is not set up: call derivatives.setup first"));
    EXPECT(rpc("derivatives.setup", "\"juris\":840,\"gateway\":900,\"first_epoch\":0"),
           ERRR("-32060", "VDERIV_ERR_ARG: a bad argument (an unusable account, the same party "
                          "twice, a zero size)"));
    EXPECT(rpc("derivatives.setup", "\"juris\":840,\"gateway\":900"),
           OKR("{\"juris\":840,\"gateway\":900,\"call_escrow\":1,\"long_escrow\":2,"
               "\"short_escrow\":3,\"reserve\":4,\"first_epoch\":\"1\",\"opening_enabled\":false,"
               "\"leverage\":false}"));
    EXPECT(rpc("derivatives.setup", "\"juris\":840,\"gateway\":900"),
           ERRR("-32060", "SET_UP: derivatives are set up already (this run)"));
    rpc("derivatives.key", "\"key\":900,\"public_key\":\"%s\"", pk_hex(&GW));
    const test_signer_t *who[4] = {&WR, &BU, &LO, &SH};
    for (uint32_t i = 0; i < 4; i++) {
        rpc("derivatives.key", "\"key\":%u,\"public_key\":\"%s\"", 11 + i, pk_hex(who[i]));
        HAS(rpc("derivatives.account_open", "\"owner\":%u,\"asset\":\"vino\"", 11 + i),
            "\"asset\":\"vino\"");
        rpc("derivatives.fund", "\"account\":%u,\"amount_minor\":1000000", 5 + i);
    }
    HAS(rpc("derivatives.account", "\"account\":8"), "\"owner\":14,\"asset\":\"vino\","
                                                     "\"balance_minor\":\"1000000\"");
    HAS(rpc("derivatives.account_open", "\"owner\":1,\"asset\":\"vbill\""), "unknown asset");

    /* V0: the index, in order */
    EXPECT(rpc("derivatives.push", "\"epoch\":2,\"price\":5,\"qty\":100"),
           ERRR("-32060", "VDERIV_ERR_EPOCH: an epoch out of order, an expiry already passed, or "
                          "no index yet"));
    EXPECT(rpc("derivatives.push", "\"epoch\":1,\"price\":5,\"qty\":100,\"now\":10"),
           OKR("{\"epoch\":\"1\",\"index\":\"100\",\"moved\":0,\"stopped\":0,\"fixed\":0,"
               "\"fees_minor\":\"0\"}"));

    /* V1: a capped call, writer 11 (acct 5), buyer 12 (acct 6) */
    const char *call = "\"writer_key\":11,\"writer\":5,\"buyer_key\":12,\"buyer\":6,\"strike\":100,"
                       "\"cap\":150,\"size\":10,\"expiry\":3,\"premium\":20000";
    char *ws = sign_msg(&WR, "derivatives.message", "\"kind\":\"call\",%s", call);
    char *bs = sign_msg(&BU, "derivatives.message", "\"kind\":\"call\",%s", call);
    EXPECT(
        rpc("derivatives.call_open", "%s,\"writer_sig\":\"%s\",\"buyer_sig\":\"%s\"", call, ws, bs),
        ERRR("-32060", "VDERIV_ERR_DISABLED: opening is off until a signed jurisdiction "
                       "manifest turns it on (derivatives.switch)"));
    EXPECT(rpc("derivatives.switch", "\"epoch\":1,\"sig\":\"%s\"", ws),
           ERRR("-32060", "VDERIV_ERR_SIG: a signature does not verify"));
    char *g = sign_msg(&GW, "derivatives.message", "\"kind\":\"switch\",\"epoch\":1");
    EXPECT(rpc("derivatives.switch", "\"epoch\":1,\"sig\":\"%s\"", g),
           OKR("{\"opening_enabled\":true,\"epoch\":\"1\",\"leverage\":false}"));
    EXPECT(rpc("derivatives.switch", "\"epoch\":1,\"sig\":\"%s\"", g),
           ERRR("-32060", "VDERIV_ERR_EPOCH: an epoch out of order, an expiry already passed, or "
                          "no index yet"));
    EXPECT(
        rpc("derivatives.call_open", "%s,\"writer_sig\":\"%s\",\"buyer_sig\":\"%s\"", call, ws, ws),
        ERRR("-32060", "VDERIV_ERR_SIG: a signature does not verify"));
    EXPECT(rpc("derivatives.call_open", "%s,\"nonce\":1,\"writer_sig\":\"%s\",\"buyer_sig\":\"%s\"",
               call, ws, bs),
           ERRR("-32060", "VDERIV_ERR_NONCE: the terms' nonce is not the next one"));
    EXPECT(rpc("derivatives.call_open", "%s,\"writer_sig\":\"%s\"", call, ws),
           ERRR("-32602", "missing hex parameter \\\"buyer_sig\\\""));
    EXPECT(rpc("derivatives.call_open", "%s,\"writer_sig\":\"%s\",\"buyer_sig\":\"%s\",\"now\":11",
               call, ws, bs),
           OKR("{\"id\":0,\"nonce\":\"0\",\"lock_minor\":\"500\",\"premium_minor\":\"20000\","
               "\"fee_minor\":\"17\",\"expiry\":\"3\"}"));
    HAS(rpc("derivatives.account", "\"account\":5"), "\"balance_minor\":\"1019483\"");
    EXPECT(rpc("derivatives.call_settle", "\"id\":0"),
           ERRR("-32060", "VDERIV_ERR_STATE: wrong position state (not fixed yet, or not open)"));

    /* V2: a perp, long 13 (acct 7), short 14 (acct 8), size 10, band 5 */
    const char *perp = "\"long_key\":13,\"long_account\":7,\"short_key\":14,\"short_account\":8,"
                       "\"size\":10,\"band\":5,\"lock_long\":100,\"lock_short\":100";
    char *ls = sign_msg(&LO, "derivatives.message", "\"kind\":\"perp\",%s", perp);
    char *ss = sign_msg(&SH, "derivatives.message", "\"kind\":\"perp\",%s", perp);
    HAS(rpc("derivatives.message", "\"kind\":\"perp\",%s,\"nonce\":7", perp),
        "\"bytes\":72,\"scheme\":\"ed25519\"");
    EXPECT(rpc("derivatives.perp_open", "%s,\"long_sig\":\"%s\",\"short_sig\":\"%s\",\"now\":12",
               perp, ls, ss),
           OKR("{\"id\":0,\"nonce\":\"1\",\"ref_index\":\"100\",\"opened_epoch\":\"1\"}"));
    EXPECT(rpc("derivatives.perp_open", "%s,\"nonce\":1,\"long_sig\":\"%s\",\"short_sig\":\"%s\"",
               perp, ls, ss),
           ERRR("-32060", "VDERIV_ERR_NONCE: the terms' nonce is not the next one"));
    /* epoch 2: up 3, long wins 30 (fee 0) */
    EXPECT(rpc("derivatives.push", "\"epoch\":2,\"price\":5,\"qty\":103"),
           OKR("{\"epoch\":\"2\",\"index\":\"103\",\"moved\":1,\"stopped\":0,\"fixed\":0,"
               "\"fees_minor\":\"0\"}"));
    /* epoch 3: up 27, clamped to the band: 50 to long, short holds 20 < 50 and stops; the
     * call fixes at 130: payoff 10 * 30 = 300 */
    EXPECT(rpc("derivatives.push", "\"epoch\":3,\"price\":5,\"qty\":130"),
           OKR("{\"epoch\":\"3\",\"index\":\"130\",\"moved\":1,\"stopped\":1,\"fixed\":1,"
               "\"fees_minor\":\"0\"}"));
    HAS(rpc("derivatives.status", ""),
        "\"perps\":[{\"id\":0,\"state\":\"stopped\",\"long\":7,\"short\":8,\"size\":\"10\","
        "\"band\":\"5\",\"long_minor\":\"180\",\"short_minor\":\"20\",\"ref_index\":\"130\","
        "\"fees_minor\":\"0\"}],\"check\":true");
    EXPECT(rpc("derivatives.call_settle", "\"id\":0,\"now\":13"),
           OKR("{\"id\":0,\"fixing\":\"130\",\"payoff_minor\":\"300\",\"fee_minor\":\"0\","
               "\"to_buyer_minor\":\"300\",\"to_writer_minor\":\"200\"}"));
    EXPECT(rpc("derivatives.call_settle", "\"id\":0"),
           ERRR("-32060", "VDERIV_ERR_STATE: wrong position state (not fixed yet, or not open)"));
    EXPECT(rpc("derivatives.perp_close", "\"id\":0,\"side\":\"middle\""),
           ERRR("-32602", "\\\"side\\\" must be \\\"long\\\" or \\\"short\\\""));
    EXPECT(rpc("derivatives.perp_close", "\"id\":0,\"side\":\"short\""),
           OKR("{\"id\":0,\"to_long_minor\":\"180\",\"to_short_minor\":\"20\"}"));
    EXPECT(rpc("derivatives.perp_close", "\"id\":0,\"side\":\"short\""),
           ERRR("-32060", "VDERIV_ERR_STATE: wrong position state (not fixed yet, or not open)"));
    EXPECT(rpc("derivatives.perp_close", "\"id\":32,\"side\":\"short\""),
           ERRR("-32060", "VDERIV_ERR_STATE: wrong position state (not fixed yet, or not open)"));
    EXPECT(rpc("derivatives.perp_close", "\"id\":99,\"side\":\"short\""),
           ERRR("-32060", "VDERIV_ERR_STATE: wrong position state (not fixed yet, or not open)"));

    /* an open perp closes only with that side's signature */
    const char *p2 = "\"long_key\":13,\"long_account\":7,\"short_key\":14,\"short_account\":8,"
                     "\"size\":1,\"band\":1,\"lock_long\":10,\"lock_short\":10";
    ls = sign_msg(&LO, "derivatives.message", "\"kind\":\"perp\",%s", p2);
    ss = sign_msg(&SH, "derivatives.message", "\"kind\":\"perp\",%s", p2);
    HAS(rpc("derivatives.perp_open", "%s,\"long_sig\":\"%s\",\"short_sig\":\"%s\"", p2, ls, ss),
        "{\"id\":0,\"nonce\":\"2\",\"ref_index\":\"130\",\"opened_epoch\":\"3\"}");
    EXPECT(rpc("derivatives.perp_close", "\"id\":0,\"side\":\"long\""),
           ERRR("-32060", "VDERIV_ERR_SIG: a signature does not verify"));
    char *cs =
        sign_msg(&LO, "derivatives.message", "\"kind\":\"close\",\"id\":0,\"side\":\"long\"");
    EXPECT(rpc("derivatives.perp_close", "\"id\":0,\"side\":\"short\",\"sig\":\"%s\"", cs),
           ERRR("-32060", "VDERIV_ERR_SIG: a signature does not verify"));
    EXPECT(rpc("derivatives.perp_close", "\"id\":0,\"side\":\"long\",\"sig\":\"12\""),
           ERRR("-32602", "\\\"sig\\\" must be 64 bytes of hex"));
    EXPECT(rpc("derivatives.perp_close", "\"id\":0,\"side\":\"long\",\"sig\":\"%s\"", cs),
           OKR("{\"id\":0,\"to_long_minor\":\"10\",\"to_short_minor\":\"10\"}"));
    EXPECT(rpc("derivatives.message", "\"kind\":\"close\",\"id\":0,\"side\":\"long\""),
           ERRR("-32060", "VDERIV_ERR_STATE: wrong position state (not fixed yet, or not open)"));
    EXPECT(
        rpc("derivatives.message", "\"kind\":\"spot\""),
        ERRR("-32602",
             "\\\"kind\\\" must be \\\"switch\\\", \\\"call\\\", \\\"perp\\\" or \\\"close\\\""));
    /* a call whose expiry has passed */
    HAS(rpc("derivatives.call_open",
            "\"nonce\":3,\"writer_key\":11,\"writer\":5,\"buyer_key\":12,"
            "\"buyer\":6,\"strike\":1,\"cap\":2,\"size\":1,\"expiry\":3,"
            "\"premium\":0,\"writer_sig\":\"%s\",\"buyer_sig\":\"%s\"",
            zero_sig(), zero_sig()),
        "VDERIV_ERR_EPOCH");
    EXPECT(
        rpc("derivatives.status", ""),
        OKR("{\"set_up\":true,\"keys\":5,\"leverage\":false,\"opening_enabled\":true,\"juris\":"
            "840,\"gateway\":900,\"switch_epoch\":\"1\",\"next_nonce\":\"3\",\"pushed\":\"3\","
            "\"last_epoch\":\"3\",\"fees_minor\":\"17\",\"calls\":[],\"perps\":[],\"check\":true,"
            "\"sandbox\":true}"));
    HAS(rpc("derivatives.account", "\"account\":4"), "\"balance_minor\":\"17\"");
    rpc("features.set", "\"feature\":\"derivatives\",\"enabled\":false");
    EXPECT(rpc("derivatives.push", "\"epoch\":4,\"price\":1,\"qty\":1"),
           ERRR("-32012", OFF_MSG("derivatives")));
    HAS(rpc("derivatives.status", ""), "\"pushed\":\"3\"");
}

/* the status of a call while it is open, and a push with a fee */
static void test_deriv_fees(void)
{
    zxvd_rpc_groups_reset();
    zxvd_rpc_add_group(zxvd_derivatives_group());
    zxvd_rpc_enable("all");
    rpc("derivatives.setup", "\"juris\":7,\"gateway\":900,\"first_epoch\":\"5\"");
    rpc("derivatives.key", "\"key\":900,\"public_key\":\"%s\"", pk_hex(&GW));
    rpc("derivatives.key", "\"key\":13,\"public_key\":\"%s\"", pk_hex(&LO));
    rpc("derivatives.key", "\"key\":14,\"public_key\":\"%s\"", pk_hex(&SH));
    rpc("derivatives.account_open", "\"owner\":13,\"asset\":\"vino\""); /* 5 */
    rpc("derivatives.account_open", "\"owner\":14,\"asset\":\"vino\""); /* 6 */
    rpc("derivatives.fund", "\"account\":5,\"amount_minor\":\"100000000\"");
    rpc("derivatives.fund", "\"account\":6,\"amount_minor\":\"100000000\"");
    char *g = sign_msg(&GW, "derivatives.message", "\"kind\":\"switch\",\"epoch\":3");
    HAS(rpc("derivatives.switch", "\"epoch\":3,\"sig\":\"%s\"", g), "\"opening_enabled\":true");
    const char *p = "\"long_key\":13,\"long_account\":5,\"short_key\":14,\"short_account\":6,"
                    "\"size\":10000,\"band\":1000,\"lock_long\":\"30000000\",\"lock_short\":"
                    "\"30000000\"";
    char *ls = sign_msg(&LO, "derivatives.message", "\"kind\":\"perp\",%s", p);
    char *ss = sign_msg(&SH, "derivatives.message", "\"kind\":\"perp\",%s", p);
    EXPECT(rpc("derivatives.perp_open", "%s,\"long_sig\":\"%s\",\"short_sig\":\"%s\"", p, ls, ss),
           ERRR("-32060", "VDERIV_ERR_EPOCH: an epoch out of order, an expiry already passed, or "
                          "no index yet"));
    rpc("derivatives.push", "\"epoch\":5,\"price\":1,\"qty\":500");
    HAS(rpc("derivatives.perp_open", "%s,\"long_sig\":\"%s\",\"short_sig\":\"%s\"", p, ls, ss),
        "\"ref_index\":\"500\",\"opened_epoch\":\"5\"");
    /* down 400: short wins 10000 * 400 = 4000000, fee floor(4000000 * 0.0008889) = 3555 */
    EXPECT(rpc("derivatives.push", "\"epoch\":6,\"price\":1,\"qty\":100"),
           OKR("{\"epoch\":\"6\",\"index\":\"100\",\"moved\":1,\"stopped\":0,\"fixed\":0,"
               "\"fees_minor\":\"3555\"}"));
    HAS(rpc("derivatives.status", ""),
        "\"state\":\"open\",\"long\":5,\"short\":6,\"size\":\"10000\",\"band\":\"1000\","
        "\"long_minor\":\"26000000\",\"short_minor\":\"33996445\",\"ref_index\":\"100\","
        "\"fees_minor\":\"3555\"}],\"check\":true");
    /* a call that is open shows in the status with its lock */
    rpc("derivatives.key", "\"key\":11,\"public_key\":\"%s\"", pk_hex(&WR));
    rpc("derivatives.key", "\"key\":12,\"public_key\":\"%s\"", pk_hex(&BU));
    rpc("derivatives.account_open", "\"owner\":11,\"asset\":\"vino\""); /* 7 */
    rpc("derivatives.account_open", "\"owner\":12,\"asset\":\"vino\""); /* 8 */
    rpc("derivatives.fund", "\"account\":7,\"amount_minor\":1000");
    rpc("derivatives.fund", "\"account\":8,\"amount_minor\":1000");
    const char *call = "\"writer_key\":11,\"writer\":7,\"buyer_key\":12,\"buyer\":8,\"strike\":10,"
                       "\"cap\":20,\"size\":5,\"expiry\":8,\"premium\":30";
    char *ws = sign_msg(&WR, "derivatives.message", "\"kind\":\"call\",%s", call);
    char *bs = sign_msg(&BU, "derivatives.message", "\"kind\":\"call\",%s", call);
    HAS(rpc("derivatives.call_open", "%s,\"writer_sig\":\"%s\",\"buyer_sig\":\"%s\"", call, ws, bs),
        "{\"id\":0,\"nonce\":\"1\",\"lock_minor\":\"50\",\"premium_minor\":\"30\",\"fee_minor\":"
        "\"0\",\"expiry\":\"8\"}");
    /* a second call and a second perp: the status lists every live position */
    ws = sign_msg(&WR, "derivatives.message", "\"kind\":\"call\",%s", call);
    bs = sign_msg(&BU, "derivatives.message", "\"kind\":\"call\",%s", call);
    HAS(rpc("derivatives.call_open", "%s,\"writer_sig\":\"%s\",\"buyer_sig\":\"%s\"", call, ws, bs),
        "{\"id\":1,\"nonce\":\"2\"");
    ls = sign_msg(&LO, "derivatives.message", "\"kind\":\"perp\",%s", p);
    ss = sign_msg(&SH, "derivatives.message", "\"kind\":\"perp\",%s", p);
    HAS(rpc("derivatives.perp_open", "%s,\"long_sig\":\"%s\",\"short_sig\":\"%s\"", p, ls, ss),
        "{\"id\":1,");
    HAS(rpc("derivatives.status", ""),
        "\"fixing\":\"0\"},{\"id\":1,\"state\":\"open\",\"writer\":7");
    HAS(rpc("derivatives.status", ""),
        "\"fees_minor\":\"3555\"},{\"id\":1,\"state\":\"open\",\"long\":5");
    HAS(rpc("derivatives.status", ""),
        "\"calls\":[{\"id\":0,\"state\":\"open\",\"writer\":7,\"buyer\":8,\"strike\":\"10\","
        "\"cap\":\"20\",\"size\":\"5\",\"expiry\":\"8\",\"lock_minor\":\"50\",\"fixing\":\"0\"},{");
    rpc("derivatives.push", "\"epoch\":7,\"price\":1,\"qty\":100");
    rpc("derivatives.push", "\"epoch\":8,\"price\":1,\"qty\":12");
    HAS(rpc("derivatives.status", ""), "\"state\":\"fixed\"");
    EXPECT(rpc("derivatives.call_settle", "\"id\":0"),
           OKR("{\"id\":0,\"fixing\":\"12\",\"payoff_minor\":\"10\",\"fee_minor\":\"0\","
               "\"to_buyer_minor\":\"10\",\"to_writer_minor\":\"40\"}"));
    EXPECT(rpc("derivatives.call_settle", "\"id\":32"),
           ERRR("-32060", "VDERIV_ERR_ARG: a bad argument (an unusable account, the same party "
                          "twice, a zero size)"));
}

int main(void)
{
    test_signer_init(&GW, 9);
    test_signer_init(&AL, 1);
    test_signer_init(&BO, 2);
    test_signer_init(&WR, 11);
    test_signer_init(&BU, 12);
    test_signer_init(&LO, 13);
    test_signer_init(&SH, 14);
    zxvd_rpc_groups_reset();
    CHECK(zxvd_rpc_add_group(zxvd_bonds_group()) == 0, "add bonds");
    CHECK(zxvd_rpc_add_group(zxvd_derivatives_group()) == 0, "add derivatives");
    CHECK(zxvd_rpc_add_group(zxvd_bonds_group()) == -1, "a group is added once");
    test_switches();
    test_params();
    test_keys();
    test_bonds();
    test_bond_terms();
    test_derivatives();
    test_deriv_fees();
    zxvd_rpc_groups_reset();
    return finish("test_zxvd_instruments");
}
