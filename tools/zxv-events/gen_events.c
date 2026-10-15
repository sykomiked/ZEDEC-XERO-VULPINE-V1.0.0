/* Copyright (c) 2024-2026 Michael Laurence Curzi and 36N9 Genetics, LLC */
/* SPDX-License-Identifier: Apache-2.0 */
/* gen_events.c — test_zxv_events.sh's log maker: drives a real chg_abi
 * session (kernel/src/chiglet/chg_abi.c) through ran, failed, refused, held,
 * accepted and dropped calls, and prints its journal the way the arm64
 * governor does (kernel_main_arm64.c gov_evt and gov_events), two boots, the
 * second signed with ML-DSA-65 from a fixed seed (a test key).
 */
#include <stdio.h>
#include <string.h>

#include "chg_abi.h"
#include "pq_security.h"

static lpres_state_t rule(void *ctx, const chga_call_t *c)
{
    (void) ctx;
    return c->op == CHGA_NETWORK_PULSE ? LPRES_STATE_FALSE : LPRES_STATE_TRUE;
}

static int run(void *ctx, const chga_call_t *c)
{
    (void) ctx;
    return c->op == CHGA_MODEL_EXEC ? -5 : 0;
}

static void hex(const uint8_t *b, size_t n)
{
    for (size_t i = 0; i < n; i++) printf("%02x", b[i]);
}

static void drain(chga_session_t *s, unsigned long *n)
{
    chga_rec_t r;
    uint8_t b[CHGA_REC_BYTES];
    while (chga_pop(s, &r)) {
        chga_rec_encode(&r, b);
        printf("[EVT] %llu ", (unsigned long long) r.seq);
        hex(b, sizeof b);
        printf(" ");
        hex(r.head, CHGA_DIGEST);
        printf("\r\n  [chiglet] #%llu %s\r\n", (unsigned long long) r.seq, chga_op_name(r.op));
        (*n)++;
    }
}

static int call(chga_session_t *s, const char *text, uint8_t from, chga_result_t *res)
{
    chga_call_t c;
    if (chga_parse(text, strlen(text), &c) != CHGA_OK) return -1;
    return chga_submit(s, &c, from, res);
}

int main(void)
{
    static chga_session_t s;
    chga_result_t res;
    unsigned long n = 0;
    int bad = 0;

    printf("[BOOT] Chiglet governor (verified ABI: typed calls, gated, held for you)...\r\n");
    bad |= chga_init(&s, (1u << CHGA_OPS) - 1u, rule, 0, run, 0);
    bad |= call(&s, "cell_quarantine 0", CHGA_FROM_CHIGLET, &res);
    bad |= chga_refuse(&s, res.seq);
    bad |= call(&s, "storage_dma_read 0 9", CHGA_FROM_CHIGLET, &res);
    drain(&s, &n);
    printf("serial noise between calls\r\n");
    bad |= call(&s, "storage_dma_read 0 2", CHGA_FROM_PERSON, &res);
    bad |= call(&s, "model_exec 6918188751797616640 1", CHGA_FROM_CHIGLET, &res);
    drain(&s, &n);

    /* a second boot: a new chain from zero */
    printf("[BOOT] Chiglet governor (verified ABI: typed calls, gated, held for you)...\r\n");
    n = 0;
    bad |= chga_init(&s, (1u << CHGA_OPS) - 1u, rule, 0, run, 0);
    bad |= call(&s, "cell_reload 1", CHGA_FROM_CHIGLET, &res);
    bad |= chga_accept(&s, res.seq, res.digest, CHGA_DIGEST, &res);
    bad |= call(&s, "network_pulse", CHGA_FROM_PERSON, &res);
    bad |= call(&s, "framebuffer_blit 0 0 4 4", CHGA_FROM_CHIGLET, &res);
    drain(&s, &n);

    static uint8_t pk[PQ_MLDSA65_PK_BYTES], sk[PQ_MLDSA65_SK_BYTES], sig[PQ_MLDSA65_SIG_BYTES];
    uint8_t seed[32], msg[8 + CHGA_DIGEST];
    static const uint8_t ctx[] = "zxv/chga/head/v1";
    for (unsigned i = 0; i < sizeof seed; i++) seed[i] = (uint8_t) (i * 11u + 5u);
    pq_mldsa65_keygen(seed, pk, sk);
    const uint8_t *head = chga_chain_head(&s);
    for (unsigned k = 0; k < 8; k++) msg[k] = (uint8_t) ((unsigned long long) n >> (8 * k));
    memcpy(msg + 8, head, CHGA_DIGEST);
    pq_mldsa65_sign(sk, msg, sizeof msg, ctx, sizeof ctx - 1, 0, sig);
    printf("[EVT] head %lu ", n);
    hex(head, CHGA_DIGEST);
    printf("\r\n[EVT] key ");
    hex(pk, sizeof pk);
    printf("\r\n[EVT] sig ");
    hex(sig, sizeof sig);
    printf("\r\n");
    if (bad) fprintf(stderr, "gen_events: a call was not accepted\n");
    return bad ? 1 : 0;
}
