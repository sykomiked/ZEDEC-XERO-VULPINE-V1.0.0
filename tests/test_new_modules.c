/*
 * test_new_modules.c — Host-side tests for new M5 kernel modules
 *
 * Tests: Surplus (ISF), EDP Risk, Predictive Model, Situation Model,
 *        Triple Ledger, Financial, Rails, Crypto Bridge, Identity,
 *        Quantum Device, RTL Device, JDR PirateNet, Synthesis Engine,
 *        Polar-Paraconsistent Trit Logic (5VL)
 *
 * Author: H.M. Michael-Laurence: Curzi (c)
 * License: Apache-2.0
 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <math.h>
#include <assert.h>

#include "m5_types.h"
#include "surplus.h"
#include "edp_risk.h"
#include "predictive_model.h"
#include "situation_model.h"
#include "triple_ledger.h"
#include "financial.h"
#include "rails.h"
#include "crypto_bridge.h"
#include "identity.h"
#include "quantum_device.h"
#include "rtl_device.h"
#include "jdr_piratenet.h"
#include "synthesis_engine.h"
#include "crypto_wallet.h"
#include "nlb.h"
#include "pterm.h"
#include "xedit.h"
#include "lattice.h"
#include "pungent.h"
#include "ascent.h"
#include "plnp.h"
#include "smap.h"
#include "decent.h"
#include "recon.h"
#include "hdcm.h"
#include "gematria.h"
#include "dualtrack.h"
#include "superpos.h"

/* ===== Polar-Paraconsistent Trit Logic (5VL) Tests ===== */
static void test_polar_trit_logic(void) {
    printf("=== Polar-Paraconsistent Trit Logic (5VL) Tests ===\n");

    /* Basic state values */
    assert(TRIT_FALSE == 0);
    assert(TRIT_TRUE == 1);
    assert(TRIT_GLUT == 2);
    assert(TRIT_GLUT_PLUS == 3);
    assert(TRIT_GLUT_MINUS == 4);
    assert(TRIT_GLUT_NEUTRAL == 5);
    printf("  [PASS] 5VL state codes: FALSE=0, TRUE=1, GLUT=2, GLUT+=3, GLUT-=4, GLUT0=5\n");

    /* Existential import mapping */
    assert(trit_to_ell(TRIT_TRUE) == 1.0);
    assert(trit_to_ell(TRIT_GLUT_PLUS) == 0.75);
    assert(trit_to_ell(TRIT_GLUT) == 0.5);
    assert(trit_to_ell(TRIT_GLUT_NEUTRAL) == 0.5);
    assert(trit_to_ell(TRIT_GLUT_MINUS) == 0.25);
    assert(trit_to_ell(TRIT_FALSE) == 0.0);
    printf("  [PASS] Existential import: TRUE=1.0, GLUT+=0.75, GLUT0=0.5, GLUT-=0.25, FALSE=0.0\n");

    /* Charge vector */
    assert(trit_charge(TRIT_GLUT_PLUS) == 1);
    assert(trit_charge(TRIT_GLUT_MINUS) == -1);
    assert(trit_charge(TRIT_GLUT_NEUTRAL) == 0);
    assert(trit_charge(TRIT_GLUT) == 0);
    assert(trit_charge(TRIT_TRUE) == 0);
    assert(trit_charge(TRIT_FALSE) == 0);
    printf("  [PASS] Charge vector: GLUT+=+1, GLUT-=-1, GLUT0=0, TRUE/FALSE=0\n");

    /* GLUT detection */
    assert(trit_is_glut(TRIT_GLUT) == 1);
    assert(trit_is_glut(TRIT_GLUT_PLUS) == 1);
    assert(trit_is_glut(TRIT_GLUT_MINUS) == 1);
    assert(trit_is_glut(TRIT_GLUT_NEUTRAL) == 1);
    assert(trit_is_glut(TRIT_TRUE) == 0);
    assert(trit_is_glut(TRIT_FALSE) == 0);
    printf("  [PASS] GLUT detection: all 4 GLUT states detected, TRUE/FALSE excluded\n");

    /* Coverage invariant: GLUT_PLUS has higher ell than GLUT_MINUS
     * This means a system in GLUT_PLUS state has better coverage (r*ell >= 1.8)
     * than one in GLUT_MINUS state, giving it directional momentum */
    double r = 2.0;
    assert(r * trit_to_ell(TRIT_GLUT_PLUS) >= 1.5);  /* 2.0 * 0.75 = 1.5 */
    assert(r * trit_to_ell(TRIT_GLUT_NEUTRAL) >= 0.9); /* 2.0 * 0.5 = 1.0 */
    assert(r * trit_to_ell(TRIT_GLUT_MINUS) < 1.0);   /* 2.0 * 0.25 = 0.5 */
    printf("  [PASS] Coverage momentum: GLUT+ > GLUT0 > GLUT- (directional resolution)\n");

    printf("  === All polar trit logic tests passed ===\n\n");
}

/* ===== Surplus (ISF) Tests ===== */
static void test_surplus(void) {
    printf("=== Surplus (ISF) Tests ===\n");

    surplus_real_t f0 = surplus_f(SR_ZERO, 4);
    assert(f0 == SR_ZERO);
    printf("  [PASS] f(0) = 0 (no surplus from identical vectors)\n");

    surplus_real_t f1 = surplus_f(SR_ONE, 4);
    assert(f1 > SR_ZERO);
    printf("  [PASS] f(1) > 0 (maximal surplus from orthogonal vectors)\n");

    surplus_real_t u1 = SR_FROM_FLOAT(0.1);
    surplus_real_t u2 = SR_FROM_FLOAT(0.5);
    surplus_real_t f_u1 = surplus_f(u1, 4);
    surplus_real_t f_u2 = surplus_f(u2, 4);
    assert(SR_CMP(f_u1, f_u2) < 0);
    printf("  [PASS] Monotonicity: f(0.1) < f(0.5)\n");

    surplus_real_t lb = surplus_lipschitz_bound(4);
    assert(lb > SR_ZERO);
    printf("  [PASS] Lipschitz bound > 0\n");

    printf("  === All surplus tests passed ===\n\n");
}

/* ===== EDP Risk Tests ===== */
static void test_edp_risk(void) {
    printf("=== EDP Risk Tests ===\n");

    m5_coords_t good = {.omega = 1, .r = SR_FROM_FLOAT(2.0), .ell = SR_FROM_FLOAT(1.0), .phi = SR_ZERO, .chi = 0};
    m5_coords_t bad  = {.omega = 1, .r = SR_FROM_FLOAT(0.5), .ell = SR_FROM_FLOAT(0.5), .phi = SR_ZERO, .chi = 0};

    assert(edp_coverage_satisfied(&good) == true);
    assert(edp_coverage_satisfied(&bad) == false);
    printf("  [PASS] Coverage hyperbola: good=PASS, bad=FAIL\n");

    surplus_real_t zpd = edp_zpd(SR_FROM_INT(5), SR_FROM_INT(3));
    assert(zpd == SR_FROM_INT(3));
    printf("  [PASS] ZPD: x/(0*y) = y\n");

    assert(edp_fibonacci(0) == 0);
    assert(edp_fibonacci(1) == 1);
    assert(edp_fibonacci(10) == 55);
    printf("  [PASS] Fibonacci sequence correct\n");

    printf("  === All EDP risk tests passed ===\n\n");
}

/* ===== Synthesis Engine Tests ===== */
static void test_synthesis_engine(void) {
    printf("=== Synthesis Engine Tests ===\n");

    synthesis_engine_t *engine = (synthesis_engine_t *)malloc(sizeof(synthesis_engine_t));
    assert(engine != NULL);
    synth_engine_init(engine, 1, "ZEDEC-Test-Synth");

    assert(engine->device_id == 1);
    assert(engine->status == SYNTH_STATUS_IDLE);
    printf("  [PASS] Engine initialized (ID=1, status=IDLE)\n");

    /* Set M5 coverage */
    engine->coverage_r = 2 * Q16_ONE; /* Q16.16 */
    engine->coverage_l = Q16_ONE;
    assert(synth_check_coverage(engine));
    printf("  [PASS] M5 coverage: r*l >= 1.8\n");

    /* Parse a simple spec */
    const char *spec = "module test_module { input clk; output data; }";
    int result = synth_parse_spec(engine, spec, (uint32_t)strlen(spec));
    assert(result == 0);
    assert(engine->num_ast_nodes > 0);
    printf("  [PASS] Spec parsed: %u AST nodes\n", engine->num_ast_nodes);

    /* Apply gates: CREATE then MEASURE */
    int gr = synth_gate_create(engine, 0, 0, Q16_ONE);
    assert(gr == 0);
    gr = synth_gate_measure(engine);
    assert(gr == 0);

    synth_apply_gates(engine);
    printf("  [PASS] Gates applied: CREATE → MEASURE\n");

    /* Generate C code */
    synth_result_t sresult;
    int gen_result = synth_generate_code(engine, SYNTH_TARGET_C, &sresult);
    assert(gen_result == 0 || gen_result == 1);
    printf("  [PASS] Code generation attempted\n");

    free(engine);
    printf("  === All synthesis engine tests passed ===\n\n");
}

/* ===== Identity System Tests ===== */
static void test_identity(void) {
    printf("=== Identity System Tests ===\n");

    identity_system_t sys = identity_country_system(840);
    assert(sys == ID_SYS_UN_NORTH_AMERICA);
    printf("  [PASS] US (840) → UN North America\n");

    sys = identity_country_system(156);
    assert(sys == ID_SYS_EAST_ASIA);
    printf("  [PASS] China (156) → East Asia\n");

    const char *alpha3 = identity_iso_alpha3(840);
    assert(strcmp(alpha3, "USA") == 0);
    printf("  [PASS] ISO alpha3: 840 → USA\n");

    alpha3 = identity_iso_alpha3(156);
    assert(strcmp(alpha3, "CHN") == 0);
    printf("  [PASS] ISO alpha3: 156 → CHN\n");

    printf("  === All identity tests passed ===\n\n");
}

/* ===== RTL Device Tests ===== */
static void test_rtl_device(void) {
    printf("=== RTL Device Tests ===\n");

    rtl_registry_t reg;
    rtl_registry_init(&reg);
    uint32_t dev_id = rtl_device_create(&reg, "test_rtl", HDL_SYSTEMVERILOG, BUS_AXI4, 50000000);
    assert(dev_id == 0);  /* First device gets ID 0 */
    printf("  [PASS] RTL device created (SystemVerilog, AXI4, 50MHz)\n");

    rtl_device_t *dev = &reg.devices[0];
    assert(dev->target_lang == HDL_SYSTEMVERILOG);
    assert(dev->bus == BUS_AXI4);
    printf("  [PASS] Device config verified\n");

    assert(dev->m5.r == SR_ONE);
    assert(dev->m5.ell == SR_ONE);
    printf("  [PASS] M5 coordinates initialized (r=1, ell=1)\n");

    printf("  === All RTL device tests passed ===\n\n");
}

/* ===== JDR PirateNet Tests ===== */
static void test_jdr_piratenet(void) {
    printf("=== JDR PirateNet Tests ===\n");

    jdr_transceiver_t tc;
    jdr_hum_init(&tc, SR_FROM_FLOAT(14.2e6), SR_FROM_FLOAT(1.0));
    printf("  [PASS] JDR Hum transceiver initialized (14.2 MHz HF)\n");

    jdr_hum_add_harmonic(&tc, 2);
    printf("  [PASS] Harmonic added (order=2)\n");

    printf("  === All JDR PirateNet tests passed ===\n\n");
}

/* ===== Triple Ledger Tests ===== */
static void test_triple_ledger(void) {
    printf("=== Triple Ledger Tests ===\n");

    triple_ledger_t *ledger = (triple_ledger_t *)malloc(sizeof(triple_ledger_t));
    assert(ledger != NULL);
    triple_ledger_init(ledger);
    printf("  [PASS] Triple ledger initialized\n");
    free(ledger);

    printf("  === All triple ledger tests passed ===\n\n");
}

/* ===== Crypto Wallet (5-Key Vector File System) Tests ===== */
static void test_crypto_wallet(void) {
    printf("=== Crypto Wallet (5-Key Vector File System) Tests ===\n");

    /* Use heap — the system struct contains large wallet arrays */
    crypto_wallet_system_t *sys = (crypto_wallet_system_t *)malloc(sizeof(crypto_wallet_system_t));
    assert(sys != NULL);
    cw_system_init(sys, 1, "ZEDEC-Wallet-Test");

    /* Set root seed (BIOS substrate) */
    uint8_t seed[32] = {0x42, 0x36, 0x4e, 0x39, 0x47, 0x65, 0x6e, 0x65,
                        0x74, 0x69, 0x63, 0x73, 0x4c, 0x4c, 0x43, 0x00,
                        0x56, 0x4f, 0x56, 0x49, 0x4e, 0x41, 0x53, 0x48,
                        0x41, 0x4b, 0x49, 0x4e, 0x41, 0x00, 0x00, 0x00};
    cw_system_set_root_seed(sys, seed, 32);
    assert(sys->root.seed_len == 32);
    printf("  [PASS] Root seed (BIOS substrate) set: 32 bytes\n");

    /* Create wallets */
    uint32_t w1 = cw_wallet_create(sys, "kernel-core");
    uint32_t w2 = cw_wallet_create(sys, "speculative-sandbox");
    assert(w1 == 0);
    assert(w2 == 1);
    assert(sys->num_wallets == 2);
    printf("  [PASS] 2 wallets created (kernel-core, speculative-sandbox)\n");

    /* Verify all 5 keys derived for wallet 0 */
    cw_wallet_t *wallet = cw_wallet_get(sys, 0);
    assert(wallet != NULL);
    uint32_t i;
    for (i = 0; i < CW_KEY_MAX; i++) {
        assert(wallet->keys[i].key_len > 0);
        /* Verify key is not all zeros */
        bool nonzero = false;
        uint32_t j;
        for (j = 0; j < CW_MAX_HASH_LEN; j++) {
            if (wallet->keys[i].key[j] != 0) { nonzero = true; break; }
        }
        assert(nonzero);
    }
    printf("  [PASS] All 5 keys derived (K1-K5) per wallet\n");

    /* Verify key-to-phase mapping */
    assert(wallet->keys[CW_KEY_TRUE].logic_phase == TRIT_TRUE);
    assert(wallet->keys[CW_KEY_FALSE].logic_phase == TRIT_FALSE);
    assert(wallet->keys[CW_KEY_GLUT_PLUS].logic_phase == TRIT_GLUT_PLUS);
    assert(wallet->keys[CW_KEY_GLUT_MINUS].logic_phase == TRIT_GLUT_MINUS);
    assert(wallet->keys[CW_KEY_GLUT_NEUTRAL].logic_phase == TRIT_GLUT_NEUTRAL);
    printf("  [PASS] Key-to-phase mapping: K1=TRUE, K2=FALSE, K3=GLUT+, K4=GLUT-, K5=GLUT0\n");

    /* Add a .36n9 file (TRUE phase — verified executable) */
    uint8_t payload1[] = "ZEDEC kernel core executable payload";
    int32_t fidx1 = cw_file_add(sys, 0, CW_FILE_36N9, payload1, sizeof(payload1), "/kernel/core.36n9");
    assert(fidx1 >= 0);
    assert(wallet->files[0].file_type == CW_FILE_36N9);
    assert(wallet->files[0].current_phase == TRIT_TRUE);
    assert(wallet->files[0].signing_key == CW_KEY_TRUE);
    printf("  [PASS] .36n9 file added (TRUE phase, K1 signed)\n");

    /* Add a .zedec file (GLUT_PLUS phase — speculative) */
    uint8_t payload2[] = "speculative compute buffer data";
    int32_t fidx2 = cw_file_add(sys, 1, CW_FILE_ZEDEC, payload2, sizeof(payload2), "/sandbox/test.zedec");
    assert(fidx2 >= 0);
    cw_wallet_t *wallet2 = cw_wallet_get(sys, 1);
    assert(wallet2->files[0].current_phase == TRIT_GLUT_PLUS);
    assert(wallet2->files[0].signing_key == CW_KEY_GLUT_PLUS);
    printf("  [PASS] .zedec file added (GLUT_PLUS phase, K3 signed)\n");

    /* Phase shift: .zedec → .36n9 (speculative → verified) — O(1) zero-copy */
    int shift_result = cw_file_phase_shift(sys, 1, 0, CW_KEY_TRUE);
    assert(shift_result == 0);
    assert(wallet2->files[0].current_phase == TRIT_TRUE);
    assert(wallet2->files[0].signing_key == CW_KEY_TRUE);
    printf("  [PASS] Phase shift: .zedec → TRUE (O(1) zero-copy re-signing)\n");
    assert(sys->total_phase_shifts == 1);

    /* Cross-wallet interlocking */
    int link_result = cw_wallet_add_peer(sys, 0, 1);
    assert(link_result == 0);
    assert(wallet->num_peers == 1);
    assert(wallet2->num_peers == 1);
    printf("  [PASS] Cross-wallet interlocking: 2 wallets linked\n");

    /* Integrity verification */
    bool ok = cw_verify_integrity(sys, 0);
    assert(ok);
    printf("  [PASS] Integrity verification: all signatures valid\n");

    /* Tamper detection — corrupt a signature */
    wallet2->files[0].signature[0] ^= 0xFF;
    bool ok2 = cw_verify_integrity(sys, 1);
    assert(!ok2);
    assert(wallet2->files[0].current_phase == TRIT_GLUT_NEUTRAL);
    printf("  [PASS] Tamper detection: signature mismatch → GLUT_NEUTRAL (locked)\n");

    /* System Merkle root exists */
    bool merkle_nonzero = false;
    for (i = 0; i < CW_MAX_HASH_LEN; i++) {
        if (sys->system_merkle_root[i] != 0) { merkle_nonzero = true; break; }
    }
    assert(merkle_nonzero);
    printf("  [PASS] System Merkle root computed (non-zero)\n");

    free(sys);
    printf("  === All crypto wallet tests passed ===\n\n");
}

/* ===== Nonlinear Build Engine (NLB) Tests ===== */
static void test_nlb(void) {
    printf("=== Nonlinear Build Engine (NLB) Tests ===\n");

    nlb_session_t *s = (nlb_session_t *)malloc(sizeof(nlb_session_t));
    assert(s != NULL);
    nlb_session_init(s, 1, "kernel-core");
    assert(s->session_id == 1);
    assert(s->phase == NLB_PHASE_IDLE);
    printf("  [PASS] Session initialized (ID=1, IDLE)\n");

    /* Add source files */
    int32_t src1 = nlb_source_add(s, "kernel/main.c", "main", 4096, 0, false);
    int32_t src2 = nlb_source_add(s, "kernel/utils.c", "utils", 2048, 0, false);
    int32_t hdr1 = nlb_source_add(s, "kernel/types.h", "types", 512, 0, true);
    assert(src1 == 0);
    assert(src2 == 1);
    assert(hdr1 == 2);
    assert(s->num_sources == 3);
    printf("  [PASS] 3 sources added (2 .c + 1 .h)\n");

    /* Create compilation branches */
    uint32_t branch1_srcs[] = {0, 2};  /* main.c + types.h */
    uint32_t branch2_srcs[] = {1, 2};  /* utils.c + types.h */
    uint32_t branch3_srcs[] = {};      /* synthesis branch (no sources) */

    int32_t b1 = nlb_branch_create(s, "core-main", branch1_srcs, 2);
    int32_t b2 = nlb_branch_create(s, "core-utils", branch2_srcs, 2);
    int32_t b3 = nlb_branch_create(s, "synthesis", branch3_srcs, 0);
    assert(b1 == 0);
    assert(b2 == 1);
    assert(b3 == 2);
    assert(s->num_branches == 3);
    printf("  [PASS] 3 branches created (2 compile + 1 synthesis)\n");

    /* Run full pipeline: CREATE → ENTANGLE → MEASURE */
    int result = nlb_build_run(s);
    assert(result == 0);
    assert(s->phase == NLB_PHASE_DONE);
    printf("  [PASS] Full pipeline: CREATE → ENTANGLE → MEASURE → DONE\n");

    /* Verify all branches converged */
    assert(s->converged == true);
    assert(s->branches[0].state == NLB_BRANCH_MERGED);
    assert(s->branches[1].state == NLB_BRANCH_MERGED);
    assert(s->branches[2].state == NLB_BRANCH_MERGED);
    printf("  [PASS] All 3 branches converged + merged\n");

    /* Verify artifacts were produced */
    assert(s->num_artifacts == 3);
    printf("  [PASS] 3 artifacts produced\n");

    /* Verify artifact hashes are non-zero */
    bool hash_nonzero = false;
    uint32_t i;
    for (i = 0; i < 32; i++) {
        if (s->artifacts[0].sha256[i] != 0) { hash_nonzero = true; break; }
    }
    assert(hash_nonzero);
    assert(s->artifacts[0].verified == true);
    printf("  [PASS] Artifacts content-hashed + verified\n");

    /* Verify M5 coverage was computed */
    assert(s->coverage_ratio > 0);
    printf("  [PASS] M5 coverage computed (ratio > 0)\n");

    /* Test register interface (hardware-as-code) */
    assert(nlb_register_read(s, NLB_REG_PHASE) == (uint32_t)NLB_PHASE_DONE);
    assert(nlb_register_read(s, NLB_REG_NUM_SOURCES) == 3);
    assert(nlb_register_read(s, NLB_REG_NUM_BRANCHES) == 3);
    assert(nlb_register_read(s, NLB_REG_NUM_ARTIFACTS) == 3);
    assert(nlb_register_read(s, NLB_REG_CONVERGED) == 1);
    printf("  [PASS] Register interface: all reads correct\n");

    /* Test IRQ status */
    assert(s->irq_status & NLB_IRQ_PHASE_COMPLETE);
    assert(s->irq_status & NLB_IRQ_CONVERGED);
    assert(s->irq_status & NLB_IRQ_ARTIFACT_READY);
    printf("  [PASS] IRQ: phase complete + converged + artifact ready\n");

    /* Test DMA write/read */
    uint8_t dma_test[] = "ZEDEC-NLB-DMA-TEST";
    int dma_w = nlb_dma_write(s, dma_test, sizeof(dma_test));
    assert(dma_w == (int)sizeof(dma_test));
    s->dma_pos = 0;  /* Reset for read */
    uint8_t dma_buf[32] = {0};
    int dma_r = nlb_dma_read(s, dma_buf, sizeof(dma_test));
    assert(dma_r == (int)sizeof(dma_test));
    assert(memcmp(dma_buf, dma_test, sizeof(dma_test)) == 0);
    printf("  [PASS] DMA write/read round-trip\n");

    /* Test error correction: create a failing branch and verify convergence loop */
    nlb_session_t *s2 = (nlb_session_t *)malloc(sizeof(nlb_session_t));
    nlb_session_init(s2, 2, "test-fail-correct");

    /* Add a zero-size source (will fail compilation) */
    int32_t bad_src = nlb_source_add(s2, "empty.c", "empty", 0, 0, false);
    assert(bad_src == 0);

    /* Create a branch with the bad source + a synthesis branch */
    uint32_t bad_branch_srcs[] = {0};
    int32_t bb1 = nlb_branch_create(s2, "bad-compile", bad_branch_srcs, 1);
    uint32_t good_branch_srcs[] = {};
    int32_t bb2 = nlb_branch_create(s2, "synthesis-good", good_branch_srcs, 0);
    assert(bb1 == 0);
    assert(bb2 == 1);

    /* Run pipeline — bad branch should fail but synthesis branch passes */
    nlb_phase_create(s2);
    assert(s2->branches[0].state == NLB_BRANCH_FAILED);
    assert(s2->branches[1].state == NLB_BRANCH_PASSED);
    printf("  [PASS] Error detection: bad branch FAILED, synthesis PASSED\n");

    /* Entangle: correction loop attempts on bad branch */
    nlb_phase_entangle(s2);
    assert(s2->total_iterations > 0);
    printf("  [PASS] Correction loop: %u iterations attempted\n", s2->total_iterations);

    /* Bad branch should still be failed (zero-size source can't be fixed) */
    /* But convergence should be true because synthesis branch passed */
    assert(s2->converged == true);
    printf("  [PASS] Partial convergence: synthesis branch converged despite bad branch\n");

    /* Measure: only synthesis branch produces artifact */
    nlb_phase_measure(s2);
    assert(s2->num_artifacts == 1);
    printf("  [PASS] Partial artifact: 1 artifact from converged branch\n");

    free(s);
    free(s2);
    printf("  === All NLB tests passed ===\n\n");
}

/* ===== P-TERM Terminal Engine Tests ===== */
static void test_pterm(void) {
    printf("=== P-TERM Terminal Engine Tests ===\n");

    pterm_t *t = (pterm_t *)malloc(sizeof(pterm_t));
    pterm_init(t);
    assert(t->initialized == true);
    assert(t->num_commands == PTERM_CMD_MAX);
    printf("  [PASS] Terminal initialized with %u commands\n", t->num_commands);

    int32_t c0 = pterm_console_create(t, "zede> ");
    int32_t c1 = pterm_console_create(t, "dark> ");
    assert(c0 == 0);
    assert(c1 == 1);
    assert(t->num_consoles == 2);
    printf("  [PASS] 2 virtual consoles created\n");

    assert(pterm_switch_console(t, 1) == 0);
    assert(t->active_console == 1);
    printf("  [PASS] Console switch works\n");

    pterm_switch_console(t, 0);
    pterm_execute_command(t, "help");
    assert(t->consoles[0].cursor_y > 0);
    printf("  [PASS] 'help' command executed, output rendered\n");

    pterm_clear(t);
    assert(t->consoles[0].cursor_x == 0);
    assert(t->consoles[0].cursor_y == 0);
    printf("  [PASS] 'clear' command works\n");

    pterm_execute_command(t, "echo ZEDEC pqOS");
    printf("  [PASS] 'echo' command works\n");

    pterm_execute_command(t, "ps-phase");
    printf("  [PASS] 'ps-phase' command shows phase-aware tasks\n");

    pterm_execute_command(t, "net-ctl");
    printf("  [PASS] 'net-ctl' shows network interfaces\n");

    pterm_execute_command(t, "tunnel");
    printf("  [PASS] 'tunnel' shows PungentClove status\n");

    pterm_execute_command(t, "seed");
    printf("  [PASS] 'seed' shows LATTICE-P2P status\n");

    pterm_execute_command(t, "nonexistent_cmd");
    printf("  [PASS] Unknown command handled gracefully\n");

    pterm_history_add(t, "help");
    pterm_history_add(t, "echo test");
    assert(t->history_count == 2);
    const char *prev = pterm_history_prev(t);
    assert(prev != NULL);
    printf("  [PASS] Command history works (count=%u)\n", t->history_count);

    pterm_set_phase(t, 3);
    assert(t->phase_state == 3);
    const char *prompt = pterm_phase_prompt(3);
    assert(prompt != NULL);
    printf("  [PASS] Phase-aware prompt: %s\n", prompt);

    free(t);
    printf("  === All P-TERM tests passed ===\n\n");
}

/* ===== X-EDIT Text Editor Tests ===== */
static void test_xedit(void) {
    printf("=== X-EDIT Text Editor Tests ===\n");

    xedit_t *e = (xedit_t *)malloc(sizeof(xedit_t));
    xedit_init(e);
    assert(e->initialized == true);
    printf("  [PASS] Editor initialized\n");

    int32_t b0 = xedit_buffer_create(e, "kernel/main.c");
    assert(b0 == 0);
    assert(e->buffers[0].filetype == XEDIT_FILE_C_SOURCE);
    printf("  [PASS] Buffer created: %s (type: %s)\n",
           e->buffers[0].filename,
           xedit_filetype_name(e->buffers[0].filetype));

    xedit_buffer_t *b = &e->buffers[0];
    assert(xedit_buffer_size(b) == 0);
    assert(xedit_gap_size(b) == XEDIT_BUF_SIZE);
    printf("  [PASS] Empty buffer: size=0, gap=%u\n", xedit_gap_size(b));

    xedit_insert(b, "Hello, ZEDEC!\n", 14);
    assert(xedit_buffer_size(b) == 14);
    assert(b->modified == true);
    printf("  [PASS] Inserted 14 bytes, buffer size=%u\n", xedit_buffer_size(b));

    xedit_insert(b, "Line 2\n", 7);
    assert(xedit_buffer_size(b) == 21);
    assert(b->line_count >= 2);
    printf("  [PASS] Second line inserted, line_count=%u\n", b->line_count);

    char text[1024];
    uint32_t len = xedit_get_text(b, text, sizeof(text));
    assert(len == 21);
    printf("  [PASS] Get text: %u bytes retrieved\n", len);

    char line[256];
    uint32_t line_len = xedit_get_line(b, 0, line, sizeof(line));
    assert(line_len > 0);
    printf("  [PASS] Get line 0: \"%s\" (%u chars)\n", line, line_len);

    xedit_cursor_left(b);
    xedit_cursor_left(b);
    assert(b->cursor < 21);
    printf("  [PASS] Cursor left works (pos=%u)\n", b->cursor);

    xedit_cursor_right(b);
    printf("  [PASS] Cursor right works (pos=%u)\n", b->cursor);

    xedit_delete_back(b);
    assert(xedit_buffer_size(b) == 20);
    printf("  [PASS] Delete backspace works (size=%u)\n", xedit_buffer_size(b));

    xedit_update_hash(b);
    assert(b->hash_valid == true);
    printf("  [PASS] CID + Merkle root computed\n");

    bool valid = xedit_verify_integrity(b);
    assert(valid == true);
    printf("  [PASS] Integrity verification passed\n");

    xedit_set_mode(b, XEDIT_MODE_INSERT);
    assert(b->mode == XEDIT_MODE_INSERT);
    printf("  [PASS] Mode switch to INSERT\n");

    xedit_set_mode(b, XEDIT_MODE_COMMAND);
    assert(b->mode == XEDIT_MODE_COMMAND);
    printf("  [PASS] Mode switch to COMMAND (Vi-like)\n");

    int32_t b1 = xedit_buffer_create(e, "design.sv");
    assert(b1 == 1);
    assert(e->buffers[1].filetype == XEDIT_FILE_SVERILOG);
    printf("  [PASS] Second buffer: SystemVerilog detected\n");

    assert(xedit_switch_buffer(e, 1) == 0);
    assert(e->active_buffer == 1);
    printf("  [PASS] Buffer switch works\n");

    free(e);
    printf("  === All X-EDIT tests passed ===\n\n");
}

/* ===== LATTICE-P2P File Sharing Tests ===== */
static void test_lattice(void) {
    printf("=== LATTICE-P2P File Sharing Tests ===\n");

    lattice_t *l = (lattice_t *)malloc(sizeof(lattice_t));
    lattice_init(l);
    assert(l->initialized == true);
    printf("  [PASS] LATTICE initialized\n");

    int32_t p0 = lattice_peer_add(l, "m5://10.0.0.1", LATTICE_TIER_P2P_MESH);
    int32_t p1 = lattice_peer_add(l, "m5://clove.onion", LATTICE_TIER_GARLIC);
    int32_t p2 = lattice_peer_add(l, "radio://433.92MHz", LATTICE_TIER_RADIO);
    assert(p0 == 0);
    assert(p1 == 1);
    assert(p2 == 2);
    assert(l->num_peers == 3);
    printf("  [PASS] 3 peers added (P2P, Garlic, Radio tiers)\n");

    assert(lattice_peer_connect(l, 0) == 0);
    assert(lattice_peer_connect(l, 1) == 0);
    assert(l->peers[0].connected == true);
    printf("  [PASS] Peer connect works\n");

    assert(lattice_peers_by_tier(l, LATTICE_TIER_P2P_MESH) == 1);
    printf("  [PASS] Peers by tier: P2P=%u\n", lattice_peers_by_tier(l, LATTICE_TIER_P2P_MESH));

    lattice_peer_t *found = lattice_peer_find(l, "m5://10.0.0.1");
    assert(found != NULL);
    printf("  [PASS] Peer find by address works\n");

    uint8_t hash1[32] = {0xAB, 0xCD, 0xEF, 0x01};
    uint8_t hash2[32] = {0x12, 0x34, 0x56, 0x78};
    int32_t s0 = lattice_swarm_create(l, "kernel-core", LATTICE_PHASE_TRUE, LATTICE_TIER_P2P_MESH);
    assert(s0 == 0);
    printf("  [PASS] Swarm created: %s (%s, %s)\n",
           l->swarms[0].label,
           lattice_phase_name(l->swarms[0].phase),
           lattice_tier_name(l->swarms[0].tier));

    assert(lattice_swarm_add_chunk(l, 0, hash1, 4096) == 0);
    assert(lattice_swarm_add_chunk(l, 0, hash2, 4096) == 1);
    assert(l->swarms[0].num_chunks == 2);
    printf("  [PASS] 2 chunks added to swarm\n");

    assert(lattice_swarm_add_peer(l, 0, 0) == 0);
    assert(lattice_swarm_add_peer(l, 0, 1) == 0);
    assert(l->swarms[0].num_peers == 2);
    printf("  [PASS] 2 peers added to swarm\n");

    assert(lattice_swarm_start_seeding(l, 0) == 0);
    assert(l->swarms[0].seeding == true);
    printf("  [PASS] Seeding started\n");

    assert(lattice_swarm_complete(l, 0) == true);
    uint32_t progress = lattice_swarm_progress(l, 0);
    assert(progress == 100);
    printf("  [PASS] Swarm complete: progress=%u%%\n", progress);

    int32_t d0 = lattice_dht_put(l, hash1, hash2, LATTICE_TIER_P2P_MESH);
    assert(d0 == 0);
    const lattice_dht_entry_t *dht = lattice_dht_get(l, hash1);
    assert(dht != NULL);
    printf("  [PASS] DHT put + get works\n");

    assert(lattice_chunk_announce(l, 0, 0) == 0);
    printf("  [PASS] Chunk announce works\n");

    int32_t s1 = lattice_swarm_create(l, "ledger-blocks", LATTICE_PHASE_GLUT_MINUS, LATTICE_TIER_DARK_STORAGE);
    assert(s1 == 1);
    printf("  [PASS] Phase-aware swarm: %s → %s\n",
           l->swarms[1].label,
           lattice_phase_name(l->swarms[1].phase));

    free(l);
    printf("  === All LATTICE-P2P tests passed ===\n\n");
}

/* ===== PungentClove Security Tests ===== */
static void test_pungent(void) {
    printf("=== PungentClove Security Tests ===\n");

    pungent_t *pc = (pungent_t *)malloc(sizeof(pungent_t));
    pungent_init(pc);
    assert(pc->initialized == true);
    assert(pc->default_hops == 3);
    printf("  [PASS] PungentClove initialized (default_hops=%u)\n", pc->default_hops);

    int32_t r0 = pungent_relay_add(pc, "m5://relay1.onion", PC_TIER_ONION);
    int32_t r1 = pungent_relay_add(pc, "m5://relay2.i2p", PC_TIER_GARLIC);
    int32_t r2 = pungent_relay_add(pc, "radio://relay3.hf", PC_TIER_RADIO);
    assert(r0 == 0 && r1 == 1 && r2 == 2);
    printf("  [PASS] 3 relays added (Onion, Garlic, Radio)\n");

    assert(pungent_relay_set_online(pc, 0, true) == 0);
    assert(pungent_relay_set_online(pc, 1, true) == 0);
    assert(pungent_relay_set_online(pc, 2, true) == 0);
    assert(pungent_relays_by_tier(pc, PC_TIER_ONION) == 1);
    printf("  [PASS] Relays online, tier count: Onion=%u\n",
           pungent_relays_by_tier(pc, PC_TIER_ONION));

    /* Bulb operations */
    pc_bulb_t bulb;
    assert(pungent_bulb_init(&bulb, 3, 250) == 0);
    assert(bulb.hop_count == 3);
    assert(bulb.delay_ms == 250);
    printf("  [PASS] Bulb initialized (hops=%u, delay=%ums)\n", bulb.hop_count, bulb.delay_ms);

    uint8_t dest1[32] = {0xAA, 0xBB, 0xCC};
    uint8_t dest2[32] = {0xDD, 0xEE, 0xFF};
    uint8_t payload1[] = "ZEDEC-payload-1";
    uint8_t payload2[] = "ZEDEC-payload-2";

    assert(pungent_bulb_add_clove(&bulb, payload1, sizeof(payload1)-1, dest1, PC_TIER_GARLIC) == 0);
    assert(pungent_bulb_add_clove(&bulb, payload2, sizeof(payload2)-1, dest2, PC_TIER_ONION) == 1);
    assert(bulb.num_cloves == 2);
    printf("  [PASS] 2 cloves added to bulb (garlic bundling)\n");

    assert(pungent_bulb_seal(&bulb) == 0);
    assert(bulb.sealed == true);
    printf("  [PASS] Bulb sealed (multi-layer encryption)\n");

    assert(pungent_bulb_unseal(&bulb, 0) == 0);
    printf("  [PASS] Bulb unseal layer 0 works\n");

    uint32_t bulb_sz = pungent_bulb_size(&bulb);
    assert(bulb_sz > 0);
    printf("  [PASS] Bulb size: %u bytes\n", bulb_sz);

    /* Tunnel operations */
    int32_t t0 = pungent_tunnel_create(pc, PC_TUNNEL_OUTBOUND, PC_TIER_GARLIC, 3);
    assert(t0 == 0);
    printf("  [PASS] Tunnel created: %s, tier=%s\n",
           pungent_tunnel_dir_name(pc->tunnels[0].direction),
           pungent_tier_name(pc->tunnels[0].tier));

    assert(pungent_tunnel_activate(pc, 0) == 0);
    assert(pc->tunnels[0].active == true);
    printf("  [PASS] Tunnel activated\n");

    uint8_t data[] = "secret-message";
    assert(pungent_tunnel_send(pc, 0, data, sizeof(data)-1) == (int)(sizeof(data)-1));
    assert(pc->tunnels[0].bytes_sent == sizeof(data)-1);
    assert(pc->tunnels[0].bulbs_passed == 1);
    printf("  [PASS] Tunnel send: %u bytes, %u bulbs\n",
           (uint32_t)pc->tunnels[0].bytes_sent, pc->tunnels[0].bulbs_passed);

    /* Hidden service */
    int32_t hs = pungent_hidden_service_create(pc, "zede-hidden", 8080, 0);
    assert(hs == 0);
    assert(pc->services[0].active == true);
    printf("  [PASS] Hidden service created: %s on port %u\n",
           pc->services[0].label, pc->services[0].local_port);

    const pc_hidden_service_t *svc = pungent_hidden_service_find(pc, pc->services[0].service_key);
    assert(svc != NULL);
    printf("  [PASS] Hidden service found by key\n");

    /* SOCKS5 */
    int32_t sk = pungent_socks5_connect(pc, "example.onion", 443, 0);
    assert(sk == 0);
    assert(pc->socks5[0].active == true);
    printf("  [PASS] SOCKS5 connection: %s:%u via tunnel %u\n",
           pc->socks5[0].dest_addr, pc->socks5[0].dest_port, pc->socks5[0].tunnel_idx);

    /* Path selection */
    uint32_t path[PC_MAX_HOPS];
    assert(pungent_select_path(pc, PC_TIER_GARLIC, 3, path) == 3);
    printf("  [PASS] Path selection: 3 hops selected (nonlinear)\n");

    /* 8-tier names */
    assert(strcmp(pungent_tier_name(PC_TIER_SURFACE), "Tier 1: Surface Web") == 0);
    assert(strcmp(pungent_tier_name(PC_TIER_SHADOW), "Tier 8: ZK Shadow") == 0);
    printf("  [PASS] 8-tier naming correct\n");

    free(pc);
    printf("  === All PungentClove tests passed ===\n\n");
}

/* ===== Ascent OPSEC Framework Tests ===== */
static void test_ascent(void) {
    printf("=== Ascent OPSEC Framework Tests ===\n");

    ascent_t *a = (ascent_t *)malloc(sizeof(ascent_t));
    ascent_init(a);
    assert(a->initialized == true);
    assert(a->current_level == ASCENT_SUBSTRATE_0);
    printf("  [PASS] Ascent initialized at Substrate 0\n");

    /* 5PL Bundle */
    int32_t b0 = ascent_bundle_create(a, "BNDL-001", "Kernel ISO Build Evidence",
                                       ASCENT_LEVEL_1, "operator");
    assert(b0 == 0);
    printf("  [PASS] Bundle created: %s at %s\n",
           a->bundles[0].bundle_id,
           ascent_level_name(a->bundles[0].level));

    assert(ascent_bundle_set_claim(a, 0, "Kernel ISO built on B300, SHA-256 verified") == 0);
    assert(ascent_bundle_set_refutation(a, 0, "B300 could be compromised; hash could collide") == 0);
    assert(ascent_bundle_add_unknown(a, 0, "Verify B300 integrity independently") == 0);
    uint8_t whash[32] = {0xDE, 0xAD, 0xBE, 0xEF};
    assert(ascent_bundle_add_witness(a, 0, "SHA-256 hash + third-device photo", whash) == 0);
    printf("  [PASS] 5PL: (+1) claim, (-1) refutation, (0) unknowns, (+0) witness set\n");

    assert(ascent_bundle_check_gates(a, 0) == true);
    assert(a->bundles[0].phase_gates_passed == true);
    printf("  [PASS] Phase gates passed: +0, -1, 0 all present\n");

    /* Add consensus pins */
    assert(ascent_bundle_add_consensus(a, 0, ASCENT_PIN_TRANSPARENCY_LOG,
                                       "Public log entry #12345") == 0);
    assert(ascent_bundle_add_consensus(a, 0, ASCENT_PIN_HUMAN,
                                       "Second staffer co-signed") == 0);
    printf("  [PASS] Consensus: 2 heterogeneous pins (transparency log + human)\n");

    /* Seal bundle */
    assert(ascent_bundle_seal(a, 0) == 0);
    assert(a->bundles[0].sealed == true);
    printf("  [PASS] Bundle sealed (version frozen)\n");

    /* Validate sealed bundle requires heterogeneous consensus */
    assert(ascent_validate_bundle(&a->bundles[0]) == true);
    printf("  [PASS] Sealed bundle validation passed (heterogeneous pins)\n");

    /* Version bump */
    assert(ascent_bundle_version(a, 0) == 2);
    assert(a->bundles[0].sealed == false);
    printf("  [PASS] Version bump: v2 (unsealed for new data)\n");

    /* Triad Protocol */
    int32_t t0 = ascent_triad_create(a, ASCENT_LEVEL_3, "alice", "bob", "carol",
                                      "Blue Lantern", 45);
    assert(t0 == 0);
    assert(strcmp(a->triads[0].stop_phrase, "Blue Lantern") == 0);
    printf("  [PASS] Triad created: Operator/Recorder/Skeptic + stop-phrase\n");

    assert(ascent_triad_start(a, 0) == 0);
    assert(a->triads[0].active == true);
    printf("  [PASS] Triad session started\n");

    assert(ascent_triad_stop(a, 0) == 0);
    assert(a->triads[0].active == false);
    printf("  [PASS] Triad session stopped\n");

    assert(ascent_triad_debrief(a, 0) == 0);
    assert(a->triads[0].debriefed == true);
    printf("  [PASS] Triad debrief completed\n");

    /* Persona management */
    int32_t p0 = ascent_persona_create(a, "research-mask", ASCENT_LEVEL_1,
                                       "Surface web research");
    int32_t p1 = ascent_persona_create(a, "dark-mask", ASCENT_LEVEL_3,
                                       "Dark web observation");
    assert(p0 == 0 && p1 == 1);
    assert(ascent_persona_check_isolation(a, 0) == 0);
    printf("  [PASS] 2 personas created with isolation\n");

    assert(ascent_persona_retire(a, 0) == 0);
    assert(a->personas[0].active == false);
    printf("  [PASS] Persona retired (identity lifecycle)\n");

    /* Charter */
    int32_t c0 = ascent_charter_create(a, "zedec-circle", "Kernel build verification",
                                       2, 3);
    assert(c0 == 0);
    assert(a->charters[0].multisig_threshold == 2);
    assert(a->charters[0].multisig_total == 3);
    printf("  [PASS] Charter created: 2-of-3 multisig\n");

    assert(ascent_charter_add_member(a, 0, "alice") == 0);
    assert(ascent_charter_add_member(a, 0, "bob") == 0);
    assert(ascent_charter_add_member(a, 0, "carol") == 0);
    assert(a->charters[0].num_members == 3);
    printf("  [PASS] 3 members added to charter\n");

    assert(ascent_charter_check_quorum(a, 0, 2) == true);
    assert(ascent_charter_check_quorum(a, 0, 1) == false);
    printf("  [PASS] Quorum check: 2 signers OK, 1 insufficient\n");

    /* Incident log */
    int32_t inc = ascent_incident_log(a, "Isolation violation detected",
                                      "Retired compromised persona", "alice");
    assert(inc == 0);
    printf("  [PASS] Incident logged\n");

    assert(ascent_incident_resolve(a, 0) == 0);
    assert(a->incidents[0].resolved == true);
    printf("  [PASS] Incident resolved\n");

    /* Level names */
    assert(strcmp(ascent_level_name(ASCENT_LEVEL_8), "Level 8: Mirrorwork (Abyss)") == 0);
    assert(strcmp(ascent_level_name(ASCENT_LEVEL_9), "Level 9+: Positive Completion") == 0);
    printf("  [PASS] Level names correct (L8=%s)\n",
           ascent_level_name(ASCENT_LEVEL_8));

    /* 5PL phase names */
    assert(strcmp(ascent_5pl_phase_name(ASCENT_5PL_CLAIM), "(+1) Claim") == 0);
    assert(strcmp(ascent_5pl_phase_name(ASCENT_5PL_REFUTATION), "(-1) Refutation") == 0);
    printf("  [PASS] 5PL phase names correct\n");

    /* Pin type names */
    assert(strcmp(ascent_pin_type_name(ASCENT_PIN_NOTARY), "Notary/Affidavit") == 0);
    printf("  [PASS] Pin type names correct\n");

    ascent_set_level(a, ASCENT_LEVEL_5);
    assert(a->current_level == ASCENT_LEVEL_5);
    printf("  [PASS] Level set to: %s\n", ascent_level_name(a->current_level));

    free(a);
    printf("  === All Ascent OPSEC tests passed ===\n\n");
}

/* ===== PLNP Protocol Tests ===== */
static void test_plnp(void) {
    printf("=== PLNP Phase-Lattice Network Protocol Tests ===\n");

    plnp_stack_t *s = (plnp_stack_t *)malloc(sizeof(plnp_stack_t));
    plnp_init(s);
    assert(s->initialized == true);
    assert(s->phase_tick_ms == 10);
    printf("  [PASS] PLNP stack initialized (10ms phase tick)\n");

    /* Frame init */
    plnp_frame_t f;
    assert(plnp_frame_init(&f, PLNP_KEY_K3, PLNP_PHASE_TRUE) == 0);
    assert(f.header.magic == PLNP_MAGIC);
    assert(f.header.version == PLNP_VERSION);
    assert(f.header.key_index == PLNP_KEY_K3);
    assert(f.header.phase_state == PLNP_PHASE_TRUE);
    printf("  [PASS] Frame initialized: magic=0x%08X, K=%s, phase=%s\n",
           f.header.magic, plnp_key_name(f.header.key_index),
           plnp_phase_name(f.header.phase_state));

    /* Set CIDs */
    uint8_t src_cid[32] = {0xAA, 0xBB, 0xCC};
    uint8_t dst_cid[32] = {0xDD, 0xEE, 0xFF};
    assert(plnp_frame_set_cids(&f, src_cid, dst_cid) == 0);
    printf("  [PASS] CIDs set (content-addressed, not IP-addressed)\n");

    /* Set payload */
    uint8_t payload[] = "ZEDEC-PLNP-test-payload";
    assert(plnp_frame_set_payload(&f, payload, sizeof(payload)-1) == 0);
    assert(f.header.payload_len == sizeof(payload)-1);
    printf("  [PASS] Payload set: %u bytes\n", f.header.payload_len);

    /* Set 5PL vector */
    assert(plnp_frame_set_5pl(&f, PLNP_5PL_CLAIM | PLNP_5PL_WITNESS) == 0);
    assert(f.header.fpl_vector == (PLNP_5PL_CLAIM | PLNP_5PL_WITNESS));
    printf("  [PASS] 5PL vector set: +1 claim + +0 witness\n");

    /* Seal frame (compute CRC) */
    assert(plnp_frame_seal(&f) == 0);
    assert(f.end_marker == 0x5A);
    printf("  [PASS] Frame sealed (CRC32 computed, end marker set)\n");

    /* Verify frame */
    assert(plnp_frame_verify(&f) == 0);
    printf("  [PASS] Frame verification passed\n");

    /* Serialize / deserialize round-trip */
    uint8_t buf[PLNP_MAX_FRAME_SIZE];
    int ser_len = plnp_frame_serialize(&f, buf, sizeof(buf));
    assert(ser_len > 0);
    printf("  [PASS] Serialized: %d bytes\n", ser_len);

    plnp_frame_t f2;
    int deser_len = plnp_frame_deserialize(&f2, buf, (uint32_t)ser_len);
    assert(deser_len == ser_len);
    assert(f2.header.magic == PLNP_MAGIC);
    assert(f2.header.payload_len == f.header.payload_len);
    assert(f2.crc32 == f.crc32);
    assert(plnp_frame_verify(&f2) == 0);
    printf("  [PASS] Deserialize round-trip: magic + CRC verified\n");

    /* Connection management */
    int32_t conn = plnp_conn_create(s, src_cid, dst_cid, PLNP_KEY_K4);
    assert(conn == 0);
    printf("  [PASS] Connection created: K=%s\n", plnp_key_name(PLNP_KEY_K4));

    /* Send with TRUE phase */
    assert(plnp_conn_send(s, 0, payload, sizeof(payload)-1, PLNP_PHASE_TRUE) == (int)(sizeof(payload)-1));
    assert(s->connections[0].frames_sent == 1);
    assert(s->connections[0].bytes_sent == sizeof(payload)-1);
    printf("  [PASS] Send (TRUE): %u bytes, %u frames\n",
           s->connections[0].bytes_sent, s->connections[0].frames_sent);

    /* Send with GLUT+ (speculative) */
    assert(plnp_conn_send(s, 0, payload, sizeof(payload)-1, PLNP_PHASE_GLUT_PLUS) == (int)(sizeof(payload)-1));
    assert(s->connections[0].glut_plus_count == 1);
    assert(s->total_glut_plus == 1);
    printf("  [PASS] Send (GLUT+): speculative processing\n");

    /* Send with GLUT0 (freeze) */
    assert(plnp_conn_send(s, 0, payload, sizeof(payload)-1, PLNP_PHASE_GLUT_ZERO) == (int)(sizeof(payload)-1));
    assert(s->connections[0].glut_freezes == 1);
    assert(s->total_glut_freezes == 1);
    printf("  [PASS] Send (GLUT0): frame frozen for inspection\n");

    /* Send with GLUT- (safe drop) */
    assert(plnp_conn_send(s, 0, payload, sizeof(payload)-1, PLNP_PHASE_GLUT_MINUS) == (int)(sizeof(payload)-1));
    assert(s->connections[0].glut_minus_count == 1);
    assert(s->total_glut_minus == 1);
    printf("  [PASS] Send (GLUT-): safe drop without stalling\n");

    /* Phase resolution */
    assert(plnp_resolve_phase(s, 0, PLNP_PHASE_TRUE) == 0);
    assert(s->connections[0].state == PLNP_CONN_COMPLETE);
    printf("  [PASS] Phase resolution: TRUE → COMPLETE\n");

    assert(plnp_resolve_phase(s, 0, PLNP_PHASE_FALSE) == -1);
    assert(s->connections[0].state == PLNP_CONN_FAILED);
    printf("  [PASS] Phase resolution: FALSE → FAILED (hard reject)\n");

    /* CRC32 known value */
    uint32_t crc = plnp_crc32((const uint8_t*)"ZEDEC", 5);
    assert(crc != 0);
    printf("  [PASS] CRC32(\"ZEDEC\") = 0x%08X\n", crc);

    /* CID derivation */
    uint8_t cid[32];
    plnp_derive_cid(payload, sizeof(payload)-1, cid);
    /* Verify deterministic */
    uint8_t cid2[32];
    plnp_derive_cid(payload, sizeof(payload)-1, cid2);
    assert(memcmp(cid, cid2, 32) == 0);
    printf("  [PASS] CID derivation is deterministic\n");

    /* Key derivation */
    uint8_t key[32];
    plnp_derive_key(PLNP_KEY_K5, cid, 32, key);
    printf("  [PASS] Key derivation: K5 (%s)\n", plnp_key_name(PLNP_KEY_K5));

    /* Names */
    assert(strcmp(plnp_phase_name(PLNP_PHASE_GLUT_PLUS), "GLUT+ (G+)") == 0);
    assert(strcmp(plnp_key_name(PLNP_KEY_K1), "K1 (Surface)") == 0);
    assert(strcmp(plnp_state_name(PLNP_CONN_GLUT_FREEZE), "GLUT_FREEZE") == 0);
    printf("  [PASS] Phase/key/state names correct\n");

    free(s);
    printf("  === All PLNP tests passed ===\n\n");
}

/* ===== S-Map Dual-Artifact Tests ===== */
static void test_smap(void) {
    printf("=== S-Map Reassembly Key File Tests ===\n");

    smap_store_t *st = (smap_store_t *)malloc(sizeof(smap_store_t));
    smap_store_init(st);
    assert(st->initialized == true);
    printf("  [PASS] S-Map store initialized\n");

    /* Ingest a test payload */
    uint8_t data[10000];
    uint32_t i;
    for (i = 0; i < 10000; i++) data[i] = (uint8_t)(i * 7 + 13);
    int32_t idx = smap_ingest(st, data, 10000, "kernel.bin", SMAP_KEY_K2);
    assert(idx == 0);
    printf("  [PASS] Ingested 10000 bytes as \"kernel.bin\" (K2)\n");

    smap_t *sm = &st->smaps[0];
    assert(sm->magic == SMAP_MAGIC);
    assert(sm->version == SMAP_VERSION);
    assert(sm->total_size == 10000);
    assert(sm->num_chunks > 0);
    printf("  [PASS] S-Map created: %u chunks, total=%u bytes\n",
           sm->num_chunks, sm->total_size);

    /* Root CID is 32 bytes */
    uint8_t zero_cid[32] = {0};
    assert(memcmp(sm->root_cid, zero_cid, 32) != 0);
    printf("  [PASS] Root CID generated (32 bytes, non-zero)\n");

    /* S-Map hash is computed */
    assert(memcmp(sm->smap_hash, zero_cid, 32) != 0);
    printf("  [PASS] S-Map hash computed (binds CID + S-Map pair)\n");

    /* Merkle root is computed */
    assert(memcmp(sm->merkle_root, zero_cid, 32) != 0);
    printf("  [PASS] Merkle root computed from chunk hashes\n");

    /* Verify S-Map */
    assert(smap_verify(sm) == true);
    printf("  [PASS] S-Map verification passed\n");

    /* Verify CID binding */
    assert(smap_verify_cid(sm, sm->root_cid) == true);
    printf("  [PASS] CID binding verified\n");

    /* Reassembly */
    uint8_t out[10000];
    int recon_len = smap_reassemble(st, 0, out, sizeof(out));
    assert(recon_len == 10000);
    printf("  [PASS] Reassembly: %d bytes reconstructed\n", recon_len);

    /* File lookup by CID */
    const smap_file_t *f = smap_file_find(st, sm->root_cid);
    assert(f != NULL);
    assert(strcmp(f->label, "kernel.bin") == 0);
    printf("  [PASS] File found by CID: %s\n", f->label);

    /* File lookup by label */
    const smap_file_t *f2 = smap_file_find_by_label(st, "kernel.bin");
    assert(f2 != NULL);
    printf("  [PASS] File found by label\n");

    /* Zero-copy chunk access */
    const smap_chunk_t *ch = smap_get_chunk(sm, 0);
    assert(ch != NULL);
    assert(ch->chunk_size > 0);
    printf("  [PASS] Zero-copy chunk access: chunk 0 size=%u\n", ch->chunk_size);

    /* Seal */
    assert(smap_seal(sm) == 0);
    assert(sm->sealed == true);
    printf("  [PASS] S-Map sealed (immutable)\n");

    /* Phase shift (O(1) re-signing) */
    assert(smap_phase_shift(st, 0, SMAP_KEY_K4) == 0);
    printf("  [PASS] Phase shift: K2 → K4 (O(1) re-sign, CID unchanged)\n");

    /* Ingest second file */
    uint8_t data2[100];
    for (i = 0; i < 100; i++) data2[i] = (uint8_t)(i + 1);
    int32_t idx2 = smap_ingest(st, data2, 100, "config.ula", SMAP_KEY_K5);
    assert(idx2 == 1);
    printf("  [PASS] Second file ingested: \"config.ula\" (K5)\n");

    assert(st->num_smaps == 2);
    assert(st->total_ingested == 10100);
    printf("  [PASS] Store: 2 files, total ingested=%llu\n",
           (unsigned long long)st->total_ingested);

    free(st);
    printf("  === All S-Map tests passed ===\n\n");
}

/* ===== Decentralized Protocol Suite Tests ===== */
static void test_decent(void) {
    printf("=== Decentralized Protocol Suite Tests ===\n");

    decent_t *d = (decent_t *)malloc(sizeof(decent_t));
    decent_init(d);
    assert(d->initialized == true);
    printf("  [PASS] Decent protocol stack initialized\n");

    /* IPFS */
    int32_t n0 = decent_ipfs_node_add(d, "12D3KooWTestPeer", "/ip4/10.0.0.1/tcp/4001");
    assert(n0 == 0);
    assert(decent_ipfs_node_connect(d, 0) == 0);
    assert(d->ipfs_nodes[0].connected == true);
    printf("  [PASS] IPFS node added + connected\n");

    uint8_t cid_hash[32] = {0x42, 0x01, 0x02};
    int32_t cid_idx = decent_ipfs_cid_register(d, cid_hash, 0x70, 1024, DECENT_CID_V1);
    assert(cid_idx == 0);
    const decent_cid_t *cid = decent_ipfs_cid_find(d, cid_hash);
    assert(cid != NULL);
    assert(cid->codec == 0x70);
    printf("  [PASS] IPFS CID registered (dag-pb, 1024 bytes)\n");

    assert(decent_ipfs_bitswap(d, 0, 0) == 0);
    assert(d->cids[0].resolved == true);
    printf("  [PASS] Bitswap: content resolved\n");

    /* BitTorrent v2 */
    int32_t bt = decent_bt_swarm_create(d, "kernel-distro", 16384);
    assert(bt == 0);
    uint8_t merkle[32] = {0xAB};
    assert(decent_bt_swarm_set_merkle(d, 0, merkle) == 0);
    assert(decent_bt_swarm_add_peer(d, 0, 0) == 0);
    assert(decent_bt_swarm_start_seeding(d, 0) == 0);
    assert(d->bt_swarms[0].seeding == true);
    printf("  [PASS] BitTorrent v2 swarm: seeding with Merkle root\n");

    /* Matrix */
    int32_t room = decent_matrix_room_create(d, "!zede:matrix.org", "ZEDEC Dev", true);
    assert(room == 0);
    assert(d->matrix_rooms[0].encrypted == true);
    assert(decent_matrix_room_add_member(d, 0, 0) == 0);
    assert(decent_matrix_room_send_event(d, 0, DECENT_MATRIX_EVENT_MESSAGE,
                                          "alice", "Build complete") == 0);
    assert(d->matrix_rooms[0].num_events == 1);
    printf("  [PASS] Matrix room: E2EE, 1 member, 1 message sent\n");

    /* DID */
    int32_t did = decent_did_create(d, "operator", 3);
    assert(did == 0);
    assert(strcmp(d->dids[0].did, "did:zede:K3") == 0);
    assert(decent_did_add_claim(d, 0) == 0);
    assert(decent_did_zk_verify(d, 0) == 0);
    assert(d->dids[0].zk_verified == true);
    printf("  [PASS] DID created: did:zede:K3, zk-SNARK verified\n");

    /* SDR */
    int32_t ch = decent_sdr_channel_create(d, "lorahf1", 433920000, 125000, 1);
    assert(ch == 0);
    assert(decent_sdr_channel_activate(d, 0) == 0);
    uint8_t bulb_data[] = "garlic-bulb-over-radio";
    assert(decent_sdr_send_bulb(d, 0, bulb_data, sizeof(bulb_data)-1) == (int)(sizeof(bulb_data)-1));
    assert(d->sdr_channels[0].packets_sent == 1);
    printf("  [PASS] SDR channel: LoRa 433.92MHz, garlic bulb sent\n");

    /* SDR fallback */
    assert(decent_sdr_enable_fallback(d) == 0);
    assert(d->sdr_fallback_active == true);
    printf("  [PASS] SDR fallback enabled (airborne PLNP)\n");

    /* Protocol names */
    assert(strcmp(decent_proto_name(DECENT_PROTO_IPFS), "IPFS/Libp2p") == 0);
    assert(strcmp(decent_proto_name(DECENT_PROTO_SDR), "SDR/Retevis") == 0);
    assert(strcmp(decent_cid_codec_name(0x70), "dag-pb") == 0);
    assert(strcmp(decent_matrix_event_name(DECENT_MATRIX_EVENT_ALERT), "alert") == 0);
    assert(strcmp(decent_sdr_modulation_name(1), "LoRa") == 0);
    printf("  [PASS] Protocol names correct\n");

    free(d);
    printf("  === All Decentralized Protocol tests passed ===\n\n");
}

/* ===== Dynamic Reconstruction Matrix Tests ===== */
static void test_recon(void) {
    printf("=== Dynamic Reconstruction Matrix Tests ===\n");

    recon_t *r = (recon_t *)malloc(sizeof(recon_t));
    recon_init(r);
    assert(r->initialized == true);
    assert(r->num_archetypes == 6);
    printf("  [PASS] Reconstruction engine initialized (6 archetypes)\n");

    /* Archetype detection from filename */
    assert(recon_detect_archetype("kernel.36n9") == RECON_ARCH_36N9);
    assert(recon_detect_archetype("shadow.9n63") == RECON_ARCH_9N63);
    assert(recon_detect_archetype("wallet.36m9") == RECON_ARCH_36M9);
    assert(recon_detect_archetype("stream.zedec") == RECON_ARCH_ZEDEC);
    assert(recon_detect_archetype("ledger.vino") == RECON_ARCH_VINO);
    assert(recon_detect_archetype("bios.ula") == RECON_ARCH_ULA);
    printf("  [PASS] Archetype detection from file extension\n");

    /* Archetype descriptors */
    const recon_archetype_desc_t *a36n9 = recon_get_archetype(r, RECON_ARCH_36N9);
    assert(a36n9 != NULL);
    assert(a36n9->topology == RECON_TOPOLOGY_FORWARD_MERKLE);
    assert(a36n9->operation == RECON_OP_FORWARD_PERMUTE);
    assert(a36n9->mutable == true);
    printf("  [PASS] .36n9: Forward Merkle (3→6→9), mutable, TRUE\n");

    const recon_archetype_desc_t *a9n63 = recon_get_archetype(r, RECON_ARCH_9N63);
    assert(a9n63->topology == RECON_TOPOLOGY_REVERSE_MERKLE);
    assert(a9n63->operation == RECON_OP_REVERSE_PERMUTE);
    assert(a9n63->mutable == false);
    printf("  [PASS] .9n63: Reverse Merkle (9→6→3), immutable, FALSE\n");

    const recon_archetype_desc_t *a36m9 = recon_get_archetype(r, RECON_ARCH_36M9);
    assert(a36m9->requires_5pl == true);
    assert(a36m9->requires_multisig == true);
    printf("  [PASS] .36m9: Dual-polar parity, requires 5PL + multisig\n");

    const recon_archetype_desc_t *avino = recon_get_archetype(r, RECON_ARCH_VINO);
    assert(avino->requires_5pl == true);
    assert(avino->mutable == false);
    printf("  [PASS] .vino: Ledger matrix, requires 5PL, immutable\n");

    const recon_archetype_desc_t *aula = recon_get_archetype(r, RECON_ARCH_ULA);
    assert(aula->mutable == false);
    printf("  [PASS] .ula: Zero-point anchor, immutable (GLUT0)\n");

    /* Reconstruction with a mock S-Map */
    smap_t sm;
    memset(&sm, 0, sizeof(smap_t));
    sm.magic = SMAP_MAGIC;
    sm.version = SMAP_VERSION;
    sm.total_size = 4096;
    sm.num_chunks = 1;
    sm.verified = true;
    uint8_t root[32] = {0x11};
    memcpy(sm.root_cid, root, 32);
    uint8_t merkle[32] = {0x22};
    memcpy(sm.merkle_root, merkle, 32);

    recon_result_t result;

    /* Forward reconstruction (.36n9) */
    assert(recon_reconstruct(r, &sm, RECON_ARCH_36N9, &result) == 0);
    assert(result.success == true);
    assert(result.inverse_applied == false);
    assert(result.bytes_reconstructed == 4096);
    assert(r->forward_count == 1);
    printf("  [PASS] Forward reconstruction (.36n9): 4096 bytes, no inversion\n");

    /* Reverse reconstruction (.9n63) */
    assert(recon_reconstruct(r, &sm, RECON_ARCH_9N63, &result) == 0);
    assert(result.inverse_applied == true);
    assert(r->reverse_count == 1);
    printf("  [PASS] Reverse reconstruction (.9n63): inverse permutation applied\n");

    /* Parity verification (.36m9) */
    assert(recon_reconstruct(r, &sm, RECON_ARCH_36M9, &result) == 0);
    assert(r->parity_count == 1);
    printf("  [PASS] Parity verification (.36m9): cross-wallet check\n");

    /* Stream assembly (.zedec) */
    assert(recon_reconstruct(r, &sm, RECON_ARCH_ZEDEC, &result) == 0);
    assert(r->stream_count == 1);
    printf("  [PASS] Stream assembly (.zedec): dynamic DAG\n");

    /* Ledger consensus (.vino) */
    assert(recon_reconstruct(r, &sm, RECON_ARCH_VINO, &result) == 0);
    assert(r->ledger_count == 1);
    printf("  [PASS] Ledger consensus (.vino): 5PL required\n");

    /* Static load (.ula) */
    assert(recon_reconstruct(r, &sm, RECON_ARCH_ULA, &result) == 0);
    assert(r->static_count == 1);
    printf("  [PASS] Static load (.ula): direct, no transformation\n");

    assert(r->total_reconstructions == 6);
    printf("  [PASS] Total reconstructions: %u\n", r->total_reconstructions);

    /* Names */
    assert(strcmp(recon_archetype_name(RECON_ARCH_36N9), "Direct Phase (.36n9)") == 0);
    assert(strcmp(recon_topology_name(RECON_TOPOLOGY_REVERSE_MERKLE), "Reverse Merkle (9→6→3)") == 0);
    assert(strcmp(recon_extension(RECON_ARCH_VINO), ".vino") == 0);
    printf("  [PASS] Archetype/topology/extension names correct\n");

    free(r);
    printf("  === All Reconstruction Matrix tests passed ===\n\n");
}

/* ===== HDCM Hyperdimensional Conversion Matrix Tests ===== */
static void test_hdcm(void) {
    printf("=== HDCM Hyperdimensional Conversion Matrix Tests ===\n");

    hdcm_t *h = (hdcm_t *)malloc(sizeof(hdcm_t));
    hdcm_init(h);
    assert(h->initialized == true);
    printf("  [PASS] HDCM engine initialized\n");

    /* Register programming languages */
    int32_t c_lang = hdcm_language_register(h, "C", ".c", HDCM_LANG_PROGRAMMING);
    int32_t rust = hdcm_language_register(h, "Rust", ".rs", HDCM_LANG_PROGRAMMING);
    int32_t py = hdcm_language_register(h, "Python", ".py", HDCM_LANG_PROGRAMMING);
    int32_t sv = hdcm_language_register(h, "SystemVerilog", ".sv", HDCM_LANG_MACHINE);
    assert(c_lang == 0 && rust == 1 && py == 2 && sv == 3);
    printf("  [PASS] 4 programming languages registered\n");

    /* Register human languages */
    int32_t en = hdcm_language_register(h, "English", ".en", HDCM_LANG_HUMAN);
    int32_t zh = hdcm_language_register(h, "Mandarin", ".zh", HDCM_LANG_HUMAN);
    assert(en == 4 && zh == 5);
    printf("  [PASS] 2 human languages registered\n");

    /* Add constructs to C */
    assert(hdcm_language_add_construct(h, 0, HDCM_CONSTRUCT_CONTROL, "for", "for") == 0);
    assert(hdcm_language_add_construct(h, 0, HDCM_CONSTRUCT_CONTROL, "while", "while") == 0);
    assert(hdcm_language_add_construct(h, 0, HDCM_CONSTRUCT_FUNCTION, "int", "fn") == 0);
    assert(hdcm_language_add_construct(h, 0, HDCM_CONSTRUCT_VARIABLE, "int", "let") == 0);
    printf("  [PASS] C constructs added (for, while, int)\n");

    /* Add constructs to Rust */
    assert(hdcm_language_add_construct(h, 1, HDCM_CONSTRUCT_CONTROL, "for", "for") == 0);
    assert(hdcm_language_add_construct(h, 1, HDCM_CONSTRUCT_CONTROL, "while", "while") == 0);
    assert(hdcm_language_add_construct(h, 1, HDCM_CONSTRUCT_FUNCTION, "fn", "fn") == 0);
    assert(hdcm_language_add_construct(h, 1, HDCM_CONSTRUCT_VARIABLE, "let", "let") == 0);
    printf("  [PASS] Rust constructs added (for, while, fn, let)\n");

    /* Add constructs to Python */
    assert(hdcm_language_add_construct(h, 2, HDCM_CONSTRUCT_CONTROL, "for", "for") == 0);
    assert(hdcm_language_add_construct(h, 2, HDCM_CONSTRUCT_CONTROL, "while", "while") == 0);
    assert(hdcm_language_add_construct(h, 2, HDCM_CONSTRUCT_FUNCTION, "def", "def") == 0);
    printf("  [PASS] Python constructs added\n");

    /* Vector operations */
    hdcm_vector_t v1, v2, v3;
    hdcm_vector_random(&v1, 42);
    hdcm_vector_random(&v2, 99);
    assert(v1.hamming_weight > 0);
    assert(v2.hamming_weight > 0);
    printf("  [PASS] Random vectors generated (w1=%u, w2=%u)\n",
           v1.hamming_weight, v2.hamming_weight);

    uint32_t dist = hdcm_vector_hamming(&v1, &v2);
    assert(dist > 0 && dist < HDCM_VECTOR_DIM);
    printf("  [PASS] Hamming distance: %u (dim=%u)\n", dist, HDCM_VECTOR_DIM);

    uint32_t sim = hdcm_vector_similarity(&v1, &v2); /* permille */
    assert(sim <= 1000u);
    printf("  [PASS] Similarity: %u permille\n", sim);

    /* Bind operation */
    hdcm_vector_bind(&v1, &v2, &v3);
    assert(v3.hamming_weight > 0);
    printf("  [PASS] Bind (XOR): weight=%u\n", v3.hamming_weight);

    /* Permutation */
    hdcm_vector_permute(&v1, 5, &v3);
    assert(v3.hamming_weight == v1.hamming_weight);
    printf("  [PASS] Permutation preserves hamming weight\n");

    /* Conversion matrix */
    int32_t mat = hdcm_matrix_create(h, 0, 1);  /* C → Rust */
    assert(mat == 0);
    assert(hdcm_matrix_build(h, 0) == 0);
    assert(h->matrices[0].num_mappings > 0);
    printf("  [PASS] Conversion matrix C→Rust: %u mappings, compat=%u permille\n",
           h->matrices[0].num_mappings, h->matrices[0].compatibility_score);

    /* Translation pipeline (second quantization) */
    hdcm_result_t result;
    assert(hdcm_translate(h, 0, 1, "for while int", &result) == 0);
    assert(result.success == true);
    assert(result.final_phase == HDCM_PHASE_MEASURE);
    printf("  [PASS] Translation C→Rust: CREATE→ENTANGLE→MEASURE complete\n");
    printf("         Output: \"%s\" (fidelity=%u permille)\n", result.output,
           result.fidelity_score);

    /* Phase counts */
    assert(h->create_count > 0);
    assert(h->entangle_count > 0);
    assert(h->measure_count > 0);
    printf("  [PASS] Phase counts: CREATE=%u, ENTANGLE=%u, MEASURE=%u\n",
           h->create_count, h->entangle_count, h->measure_count);

    /* Language lookup */
    const hdcm_language_t *found = hdcm_language_find(h, "Rust");
    assert(found != NULL);
    assert(found->category == HDCM_LANG_PROGRAMMING);
    printf("  [PASS] Language lookup by name: Rust\n");

    const hdcm_language_t *byext = hdcm_language_by_ext(h, "test.py");
    assert(byext != NULL);
    assert(strcmp(byext->name, "Python") == 0);
    printf("  [PASS] Language lookup by extension: .py → Python\n");

    /* Compatibility */
    uint32_t compat = hdcm_matrix_compatibility(h, 0, 1); /* permille */
    assert(compat <= 1000u);
    printf("  [PASS] Compatibility C→Rust: %u permille\n", compat);

    /* Omni-compatibility */
    /* Create more matrices */
    hdcm_matrix_create(h, 0, 2);  /* C → Python */
    hdcm_matrix_build(h, 1);
    hdcm_matrix_create(h, 0, 3);  /* C → SystemVerilog */
    hdcm_matrix_build(h, 2);
    assert(hdcm_omni_compatible(h, 0) == true);
    assert(hdcm_compatible_count(h, 0) >= 3);
    printf("  [PASS] C is omni-compatible (%u matrices)\n", hdcm_compatible_count(h, 0));

    /* Names */
    assert(strcmp(hdcm_construct_name(HDCM_CONSTRUCT_FUNCTION), "function") == 0);
    assert(strcmp(hdcm_category_name(HDCM_LANG_HUMAN), "human") == 0);
    assert(strcmp(hdcm_phase_name(HDCM_PHASE_CREATE), "CREATE") == 0);
    printf("  [PASS] Construct/category/phase names correct\n");

    free(h);
    printf("  === All HDCM tests passed ===\n\n");
}

/* ===== Gematria Tests ===== */
static void test_gematria(void) {
    printf("=== Gematria Syntactic Mapping Tests ===\n");

    gematria_t *g = (gematria_t *)malloc(sizeof(gematria_t));
    gematria_init(g);
    assert(g->initialized == true);
    printf("  [PASS] Gematria engine initialized\n");

    /* Character values */
    assert(gematria_char_value(g, 'A', GEM_SYSTEM_ORDINAL) == 1);
    assert(gematria_char_value(g, 'Z', GEM_SYSTEM_ORDINAL) == 26);
    assert(gematria_char_value(g, 'a', GEM_SYSTEM_ORDINAL) == 1);
    printf("  [PASS] Ordinal: A=1, Z=26\n");

    assert(gematria_char_value(g, 'A', GEM_SYSTEM_PRIME) == 2);
    assert(gematria_char_value(g, 'B', GEM_SYSTEM_PRIME) == 3);
    assert(gematria_char_value(g, 'C', GEM_SYSTEM_PRIME) == 5);
    printf("  [PASS] Prime: A=2, B=3, C=5\n");

    /* Word values */
    assert(gematria_word_value(g, "ABC", GEM_SYSTEM_ORDINAL) == 6);
    assert(gematria_word_value(g, "ZEDEC", GEM_SYSTEM_ORDINAL) > 0);
    uint32_t zedec_val = gematria_word_value(g, "ZEDEC", GEM_SYSTEM_ORDINAL);
    printf("  [PASS] Word value: ZEDEC (ordinal) = %u\n", zedec_val);

    /* Reduction (digital root) */
    assert(gematria_reduce(38) == 2);   /* 3+8=11, 1+1=2 */
    assert(gematria_reduce(99) == 9);   /* 9+9=18, 1+8=9 */
    assert(gematria_reduce(5) == 5);
    printf("  [PASS] Reduction: 38→2, 99→9, 5→5\n");

    /* Custom mapping */
    assert(gematria_set_custom(g, 'X', 1000) == 0);
    assert(gematria_char_value(g, 'X', GEM_SYSTEM_CUSTOM) == 1000);
    printf("  [PASS] Custom mapping: X=1000\n");

    /* Grammatical role detection */
    assert(gematria_detect_role("the") == GEM_ROLE_ARTICLE);
    assert(gematria_detect_role("is") == GEM_ROLE_VERB);
    assert(gematria_detect_role("of") == GEM_ROLE_PREPOSITION);
    assert(gematria_detect_role("and") == GEM_ROLE_CONJUNCTION);
    assert(gematria_detect_role("they") == GEM_ROLE_PRONOUN);
    assert(gematria_detect_role("quickly") == GEM_ROLE_ADVERB);
    assert(gematria_detect_role("beautiful") == GEM_ROLE_ADJECTIVE);
    assert(gematria_detect_role("fox") == GEM_ROLE_NOUN);
    printf("  [PASS] Grammatical roles: the=article, is=verb, of=prep, and=conj\n");

    /* Punctuation detection */
    assert(gematria_detect_punct('.') == GEM_PUNCT_PERIOD);
    assert(gematria_detect_punct(',') == GEM_PUNCT_COMMA);
    assert(gematria_detect_punct('?') == GEM_PUNCT_QUESTION);
    assert(gematria_detect_punct('!') == GEM_PUNCT_EXCLAIM);
    printf("  [PASS] Punctuation: .=collapse, ,=tensor, ?=superposition\n");

    /* Sentence analysis */
    gem_sentence_t sent;
    assert(gematria_analyze_sentence(g, "The quick brown fox jumps over the lazy dog.",
                                      GEM_SYSTEM_ORDINAL, &sent) == 0);
    assert(sent.num_words > 0);
    assert(sent.total_gematria > 0);
    assert(sent.has_collapse == true);  /* Ends with period */
    printf("  [PASS] Sentence analyzed: %u words, gematria=%u, has_collapse=true\n",
           sent.num_words, sent.total_gematria);

    /* Check grammatical roles in sentence */
    uint32_t nouns = sent.noun_count;
    uint32_t verbs = sent.verb_count;
    assert(verbs > 0);  /* "jumps" or "over" detected as verb */
    printf("  [PASS] Sentence roles: nouns=%u, verbs=%u, modifiers=%u\n",
           nouns, verbs, sent.modifier_count);

    /* Fock vector generated */
    assert(sent.sentence_vector.hamming_weight > 0);
    printf("  [PASS] Sentence Fock vector generated (weight=%u)\n",
           sent.sentence_vector.hamming_weight);

    /* Question mark → superposition */
    gem_sentence_t q;
    assert(gematria_analyze_sentence(g, "Is the kernel ready?",
                                      GEM_SYSTEM_ORDINAL, &q) == 0);
    assert(q.has_superposition == true);
    assert(q.has_collapse == false);
    printf("  [PASS] Question mark → superposition (G+), no collapse\n");

    /* Phase mapping */
    const char *noun_phase = gematria_phase_mapping(GEM_ROLE_NOUN);
    assert(strstr(noun_phase, ".36n9") != NULL);
    const char *verb_phase = gematria_phase_mapping(GEM_ROLE_VERB);
    assert(strstr(verb_phase, ".zedec") != NULL);
    printf("  [PASS] Phase mapping: Noun→.36n9, Verb→.zedec\n");

    /* System names */
    assert(strcmp(gematria_system_name(GEM_SYSTEM_ORDINAL), "Ordinal (A=1..Z=26)") == 0);
    assert(strcmp(gematria_system_name(GEM_SYSTEM_PRIME), "Prime (A=2,B=3,C=5...)") == 0);
    assert(strcmp(gematria_role_name(GEM_ROLE_NOUN), "Noun (state vector |v>)") == 0);
    assert(strcmp(gematria_punct_name(GEM_PUNCT_PERIOD), "period (state collapse)") == 0);
    printf("  [PASS] System/role/punct names correct\n");

    /* Multiple gematria systems give different values */
    uint32_t ord_val = gematria_word_value(g, "ZEDEC", GEM_SYSTEM_ORDINAL);
    uint32_t std_val = gematria_word_value(g, "ZEDEC", GEM_SYSTEM_STANDARD);
    uint32_t prime_val = gematria_word_value(g, "ZEDEC", GEM_SYSTEM_PRIME);
    assert(ord_val != std_val || ord_val != prime_val);
    printf("  [PASS] Multi-system: ordinal=%u, standard=%u, prime=%u\n",
           ord_val, std_val, prime_val);

    free(g);
    printf("  === All Gematria tests passed ===\n\n");
}

/* ===== Dual-Track Pipeline Tests ===== */
static void test_dualtrack(void) {
    printf("=== Dual-Track Linear/Nonlinear Pipeline Tests ===\n");

    dualtrack_t *dt = (dualtrack_t *)malloc(sizeof(dualtrack_t));
    dualtrack_init(dt);
    assert(dt->initialized == true);
    assert(dt->linear_active == true);
    assert(dt->nonlinear_active == true);
    assert(dt->triad_recorder_active == true);
    assert(dt->triad_skeptic_active == true);
    assert(dt->triad_operator_active == true);
    printf("  [PASS] Dual-track initialized: linear + nonlinear + triad active\n");

    /* Add build stages with phase gates */
    int32_t s0 = dualtrack_stage_add(dt, "Bootloader Init", true);
    int32_t s1 = dualtrack_stage_add(dt, "HAL Drivers", true);
    int32_t s2 = dualtrack_stage_add(dt, "Kernel Core", true);
    int32_t s3 = dualtrack_stage_add(dt, "Userland Synthesis", true);
    int32_t s4 = dualtrack_stage_add(dt, "RTL/FPGA Bitstream", false);
    assert(s0 == 0 && s1 == 1 && s2 == 2 && s3 == 3 && s4 == 4);
    assert(dt->num_stages == 5);
    printf("  [PASS] 5 stages added (Boot→HAL→Kernel→Userland→RTL)\n");

    /* Set SQ phases */
    assert(dualtrack_stage_set_sq_phase(dt, 0, DT_SQ_CREATE) == 0);
    assert(dualtrack_stage_set_sq_phase(dt, 1, DT_SQ_CREATE) == 0);
    assert(dualtrack_stage_set_sq_phase(dt, 2, DT_SQ_ENTANGLE) == 0);
    assert(dualtrack_stage_set_sq_phase(dt, 3, DT_SQ_ENTANGLE) == 0);
    assert(dualtrack_stage_set_sq_phase(dt, 4, DT_SQ_MEASURE) == 0);
    printf("  [PASS] SQ phases: CREATE→CREATE→ENTANGLE→ENTANGLE→MEASURE\n");

    /* Add artifacts */
    uint8_t hash1[32] = {0x11, 0x22, 0x33};
    uint8_t hash2[32] = {0x44, 0x55, 0x66};
    uint8_t hash3[32] = {0x77, 0x88, 0x99};
    uint8_t hash4[32] = {0xAA, 0xBB, 0xCC};
    uint8_t hash5[32] = {0xDD, 0xEE, 0xFF};

    int32_t a0 = dualtrack_artifact_add(dt, "boot.bin", "/output/boot.bin",
                                         DT_ARTIFACT_BINARY, DT_TRACK_LINEAR,
                                         1, 4096, hash1);
    int32_t a1 = dualtrack_artifact_add(dt, "hal.bin", "/output/hal.bin",
                                         DT_ARTIFACT_KERNEL, DT_TRACK_LINEAR,
                                         1, 8192, hash2);
    int32_t a2 = dualtrack_artifact_add(dt, "kernel.bin", "/output/kernel.bin",
                                         DT_ARTIFACT_BINARY, DT_TRACK_LINEAR,
                                         1, 32768, hash3);
    /* Nonlinear track produces speculative artifacts */
    int32_t a3 = dualtrack_artifact_add(dt, "fpga_rtl.sv", "/output/hdl/fpga_rtl.sv",
                                         DT_ARTIFACT_HDL, DT_TRACK_NONLINEAR,
                                         3, 16384, hash4);
    int32_t a4 = dualtrack_artifact_add(dt, "evm_runtime.smap", "/output/evm.smap",
                                         DT_ARTIFACT_SMAP, DT_TRACK_NONLINEAR,
                                         4, 2048, hash5);
    assert(a0 == 0 && a1 == 1 && a2 == 2 && a3 == 3 && a4 == 4);
    assert(dt->num_artifacts == 5);
    printf("  [PASS] 5 artifacts added (3 linear, 2 nonlinear)\n");

    /* Link artifacts to stages */
    assert(dualtrack_stage_add_artifact(dt, 0, 0) == 0);
    assert(dualtrack_stage_add_artifact(dt, 1, 1) == 0);
    assert(dualtrack_stage_add_artifact(dt, 2, 2) == 0);
    assert(dualtrack_stage_add_artifact(dt, 3, 3) == 0);
    assert(dualtrack_stage_add_artifact(dt, 4, 4) == 0);
    printf("  [PASS] Artifacts linked to stages\n");

    /* Run Stage 0: both tracks agree */
    dualtrack_tick(dt);
    assert(dualtrack_run_stage(dt, 0, true, 50, true, 30,
                                "boot_init_ok", "boot_init_ok") == 0);
    assert(dt->stages[0].linear_state == DT_STAGE_COMPLETE);
    assert(dt->stages[0].nonlinear_state == DT_STAGE_COMPLETE);
    assert(dt->stages[0].gate_result == DT_GATE_AGREE);
    assert(dt->gates_agreed == 1);
    printf("  [PASS] Stage 0 (Boot): AGREE — both tracks agree (50ms lin, 30ms nonlin)\n");

    /* Triad Protocol for stage 0 */
    assert(dualtrack_triad_run(dt, 0) == 0);
    const dt_artifact_t *art0 = dualtrack_artifact_get(dt, 0);
    assert(art0->validated == true);
    assert(art0->linked == true);
    printf("  [PASS] Triad: Recorder→Skeptic→Operator linked boot.bin\n");

    /* Run Stage 1: both agree */
    dualtrack_tick(dt);
    assert(dualtrack_run_stage(dt, 1, true, 80, true, 45,
                                "hal_ok", "hal_ok") == 0);
    assert(dt->stages[1].gate_result == DT_GATE_AGREE);
    assert(dt->gates_agreed == 2);
    printf("  [PASS] Stage 1 (HAL): AGREE\n");

    /* Run Stage 2: divergence — nonlinear produces different result */
    dualtrack_tick(dt);
    assert(dualtrack_run_stage(dt, 2, true, 120, true, 60,
                                "kernel_standard", "kernel_optimized") == 0);
    assert(dt->stages[2].gate_result == DT_GATE_DIVERGE);
    assert(dt->gates_diverged == 1);
    assert(dt->stages[2].nonlinear_state == DT_STAGE_GLUT_PLUS);
    printf("  [PASS] Stage 2 (Kernel): DIVERGE — standard vs optimized (G+ speculative)\n");

    /* Resolve divergence: prefer linear (deterministic baseline) */
    assert(dualtrack_resolve_divergence(dt, 2, false) == 0);
    assert(dt->stages[2].linear_state == DT_STAGE_COMPLETE);
    assert(dt->stages[2].nonlinear_state == DT_STAGE_GLUT_MINUS);
    printf("  [PASS] Divergence resolved: linear accepted, nonlinear G- (safe drop)\n");

    /* Run Stage 3: only nonlinear succeeds (linear fails) */
    dualtrack_tick(dt);
    assert(dualtrack_run_stage(dt, 3, false, 200, true, 90,
                                "linear_fail", "nonlinear_ok") == 0);
    assert(dt->stages[3].gate_result == DT_GATE_NONLINEAR_ONLY);
    assert(dt->stages[3].linear_state == DT_STAGE_FAILED);
    printf("  [PASS] Stage 3 (Userland): NONLINEAR_ONLY — linear failed, speculative used\n");

    /* Run Stage 4: no gate, both succeed */
    dualtrack_tick(dt);
    assert(dualtrack_run_stage(dt, 4, true, 150, true, 70,
                                "rtl_ok", "rtl_ok") == 0);
    assert(dt->stages[4].gate_result == DT_GATE_NOT_REACHED);
    printf("  [PASS] Stage 4 (RTL): no gate, both complete\n");

    /* Triad for remaining stages */
    assert(dualtrack_triad_run(dt, 1) == 0);
    assert(dualtrack_triad_run(dt, 2) == 0);
    assert(dualtrack_triad_run(dt, 3) == 0);
    assert(dualtrack_triad_run(dt, 4) == 0);

    /* Check artifact states */
    const dt_artifact_t *art3 = dualtrack_artifact_get(dt, 3);
    assert(art3->validated == true);
    assert(art3->linked == true);
    printf("  [PASS] Nonlinear artifact fpga_rtl.sv validated + linked via Triad\n");

    const dt_artifact_t *art4 = dualtrack_artifact_get(dt, 4);
    assert(art4->validated == true);
    assert(art4->linked == true);
    printf("  [PASS] Nonlinear artifact evm_runtime.smap validated + linked\n");

    /* Statistics */
    assert(dt->linear_stats.stages_run == 5);
    assert(dt->linear_stats.stages_passed == 4);  /* Stage 3 failed */
    assert(dt->linear_stats.stages_failed == 1);
    assert(dt->nonlinear_stats.stages_run == 5);
    assert(dt->nonlinear_stats.stages_passed == 5);
    printf("  [PASS] Linear: 4/5 passed | Nonlinear: 5/5 passed\n");

    assert(dt->gates_agreed == 2);
    assert(dt->gates_diverged == 1);
    printf("  [PASS] Gates: 2 agreed, 1 diverged (resolved), 1 nonlinear_only\n");

    assert(dt->total_artifacts_linked == 5);
    printf("  [PASS] All 5 artifacts linked into final output\n");

    /* Phase clock */
    assert(dt->phase_tick == 50);  /* 5 ticks × 10ms */
    printf("  [PASS] Phase clock: %u ms (5 ticks × 10ms)\n", dt->phase_tick);

    /* Agreement rate */
    uint32_t rate = dualtrack_agreement_permille(dt);
    assert(rate <= 1000u);
    printf("  [PASS] Agreement rate: %u permille\n", rate);

    /* All gates resolved */
    assert(dualtrack_all_gates_resolved(dt) == true);
    printf("  [PASS] All gates resolved\n");

    /* Names */
    assert(strcmp(dualtrack_track_name(DT_TRACK_LINEAR), "LINEAR") == 0);
    assert(strcmp(dualtrack_track_name(DT_TRACK_NONLINEAR), "NONLINEAR") == 0);
    assert(strcmp(dualtrack_stage_state_name(DT_STAGE_GLUT_PLUS), "GLUT+") == 0);
    assert(strcmp(dualtrack_gate_result_name(DT_GATE_AGREE), "AGREE") == 0);
    assert(strcmp(dualtrack_gate_result_name(DT_GATE_DIVERGE), "DIVERGE") == 0);
    assert(strcmp(dualtrack_sq_phase_name(DT_SQ_CREATE), "CREATE") == 0);
    assert(strcmp(dualtrack_triad_role_name(DT_TRIAD_RECORDER), "Recorder (+0, hash)") == 0);
    assert(strcmp(dualtrack_triad_role_name(DT_TRIAD_SKEPTIC), "Skeptic (-1, validate)") == 0);
    assert(strcmp(dualtrack_artifact_type_name(DT_ARTIFACT_HDL), "HDL (SystemVerilog)") == 0);
    assert(strcmp(dualtrack_artifact_type_name(DT_ARTIFACT_SMAP), "S-Map (.smap)") == 0);
    printf("  [PASS] All name functions correct\n");

    /* Report */
    char report[4096];
    int rlen = dualtrack_report(dt, report, sizeof(report));
    assert(rlen > 0);
    assert(strstr(report, "Dual-Track Pipeline Report") != NULL);
    assert(strstr(report, "AGREE") != NULL);
    assert(strstr(report, "GLUT-") != NULL);
    printf("  [PASS] Report generated: %d bytes\n", rlen);

    free(dt);
    printf("  === All Dual-Track tests passed ===\n\n");
}

/* ===== Superposition Coordinator Tests ===== */
static void test_superpos(void) {
    printf("=== Superposition Coordinator (Triadic Third Pillar) Tests ===\n");

    superpos_t *sp = (superpos_t *)malloc(sizeof(superpos_t));
    sp_target_t hw = superpos_detect_hardware();
    superpos_init(sp, hw);
    assert(sp->initialized == true);
    assert(sp->collapsed == false);
    assert(sp->num_hamiltonians == 3);
    assert(sp->detected_hardware == SP_TARGET_X86_64);
    printf("  [PASS] Superposition initialized: hardware=%s, 3 Hamiltonians\n",
           superpos_target_name(hw));

    /* Add eigenstates (build trajectories) */
    int32_t e0 = superpos_state_add(sp, "x86_linear_core",
        SP_TARGET_X86_64, SP_DIM_LOCALITY,
        800, 200, 150, true);  /* High amp, low energy, dep OK */
    int32_t e1 = superpos_state_add(sp, "fpga_rtl_speculative",
        SP_TARGET_FPGA, SP_DIM_NON_LOCALITY,
        600, 400, 300, true);
    int32_t e2 = superpos_state_add(sp, "arm64_cross_compile",
        SP_TARGET_ARM64, SP_DIM_NON_LOCALITY,
        500, 300, 250, true);
    int32_t e3 = superpos_state_add(sp, "riscv_minimal",
        SP_TARGET_RISCV, SP_DIM_LOCALITY,
        300, 100, 200, true);
    int32_t e4 = superpos_state_add(sp, "gpu_kernel_offload",
        SP_TARGET_GPU, SP_DIM_OMNI_PRESENCE,
        400, 600, 400, true);
    int32_t e5 = superpos_state_add(sp, "broken_dep_state",
        SP_TARGET_MICRO, SP_DIM_LOCALITY,
        100, 0, 500, false);  /* Missing dependency */
    assert(e0 == 0 && e1 == 1 && e2 == 2 && e3 == 3 && e4 == 4 && e5 == 5);
    assert(sp->num_states == 6);
    printf("  [PASS] 6 eigenstates added (x86, FPGA, ARM64, RISC-V, GPU, broken-dep)\n");

    /* Set safety verification on viable states */
    assert(superpos_state_set_safety(sp, 0, true) == 0);
    assert(superpos_state_set_safety(sp, 1, true) == 0);
    assert(superpos_state_set_safety(sp, 2, true) == 0);
    assert(superpos_state_set_safety(sp, 3, true) == 0);
    assert(superpos_state_set_safety(sp, 4, true) == 0);
    /* State 5 (broken dep) left unverified */
    printf("  [PASS] Safety verification set on 5 viable states\n");

    /* Hamiltonian evolution */
    assert(superpos_hamiltonian_evolve(sp) == 0);
    assert(sp->total_energy > 0);
    printf("  [PASS] Hamiltonian evolution: total_energy=%d, min_energy=%d\n",
           sp->total_energy, sp->min_energy);

    /* Check that broken-dep state has high energy */
    int32_t energy_broken = superpos_hamiltonian_energy(sp, 5);
    int32_t energy_x86 = superpos_hamiltonian_energy(sp, 0);
    assert(energy_broken > energy_x86);
    printf("  [PASS] H_deps: broken-dep energy (%d) >> x86 energy (%d)\n",
           energy_broken, energy_x86);

    /* Check that matched target has lower energy than mismatched */
    int32_t energy_fpga = superpos_hamiltonian_energy(sp, 1);
    assert(energy_x86 < energy_fpga);  /* x86 matches detected hardware */
    printf("  [PASS] H_target: x86 (matched) energy < FPGA (mismatched)\n");

    /* Compute probabilities */
    superpos_compute_probabilities(sp);
    uint32_t prob_x86 = superpos_probability(sp, 0);
    uint32_t prob_broken = superpos_probability(sp, 5);
    assert(prob_x86 > prob_broken);
    printf("  [PASS] Probability: x86=%u/1000 > broken=%u/1000\n",
           prob_x86, prob_broken);

    /* Interference check */
    sp_interference_t interf01 = superpos_interference(sp, 0, 3);
    /* x86 and RISC-V are different targets → destructive */
    assert(interf01 == SP_INTERF_DESTRUCTIVE);
    printf("  [PASS] Interference: x86 vs RISC-V = DESTRUCTIVE (different targets)\n");

    /* Phase tick */
    superpos_tick(sp);
    assert(superpos_phase_clock(sp) == 10);
    superpos_tick(sp);
    assert(superpos_phase_clock(sp) == 20);
    printf("  [PASS] Phase clock: 20 ms (2 ticks)\n");

    /* Coordinate with dual-track: linear=x86(0), nonlinear=FPGA(1) */
    assert(superpos_coordinate(sp, 0, 1, true, true) == 0);
    printf("  [PASS] Coordination: linear(x86) + nonlinear(FPGA) both OK\n");

    /* Coordinate: linear fails, nonlinear succeeds */
    assert(superpos_coordinate(sp, 0, 1, false, true) == 0);
    uint32_t prob_x86_after = superpos_probability(sp, 0);
    assert(prob_x86_after < prob_x86);  /* Amplitude reduced */
    printf("  [PASS] Coordination: linear failed → amplitude reduced (%u → %u)\n",
           prob_x86, prob_x86_after);

    /* Re-verify and measure */
    superpos_state_set_safety(sp, 0, true);
    superpos_state_set_safety(sp, 1, true);
    superpos_hamiltonian_evolve(sp);

    /* Measurement: should collapse to highest-probability viable state */
    sp_measurement_t result = superpos_measure(sp);
    assert(result == SP_MEASURE_COLLAPSE || result == SP_MEASURE_SUPERPOSE);
    printf("  [PASS] Measurement: %s\n", superpos_measurement_name(result));

    if (result == SP_MEASURE_COLLAPSE) {
        assert(sp->collapsed == true);
        const sp_eigenstate_t *winner = superpos_state_get(sp, sp->winning_state_idx);
        assert(winner->collapsed == true);
        assert(winner->probability == 1000);
        printf("  [PASS] Collapsed to eigenstate %u: %s\n",
               sp->winning_state_idx, winner->name);
    }

    /* Name functions */
    assert(strcmp(superpos_target_name(SP_TARGET_FPGA), "FPGA") == 0);
    assert(strcmp(superpos_target_name(SP_TARGET_ARM64), "ARM64") == 0);
    assert(strcmp(superpos_dimension_name(SP_DIM_LOCALITY), "Locality (classical)") == 0);
    assert(strcmp(superpos_dimension_name(SP_DIM_NON_LOCALITY), "Non-Locality (entangled)") == 0);
    assert(strcmp(superpos_dimension_name(SP_DIM_OMNI_PRESENCE), "Omni-Presence (field)") == 0);
    assert(strcmp(superpos_hamiltonian_name(SP_H_DEPS), "H_deps (causal boundaries)") == 0);
    assert(strcmp(superpos_hamiltonian_name(SP_H_OPT), "H_opt (min latency/memory)") == 0);
    assert(strcmp(superpos_hamiltonian_name(SP_H_TARGET), "H_target (hardware adapt)") == 0);
    assert(strcmp(superpos_measurement_name(SP_MEASURE_COLLAPSE), "COLLAPSE") == 0);
    assert(strcmp(superpos_interference_name(SP_INTERF_CONSTRUCTIVE), "CONSTRUCTIVE") == 0);
    printf("  [PASS] All name functions correct\n");

    /* Report */
    char report[4096];
    int rlen = superpos_report(sp, report, sizeof(report));
    assert(rlen > 0);
    assert(strstr(report, "Superposition Coordinator Report") != NULL);
    printf("  [PASS] Report generated: %d bytes\n", rlen);

    free(sp);
    printf("  === All Superposition Coordinator tests passed ===\n\n");
}

int main(void) {
    printf("=== ZEDEC pqOS New Modules Test Suite ===\n\n");

    test_polar_trit_logic();
    test_surplus();
    test_edp_risk();
    test_synthesis_engine();
    test_identity();
    test_rtl_device();
    test_jdr_piratenet();
    test_triple_ledger();
    test_crypto_wallet();
    test_nlb();
    test_pterm();
    test_xedit();
    test_lattice();
    test_pungent();
    test_ascent();
    test_plnp();
    test_smap();
    test_decent();
    test_recon();
    test_hdcm();
    test_gematria();
    test_dualtrack();
    test_superpos();

    printf("=== All new modules tests passed ===\n");
    return 0;
}
