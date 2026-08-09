/* test_zab_exec.c — ZAB virtual machine tests.
 *
 * The headline test is THE UNGRANTED EFFECT: a perfectly valid, correctly
 * sealed program containing SEND, run by a caller who did not grant NET. The
 * network handler is present and would happily fire. It must not be reached.
 *
 * Build/run:
 *   gcc -std=c11 -Wall -Wextra -Werror -Isrc/zab src/zab/test_zab_exec.c \
 *       src/zab/zab_exec.c src/zab/zab.c -o /tmp/test_zab_exec && /tmp/test_zab_exec
 *
 * Author: H.M. Michael-Laurence: Curzi (c)
 * Copyright (c) 2024-2026 Michael Laurence Curzi and 36N9 Genetics, LLC
 * SPDX-License-Identifier: LicenseRef-OPL-1.1 AND CC-BY-SA-4.0
 */
#include <stdio.h>
#include <string.h>
#include "zab_exec.h"

static int failures = 0;
#define CHECK(cond, msg) do { \
    if (!(cond)) { printf("  [FAIL] %s\n", msg); failures++; } \
    else         { printf("  [PASS] %s\n", msg); } } while (0)

/* ---- a host that counts what it was actually asked to do ---- */
typedef struct { int observe, read, write, fsr, fsw, post, send, spawn, emit;
                 int fail_on; } tally_t;
static tally_t T;

static int h_observe(void *c,uint8_t a,uint16_t b){(void)c;(void)a;(void)b;T.observe++;return 0;}
static int h_read   (void *c,uint8_t a,uint16_t b){(void)c;(void)a;(void)b;T.read++;return 0;}
static int h_write  (void *c,uint8_t a,uint16_t b){(void)c;(void)a;(void)b;T.write++;return 0;}
static int h_fsr    (void *c,uint8_t a,uint16_t b){(void)c;(void)a;(void)b;T.fsr++;return 0;}
static int h_fsw    (void *c,uint8_t a,uint16_t b){(void)c;(void)a;(void)b;T.fsw++;return 0;}
static int h_post   (void *c,uint8_t a,uint16_t b){(void)c;(void)a;(void)b;T.post++;
                                                   return T.fail_on == ZAB_OP_POST ? -1 : 0;}
static int h_send   (void *c,uint8_t a,uint16_t b){(void)c;(void)a;(void)b;T.send++;return 0;}
static int h_spawn  (void *c,uint8_t a,uint16_t b){(void)c;(void)a;(void)b;T.spawn++;return 0;}
static int h_emit   (void *c,uint8_t a,uint16_t b){(void)c;(void)a;(void)b;T.emit++;return 0;}

static zab_host_t full_host(void) {
    zab_host_t h; memset(&h, 0, sizeof(h));
    h.observe=h_observe; h.read_state=h_read; h.write_state=h_write;
    h.fs_read=h_fsr; h.fs_write=h_fsw; h.ledger_post=h_post;
    h.net_send=h_send; h.spawn=h_spawn; h.emit=h_emit; h.ctx = 0;
    return h;
}

static uint32_t mkprog(uint8_t *out, const uint8_t *ops, uint16_t n) {
    zab_ins_t ins[64]; zab_header_t h;
    for (uint16_t i = 0; i < n; i++) { ins[i].op = ops[i]; ins[i].a = 0; ins[i].b = 0; }
    h.magic = ZAB_MAGIC; h.version = ZAB_VERSION; h.count = n; h.seal = 0;
    uint32_t seal = zab_compute_seal(&h, ins, n), o = 0;
    out[o++]=(uint8_t)(ZAB_MAGIC);      out[o++]=(uint8_t)(ZAB_MAGIC>>8);
    out[o++]=(uint8_t)(ZAB_MAGIC>>16);  out[o++]=(uint8_t)(ZAB_MAGIC>>24);
    out[o++]=(uint8_t)(ZAB_VERSION);    out[o++]=(uint8_t)(ZAB_VERSION>>8);
    out[o++]=(uint8_t)(n);              out[o++]=(uint8_t)(n>>8);
    out[o++]=(uint8_t)(seal);           out[o++]=(uint8_t)(seal>>8);
    out[o++]=(uint8_t)(seal>>16);       out[o++]=(uint8_t)(seal>>24);
    for (uint16_t i=0;i<n;i++){ out[o++]=ins[i].op; out[o++]=ins[i].a;
                                out[o++]=(uint8_t)(ins[i].b); out[o++]=(uint8_t)(ins[i].b>>8); }
    return o;
}

int main(void) {
    uint8_t prog[512]; zab_exec_t ex; zab_host_t host = full_host();

    printf("ZAB virtual machine\n");

    /* ---- effects actually happen when granted ---- */
    printf("execution:\n");
    { const uint8_t ops[] = { ZAB_OP_READ, ZAB_OP_POST, ZAB_OP_WRITE, ZAB_OP_END };
      uint32_t n = mkprog(prog, ops, 4);
      memset(&T, 0, sizeof(T));
      zab_exec_result_t r = zab_execute(prog, n,
            ZAB_CAP_READ_STATE|ZAB_CAP_LEDGER|ZAB_CAP_WRITE_STATE, &host, &ex);
      CHECK(r == ZABX_OK, "granted program runs to completion");
      CHECK(T.read==1 && T.post==1 && T.write==1, "each effect fired exactly once");
      CHECK(ex.effects == 3 && ex.steps == 4, "step/effect counts are exact");
      CHECK(ex.caps_used == (ZAB_CAP_READ_STATE|ZAB_CAP_LEDGER|ZAB_CAP_WRITE_STATE),
            "caps_used reports what was actually exercised");
      CHECK(ex.log_count == 3 && ex.log_op[1] == ZAB_OP_POST,
            "effect log records what really happened, in order"); }

    /* ---- THE ATTACK: a valid program doing what it was not permitted ---- */
    printf("the ungranted effect:\n");
    { const uint8_t ops[] = { ZAB_OP_READ, ZAB_OP_SEND, ZAB_OP_POST, ZAB_OP_END };
      uint32_t n = mkprog(prog, ops, 4);
      memset(&T, 0, sizeof(T));
      /* grant everything EXCEPT the network */
      uint32_t granted = ZAB_CAP_ALL & ~ZAB_CAP_NET;
      zab_exec_result_t r = zab_execute(prog, n, granted, &host, &ex);
      CHECK(r == ZABX_ERR_CAP_DENIED, "ungranted SEND is refused");
      CHECK(T.send == 0, "the network handler was NEVER called");
      CHECK(ex.fault_op == ZAB_OP_SEND && ex.fault_pc == 1,
            "the fault names the exact instruction");
      CHECK(T.read == 1 && T.post == 0,
            "execution stopped AT the violation (prior effects ran, later ones did not)"); }

    /* ---- granted but unimplemented is not a silent no-op ---- */
    printf("granted but unavailable:\n");
    { const uint8_t ops[] = { ZAB_OP_SPAWN, ZAB_OP_END };
      uint32_t n = mkprog(prog, ops, 2);
      zab_host_t h2 = full_host(); h2.spawn = 0;      /* host provides none */
      memset(&T, 0, sizeof(T));
      CHECK(zab_execute(prog, n, ZAB_CAP_ALL, &h2, &ex) == ZABX_ERR_NO_HOST,
            "granted capability with no handler fails loudly, not as a no-op"); }

    /* ---- a host refusal stops the run ---- */
    printf("host refusal:\n");
    { const uint8_t ops[] = { ZAB_OP_POST, ZAB_OP_EMIT, ZAB_OP_END };
      uint32_t n = mkprog(prog, ops, 3);
      memset(&T, 0, sizeof(T)); T.fail_on = ZAB_OP_POST;
      CHECK(zab_execute(prog, n, ZAB_CAP_ALL, &host, &ex) == ZABX_ERR_HOST_FAILED,
            "a host that rejects an effect fails the run");
      CHECK(T.emit == 0, "no further effects run after a failure");
      CHECK(ex.effects == 0, "a rejected effect is NOT counted as having happened"); }

    /* ---- nothing runs unless it verifies ---- */
    printf("verification precedes execution:\n");
    { const uint8_t ops[] = { ZAB_OP_POST, ZAB_OP_END };
      uint32_t n = mkprog(prog, ops, 2);
      prog[13] ^= 0xFF;   /* corrupt inside instruction 0 (header is 12 bytes) */
      memset(&T, 0, sizeof(T));
      CHECK(zab_execute(prog, n, ZAB_CAP_ALL, &host, &ex) == ZABX_ERR_VERIFY,
            "a program that fails re-verification never executes");
      CHECK(T.post == 0, "no effect fired from an unverified program"); }

    { const char *data = "plain data, not a program";
      memset(&T, 0, sizeof(T));
      CHECK(zab_execute((const uint8_t *)data, (uint32_t)strlen(data),
                        ZAB_CAP_ALL, &host, &ex) == ZABX_ERR_VERIFY,
            "data is not executable"); }

    /* ---- zero grant means zero effect ---- */
    printf("no authority:\n");
    { const uint8_t ops[] = { ZAB_OP_OBSERVE, ZAB_OP_END };
      uint32_t n = mkprog(prog, ops, 2);
      memset(&T, 0, sizeof(T));
      CHECK(zab_execute(prog, n, ZAB_CAP_NONE, &host, &ex) == ZABX_ERR_CAP_DENIED,
            "with no grant even OBSERVE is denied");
      CHECK(T.observe == 0, "nothing at all was called"); }

    { const uint8_t ops[] = { ZAB_OP_NOP, ZAB_OP_NOP, ZAB_OP_END };
      uint32_t n = mkprog(prog, ops, 3);
      memset(&T, 0, sizeof(T));
      CHECK(zab_execute(prog, n, ZAB_CAP_NONE, &host, &ex) == ZABX_OK &&
            ex.effects == 0, "an inert program runs fine with no grant"); }

    printf("\n%s zab_exec: %d failure(s)\n", failures ? "FAILED" : "ALL PASS", failures);
    return failures ? 1 : 0;
}
