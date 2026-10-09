/* test_zab.c — capability verifier tests.
 *
 * The headline test is THE LYING S-: an artifact that declares no capabilities
 * while containing instructions that post to the ledger and send on the
 * network. Under the old declared-capability model it passed requirement 2.
 * Here it must be caught, because the capability set is derived from what the
 * instructions actually are.
 *
 * Build/run:
 *   gcc -std=c11 -Wall -Wextra -Werror -Isrc/zab \
 *       src/zab/test_zab.c src/zab/zab.c -o /tmp/test_zab && /tmp/test_zab
 *
 * Author: H.M. Michael-Laurence: Curzi (c)
 * Copyright (c) 2024-2026 Michael Laurence Curzi and 36N9 Genetics, LLC
 * SPDX-License-Identifier: LicenseRef-OPL-1.1 AND CC-BY-SA-4.0
 */
#include <stdio.h>
#include <string.h>
#include "zab.h"

static int failures = 0;
#define CHECK(cond, msg) do { \
    if (!(cond)) { printf("  [FAIL] %s\n", msg); failures++; } \
    else         { printf("  [PASS] %s\n", msg); } } while (0)

/* Serialise a program to the wire format (explicit LE, no struct punning). */
static uint32_t build(uint8_t *out, const zab_ins_t *ins, uint16_t count,
                      int corrupt_seal) {
    zab_header_t h;
    h.magic = ZAB_MAGIC; h.version = ZAB_VERSION; h.count = count; h.seal = 0;
    uint32_t seal = zab_compute_seal(&h, ins, count);
    if (corrupt_seal) seal ^= 0xFFFFFFFFu;
    uint32_t o = 0;
    out[o++] = (uint8_t)(ZAB_MAGIC);       out[o++] = (uint8_t)(ZAB_MAGIC >> 8);
    out[o++] = (uint8_t)(ZAB_MAGIC >> 16); out[o++] = (uint8_t)(ZAB_MAGIC >> 24);
    out[o++] = (uint8_t)(ZAB_VERSION);     out[o++] = (uint8_t)(ZAB_VERSION >> 8);
    out[o++] = (uint8_t)(count);           out[o++] = (uint8_t)(count >> 8);
    out[o++] = (uint8_t)(seal);            out[o++] = (uint8_t)(seal >> 8);
    out[o++] = (uint8_t)(seal >> 16);      out[o++] = (uint8_t)(seal >> 24);
    for (uint16_t i = 0; i < count; i++) {
        out[o++] = ins[i].op; out[o++] = ins[i].a;
        out[o++] = (uint8_t)(ins[i].b); out[o++] = (uint8_t)(ins[i].b >> 8);
    }
    return o;
}

int main(void) {
    uint8_t buf[1024];
    uint32_t caps; zab_result_t why;

    printf("ZAB capability verifier\n");

    /* ---- the opcode table is total and complete ---- */
    printf("opcode -> capability table:\n");
    int all_classified = 1, unknown_safe = 1;
    for (int op = 0; op < 256; op++) {
        uint32_t c = zab_op_capability((uint8_t)op);
        if (op < (int)ZAB_OP__MAX) {
            if ((c & ~ZAB_CAP_ALL) != 0) all_classified = 0;   /* bogus bits */
        } else {
            if (c != ZAB_CAP_ALL) unknown_safe = 0;  /* must assume the worst */
        }
    }
    CHECK(all_classified, "every defined opcode maps to known capability bits");
    CHECK(unknown_safe, "every UNDEFINED opcode implies ZAB_CAP_ALL (fails closed)");

    /* ---- derivation is exact ---- */
    printf("derivation:\n");
    { zab_ins_t p[] = { {ZAB_OP_OBSERVE,0,0}, {ZAB_OP_END,0,0} };
      uint32_t n = build(buf, p, 2, 0);
      CHECK(zab_derive_capabilities(buf, n, &caps) == ZAB_OK &&
            caps == ZAB_CAP_OBSERVE, "observe-only program derives OBSERVE"); }

    { zab_ins_t p[] = { {ZAB_OP_READ,0,0}, {ZAB_OP_POST,0,0},
                        {ZAB_OP_SEND,0,0}, {ZAB_OP_END,0,0} };
      uint32_t n = build(buf, p, 4, 0);
      CHECK(zab_derive_capabilities(buf, n, &caps) == ZAB_OK &&
            caps == (ZAB_CAP_READ_STATE|ZAB_CAP_LEDGER|ZAB_CAP_NET),
            "multi-effect program derives the exact union"); }

    { zab_ins_t p[] = { {ZAB_OP_NOP,0,0}, {ZAB_OP_END,0,0} };
      uint32_t n = build(buf, p, 2, 0);
      CHECK(zab_derive_capabilities(buf, n, &caps) == ZAB_OK &&
            caps == ZAB_CAP_NONE, "inert program derives NO capability"); }

    /* ---- THE ATTACK: an artifact that lies about what it can do ---- */
    printf("the lying S- (what this module exists to catch):\n");
    { zab_ins_t p[] = { {ZAB_OP_POST,0,0}, {ZAB_OP_SEND,0,0},
                        {ZAB_OP_SPAWN,0,0}, {ZAB_OP_END,0,0} };
      uint32_t n = build(buf, p, 4, 0);
      uint32_t declared = ZAB_CAP_NONE;      /* "I can do nothing, honest" */
      CHECK(zab_derive_capabilities(buf, n, &caps) == ZAB_OK, "  program parses");
      CHECK(caps == (ZAB_CAP_LEDGER|ZAB_CAP_NET|ZAB_CAP_SPAWN),
            "derived set exposes ledger+net+spawn");
      CHECK((caps & ~declared) != 0,
            "declared=NONE is caught: derived exceeds it");
      CHECK((caps & ZAB_CAP_PRODUCTION) != 0,
            "the lie is a PRODUCTION capability (requirement 3 material)"); }

    /* ---- malformed input: every case must refuse, none may be trusted ---- */
    printf("malformed input (all must refuse, never 'assume harmless'):\n");
    { zab_ins_t p[] = { {ZAB_OP_POST,0,0}, {ZAB_OP_END,0,0} };
      uint32_t n = build(buf, p, 2, 1);   /* corrupted seal */
      CHECK(zab_derive_capabilities(buf, n, &caps) == ZAB_ERR_SEAL,
            "corrupted seal refused"); }

    { zab_ins_t p[] = { {ZAB_OP_POST,0,0}, {ZAB_OP_END,0,0} };
      uint32_t n = build(buf, p, 2, 0);
      CHECK(zab_derive_capabilities(buf, n - 3, &caps) == ZAB_ERR_TRUNCATED,
            "truncated body refused (not read as the visible part)"); }

    { zab_ins_t p[] = { {200,0,0}, {ZAB_OP_END,0,0} };   /* unknown opcode */
      uint32_t n = build(buf, p, 2, 0);
      CHECK(zab_derive_capabilities(buf, n, &caps) == ZAB_ERR_BAD_OPCODE,
            "unknown opcode refused (could imply anything)"); }

    { zab_ins_t p[] = { {ZAB_OP_POST,0,0} };   /* no END */
      uint32_t n = build(buf, p, 1, 0);
      CHECK(zab_derive_capabilities(buf, n, &caps) == ZAB_ERR_NO_END,
            "program without END refused"); }

    { CHECK(zab_derive_capabilities(buf, 4, &caps) == ZAB_ERR_TRUNCATED,
            "sub-header input refused"); }

    { zab_ins_t p[] = { {ZAB_OP_END,0,0} };
      uint32_t n = build(buf, p, 1, 0);
      buf[4] = 99;   /* bad version */
      CHECK(zab_derive_capabilities(buf, n, &caps) == ZAB_ERR_VERSION,
            "unknown version refused"); }

    /* ---- data is not code, and carries nothing ---- */
    printf("data vs code:\n");
    { const char *doc = "this is a plain text artifact, not a program";
      caps = 0xDEADBEEF;
      CHECK(zab_artifact_capabilities((const uint8_t *)doc,
                                      (uint32_t)strlen(doc), &caps, &why) &&
            caps == ZAB_CAP_NONE && why == ZAB_ERR_NOT_PROGRAM,
            "plain data derives NO capability (rule stays total)"); }

    { zab_ins_t p[] = { {ZAB_OP_POST,0,0}, {ZAB_OP_END,0,0} };
      uint32_t n = build(buf, p, 2, 1);   /* malformed PROGRAM, not data */
      caps = 0xDEADBEEF;
      CHECK(!zab_artifact_capabilities(buf, n, &caps, &why) &&
            why == ZAB_ERR_SEAL && caps == 0xDEADBEEF,
            "a MALFORMED program is refused, never downgraded to 'data'"); }

    printf("\n%s zab: %d failure(s)\n", failures ? "FAILED" : "ALL PASS", failures);
    return failures ? 1 : 0;
}
