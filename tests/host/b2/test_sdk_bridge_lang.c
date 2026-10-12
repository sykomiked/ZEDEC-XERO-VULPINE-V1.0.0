/* Copyright (c) 2024-2026 Michael Laurence Curzi and 36N9 Genetics, LLC */
/* SPDX-License-Identifier: Apache-2.0 */
/* test_sdk_bridge_lang.c: unit tests for kernel/src/sdk_bridge/sdk_bridge_lang.c.
 * The financial, wallet, mesh and crypto-bridge subsystems are replaced by
 * recording fakes so every argument the dispatcher unpacks from the IR is
 * checked. Covers sb_lang_execute's gates (NULL, inactive, coverage boundary,
 * attestation, malformed op field, unknown op), the op-id sequence, the
 * metadata padding of the output IR, every reachable op (capability gate,
 * missing subsystem, field-count bounds, argument unpacking, result), the
 * ops that need more fields than an IR holds, and the context helpers
 * (create, destroy, has, grant, self-audit, self-heal). */
#include <stdio.h>
#include <string.h>
#include "sdk_bridge_lang.h"
#include "crypto_bridge.h"

static int fails = 0;
static int checks = 0;
#define CHECK(cond)                                                                                \
    do {                                                                                           \
        checks++;                                                                                  \
        if (!(cond)) {                                                                             \
            printf("  FAIL line %d: %s\n", __LINE__, #cond);                                       \
            fails++;                                                                               \
        }                                                                                          \
    } while (0)

/* ---- fakes ---------------------------------------------------------------- */
static int n_calls;  /* calls into any fake */
static const void *f_obj; /* subsystem pointer passed */
static uint64_t f_a, f_b, f_c, f_d;
static double f_x, f_y;
static int32_t f_rc;
static double f_val;

void oc_zero_ir(oc_ir_t *ir)
{
    memset(ir, 0, sizeof *ir);
}

int32_t ff_create_account(financial_fabric_t *fabric, const char *name, const word168_t *owner_id,
                          const uint64_t initial_balances[9])
{
    (void) name;
    (void) owner_id;
    (void) initial_balances;
    n_calls++;
    f_obj = fabric;
    return f_rc;
}

int32_t ff_create_derivative(financial_fabric_t *fabric, uint32_t account_id,
                             m5_capital_form_t underlying_form, const surplus_real_t *notional,
                             const surplus_real_t *strike, uint64_t expiry_tick,
                             const uint8_t backing_cid[32], const m5_lpres_proof_t *backing_proof,
                             const uint8_t jurisdiction_cid[32], bool treaty_backed)
{
    (void) account_id;
    (void) underlying_form;
    (void) notional;
    (void) strike;
    (void) expiry_tick;
    (void) backing_cid;
    (void) backing_proof;
    (void) jurisdiction_cid;
    (void) treaty_backed;
    n_calls++;
    f_obj = fabric;
    return f_rc;
}

int32_t fm_post_quote(fm_book_t *book, uint32_t inst_id, surplus_real_t price, uint32_t phase_tick)
{
    n_calls++;
    f_obj = book;
    f_a = inst_id;
    f_x = price;
    f_b = phase_tick;
    return f_rc;
}

surplus_real_t fm_value_position(const fm_book_t *book, uint32_t inst_id, surplus_real_t qty)
{
    n_calls++;
    f_obj = book;
    f_a = inst_id;
    f_x = qty;
    return f_val;
}

int32_t fm_open_position(fm_book_t *book, uint32_t acct, uint32_t inst_id, surplus_real_t qty,
                         surplus_real_t cost_basis)
{
    n_calls++;
    f_obj = book;
    f_a = acct;
    f_b = inst_id;
    f_x = qty;
    f_y = cost_basis;
    return f_rc;
}

int32_t fm_settle(fm_book_t *book, uint32_t from_acct, uint32_t to_acct, surplus_real_t amount,
                  uint16_t fee_bps)
{
    n_calls++;
    f_obj = book;
    f_a = from_acct;
    f_b = to_acct;
    f_x = amount;
    f_c = fee_bps;
    return f_rc;
}

uint32_t cw_wallet_create(crypto_wallet_system_t *sys, const char *label)
{
    (void) label;
    n_calls++;
    f_obj = sys;
    return (uint32_t) f_rc;
}

int32_t cw_file_add(crypto_wallet_system_t *sys, uint32_t wallet_id, cw_file_type_t type,
                    const uint8_t *payload, uint32_t size, const char *path)
{
    (void) wallet_id;
    (void) type;
    (void) payload;
    (void) size;
    (void) path;
    n_calls++;
    f_obj = sys;
    return f_rc;
}

int32_t mn_create_network(mesh_net_t *mn, const char *name, mn_net_access_t access,
                          const word168_t *creator_id, uint32_t creator_trust)
{
    (void) name;
    (void) access;
    (void) creator_id;
    (void) creator_trust;
    n_calls++;
    f_obj = mn;
    return f_rc;
}

int32_t mn_send_data(mesh_net_t *mn, uint32_t route_id, uint64_t data_size, uint64_t current_cycle)
{
    n_calls++;
    f_obj = mn;
    f_a = route_id;
    f_b = data_size;
    f_c = current_cycle;
    return f_rc;
}

uint32_t bridge_create(bridge_registry_t *reg, chain_id_t source, chain_id_t dest,
                       bridge_direction_t dir, surplus_real_t amount, uint64_t source_addr,
                       uint64_t dest_addr, token_standard_t token_type, contract_lang_t lang,
                       const char *token_symbol, const char *contract_addr)
{
    (void) source;
    (void) dest;
    (void) dir;
    (void) amount;
    (void) source_addr;
    (void) dest_addr;
    (void) token_type;
    (void) lang;
    (void) token_symbol;
    (void) contract_addr;
    n_calls++;
    f_obj = reg;
    return (uint32_t) f_rc;
}

/* ---- fixtures ------------------------------------------------------------- */
static sdk_bridge_t B;
static financial_fabric_t FF;
static crypto_wallet_system_t CW;
static mesh_net_t MN;
static sb_lang_context_t L;
static oc_ir_t IN, OUT;

static void reset_fakes(void)
{
    n_calls = 0;
    f_obj = NULL;
    f_a = f_b = f_c = f_d = 0xDEAD;
    f_x = f_y = -1.0;
    f_rc = 0;
    f_val = 0.0;
}

/* active context, attestation TRUE, coverage 6*1.5*2/(2*3) = 3.0 */
static void fresh_ctx(void)
{
    memset(&L, 0, sizeof L);
    L.active = true;
    L.attestation = LPRES_STATE_TRUE;
    L.m5.omega = 6;
    L.m5.r = 1.5;
    L.m5.ell = 2.0;
    L.m5.phi = 2.0;
    L.m5.chi = 3;
}

static void give(sb_capability_t cap)
{
    L.capabilities[L.num_capabilities++] = cap;
}

static rat_t ri(int64_t v)
{
    return rat_from_int(v);
}

/* builds IN: op + n integer/rational args */
static void op_in(sb_lang_op_t op, const rat_t *args, uint32_t n)
{
    CHECK(sb_lang_build_op(&IN, op, args, n) == OC_OK);
}

static int32_t run(void)
{
    memset(&OUT, 0, sizeof OUT);
    return sb_lang_execute(&L, &IN, &OUT);
}

static bool is_q32(rat_t r, double v)
{
    return r.valid && r.den == 4294967296LL && r.num == (int64_t) (v * 4294967296.0);
}

/* the metadata the dispatcher appends: attestation, coverage, op id */
static bool meta_ok(lpres_state_t att, double cov, uint32_t op_id)
{
    bool ok = OUT.num_fields == 5;
    for (uint32_t i = 0; i < 5; i++)
        ok = ok && OUT.fields[i].type == OC_TYPE_RATIONAL && OUT.fields[i].scale == 0;
    ok = ok && rat_eq(OUT.fields[2].num, ri(att)) && is_q32(OUT.fields[3].num, cov) &&
         rat_eq(OUT.fields[4].num, ri(op_id));
    return ok;
}

static uint32_t next_id = 1; /* the dispatcher's op-id counter starts at 1 */

/* fills the stack below the caller with 0xFF so a bridge context that is
 * not fully cleared shows up as garbage (sandboxed, callbacks); effective
 * only when ASan runs without its fake stack */
static void __attribute__((noinline)) dirty_stack(void)
{
    volatile uint8_t buf[16384];
    for (size_t i = 0; i < sizeof buf; i++) buf[i] = 0xFF;
}

/* ---- tests ---------------------------------------------------------------- */
static void test_gates(void)
{
    sb_lang_set_bridge(NULL);
    reset_fakes();
    fresh_ctx();
    op_in(SB_OP_RAILS_ISSUE_CARD, NULL, 0);
    CHECK(sb_lang_execute(NULL, &IN, &OUT) == SB_LANG_ERR_ARG);
    CHECK(sb_lang_execute(&L, NULL, &OUT) == SB_LANG_ERR_ARG);
    CHECK(sb_lang_execute(&L, &IN, NULL) == SB_LANG_ERR_ARG);
    L.active = false;
    CHECK(run() == SB_LANG_ERR_CAP);
    CHECK(OUT.num_fields == 0);
    CHECK(L.coverage_ratio == 0.0); /* nothing computed before the gate */
    L.active = true;

    /* attestation NEITHER is refused, after coverage was computed */
    L.attestation = LPRES_STATE_NEITHER;
    CHECK(run() == SB_LANG_ERR_AUDIT);
    CHECK(L.coverage_ratio == 3.0);
    CHECK(OUT.num_fields == 0);
    L.attestation = LPRES_STATE_TRUE;

    /* malformed op field: no fields, text, non-integer, invalid */
    IN.num_fields = 0;
    CHECK(run() == SB_LANG_ERR_ARG);
    op_in(SB_OP_RAILS_ISSUE_CARD, NULL, 0);
    IN.fields[0].type = OC_TYPE_TEXT;
    CHECK(run() == SB_LANG_ERR_ARG);
    op_in(SB_OP_RAILS_ISSUE_CARD, NULL, 0);
    IN.fields[0].num = rat_make(1, 2);
    CHECK(run() == SB_LANG_ERR_ARG);
    IN.fields[0].num.valid = false;
    IN.fields[0].num.den = 1;
    CHECK(run() == SB_LANG_ERR_ARG);
    CHECK(OUT.num_fields == 0);

    /* first op id handed out is 1; coverage via a zero denominator = 100 */
    L.m5.chi = 0;
    give(SB_CAP_CAPITAL_TRANSFER);
    op_in(SB_OP_RAILS_ISSUE_CARD, NULL, 0);
    CHECK(run() == SB_LANG_OK);
    CHECK(L.coverage_ratio == 100.0);
    CHECK(rat_eq(OUT.fields[1].num, ri(0xFFFFFFFFLL)));
    CHECK(meta_ok(LPRES_STATE_TRUE, 100.0, next_id++));
    CHECK(n_calls == 0);

    /* unknown op: ERR_ARG, output untouched, but an op id is consumed */
    op_in((sb_lang_op_t) 0x1234, NULL, 0);
    CHECK(run() == SB_LANG_ERR_ARG);
    CHECK(OUT.num_fields == 0);
    next_id++;
    op_in(SB_OP_FF_SETTLE_DERIVATIVE, NULL, 0); /* declared but not dispatched */
    CHECK(run() == SB_LANG_ERR_ARG);
    next_id++;
    op_in(SB_OP_RAILS_ISSUE_CARD, NULL, 0);
    CHECK(run() == SB_LANG_OK);
    CHECK(meta_ok(LPRES_STATE_TRUE, 100.0, next_id++));

    /* coverage gate with a bridge: exactly at the minimum (18/10) passes */
    memset(&B, 0, sizeof B);
    sb_init(&B, NULL, NULL, NULL);
    sb_lang_set_bridge(&B);
    fresh_ctx();
    give(SB_CAP_CAPITAL_TRANSFER);
    L.m5.omega = 18;
    L.m5.r = 1.0;
    L.m5.ell = 1.0;
    L.m5.phi = 10.0;
    L.m5.chi = 1;
    CHECK(run() == SB_LANG_OK);
    CHECK(L.coverage_ratio == 1.8);
    CHECK(meta_ok(LPRES_STATE_TRUE, 1.8, next_id++));
    L.m5.omega = 17; /* 1.7 */
    CHECK(run() == SB_LANG_ERR_COVERAGE);
    CHECK(OUT.num_fields == 0);
    CHECK(L.coverage_ratio == 1.7);
    /* coverage is checked before attestation */
    L.attestation = LPRES_STATE_NEITHER;
    CHECK(run() == SB_LANG_ERR_COVERAGE);
    /* a coverage failure consumes no op id */
    fresh_ctx();
    give(SB_CAP_CAPITAL_TRANSFER);
    CHECK(run() == SB_LANG_OK);
    CHECK(meta_ok(LPRES_STATE_TRUE, 3.0, next_id++));
}

static void test_metadata(void)
{
    sb_lang_set_bridge(NULL);
    fresh_ctx();
    /* an op that fails before writing: caller's output is padded/stamped */
    op_in(SB_OP_FF_POST_QUOTE, NULL, 0); /* no capability -> ERR_CAP */
    memset(&OUT, 0, sizeof OUT);
    OUT.num_fields = 1;
    OUT.fields[0].type = OC_TYPE_TEXT;
    OUT.fields[1].type = OC_TYPE_TEXT;
    OUT.fields[1].num = ri(42);
    OUT.fields[1].scale = 9;
    CHECK(sb_lang_execute(&L, &IN, &OUT) == SB_LANG_ERR_CAP);
    CHECK(OUT.fields[0].type == OC_TYPE_TEXT);
    CHECK(OUT.fields[1].type == OC_TYPE_RATIONAL && rat_eq(OUT.fields[1].num, rat_zero()) &&
          OUT.fields[1].scale == 0);
    CHECK(OUT.fields[5].type == OC_TYPE_NONE && OUT.fields[5].num.valid == false);
    OUT.fields[0].type = OC_TYPE_RATIONAL;
    CHECK(meta_ok(LPRES_STATE_TRUE, 3.0, next_id++));
    CHECK(OUT.fields[2].scale == 0 && OUT.fields[3].scale == 0 && OUT.fields[4].scale == 0);

    /* four fields: only the fifth is padded */
    memset(&OUT, 0, sizeof OUT);
    OUT.num_fields = 4;
    OUT.fields[3].type = OC_TYPE_TEXT;
    OUT.fields[3].scale = 7;
    OUT.fields[4].scale = 7;
    CHECK(sb_lang_execute(&L, &IN, &OUT) == SB_LANG_ERR_CAP);
    CHECK(OUT.num_fields == 5);
    CHECK(OUT.fields[3].type == OC_TYPE_TEXT && OUT.fields[3].scale == 7);
    CHECK(OUT.fields[4].type == OC_TYPE_RATIONAL && OUT.fields[4].scale == 0);
    CHECK(OUT.fields[1].type == OC_TYPE_NONE);
    CHECK(rat_eq(OUT.fields[4].num, ri(next_id++)));

    /* five or more fields: count kept, nothing padded, 2..4 stamped */
    memset(&OUT, 0, sizeof OUT);
    OUT.num_fields = 7;
    OUT.fields[5].num = ri(55);
    CHECK(sb_lang_execute(&L, &IN, &OUT) == SB_LANG_ERR_CAP);
    CHECK(OUT.num_fields == 7);
    CHECK(OUT.fields[4].type == OC_TYPE_NONE && OUT.fields[5].type == OC_TYPE_NONE);
    CHECK(rat_eq(OUT.fields[5].num, ri(55)));
    CHECK(rat_eq(OUT.fields[2].num, ri(LPRES_STATE_TRUE)) && is_q32(OUT.fields[3].num, 3.0) &&
          rat_eq(OUT.fields[4].num, ri(next_id++)));
    memset(&OUT, 0, sizeof OUT);
    OUT.num_fields = 5;
    CHECK(sb_lang_execute(&L, &IN, &OUT) == SB_LANG_ERR_CAP);
    CHECK(OUT.num_fields == 5 && OUT.fields[1].type == OC_TYPE_NONE &&
          OUT.fields[4].type == OC_TYPE_NONE);
    CHECK(rat_eq(OUT.fields[4].num, ri(next_id++)));
}

/* gate checks shared by every op that needs cap and a subsystem */
static void op_gates(sb_lang_op_t op, sb_capability_t cap, uint32_t need, void (*bind)(bool))
{
    rat_t args[OC_MAX_FIELDS] = {0};
    for (int i = 0; i < OC_MAX_FIELDS; i++) args[i] = ri(i + 1);
    /* missing capability */
    if (cap) {
        bind(true);
        fresh_ctx();
        give(SB_CAP_MEDIA_STREAM);
        op_in(op, args, need - 1);
        reset_fakes();
        CHECK(run() == SB_LANG_ERR_CAP);
        CHECK(n_calls == 0 && OUT.num_fields == 0);
        next_id++;
    }
    fresh_ctx();
    if (cap) give(cap);
    /* missing subsystem */
    bind(false);
    op_in(op, args, need - 1);
    reset_fakes();
    CHECK(run() == SB_LANG_ERR_INTERNAL);
    CHECK(n_calls == 0 && OUT.num_fields == 0);
    next_id++;
    bind(true);
    /* one field short */
    op_in(op, args, need - 2);
    CHECK(run() == SB_LANG_ERR_ARG);
    CHECK(n_calls == 0 && OUT.num_fields == 0);
    next_id++;
    /* a caller-set count beyond the array */
    op_in(op, args, OC_MAX_FIELDS - 1);
    IN.num_fields = OC_MAX_FIELDS + 1;
    CHECK(run() == SB_LANG_ERR_ARG);
    CHECK(n_calls == 0);
    next_id++;
    /* exactly the count needed, and the full array */
    op_in(op, args, need - 1);
    CHECK(run() == SB_LANG_OK && n_calls == 1);
    next_id++;
    op_in(op, args, OC_MAX_FIELDS - 1);
    CHECK(run() == SB_LANG_OK && n_calls == 2);
    next_id++;
}

static void bind_ff(bool on)
{
    memset(&B, 0, sizeof B);
    sb_init(&B, &CW, &MN, on ? &FF : NULL);
    sb_lang_set_bridge(&B);
}
static void bind_mesh(bool on)
{
    memset(&B, 0, sizeof B);
    sb_init(&B, &CW, on ? &MN : NULL, &FF);
    sb_lang_set_bridge(&B);
}
static void bind_none(bool on)
{
    if (on) {
        memset(&B, 0, sizeof B);
        sb_init(&B, &CW, &MN, &FF);
        sb_lang_set_bridge(&B);
    } else {
        sb_lang_set_bridge(NULL);
    }
}

static void test_ops(void)
{
    /* the gates of each reachable op */
    op_gates(SB_OP_FF_POST_QUOTE, SB_CAP_CAPITAL_TRANSFER, 5, bind_ff);
    op_gates(SB_OP_FF_VALUE_POSITION, (sb_capability_t) 0, 4, bind_ff);
    op_gates(SB_OP_FF_OPEN_POSITION, SB_CAP_CAPITAL_TRANSFER, 7, bind_ff);
    op_gates(SB_OP_FF_SETTLE, SB_CAP_CAPITAL_TRANSFER, 6, bind_ff);
    op_gates(SB_OP_MN_SEND_MESSAGE, SB_CAP_NETWORK_MESH, 4, bind_mesh);
    /* no bridge at all */
    op_gates(SB_OP_FF_POST_QUOTE, SB_CAP_CAPITAL_TRANSFER, 5, bind_none);
    op_gates(SB_OP_MN_SEND_MESSAGE, SB_CAP_NETWORK_MESH, 4, bind_none);

    /* value_position needs no capability */
    bind_ff(true);
    fresh_ctx();
    reset_fakes();
    f_val = 12.25;
    rat_t a[7];
    a[0] = ri(77);
    a[1] = rat_make(-3, 4);
    a[2] = ri(0);
    op_in(SB_OP_FF_VALUE_POSITION, a, 3);
    CHECK(run() == SB_LANG_OK);
    CHECK(n_calls == 1 && f_obj == &FF.markets && f_a == 77 && f_x == -0.75);
    CHECK(is_q32(OUT.fields[1].num, 12.25));
    CHECK(meta_ok(LPRES_STATE_TRUE, 3.0, next_id++));
    /* an invalid quantity reaches the market as zero */
    a[1].valid = false;
    op_in(SB_OP_FF_VALUE_POSITION, a, 3);
    CHECK(run() == SB_LANG_OK && f_x == 0.0);
    next_id++;

    /* post_quote: inst, price, tick */
    give(SB_CAP_CAPITAL_TRANSFER);
    reset_fakes();
    f_rc = -17;
    a[0] = ri(9);
    a[1] = rat_make(5, 2);
    a[2] = ri(123456);
    a[3] = ri(0);
    op_in(SB_OP_FF_POST_QUOTE, a, 4);
    CHECK(run() == SB_LANG_OK);
    CHECK(n_calls == 1 && f_obj == &FF.markets && f_a == 9 && f_x == 2.5 && f_b == 123456);
    CHECK(rat_eq(OUT.fields[1].num, ri(-17)));
    CHECK(meta_ok(LPRES_STATE_TRUE, 3.0, next_id++));

    /* open_position: acct, inst, qty, cost */
    reset_fakes();
    f_rc = 31;
    a[0] = ri(4);
    a[1] = ri(8);
    a[2] = rat_make(3, 2);
    a[3] = rat_make(-1, 8);
    a[4] = ri(0);
    a[5] = ri(0);
    op_in(SB_OP_FF_OPEN_POSITION, a, 6);
    CHECK(run() == SB_LANG_OK);
    CHECK(n_calls == 1 && f_obj == &FF.markets && f_a == 4 && f_b == 8 && f_x == 1.5 &&
          f_y == -0.125);
    CHECK(rat_eq(OUT.fields[1].num, ri(31)));
    CHECK(meta_ok(LPRES_STATE_TRUE, 3.0, next_id++));

    /* settle: from, to, amount, fee (16-bit) */
    reset_fakes();
    f_rc = 2;
    a[0] = ri(11);
    a[1] = ri(12);
    a[2] = rat_make(7, 4);
    a[3] = ri(65536 + 25);
    a[4] = ri(0);
    op_in(SB_OP_FF_SETTLE, a, 5);
    CHECK(run() == SB_LANG_OK);
    CHECK(n_calls == 1 && f_obj == &FF.markets && f_a == 11 && f_b == 12 && f_x == 1.75 &&
          f_c == 25);
    CHECK(rat_eq(OUT.fields[1].num, ri(2)));
    CHECK(meta_ok(LPRES_STATE_TRUE, 3.0, next_id++));

    /* send_message: route, size (64-bit), cycle */
    bind_mesh(true);
    fresh_ctx();
    give(SB_CAP_NETWORK_MESH);
    reset_fakes();
    f_rc = 6;
    a[0] = ri(3);
    a[1] = ri(5000000000LL);
    a[2] = ri(99);
    op_in(SB_OP_MN_SEND_MESSAGE, a, 3);
    CHECK(run() == SB_LANG_OK);
    CHECK(n_calls == 1 && f_obj == &MN && f_a == 3 && f_b == 5000000000ULL && f_c == 99);
    CHECK(rat_eq(OUT.fields[1].num, ri(6)));
    CHECK(meta_ok(LPRES_STATE_TRUE, 3.0, next_id++));

    /* rails: capital-transfer gate; placeholder id, no subsystem needed */
    sb_lang_set_bridge(NULL);
    fresh_ctx();
    op_in(SB_OP_RAILS_ISSUE_CARD, NULL, 0);
    CHECK(run() == SB_LANG_ERR_CAP && OUT.num_fields == 0);
    next_id++;
    give(SB_CAP_NETWORK_MESH);
    give(SB_CAP_CAPITAL_TRANSFER);
    CHECK(run() == SB_LANG_OK);
    CHECK(rat_eq(OUT.fields[1].num, ri(0xFFFFFFFFLL)));
    CHECK(meta_ok(LPRES_STATE_TRUE, 3.0, next_id++));

    /* ops whose argument list is longer than an IR: always refused, even
     * with a full IR; the capability and subsystem gates come first */
    static const struct {
        sb_lang_op_t op;
        sb_capability_t cap;
        int sub; /* 0 ff, 1 wallet, 2 mesh, 3 ff (bridge registry) */
    } big[] = {
        {SB_OP_FF_CREATE_ACCOUNT, SB_CAP_CAPITAL_TRANSFER, 0},
        {SB_OP_FF_CREATE_DERIVATIVE, SB_CAP_DERIVATIVES_TRADE, 0},
        {SB_OP_CW_CREATE_WALLET, SB_CAP_CRYPTO_SIGN, 1},
        {SB_OP_CW_STORE_FILE, SB_CAP_STORAGE_PERSIST, 1},
        {SB_OP_MN_CREATE_NETWORK, SB_CAP_NETWORK_MESH, 2},
        {SB_OP_BRIDGE_CREATE, SB_CAP_CRYPTO_SIGN, 3},
    };
    rat_t full[OC_MAX_FIELDS - 1];
    for (int i = 0; i < OC_MAX_FIELDS - 1; i++) full[i] = ri(1);
    for (uint32_t k = 0; k < sizeof big / sizeof big[0]; k++) {
        memset(&B, 0, sizeof B);
        sb_init(&B, &CW, &MN, &FF);
        sb_lang_set_bridge(&B);
        reset_fakes();
        fresh_ctx();
        op_in(big[k].op, full, OC_MAX_FIELDS - 1);
        CHECK(run() == SB_LANG_ERR_CAP); /* no capability */
        next_id++;
        /* a capability other ops use is not enough */
        give(big[k].cap == SB_CAP_CRYPTO_SIGN ? SB_CAP_CAPITAL_TRANSFER : SB_CAP_CRYPTO_SIGN);
        CHECK(run() == SB_LANG_ERR_CAP);
        next_id++;
        give(big[k].cap);
        CHECK(run() == SB_LANG_ERR_ARG);
        next_id++;
        IN.num_fields = OC_MAX_FIELDS + 1;
        CHECK(run() == SB_LANG_ERR_ARG);
        next_id++;
        /* the subsystem it needs is missing */
        if (big[k].sub == 0 || big[k].sub == 3) B.financial = NULL;
        if (big[k].sub == 1) B.crypto_wallet = NULL;
        if (big[k].sub == 2) B.mesh = NULL;
        CHECK(run() == SB_LANG_ERR_INTERNAL);
        next_id++;
        sb_lang_set_bridge(NULL);
        CHECK(run() == SB_LANG_ERR_INTERNAL);
        next_id++;
        CHECK(n_calls == 0 && OUT.num_fields == 0);
    }
}

static void test_audit_heal_ops(void)
{
    /* no bridge: internal error */
    sb_lang_set_bridge(NULL);
    fresh_ctx();
    op_in(SB_OP_SELF_AUDIT, NULL, 0);
    CHECK(run() == SB_LANG_ERR_INTERNAL && OUT.num_fields == 0);
    next_id++;
    op_in(SB_OP_SELF_HEAL, NULL, 0);
    CHECK(run() == SB_LANG_ERR_INTERNAL && OUT.num_fields == 0);
    next_id++;

    memset(&B, 0, sizeof B);
    sb_init(&B, NULL, NULL, NULL);
    sb_lang_set_bridge(&B);

    /* passing audit: code 0, attestation TRUE */
    fresh_ctx();
    give(SB_CAP_CAPITAL_TRANSFER);
    op_in(SB_OP_SELF_AUDIT, NULL, 0);
    CHECK(run() == SB_LANG_OK);
    CHECK(rat_eq(OUT.fields[0].num, ri(0)));
    CHECK(rat_eq(OUT.fields[1].num, ri(LPRES_STATE_TRUE)));
    CHECK(meta_ok(LPRES_STATE_TRUE, 3.0, next_id++));
    CHECK(B.stats.total_audits == 1);
    CHECK(L.num_capabilities == 1 && L.capabilities[0] == SB_CAP_CAPITAL_TRANSFER);

    /* failing audit: a held capability needs 2.5, coverage is 2.4 */
    fresh_ctx();
    L.m5.omega = 12;
    L.m5.r = 1.0;
    L.m5.ell = 1.0;
    L.m5.phi = 5.0;
    L.m5.chi = 1;
    give(SB_CAP_TREATY_TOKENIZE);
    CHECK(run() == SB_LANG_OK);
    CHECK(rat_eq(OUT.fields[0].num, ri(-1)));
    CHECK(rat_eq(OUT.fields[1].num, ri(LPRES_STATE_BOTH)));
    CHECK(L.attestation == LPRES_STATE_BOTH);
    CHECK(meta_ok(LPRES_STATE_BOTH, 2.4, next_id++));
    CHECK(B.stats.total_audits == 2);

    /* heal through the dispatcher on a TRUE context: no change */
    fresh_ctx();
    op_in(SB_OP_SELF_HEAL, NULL, 0);
    CHECK(run() == SB_LANG_OK);
    CHECK(rat_eq(OUT.fields[0].num, ri(0)));
    CHECK(rat_eq(OUT.fields[1].num, ri(LPRES_STATE_TRUE)));
    CHECK(meta_ok(LPRES_STATE_TRUE, 3.0, next_id++));
    CHECK(B.stats.total_healings == 0);
}

static void test_helpers(void)
{
    memset(&B, 0, sizeof B);
    sb_init(&B, NULL, NULL, NULL);
    word168_t id;
    memset(&id, 0x3C, sizeof id);
    sb_lang_context_t out;
    memset(&out, 0x77, sizeof out);
    CHECK(sb_lang_create_context(NULL, &id, &out) == SB_LANG_ERR_ARG);
    CHECK(sb_lang_create_context(&B, NULL, &out) == SB_LANG_ERR_ARG);
    CHECK(sb_lang_create_context(&B, &id, NULL) == SB_LANG_ERR_ARG);
    /* the bridge's context pool is not wired: always an internal error */
    CHECK(sb_lang_create_context(&B, &id, &out) == SB_LANG_ERR_INTERNAL);
    B.initialized = false;
    CHECK(sb_lang_create_context(&B, &id, &out) == SB_LANG_ERR_INTERNAL);
    CHECK(out.process_id == 0x77777777u); /* untouched */
    B.initialized = true;

    fresh_ctx();
    give(SB_CAP_MEDIA_STREAM);
    give(SB_CAP_CRYPTO_KEM);
    CHECK(!sb_lang_has_capability(NULL, SB_CAP_MEDIA_STREAM));
    CHECK(sb_lang_has_capability(&L, SB_CAP_MEDIA_STREAM));
    CHECK(sb_lang_has_capability(&L, SB_CAP_CRYPTO_KEM));
    CHECK(!sb_lang_has_capability(&L, SB_CAP_CRYPTO_SIGN));
    L.num_capabilities = 1;
    CHECK(!sb_lang_has_capability(&L, SB_CAP_CRYPTO_KEM));
    sb_lang_destroy_context(NULL);
    sb_lang_destroy_context(&L);
    CHECK(!L.active && L.num_capabilities == 0);
    CHECK(!sb_lang_has_capability(&L, SB_CAP_MEDIA_STREAM));

    /* grant into an empty context: slot 0 is copied back */
    fresh_ctx();
    L.coverage_ratio = 9.0;
    dirty_stack();
    CHECK(sb_lang_grant_capability(&B, &L, SB_CAP_IDENTITY_CREDENTIAL) == 0);
    CHECK(L.num_capabilities == 1 && L.capabilities[0] == SB_CAP_IDENTITY_CREDENTIAL);
    dirty_stack();
    CHECK(sb_lang_grant_capability(&B, &L, SB_CAP_HSM_OPERATION) == -1);
    CHECK(L.num_capabilities == 1);

    /* grant goes through the bridge's checks and copies the result back */
    fresh_ctx();
    CHECK(sb_lang_grant_capability(NULL, &L, SB_CAP_MEDIA_STREAM) == SB_LANG_ERR_ARG);
    CHECK(sb_lang_grant_capability(&B, NULL, SB_CAP_MEDIA_STREAM) == SB_LANG_ERR_ARG);
    L.coverage_ratio = 2.0;
    give(SB_CAP_CAPITAL_TRANSFER);
    CHECK(sb_lang_grant_capability(&B, &L, SB_CAP_MEDIA_STREAM) == 0);
    CHECK(L.num_capabilities == 2 && L.capabilities[1] == SB_CAP_MEDIA_STREAM);
    CHECK(L.capabilities[0] == SB_CAP_CAPITAL_TRANSFER);
    CHECK(L.attestation == LPRES_STATE_TRUE && L.active && L.coverage_ratio == 2.0);
    CHECK(L.m5.omega == 6 && L.m5.chi == 3 && L.m5.r == 1.5);
    /* below the capability's coverage (2.5) */
    CHECK(sb_lang_grant_capability(&B, &L, SB_CAP_TREATY_TOKENIZE) == -1);
    CHECK(L.num_capabilities == 2);
    /* HSM capabilities: a language context is never sandboxed */
    L.coverage_ratio = 9.0;
    CHECK(sb_lang_grant_capability(&B, &L, SB_CAP_CRYPTO_SIGN) == -1);
    CHECK(sb_lang_grant_capability(&B, &L, SB_CAP_GOVERNANCE_VOTE) == 0);
    CHECK(L.num_capabilities == 3);
    /* attestation is carried into the bridge */
    L.attestation = LPRES_STATE_NEITHER;
    CHECK(sb_lang_grant_capability(&B, &L, SB_CAP_STORAGE_PERSIST) == -1);
    CHECK(L.num_capabilities == 3);
    /* a capability already held is reported held */
    CHECK(sb_lang_grant_capability(&B, &L, SB_CAP_GOVERNANCE_VOTE) == 0);
    CHECK(L.num_capabilities == 3);

    /* self-audit */
    oc_ir_t rep;
    fresh_ctx();
    CHECK(sb_lang_self_audit(NULL, &L, &rep) == SB_LANG_ERR_ARG);
    CHECK(sb_lang_self_audit(&B, NULL, &rep) == SB_LANG_ERR_ARG);
    CHECK(sb_lang_self_audit(&B, &L, NULL) == SB_LANG_ERR_ARG);
    L.attestation = LPRES_STATE_NEITHER;
    give(SB_CAP_TREATY_TOKENIZE); /* 2.5 <= 3.0 */
    CHECK(sb_lang_self_audit(&B, &L, &rep) == 0);
    CHECK(L.attestation == LPRES_STATE_TRUE && L.coverage_ratio == 3.0);
    L.m5.omega = 4; /* 2.0 */
    CHECK(sb_lang_self_audit(&B, &L, &rep) == -1);
    CHECK(L.attestation == LPRES_STATE_BOTH && L.coverage_ratio == 2.0);
    B.initialized = false;
    L.attestation = LPRES_STATE_NEITHER;
    CHECK(sb_lang_self_audit(&B, &L, &rep) == -1);
    CHECK(L.attestation == LPRES_STATE_NEITHER);
    B.initialized = true;

    /* self-heal */
    CHECK(sb_lang_self_heal(NULL, &L, &rep) == SB_LANG_ERR_ARG);
    CHECK(sb_lang_self_heal(&B, NULL, &rep) == SB_LANG_ERR_ARG);
    CHECK(sb_lang_self_heal(&B, &L, NULL) == SB_LANG_ERR_ARG);
    L.attestation = LPRES_STATE_BOTH;
    uint64_t h0 = B.stats.total_healings;
    CHECK(sb_lang_self_heal(&B, &L, &rep) == 0);
    CHECK(L.attestation == LPRES_STATE_TRUE && B.stats.total_healings == h0 + 1);
    L.attestation = LPRES_STATE_FALSE;
    CHECK(sb_lang_self_heal(&B, &L, &rep) == 0);
    CHECK(L.attestation == LPRES_STATE_TRUE && B.stats.total_healings == h0 + 2);
    L.attestation = LPRES_STATE_NEITHER;
    CHECK(sb_lang_self_heal(&B, &L, &rep) == 0);
    CHECK(L.attestation == LPRES_STATE_NEITHER && B.stats.total_healings == h0 + 2);
    CHECK(L.num_capabilities == 1 && L.capabilities[0] == SB_CAP_TREATY_TOKENIZE && L.active);
}

static void test_parse_helpers(void)
{
    /* header helpers used by language front-ends */
    rat_t args[OC_MAX_FIELDS];
    for (int i = 0; i < OC_MAX_FIELDS; i++) args[i] = ri(i);
    CHECK(sb_lang_build_op(NULL, SB_OP_SELF_AUDIT, args, 1) == OC_ERR_ARG);
    CHECK(sb_lang_build_op(&IN, SB_OP_SELF_AUDIT, args, OC_MAX_FIELDS) == OC_ERR_ARG);
    CHECK(sb_lang_build_op(&IN, SB_OP_SELF_AUDIT, args, OC_MAX_FIELDS - 1) == OC_OK);
    CHECK(IN.num_fields == OC_MAX_FIELDS && rat_eq(IN.fields[0].num, ri(SB_OP_SELF_AUDIT)));
    CHECK(is_q32(surplus_to_rat(1.5), 1.5));
    CHECK(rat_to_surplus(rat_make(3, 8)) == 0.375);
    rat_t bad = rat_make(1, 0);
    CHECK(rat_to_surplus(bad) == 0.0);
}

int main(void)
{
    test_gates();
    test_metadata();
    test_ops();
    test_audit_heal_ops();
    test_helpers();
    test_parse_helpers();
    if (fails) {
        printf("%d of %d checks failed\n", fails, checks);
        return 1;
    }
    printf("%d checks passed\n", checks);
    return 0;
}
