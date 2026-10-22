/* Copyright (c) 2024-2026 Michael Laurence Curzi and 36N9 Genetics, LLC */
/* SPDX-License-Identifier: Apache-2.0 */
/* Bramble's model against zxvd's real dispatcher (LINK): its rules, then a
 * mind map grown, laid out on its rings, saved to the Hoard on the owner's
 * word and read back. */
#include "bramble_model.h"

APPS_TEST(bramble)
{
    static bramble_t B, B2;
    uint32_t id = 0, kid[8];

    /* BR2: the rings, as the blocks run them */
    CHECK(bramble_ring_x(0, 0, 1, 100) == 0 && bramble_ring_y(0, 0, 1, 100) == 0,
          "bramble: the root sits at the centre");
    CHECK(bramble_ring_x(1, 0, 8, 100) == -100 && bramble_ring_y(1, 0, 8, 100) == -100 &&
              bramble_ring_x(1, 3, 8, 100) == 100 && bramble_ring_y(1, 3, 8, 100) == 0 &&
              bramble_ring_x(1, 5, 8, 100) == 0 && bramble_ring_y(1, 5, 8, 100) == 100 &&
              bramble_ring_x(1, 7, 8, 100) == -100 && bramble_ring_y(1, 7, 8, 100) == 0,
          "bramble: a ring of 8 round its four sides");
    CHECK(bramble_ring_x(2, 1, 3, 16) == 32 && bramble_ring_y(2, 1, 3, 16) == -11 &&
              bramble_ring_x(2, 2, 3, 16) == -10 && bramble_ring_y(2, 2, 3, 16) == 32,
          "bramble: a ring of 3 floors its places");
    CHECK(bramble_ring_x(-1, 0, 8, 100) == BRAMBLE_OFF &&
              bramble_ring_x(65, 0, 8, 1) == BRAMBLE_OFF &&
              bramble_ring_y(1, 8, 8, 100) == BRAMBLE_OFF &&
              bramble_ring_y(1, -1, 8, 100) == BRAMBLE_OFF &&
              bramble_ring_x(1, 0, 0, 100) == BRAMBLE_OFF &&
              bramble_ring_x(1, 0, 65537, 100) == BRAMBLE_OFF &&
              bramble_ring_y(1, 0, 8, 4097) == BRAMBLE_OFF &&
              bramble_ring_y(1, 0, 8, 0) == BRAMBLE_OFF,
          "bramble: outside the ring's domain, as the C++ traps");
    /* BR3, BR4 */
    CHECK(bramble_limits_ok(1, 0, 16) && bramble_limits_ok(512, 15, 256) &&
              !bramble_limits_ok(0, 0, 16) && !bramble_limits_ok(513, 0, 16) &&
              !bramble_limits_ok(1, 16, 16) && !bramble_limits_ok(1, -1, 16) &&
              !bramble_limits_ok(1, 0, 15) && !bramble_limits_ok(1, 0, 257),
          "bramble: the map limits (Eve's ranges)");
    CHECK(bramble_fits_canvas(65, 65, 2, 16) && !bramble_fits_canvas(64, 65, 2, 16) &&
              !bramble_fits_canvas(2049, 2049, 0, 16) && !bramble_fits_canvas(0, 100, 0, 16) &&
              bramble_fits_canvas(2048, 2048, 3, 256) && !bramble_fits_canvas(2048, 2048, 4, 256),
          "bramble: the deepest ring inside Gesso's canvas");
    CHECK(bramble_node_ok(999, -999, 99, 999, 9999, -9999) &&
              !bramble_node_ok(1000, 0, 0, 0, 0, 0) && !bramble_node_ok(0, 0, 0, 0, 10000, 0) &&
              !bramble_node_ok(0, 0, 100, 0, 0, 0),
          "bramble: the node record's digit widths (MYRA's)");

    /* a map grown from its root */
    CHECK(bramble_new(&B, &LINK, 2049, 600, 32, "Plans") == ZXM_E_ARG &&
              bramble_new(&B, &LINK, 800, 600, 8, "Plans") == ZXM_E_ARG &&
              bramble_new(&B, &LINK, 800, 600, 32, "") == ZXM_E_ARG &&
              bramble_new(&B, &LINK, 800, 600, 32, "two\nlines") == ZXM_E_ARG,
          "bramble: a canvas, step or root label past the limits is refused");
    CHECK(bramble_new(&B, &LINK, 800, 600, 32, "Plans") == ZXM_OK && B.n == 1 && B.node[0].x == 0 &&
              B.node[0].y == 0 && B.node[0].parent == -1,
          "bramble: a new map is its root at the centre");
    static const char *const first[4] = {"Garden", "House", "Trips", "Books"};
    for (uint32_t i = 0; i < 4; i++)
        CHECK(bramble_add(&B, 0, first[i], &kid[i]) == ZXM_OK && kid[i] == i + 1,
              "bramble: child %u added", i);
    CHECK(B.node[1].x == -32 && B.node[1].y == -32 && B.node[2].x == 32 && B.node[2].y == -32 &&
              B.node[3].x == 32 && B.node[3].y == 32 && B.node[4].x == -32 && B.node[4].y == 32,
          "bramble: four children on the ring of 32 at its corners");
    /* grandchildren: ordered by their parents' order, so siblings sit together */
    CHECK(bramble_add(&B, kid[2], "Spain", &kid[4]) == ZXM_OK &&
              bramble_add(&B, kid[0], "Beans", &kid[5]) == ZXM_OK &&
              bramble_add(&B, kid[2], "Peru", &kid[6]) == ZXM_OK && B.depth == 2,
          "bramble: three grandchildren");
    CHECK(B.node[kid[5]].order == 0 && B.node[kid[4]].order == 1 && B.node[kid[6]].order == 2 &&
              B.node[kid[5]].x == bramble_ring_x(2, 0, 3, 32) &&
              B.node[kid[4]].y == bramble_ring_y(2, 1, 3, 32) &&
              B.node[kid[6]].x == bramble_ring_x(2, 2, 3, 32),
          "bramble: the ring of 64 holds Beans, then Spain and Peru together");
    /* the canvas holds rings to depth 9 at step 32 (2 * 9 * 32 + 1 = 577 <= 600) */
    id = kid[5];
    for (uint32_t dd = 3; dd <= 9; dd++)
        CHECK(bramble_add(&B, id, "deeper", &id) == ZXM_OK, "bramble: depth %u", dd);
    uint32_t n_before = B.n;
    CHECK(bramble_add(&B, id, "too deep", &kid[7]) == ZXM_E_ARG && B.n == n_before &&
              B.node[kid[5]].order == 0 && B.node[kid[4]].order == 1,
          "bramble: a ring past the canvas is refused and the map is as it was");
    CHECK(bramble_add(&B, 999, "x", &kid[7]) == ZXM_E_ARG &&
              bramble_add(&B, 0,
                          "a label far too long for one node of a mind map: it runs on well past "
                          "sixty-four bytes",
                          &kid[7]) == ZXM_E_ARG,
          "bramble: a missing parent or too long a label is refused");
    CHECK(bramble_rename(&B, kid[1], "Home") == ZXM_OK && !strcmp(B.node[kid[1]].label, "Home") &&
              bramble_rename(&B, 99, "x") == ZXM_E_ARG,
          "bramble: renamed");

    /* BR5: nothing is saved without the owner's word */
    CHECK(bramble_save(&B, true) == ZXM_E_STATE, "bramble: no path yet");
    CHECK(bramble_save_as(&B, "", "plans.bramble", false) == ZXM_E_STATE && B.path[0] == 0 &&
              B.dirty,
          "bramble: save_as without the owner's word sends nothing");
    zxm_begin(&LINK, "files.read");
    zxm_p_str(&LINK, "path", "plans.bramble");
    CHECK(zxm_call(&LINK) == ZXM_E_REMOTE, "bramble: and no file appeared");
    CHECK(bramble_save_as(&B, "", "", true) == ZXM_E_ARG, "bramble: no path");
    CHECK(bramble_save_as(&B, "home", "plans.bramble", true) == ZXM_OK && !B.dirty &&
              !strcmp(B.path, "plans.bramble") && B.space[0] == 0,
          "bramble: saved: %s", LINK.err);
    CHECK(bramble_save_as(&B, "", "plans.bramble", true) == ZXM_E_REMOTE,
          "bramble: save_as never writes over a file that is there: %s", LINK.err);

    /* read back: the same tree, laid out the same */
    memset(&B2, 0, sizeof B2);
    B2.link = &LINK;
    CHECK(bramble_open(&B2, "", "plans.bramble") == ZXM_OK && B2.n == B.n && B2.depth == 9 &&
              B2.step == 32 && B2.width == 800 && B2.height == 600 && !B2.dirty,
          "bramble: opened: %s", LINK.err);
    uint32_t same = 0;
    for (uint32_t i = 0; i < B.n; i++)
        same += B2.node[i].parent == B.node[i].parent && B2.node[i].x == B.node[i].x &&
                B2.node[i].y == B.node[i].y && !strcmp(B2.node[i].label, B.node[i].label);
    CHECK(same == B.n, "bramble: %u of %u nodes read back as saved", same, B.n);

    /* a subtree removed, saved in place on the owner's word */
    CHECK(bramble_remove(&B2, 0) == ZXM_E_ARG && bramble_remove(&B2, 99) == ZXM_E_ARG,
          "bramble: the root and a missing node cannot be removed");
    CHECK(bramble_remove(&B2, kid[0]) == ZXM_OK && B2.n == 6 && B2.depth == 2 && B2.dirty &&
              !strcmp(B2.node[1].label, "Home") && B2.node[4].parent == 2 && B2.node[5].parent == 2,
          "bramble: Garden and everything under it removed (%u left)", B2.n);
    CHECK(B2.node[4].order == 0 && B2.node[5].order == 1 &&
              B2.node[4].x == bramble_ring_x(2, 0, 2, 32),
          "bramble: the ring of 64 laid out again for two");
    CHECK(bramble_save(&B2, false) == ZXM_E_STATE && B2.dirty,
          "bramble: save without the owner's word sends nothing");
    CHECK(bramble_open(&B, "", "plans.bramble") == ZXM_OK && B.n == 15,
          "bramble: so the file still holds 15 nodes");
    CHECK(bramble_save(&B2, true) == ZXM_OK && !B2.dirty &&
              bramble_open(&B, "", "plans.bramble") == ZXM_OK && B.n == 6 &&
              !strcmp(B.node[5].label, "Peru"),
          "bramble: saved in place and read again: %s", LINK.err);

    /* files that are not boards leave the map as it was */
    zxm_begin(&LINK, "files.write");
    zxm_p_str(&LINK, "path", "not-a-board.bramble");
    zxm_p_str(&LINK, "text", "bramble 1\ncanvas 800 600\nstep 32\nnode 0 loop\n");
    CHECK(zxm_call(&LINK) == ZXM_OK, "bramble: a broken board written: %s", LINK.err);
    CHECK(bramble_open(&B, "", "not-a-board.bramble") == ZXM_E_ARG && B.n == 6 &&
              !strcmp(B.path, "plans.bramble"),
          "bramble: a root with a parent is refused");
    zxm_begin(&LINK, "files.write");
    zxm_p_str(&LINK, "path", "too-deep.bramble");
    zxm_p_str(&LINK, "text", "bramble 1\ncanvas 100 100\nstep 64\nnode -1 a\nnode 0 b\n");
    CHECK(zxm_call(&LINK) == ZXM_OK, "bramble: a board past its canvas written: %s", LINK.err);
    CHECK(bramble_open(&B, "", "too-deep.bramble") == ZXM_E_ARG && B.n == 6,
          "bramble: a ring past the canvas is refused on open");
    CHECK(bramble_open(&B, "", "no-such.bramble") == ZXM_E_REMOTE && B.n == 6,
          "bramble: a missing board is zxvd's error: %s", LINK.err);
}
