/* Copyright (c) 2024-2026 Michael Laurence Curzi and 36N9 Genetics, LLC */
/* SPDX-License-Identifier: Apache-2.0 */
/* size_probe.c — a firmware-shaped program that links one slice of the
 * embedded subset, so `make size` can report what each slice costs in
 * flash and RAM after --gc-sections. One of:
 *
 *   -DPROBE_PURE    rationals, four-valued logic, hexagram tags, SHA3-256
 *                   (no context, no allocation)
 *   -DPROBE_CORE    PURE + context, capabilities, rmag budgets, phase clock,
 *                   quarantine log, zxe_ledger, zxe_clock (the MCU set)
 *   -DPROBE_LEDGER  CORE + libzxv's own ledger (code only: its RAM need,
 *                   one ~24 MB kernel book, rules it out on an MCU)
 *   -DPROBE_PQC     CORE + ML-KEM-1024, ML-DSA-87, Ed25519 (build with
 *                   ZXE_PQC=1; key and signature buffers are static here)
 *
 * The probe never runs on hardware; it is a link-time measurement. The
 * entropy hook here is a placeholder that ALWAYS FAILS, which is the
 * correct default: a real board installs its TRNG. */
#include <stdint.h>
#include <string.h>

#include "zxe.h"
#include "zxp.h"

volatile int64_t g_sink;

static int board_entropy_placeholder(void *user, uint8_t *buf, size_t len)
{
    (void) user;
    (void) buf;
    (void) len;
    return -1; /* integrator's responsibility: replace with the board TRNG */
}

static void pure(void)
{
    int64_t n = 0, d = 1;
    int32_t t = 0;
    uint8_t tag = 0, h[32];
    char buf[32];
    size_t len;
    zxv_rat_parse("12.75", &n, &d);
    zxv_rat_add(n, d, 1, 3, &n, &d);
    zxv_rat_mul(n, d, 2, 7, &n, &d);
    zxv_rat_div(n, d, 5, 11, &n, &d);
    zxv_rat_format(n, d, buf, sizeof buf, &len);
    zxv_lp_and(ZXV_LP_TRUE, ZXV_LP_BOTH, &t);
    zxv_lp_or(t, ZXV_LP_NEITHER, &t);
    zxv_hex_pack(ZXV_HEX_BASE_TRUE, ZXV_HEX_POL_NEUTRAL, ZXV_HEX_PHASE_MANIFEST, &tag);
    zxv_hex_and(tag, tag, &tag);
    zxv_hex_action(tag, &t);
    zxv_sha3_256((const uint8_t *) buf, len, h);
    g_sink = n + d + t + h[0];
}

#if defined(PROBE_CORE) || defined(PROBE_LEDGER) || defined(PROBE_PQC)
static zxe_account accounts[4];
static zxe_refkey refs[64];

static void core(void)
{
    zxv_ctx *ctx;
    zxv_cap *cap;
    zxv_rmag *rm;
    zxv_phase *ph;
    zxv_hexlog *log;
    zxe_ledger L;
    zxe_clock clk;
    uint32_t b = 0, a0 = 0, a1 = 0;
    uint64_t id = 0, seq = 0;
    uint8_t subj[32] = {0};
    if (zxv_ctx_create(&ctx) != ZXV_OK) return;
    zxv_cap_create(ctx, 1, NULL, ZXV_CAP_ALL, &cap);
    zxv_rmag_create_cap(cap, &rm);
    zxv_rmag_budget(cap, rm, "energy", ZXV_RES_ENERGY, 5, 2, 1, &b);
    zxv_rmag_consume(cap, rm, b, 1, 3);
    zxv_rmag_release(cap, rm, b, 1, 3);
    zxv_phase_create_cap(cap, &ph);
    zxe_clock_init(&clk, ZXV_PHASE_CYCLE);
    zxe_clock_pump(&clk, cap, ph, NULL, NULL, NULL);
    zxv_hexlog_create(cap, 8, &log);
    zxv_hexlog_append(cap, log, 0x28, 1, 1, subj, &seq);
    zxv_hexlog_verify(log);
    zxe_ledger_init(&L, ctx, accounts, 4, refs, 64);
    zxe_ledger_ccy_enable(&L, cap, "EUR", 2);
    zxe_ledger_open_account(&L, cap, "issuer", "EUR", ZXV_ACCT_ISSUER, &a0);
    zxe_ledger_open_account(&L, cap, "meter", "EUR", ZXV_ACCT_HOLDER, &a1);
    zxe_ledger_post(&L, cap, a0, a1, 100, "EUR", "r1", &id);
    zxe_ledger_check(&L, cap);
#    if defined(PROBE_LEDGER)
    zxv_ccy_enable_with_minor_cap(cap, "EUR", 2);
    zxv_ledger_open_account_cap(cap, "a", "EUR", ZXV_ACCT_ISSUER, &a0);
    zxv_ledger_open_account_cap(cap, "b", "EUR", ZXV_ACCT_HOLDER, &a1);
    zxv_ledger_post_cap(cap, a0, a1, 100, "EUR", "r1", &id);
    zxv_ledger_check_cap(cap);
#    endif
#    if defined(PROBE_PQC)
    static uint8_t ek[ZXV_MLKEM1024_EK_BYTES], dk[ZXV_MLKEM1024_DK_BYTES], ct[ZXV_MLKEM1024_CT_BYTES];
    static uint8_t pk[ZXV_MLDSA87_PK_BYTES], sk[ZXV_MLDSA87_SK_BYTES], sig[ZXV_MLDSA87_SIG_BYTES];
    uint8_t ss[32], epk[32], esk[32], esig[64];
    zxv_mlkem1024_keygen(cap, ek, dk);
    zxv_mlkem1024_encaps(cap, ek, ct, ss);
    zxv_mlkem1024_decaps(cap, dk, ct, ss);
    zxv_mldsa87_keygen(cap, pk, sk);
    zxv_mldsa87_sign(cap, sk, ss, 32, NULL, 0, sig);
    zxv_mldsa87_verify(cap, pk, ss, 32, NULL, 0, sig);
    zxv_ed25519_keygen(cap, epk, esk);
    zxv_ed25519_sign(cap, esk, ss, 32, esig);
    zxv_ed25519_verify(cap, epk, ss, 32, esig);
#    endif
    g_sink = (int64_t) (id + seq);
    zxe_ledger_destroy(&L);
    zxv_hexlog_destroy(log);
    zxv_phase_destroy(ph);
    zxv_rmag_destroy(rm);
    zxv_cap_destroy(cap);
    zxv_ctx_destroy(ctx);
}
#endif

int main(void)
{
    zxp_set_entropy(board_entropy_placeholder, NULL);
    if (zxp_init() != ZXP_OK) g_sink = -1; /* fails closed with the placeholder */
    pure();
#if defined(PROBE_CORE) || defined(PROBE_LEDGER) || defined(PROBE_PQC)
    core();
#endif
    return 0;
}
