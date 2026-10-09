/* test_zab_scope.c — OBJECT-SCOPED capabilities: the negative tests.
 *
 * THE DEFECT THIS FILE EXISTS TO PROVE CLOSED
 * -------------------------------------------
 * `zab_host_t` used to be nine handlers and a ctx, with no path, namespace or
 * object anywhere. The table was bound wholesale —
 *     h.fs_write = (granted & ZAB_CAP_FS_WRITE) ? real_fs_write : NULL;
 * — so ZAB_CAP_FS_WRITE meant "write THE FILESYSTEM", not "write THIS FILE".
 * Nobody could ACQUIRE a capability they lacked; the failure was that anyone
 * legitimately HOLDING one could reach every object the host could reach.
 * Least privilege failed inside the grant rather than at its edge.
 *
 * A scope check that cannot be made to deny is not a check. So the headline
 * case here is deliberately the hardest one to fake: a program that HOLDS
 * ZAB_CAP_FS_WRITE, run by a caller who GRANTED ZAB_CAP_FS_WRITE, against a
 * host whose fs_write handler is present and eager — writing to an object it
 * was not given. The handler must never be entered.
 *
 * Every other case here is an attempt to find a scope the VM will accept
 * without meaning it: no table, an empty table, an oversized table, a slot off
 * the end, an unknown kind, kind 0, an unterminated name, a zero extent, a
 * wrapping extent, undefined capability bits, and a verb the kind cannot carry.
 * All must DENY.
 *
 * Build/run:
 *   gcc -std=c11 -Wall -Wextra -Werror -Isrc/zab src/zab/test_zab_scope.c \
 *       src/zab/zab_exec.c src/zab/zab.c -o /tmp/test_zab_scope && /tmp/test_zab_scope
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

/* ---- a host that records WHICH SLOT it was asked to act on ---- */
typedef struct { int fsw, fsr, post, send, observe; int last_slot; } tally_t;
static tally_t T;
static int h_fsw    (void *c,uint8_t a,uint16_t b){(void)c;(void)b;T.fsw++;T.last_slot=a;return 0;}
static int h_fsr    (void *c,uint8_t a,uint16_t b){(void)c;(void)b;T.fsr++;T.last_slot=a;return 0;}
static int h_post   (void *c,uint8_t a,uint16_t b){(void)c;(void)b;T.post++;T.last_slot=a;return 0;}
static int h_send   (void *c,uint8_t a,uint16_t b){(void)c;(void)b;T.send++;T.last_slot=a;return 0;}
static int h_observe(void *c,uint8_t a,uint16_t b){(void)c;(void)b;T.observe++;T.last_slot=a;return 0;}

static zab_host_t bare_host(void) {
    zab_host_t h; memset(&h, 0, sizeof(h));
    h.fs_write = h_fsw; h.fs_read = h_fsr; h.ledger_post = h_post;
    h.net_send = h_send; h.observe = h_observe;
    return h;                    /* NO scopes: the fail-closed default */
}

/* ---- program builder, with per-instruction operands ---- */
static uint32_t mkprog(uint8_t *out, const zab_ins_t *ins, uint16_t n) {
    zab_header_t h;
    h.magic = ZAB_MAGIC; h.version = ZAB_VERSION; h.count = n; h.seal = 0;
    uint32_t seal = zab_compute_seal(&h, ins, n), o = 0;
    out[o++]=(uint8_t)(ZAB_MAGIC);      out[o++]=(uint8_t)(ZAB_MAGIC>>8);
    out[o++]=(uint8_t)(ZAB_MAGIC>>16);  out[o++]=(uint8_t)(ZAB_MAGIC>>24);
    out[o++]=(uint8_t)(ZAB_VERSION);    out[o++]=(uint8_t)(ZAB_VERSION>>8);
    out[o++]=(uint8_t)(n);              out[o++]=(uint8_t)(n>>8);
    out[o++]=(uint8_t)(seal);           out[o++]=(uint8_t)(seal>>8);
    out[o++]=(uint8_t)(seal>>16);       out[o++]=(uint8_t)(seal>>24);
    for (uint16_t i=0;i<n;i++){ out[o++]=ins[i].op; out[o++]=ins[i].a;
                                out[o++]=(uint8_t)(ins[i].b);
                                out[o++]=(uint8_t)(ins[i].b>>8); }
    return o;
}

int main(void) {
    uint8_t prog[512];
    zab_exec_t ex;

    printf("ZAB object scoping\n");

    /* ---------------------------------------------------------------- */
    printf("a scope must be constructible, and refuse what it cannot name:\n");
    {   zab_scope_t s;
        CHECK(zab_scope_set(&s, "alpha.dat", ZAB_SCOPE_FILE,
                            ZAB_CAP_FS_READ|ZAB_CAP_FS_WRITE, 0, 4096),
              "a well-formed file extent builds");
        CHECK(zab_scope_valid(&s), "and validates");

        /* 32 chars + NUL does not fit a 32-byte field */
        const char *too_long = "0123456789012345678901234567890123456789";
        CHECK(!zab_scope_set(&s, too_long, ZAB_SCOPE_FILE, ZAB_CAP_FS_WRITE, 0, 16),
              "a name that will not fit is REFUSED, not truncated");
        CHECK(!zab_scope_valid(&s),
              "and the refused entry is left denying (zeroed), not half-built");

        CHECK(!zab_scope_set(&s, "peer.one", ZAB_SCOPE_PEER, ZAB_CAP_FS_WRITE, 0, 16),
              "FS_WRITE on a network PEER is refused at construction");
        CHECK(!zab_scope_set(&s, "alpha.dat", ZAB_SCOPE_FILE, ZAB_CAP_FS_WRITE, 0, 0),
              "a zero-length extent is refused");
        CHECK(!zab_scope_set(&s, "alpha.dat", ZAB_SCOPE_FILE, ZAB_CAP_FS_WRITE,
                             0xFFFFFF00u, 0x00000200u),
              "an extent that WRAPS is refused (a wrap covers everything)");
        CHECK(!zab_scope_set(&s, "", ZAB_SCOPE_FILE, ZAB_CAP_FS_WRITE, 0, 16),
              "an empty name names nothing and is refused");
        CHECK(!zab_scope_set(&s, "x", 99, ZAB_CAP_FS_WRITE, 0, 16),
              "an unrecognised KIND is refused");
        CHECK(!zab_scope_set(&s, "x", ZAB_SCOPE_NONE, ZAB_CAP_FS_WRITE, 0, 16),
              "kind NONE is refused");
        CHECK(zab_scope_kind_caps(99) == ZAB_CAP_NONE &&
              zab_scope_kind_caps(ZAB_SCOPE_NONE) == ZAB_CAP_NONE,
              "an unknown kind carries NO verbs (mirror of unknown-opcode->ALL)");
    }

    /* ---------------------------------------------------------------- */
    printf("hand-built malformed entries are refused by the predicate:\n");
    {   zab_scope_t s;
        memset(&s, 0, sizeof(s));
        memset(s.name, 'A', sizeof(s.name));      /* no NUL anywhere */
        s.kind = ZAB_SCOPE_FILE; s.caps = ZAB_CAP_FS_WRITE; s.span = 16;
        CHECK(!zab_scope_valid(&s), "an UNTERMINATED name is refused");

        memset(&s, 0, sizeof(s));
        s.name[0] = 'x'; s.kind = ZAB_SCOPE_FILE;
        s.caps = 0x80000000u; s.span = 16;
        CHECK(!zab_scope_valid(&s),
              "an undefined capability BIT is refused (it could mean anything)");

        memset(&s, 0, sizeof(s));
        s.name[0] = 'x'; s.kind = ZAB_SCOPE_FILE;
        s.caps = ZAB_CAP_FS_WRITE; s.span = 16; s.reserved[1] = 1;
        CHECK(!zab_scope_valid(&s),
              "a non-zero RESERVED byte is refused (written by a future we cannot read)");

        memset(&s, 0, sizeof(s));
        CHECK(!zab_scope_valid(&s),
              "a ZEROED entry denies -- which is what every unfilled host holds");
        CHECK(!zab_scope_permits(0, ZAB_CAP_FS_WRITE, 0),
              "a NULL extent denies");
    }

    /* ---------------------------------------------------------------- */
    printf("no scope table at all -> nothing may act:\n");
    {   const zab_ins_t ins[] = { {ZAB_OP_FS_WRITE,0,0}, {ZAB_OP_END,0,0} };
        uint32_t n = mkprog(prog, ins, 2);
        zab_host_t h = bare_host();          /* scopes == NULL */
        memset(&T, 0, sizeof(T));
        zab_exec_result_t r = zab_execute(prog, n, ZAB_CAP_ALL, &h, &ex);
        CHECK(r == ZABX_ERR_SCOPE,
              "an UNSCOPED host denies a granted, hosted FS_WRITE");
        CHECK(T.fsw == 0, "the fs_write handler was NEVER called");
        CHECK(ex.effects == 0, "nothing was recorded as having happened");
    }
    {   const zab_ins_t ins[] = { {ZAB_OP_FS_WRITE,0,0}, {ZAB_OP_END,0,0} };
        uint32_t n = mkprog(prog, ins, 2);
        zab_scope_t s; zab_scope_set(&s,"alpha.dat",ZAB_SCOPE_FILE,ZAB_CAP_FS_WRITE,0,16);
        zab_host_t h = bare_host(); h.scopes = &s; h.n_scopes = 0;
        memset(&T, 0, sizeof(T));
        CHECK(zab_execute(prog, n, ZAB_CAP_ALL, &h, &ex) == ZABX_ERR_SCOPE &&
              T.fsw == 0, "a table with ZERO entries denies");
        h.n_scopes = (uint16_t)(ZAB_MAX_SCOPES + 1u);
        memset(&T, 0, sizeof(T));
        CHECK(zab_execute(prog, n, ZAB_CAP_ALL, &h, &ex) == ZABX_ERR_SCOPE &&
              T.fsw == 0, "a table LARGER than ZAB_MAX_SCOPES denies the whole run");
        h.n_scopes = 1; h.reserved = 1;
        memset(&T, 0, sizeof(T));
        CHECK(zab_execute(prog, n, ZAB_CAP_ALL, &h, &ex) == ZABX_ERR_SCOPE &&
              T.fsw == 0, "a host with a non-zero RESERVED field denies");
    }

    /* ================================================================ *
     * THE HEADLINE: a HELD, GRANTED, HOSTED capability, aimed at an     *
     * object it was not given. This is the defect, stated as a test.    *
     * ================================================================ */
    printf("THE OUT-OF-SCOPE WRITE (the defect itself):\n");
    {   zab_scope_t two[2];
        /* slot 0: the contract's own file -- read AND write */
        CHECK(zab_scope_set(&two[0], "mine.dat", ZAB_SCOPE_FILE,
                            ZAB_CAP_FS_READ|ZAB_CAP_FS_WRITE, 0, 4096),
              "  bound: mine.dat  (read+write)");
        /* slot 1: somebody else's file -- READ ONLY */
        CHECK(zab_scope_set(&two[1], "theirs.dat", ZAB_SCOPE_FILE,
                            ZAB_CAP_FS_READ, 0, 4096),
              "  bound: theirs.dat (read only)");

        zab_host_t h = bare_host();
        h.scopes = two; h.n_scopes = 2;

        /* the caller grants FS_WRITE; the program contains FS_WRITE; the host
         * implements fs_write. Only the OBJECT differs. */
        const uint32_t granted = ZAB_CAP_FS_READ | ZAB_CAP_FS_WRITE;

        {   const zab_ins_t ok[] = { {ZAB_OP_FS_WRITE,0,0}, {ZAB_OP_END,0,0} };
            uint32_t n = mkprog(prog, ok, 2);
            memset(&T, 0, sizeof(T));
            CHECK(zab_execute(prog, n, granted, &h, &ex) == ZABX_OK &&
                  T.fsw == 1 && T.last_slot == 0,
                  "writing its OWN object succeeds (the check is not vacuous)");
            CHECK(ex.log_count == 1 && ex.log_op[0] == ZAB_OP_FS_WRITE &&
                  ex.log_slot[0] == 0,
                  "the audit trail records the VERB and the OBJECT");
        }
        {   const zab_ins_t bad[] = { {ZAB_OP_FS_WRITE,1,0}, {ZAB_OP_END,0,0} };
            uint32_t n = mkprog(prog, bad, 2);
            memset(&T, 0, sizeof(T));
            zab_exec_result_t r = zab_execute(prog, n, granted, &h, &ex);
            CHECK(r == ZABX_ERR_SCOPE,
                  "*** writing ANOTHER object with the SAME granted capability is REFUSED ***");
            CHECK(T.fsw == 0, "*** the fs_write handler was NEVER entered ***");
            CHECK(ex.fault_op == ZAB_OP_FS_WRITE && ex.fault_pc == 0 &&
                  ex.fault_slot == 1,
                  "the fault names the instruction AND the object it reached for");
            printf("       result=%d (%s) fault_pc=%u fault_op=%s fault_slot=%u"
                   " effects=%u\n",
                   (int)r, zab_exec_result_name(r), ex.fault_pc,
                   zab_op_name(ex.fault_op), (unsigned)ex.fault_slot, ex.effects);
        }
        {   /* reading that same object is still fine: the extent carries READ */
            const zab_ins_t rd[] = { {ZAB_OP_FS_READ,1,0}, {ZAB_OP_END,0,0} };
            uint32_t n = mkprog(prog, rd, 2);
            memset(&T, 0, sizeof(T));
            CHECK(zab_execute(prog, n, granted, &h, &ex) == ZABX_OK && T.fsr == 1,
                  "READING the other object still works -- scoping is per VERB per OBJECT");
        }
        {   /* a slot past the end of the table */
            const zab_ins_t off[] = { {ZAB_OP_FS_WRITE,7,0}, {ZAB_OP_END,0,0} };
            uint32_t n = mkprog(prog, off, 2);
            memset(&T, 0, sizeof(T));
            CHECK(zab_execute(prog, n, granted, &h, &ex) == ZABX_ERR_SCOPE &&
                  T.fsw == 0, "a slot PAST the end of the table denies");
        }
        {   /* partial run: the legal write lands, the illegal one stops it */
            const zab_ins_t mix[] = { {ZAB_OP_FS_WRITE,0,0}, {ZAB_OP_FS_WRITE,1,0},
                                      {ZAB_OP_FS_WRITE,0,0}, {ZAB_OP_END,0,0} };
            uint32_t n = mkprog(prog, mix, 4);
            memset(&T, 0, sizeof(T));
            CHECK(zab_execute(prog, n, granted, &h, &ex) == ZABX_ERR_SCOPE &&
                  T.fsw == 1 && ex.effects == 1 && ex.fault_pc == 1,
                  "execution stops AT the out-of-scope instruction, mid-program");
        }
    }

    /* ---------------------------------------------------------------- */
    printf("the extent bounds the OFFSET too:\n");
    {   zab_scope_t s;
        CHECK(zab_scope_set(&s, "small.dat", ZAB_SCOPE_FILE, ZAB_CAP_FS_WRITE, 0, 256),
              "  bound: small.dat spanning 256");
        zab_host_t h = bare_host(); h.scopes = &s; h.n_scopes = 1;

        const zab_ins_t in[]  = { {ZAB_OP_FS_WRITE,0,255}, {ZAB_OP_END,0,0} };
        const zab_ins_t out_[] = { {ZAB_OP_FS_WRITE,0,256}, {ZAB_OP_END,0,0} };
        uint32_t n = mkprog(prog, in, 2);
        memset(&T, 0, sizeof(T));
        CHECK(zab_execute(prog, n, ZAB_CAP_FS_WRITE, &h, &ex) == ZABX_OK && T.fsw == 1,
              "the last offset INSIDE the extent is allowed");
        n = mkprog(prog, out_, 2);
        memset(&T, 0, sizeof(T));
        CHECK(zab_execute(prog, n, ZAB_CAP_FS_WRITE, &h, &ex) == ZABX_ERR_SCOPE &&
              T.fsw == 0, "the first offset PAST the extent is refused");
    }

    /* ---------------------------------------------------------------- */
    printf("the kind ceiling holds even against a host that gets it wrong:\n");
    {   /* hand-build a PEER extent carrying FS_WRITE -- zab_scope_set would
         * have refused it, so this is what a host bypassing the builder (or
         * loading a bad record) would produce. */
        zab_scope_t s; memset(&s, 0, sizeof(s));
        s.name[0]='p'; s.name[1]='1'; s.kind = ZAB_SCOPE_PEER;
        s.caps = ZAB_CAP_FS_WRITE | ZAB_CAP_NET; s.span = 4096;
        CHECK(zab_scope_valid(&s), "  (the entry is structurally valid)");
        CHECK(!zab_scope_permits(&s, ZAB_CAP_FS_WRITE, 0),
              "a PEER extent cannot carry FS_WRITE however the host filled it in");
        CHECK(zab_scope_permits(&s, ZAB_CAP_NET, 0),
              "but it does carry NET -- the ceiling narrows, it does not blanket-deny");

        zab_host_t h = bare_host(); h.scopes = &s; h.n_scopes = 1;
        const zab_ins_t ins[] = { {ZAB_OP_FS_WRITE,0,0}, {ZAB_OP_END,0,0} };
        uint32_t n = mkprog(prog, ins, 2);
        memset(&T, 0, sizeof(T));
        CHECK(zab_execute(prog, n, ZAB_CAP_ALL, &h, &ex) == ZABX_ERR_SCOPE &&
              T.fsw == 0, "and the VM refuses the effect on that basis");
    }

    /* ---------------------------------------------------------------- */
    printf("the guarantees that existed before are unchanged:\n");
    {   zab_scope_t s;
        zab_scope_set(&s, "everything", ZAB_SCOPE_TRIAD, ZAB_CAP_ALL, 0, 65536);
        zab_host_t h = bare_host(); h.scopes = &s; h.n_scopes = 1;

        /* 1. an ungranted capability is still denied FIRST, by capability --
         *    a permissive extent does not turn into a grant. */
        const zab_ins_t send[] = { {ZAB_OP_SEND,0,0}, {ZAB_OP_END,0,0} };
        uint32_t n = mkprog(prog, send, 2);
        memset(&T, 0, sizeof(T));
        CHECK(zab_execute(prog, n, ZAB_CAP_ALL & ~ZAB_CAP_NET, &h, &ex)
                  == ZABX_ERR_CAP_DENIED && T.send == 0,
              "an ungranted verb is still CAP_DENIED, even inside a full extent");

        /* 2. granted + in scope + no handler is still NO_HOST, not a no-op
         *    and not a scope error. */
        const zab_ins_t spawn[] = { {ZAB_OP_SPAWN,0,0}, {ZAB_OP_END,0,0} };
        n = mkprog(prog, spawn, 2);
        CHECK(zab_execute(prog, n, ZAB_CAP_ALL, &h, &ex) == ZABX_ERR_NO_HOST,
              "granted + scoped but UNHOSTED still fails loudly as NO_HOST");

        /* 3. re-verification still precedes everything (TOCTOU). */
        const zab_ins_t w[] = { {ZAB_OP_FS_WRITE,0,0}, {ZAB_OP_END,0,0} };
        n = mkprog(prog, w, 2);
        prog[13] ^= 0xFF;
        memset(&T, 0, sizeof(T));
        CHECK(zab_execute(prog, n, ZAB_CAP_ALL, &h, &ex) == ZABX_ERR_VERIFY &&
              T.fsw == 0, "tampered bytes still fail re-verification before any scope work");

        /* 4. an inert program still needs nothing. */
        const zab_ins_t nop[] = { {ZAB_OP_NOP,0,0}, {ZAB_OP_END,0,0} };
        n = mkprog(prog, nop, 2);
        zab_host_t bare = bare_host();
        CHECK(zab_execute(prog, n, ZAB_CAP_NONE, &bare, &ex) == ZABX_OK &&
              ex.effects == 0,
              "an inert program still runs with no grant and no scopes");
    }

    printf("\n%s zab_scope: %d failure(s)\n", failures ? "FAILED" : "ALL PASS", failures);
    return failures ? 1 : 0;
}
