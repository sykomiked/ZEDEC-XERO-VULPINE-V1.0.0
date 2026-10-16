/* Copyright (c) 2024-2026 Michael Laurence Curzi and 36N9 Genetics, LLC */
/* SPDX-License-Identifier: Apache-2.0 */
/* test_vna_delta.c — the L2 state bus: canonical bytes, the evidence join,
 * the Merkle clock and the fork counter. Prove the lattice absorbs a
 * collision instead of forking the bus. */
#include <stdio.h>
#include <string.h>
#include "vna_delta.h"

static int passes, failures;
#define CHECK(c)                                                                                   \
    do {                                                                                           \
        if (c)                                                                                     \
            passes++;                                                                              \
        else {                                                                                     \
            failures++;                                                                            \
            fprintf(stderr, "FAIL %s:%d: %s\n", __FILE__, __LINE__, #c);                           \
        }                                                                                          \
    } while (0)

static int pack(uint16_t cell, lpres_state_t st, const uint8_t *parent, const uint8_t *pay,
                uint16_t n, uint8_t *out)
{
    vna_delta_t d;
    memset(&d, 0, sizeof d);
    if (parent) memcpy(d.parent, parent, 32);
    d.cell = cell;
    d.lattice = (uint8_t) st;
    d.len = n;
    d.payload = pay;
    return vna_delta_encode(&d, out, VNA_DELTA_HDR + n);
}

int main(void)
{
    uint8_t buf[VNA_DELTA_HDR + VNA_DELTA_PAYLOAD_MAX];
    vna_delta_page_t pg, pg2;
    vna_delta_t d;
    const char *words = "the desk is open";

    /* ---- canonical bytes: encode then parse is identity ---- */
    int n = pack(7, LPRES_STATE_TRUE, 0, (const uint8_t *) words, 16, buf);
    CHECK(n == (int) (VNA_DELTA_HDR + 16));
    CHECK(vna_delta_parse(buf, (uint32_t) n, &d) == VNA_OK);
    CHECK(d.cell == 7 && d.lattice == LPRES_STATE_TRUE && d.len == 16);
    CHECK(memcmp(d.payload, words, 16) == 0);
    CHECK(d.parent[0] == 0 && d.parent[31] == 0);

    /* ---- malformed bytes are refused ---- */
    uint8_t bad[sizeof buf];
    memcpy(bad, buf, (size_t) n);
    bad[0] = 'X';
    CHECK(vna_delta_parse(bad, (uint32_t) n, &d) == VNA_ERR_PARSE); /* magic */
    memcpy(bad, buf, (size_t) n);
    bad[3 + 32 + 2] = 4;
    CHECK(vna_delta_parse(bad, (uint32_t) n, &d) == VNA_ERR_PARSE);      /* lattice > BOTH */
    CHECK(vna_delta_parse(buf, VNA_DELTA_HDR - 1, &d) == VNA_ERR_PARSE); /* short */
    memcpy(bad, buf, (size_t) n);
    bad[VNA_DELTA_HDR - 1] = 9; /* len field says 9, only the header follows */
    CHECK(vna_delta_parse(bad, VNA_DELTA_HDR, &d) == VNA_ERR_PARSE); /* length lie */
    vna_delta_t big;
    memset(&big, 0, sizeof big);
    big.cell = 1;
    big.lattice = LPRES_STATE_TRUE;
    big.len = VNA_DELTA_PAYLOAD_MAX + 1;
    CHECK(vna_delta_encode(&big, buf, sizeof buf) == VNA_ERR_ARG); /* payload cap */

    /* ---- first delta wins outright (NEITHER joins to the claim) ---- */
    vna_delta_page_init(&pg);
    CHECK(vna_delta_apply(&pg, buf, (uint32_t) n, 1) == LPRES_STATE_TRUE);
    const vna_delta_cell_t *c = vna_delta_cell(&pg, 7);
    CHECK(c && c->lattice == LPRES_STATE_TRUE && c->napp == 1 && c->forks == 0);
    CHECK(c->tick == 1 && pg.clock == 1);

    /* ---- the opposite claim gives BOTH; the bus does not fork ---- */
    n = pack(7, LPRES_STATE_FALSE, 0, 0, 0, buf);
    CHECK(vna_delta_apply(&pg, buf, (uint32_t) n, 2) == LPRES_STATE_BOTH);
    c = vna_delta_cell(&pg, 7);
    CHECK(c->lattice == LPRES_STATE_BOTH);
    CHECK(c->forks == 1); /* a foreign parent, evidence not an error */

    /* ---- same claim again stays BOTH; a contradiction persists ---- */
    n = pack(7, LPRES_STATE_TRUE, 0, 0, 0, buf);
    CHECK(vna_delta_apply(&pg, buf, (uint32_t) n, 3) == LPRES_STATE_BOTH);

    /* ---- NEITHER is no claim at all: it does not weaken a cell ---- */
    n = pack(9, LPRES_STATE_FALSE, 0, 0, 0, buf);
    CHECK(vna_delta_apply(&pg, buf, (uint32_t) n, 4) == LPRES_STATE_FALSE);
    n = pack(9, LPRES_STATE_NEITHER, 0, 0, 0, buf);
    CHECK(vna_delta_apply(&pg, buf, (uint32_t) n, 5) == LPRES_STATE_FALSE);

    /* ---- issue: parent is the head, the clock advances one tick ---- */
    vna_delta_page_init(&pg2);
    uint64_t tick = 0;
    n = vna_delta_issue(&pg2, 42, LPRES_STATE_TRUE, (const uint8_t *) "a", 1, buf, sizeof buf,
                        &tick);
    CHECK(n > 0 && tick == 1);
    uint8_t dig1[32];
    vna_delta_digest(buf, (uint32_t) n, dig1);
    n = vna_delta_issue(&pg2, 42, LPRES_STATE_TRUE, (const uint8_t *) "b", 1, buf, sizeof buf,
                        &tick);
    CHECK(n > 0 && tick == 2);
    CHECK(vna_delta_parse(buf, (uint32_t) n, &d) == VNA_OK);
    CHECK(memcmp(d.parent, dig1, 32) == 0); /* the Merkle clock links */
    c = vna_delta_cell(&pg2, 42);
    CHECK(c->napp == 2 && c->forks == 0); /* own chain never forks */

    /* ---- two pages agree: the same delta stream converges ---- */
    vna_delta_page_init(&pg);
    n = vna_delta_issue(&pg2, 42, LPRES_STATE_FALSE, 0, 0, buf, sizeof buf, &tick);
    CHECK(n > 0);
    CHECK(vna_delta_apply(&pg, buf, (uint32_t) n, tick) == LPRES_STATE_FALSE);

    /* ---- bounded: a 65th cell is refused, the page stays whole ---- */
    vna_delta_page_init(&pg);
    for (uint32_t i = 0; i < VNA_DELTA_CELLS; i++) {
        n = pack((uint16_t) i, LPRES_STATE_TRUE, 0, 0, 0, buf);
        CHECK(vna_delta_apply(&pg, buf, (uint32_t) n, i + 1) == LPRES_STATE_TRUE);
    }
    n = pack(999, LPRES_STATE_TRUE, 0, 0, 0, buf);
    CHECK(vna_delta_apply(&pg, buf, (uint32_t) n, 100) == VNA_ERR_SPACE);
    CHECK(vna_delta_cell(&pg, 999) == 0);

    /* ---- the bus in one line: opposite claims, one cell, BOTH, live ---- */
    vna_delta_page_init(&pg);
    n = pack(1, LPRES_STATE_TRUE, 0, 0, 0, buf);
    CHECK(vna_delta_apply(&pg, buf, (uint32_t) n, 1) == LPRES_STATE_TRUE);
    n = pack(1, LPRES_STATE_FALSE, 0, 0, 0, buf);
    CHECK(vna_delta_apply(&pg, buf, (uint32_t) n, 1) == LPRES_STATE_BOTH);
    CHECK(pg.ncell == 1);

    printf("vna_delta: %d pass, %d fail\n", passes, failures);
    return failures ? 1 : 0;
}
