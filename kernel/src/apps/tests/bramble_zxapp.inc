/* Copyright (c) 2024-2026 Michael Laurence Curzi and 36N9 Genetics, LLC */
/* SPDX-License-Identifier: Apache-2.0 */
/* Bramble (slot 48): ring_place_x (@num.bramble_ring_place_x) and
 * ring_place_y (@num.bramble_ring_place_y), C++ and KRON witnessed;
 * mindmap_limits_ok (@logic.bramble_mindmap_limits_ok), Eve and Sutra
 * witnessed; mind_node_ok (@logic.bramble_mind_node_ok, MYRA); and the
 * canvas it uses, gesso.canvas_limits_ok (@logic.gesso_canvas_limits_ok).
 * Each row against the rule as bramble_model.c runs it (written out here,
 * since this file cannot reach the model; tests/bramble_apps.inc checks
 * the model). */

static int64_t br_ring(int64_t depth, int64_t order, int64_t count, int64_t step, bool down)
{
    int64_t r = depth * step, p = order * 8 * r / count;
    if (p < 2 * r) return down ? -r : p - r;
    if (p < 4 * r) return down ? p - 3 * r : r;
    if (p < 6 * r) return down ? r : 5 * r - p;
    return down ? 7 * r - p : -r;
}

static int64_t br_limits(int64_t nodes, int64_t depth, int64_t step)
{
    return T_(nodes >= 1 && nodes <= 512 && depth >= 0 && depth <= 15 && step >= 16 && step <= 256);
}

static bool br_digits(int64_t v, int64_t top)
{
    return v > -top && v < top;
}

APP_TEST(bramble, 20, 48)
{
    static int64_t d[WAVE_ROWS], o[WAVE_ROWS], c[WAVE_ROWS], s[WAVE_ROWS], want[WAVE_ROWS],
        e[WAVE_ROWS], f[WAVE_ROWS];
    static const char *const entry[2] = {"ring_place_x", "ring_place_y"};

    /* the ring: a sweep over the whole domain, then whole rings of 8 and 5 */
    for (uint32_t k = 0; k < 2; k++) {
        for (uint32_t i = 0; i < WAVE_ROWS; i++) {
            d[i] = wrand(0, 64);
            s[i] = wrand(1, 4096);
            c[i] = wrand(1, 65536);
            o[i] = wrand(0, c[i] - 1);
            want[i] = br_ring(d[i], o[i], c[i], s[i], k == 1);
        }
        zxapp_arg_t ra[4] = {{"depth", d, WAVE_ROWS},
                             {"order", o, WAVE_ROWS},
                             {"count", c, WAVE_ROWS},
                             {"step", s, WAVE_ROWS}};
        CHECK(wave_same(app, entry[k], ra, 4, want, WAVE_ROWS, false),
              "Bramble: %s sweep, C++ = KRON = C", entry[k]);
        for (uint32_t i = 0; i < WAVE_ROWS; i++) {
            d[i] = 1 + i / 13u;
            s[i] = 16 + 8 * (i % 3u);
            c[i] = 13;
            o[i] = i % 13u;
            want[i] = br_ring(d[i], o[i], c[i], s[i], k == 1);
        }
        CHECK(wave_same(app, entry[k], ra, 4, want, WAVE_ROWS, false),
              "Bramble: %s, rings of 13 nodes round every side", entry[k]);
    }
    /* the edges: the root, each corner of a ring of 8, the widest ring */
    {
        int64_t ed[6] = {0, 1, 1, 1, 1, 64}, eo[6] = {0, 0, 2, 4, 6, 65535},
                ec[6] = {1, 8, 8, 8, 8, 65536}, es[6] = {1, 100, 100, 100, 100, 4096};
        int64_t wx[6] = {0, -100, 100, 100, -100, -262144},
                wy[6] = {0, -100, -100, 100, 100, -262112};
        zxapp_arg_t ea[4] = {{"depth", ed, 6}, {"order", eo, 6}, {"count", ec, 6}, {"step", es, 6}};
        for (uint32_t i = 0; i < 6; i++)
            CHECK(br_ring(ed[i], eo[i], ec[i], es[i], false) == wx[i] &&
                      br_ring(ed[i], eo[i], ec[i], es[i], true) == wy[i],
                  "Bramble: the C rule at edge %u", i);
        CHECK(wave_same(app, "ring_place_x", ea, 4, wx, 6, false),
              "Bramble: ring_place_x at the root, the corners and the widest ring");
        CHECK(wave_same(app, "ring_place_y", ea, 4, wy, 6, false),
              "Bramble: ring_place_y at the root, the corners and the widest ring");
    }
    /* past the domain: C++ traps, KRON answers: BOTH */
    {
        static const int64_t bd[5] = {-1, 65, 1, 1, 1}, bo[5] = {0, 0, 8, -1, 0},
                             bc[5] = {8, 8, 8, 8, 65537}, bs[5] = {100, 100, 100, 100, 100};
        for (uint32_t k = 0; k < 2; k++)
            for (uint32_t i = 0; i < 5; i++) {
                int64_t vd[2] = {1, bd[i]}, vo[2] = {0, bo[i]}, vc[2] = {8, bc[i]},
                        vs[2] = {100, bs[i]};
                zxapp_arg_t ba[4] = {
                    {"depth", vd, 2}, {"order", vo, 2}, {"count", vc, 2}, {"step", vs, 2}};
                CHECK(wave_lp(app, entry[k], ba, 4) == ZXAPP_BOTH,
                      "Bramble: %s past its domain (case %u): BOTH", entry[k], i);
            }
        int64_t vd[2] = {1, 1}, vo[2] = {0, 0}, vc[2] = {8, 8}, vs[2] = {100, 4097};
        zxapp_arg_t ba[4] = {{"depth", vd, 2}, {"order", vo, 2}, {"count", vc, 2}, {"step", vs, 2}};
        CHECK(wave_lp(app, "ring_place_x", ba, 4) == ZXAPP_BOTH, "Bramble: a step past 4096: BOTH");
    }

    /* mindmap_limits_ok: Eve's ranges, Sutra witnessed, across and past each edge */
    {
        static const int64_t ln[10] = {1, 512, 0, 513, 10, 10, 10, 10, 10, 9999};
        static const int64_t ld[10] = {0, 15, 3, 3, 16, 3, 3, 3, 0, 999};
        static const int64_t ls[10] = {16, 256, 64, 64, 64, 15, 257, 100, 16, 9999};
        for (uint32_t i = 0; i < 10; i++) {
            d[i] = ln[i];
            o[i] = ld[i];
            s[i] = ls[i];
            want[i] = br_limits(ln[i], ld[i], ls[i]);
        }
        zxapp_arg_t la[3] = {{"nodes", d, 10}, {"depth", o, 10}, {"step", s, 10}};
        CHECK(wave_same(app, "mindmap_limits_ok", la, 3, want, 10, true),
              "Bramble: mindmap_limits_ok edges, Eve = Sutra = C");
        for (uint32_t i = 0; i < WAVE_ROWS; i++) {
            d[i] = wrand(0, 600);
            o[i] = wrand(0, 20);
            s[i] = wrand(0, 300);
            want[i] = br_limits(d[i], o[i], s[i]);
        }
        zxapp_arg_t sa[3] = {
            {"nodes", d, WAVE_ROWS}, {"depth", o, WAVE_ROWS}, {"step", s, WAVE_ROWS}};
        CHECK(wave_same(app, "mindmap_limits_ok", sa, 3, want, WAVE_ROWS, true),
              "Bramble: mindmap_limits_ok sweep, Eve = Sutra = C");
        int64_t bn[2] = {10, -1}, bd[2] = {3, 3}, bs[2] = {64, 64};
        zxapp_arg_t ba[3] = {{"nodes", bn, 2}, {"depth", bd, 2}, {"step", bs, 2}};
        CHECK(wave_lp(app, "mindmap_limits_ok", ba, 3) == ZXAPP_BOTH,
              "Bramble: a negative node count: Sutra refuses, Eve answers false: BOTH");
        int64_t cn[2] = {10, 10}, cs[2] = {64, 10000};
        zxapp_arg_t ca[3] = {{"nodes", cn, 2}, {"depth", bd, 2}, {"step", cs, 2}};
        CHECK(wave_lp(app, "mindmap_limits_ok", ca, 3) == ZXAPP_BOTH,
              "Bramble: a step past Sutra's 9(4): BOTH");
    }

    /* mind_node_ok: MYRA's digit widths, i3 i3 i2 i3 i4 i4 */
    {
        for (uint32_t i = 0; i < WAVE_ROWS; i++) {
            d[i] = wrand(0, 1999);
            o[i] = wrand(-1999, 1999);
            c[i] = wrand(-199, 199);
            s[i] = wrand(-1999, 1999);
            e[i] = wrand(-19999, 19999);
            f[i] = wrand(-19999, 19999);
            want[i] = T_(br_digits(d[i], 1000) && br_digits(o[i], 1000) && br_digits(c[i], 100) &&
                         br_digits(s[i], 1000) && br_digits(e[i], 10000) && br_digits(f[i], 10000));
        }
        zxapp_arg_t na[6] = {{"node", d, WAVE_ROWS},  {"parent", o, WAVE_ROWS},
                             {"depth", c, WAVE_ROWS}, {"order", s, WAVE_ROWS},
                             {"x", e, WAVE_ROWS},     {"y", f, WAVE_ROWS}};
        CHECK(wave_same(app, "mind_node_ok", na, 6, want, WAVE_ROWS, true),
              "Bramble: mind_node_ok sweep (MYRA = C)");
        int64_t nn[5] = {0, 511, 999, 1000, 1}, np[5] = {-1, 510, -999, 0, 1000},
                nd[5] = {0, 15, 99, 1, 1}, no[5] = {0, 511, 999, 0, 0},
                nx[5] = {0, -1023, 9999, 0, 0}, ny[5] = {0, 1023, -9999, 0, 0},
                nw[5] = {ZXAPP_TRUE, ZXAPP_TRUE, ZXAPP_TRUE, ZXAPP_FALSE, ZXAPP_FALSE};
        zxapp_arg_t ea[6] = {{"node", nn, 5},  {"parent", np, 5}, {"depth", nd, 5},
                             {"order", no, 5}, {"x", nx, 5},      {"y", ny, 5}};
        CHECK(wave_same(app, "mind_node_ok", ea, 6, nw, 5, true),
              "Bramble: mind_node_ok at 0, the widest values either side and past them");
    }

    /* the canvas Bramble uses: Gesso's, 1..2048 a side */
    {
        int64_t cw[5] = {1, 2048, 0, 2049, 800}, chh[5] = {1, 2048, 600, 600, 0},
                cwant[5] = {ZXAPP_TRUE, ZXAPP_TRUE, ZXAPP_FALSE, ZXAPP_FALSE, ZXAPP_FALSE};
        zxapp_arg_t ga[2] = {{"width", cw, 5}, {"height", chh, 5}};
        CHECK(wave_same(app, "gesso.canvas_limits_ok", ga, 2, cwant, 5, true),
              "Bramble: a board's canvas through gesso.canvas_limits_ok");
    }
}
