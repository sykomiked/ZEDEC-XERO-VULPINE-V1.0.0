/* Copyright (c) 2024-2026 Michael Laurence Curzi and 36N9 Genetics, LLC */
/* SPDX-License-Identifier: Apache-2.0 */
/* Mockingbird's languages against the C its model runs (mockingbird_rules.h).
 * Slots: @logic.mockingbird_boot_passed, @num.mockingbird_region_overlaps,
 * @num.mockingbird_run_cycles, @logic.mockingbird_emu_limits_ok,
 * @logic.mockingbird_emu_device_ok. */
#include "mockingbird_rules.h"

APP_TEST(mockingbird, 8, 128)
{
    static int64_t s[MOCK_STAGES][WAVE_ROWS], a[WAVE_ROWS], b[WAVE_ROWS], c[WAVE_ROWS],
        d[WAVE_ROWS], e[WAVE_ROWS], want[WAVE_ROWS];
    static const char *const stage[MOCK_STAGES] = {"rom", "loader", "kernel", "drivers", "apps"};

    /* boot_passed (MR1): boots as the model runs them, in order: every stage
     * before the first failure 0, that one a fault, the rest never run */
    for (uint32_t r = 0; r < 2; r++) {
        for (uint32_t i = 0; i < WAVE_ROWS; i++) {
            int64_t code[MOCK_STAGES];
            uint32_t f = (uint32_t) wrand(0, r ? 9 : MOCK_STAGES);
            for (uint32_t k = 0; k < MOCK_STAGES; k++) {
                code[k] = k < f ? 0 : k == f ? wrand(1, MOCK_CODE_MAX) : MOCK_NOT_RUN;
                s[k][i] = code[k];
            }
            want[i] = T_(mockingbird_boot_passed(code));
            CHECK(mockingbird_first_failed(code) == (f < MOCK_STAGES ? f : MOCK_STAGES),
                  "Mockingbird: the first failed stage, in order, row %u", i);
        }
        zxapp_arg_t ba[MOCK_STAGES];
        for (uint32_t k = 0; k < MOCK_STAGES; k++) ba[k] = (zxapp_arg_t){stage[k], s[k], WAVE_ROWS};
        CHECK(wave_same(app, "boot_passed", ba, MOCK_STAGES, want, WAVE_ROWS, true),
              "Mockingbird: boot_passed sweep %u, Eve = KRON = C", r);
    }
    {
        /* the edges: all passed, a fault at each end of its range, each
         * stage failing alone with the rest never run */
        static const int64_t er[7] = {0, 1, 255, 0, 0, 0, 0}, el[7] = {0, -1, -1, 2, 0, 0, 0},
                             ek[7] = {0, -1, -1, -1, 3, 0, 0}, ed[7] = {0, -1, -1, -1, -1, 4, 0},
                             ea[7] = {0, -1, -1, -1, -1, -1, 5};
        for (uint32_t i = 0; i < 7; i++) {
            int64_t code[MOCK_STAGES] = {er[i], el[i], ek[i], ed[i], ea[i]};
            want[i] = T_(mockingbird_boot_passed(code));
        }
        CHECK(want[0] == ZXAPP_TRUE && want[1] == ZXAPP_FALSE && want[6] == ZXAPP_FALSE,
              "Mockingbird: the C boot rule at its edges");
        zxapp_arg_t ba[MOCK_STAGES] = {{"rom", er, 7},
                                       {"loader", el, 7},
                                       {"kernel", ek, 7},
                                       {"drivers", ed, 7},
                                       {"apps", ea, 7}};
        CHECK(wave_same(app, "boot_passed", ba, MOCK_STAGES, want, 7, true),
              "Mockingbird: boot_passed edges (Eve = KRON = C)");
        /* a stage that never ran where none failed before it: Eve refuses
         * the record, KRON reads -1 as passed: held */
        int64_t z2[2] = {0, 0}, skip[2] = {0, -1};
        zxapp_arg_t sk[MOCK_STAGES] = {{"rom", z2, 2},
                                       {"loader", z2, 2},
                                       {"kernel", skip, 2},
                                       {"drivers", z2, 2},
                                       {"apps", z2, 2}};
        CHECK(wave_lp(app, "boot_passed", sk, MOCK_STAGES) == ZXAPP_BOTH,
              "Mockingbird: a skipped stage: Eve refuses, KRON passes it: BOTH");
        int64_t code[MOCK_STAGES] = {0, 0, -1, 0, 0};
        CHECK(!mockingbird_boot_passed(code) && mockingbird_first_failed(code) == 2,
              "Mockingbird: the C names the skipped stage");
    }

    /* region_overlaps (MR2): maps sorted by base, mostly clean, some
     * neighbours overlapping and some out of order, Trebago = KRON = C */
    for (uint32_t r = 0; r < 16; r++) {
        int64_t at = 0, over = -1;
        uint32_t n = r < 2 ? r + 2u : WAVE_ROWS;
        for (uint32_t i = 0; i < n; i++) {
            at += wrand(0, INT64_C(1) << 40);
            a[i] = at;
            b[i] = wrand(0, INT64_C(1) << 40);
            if (wrand(0, 15) == 0) a[i] = wrand(0, at); /* out of order */
        }
        CHECK(mockingbird_region_overlaps(a, b, n, &over) && over >= 0,
              "Mockingbird: the C counts map %u", r);
        zxapp_arg_t ra[2] = {{"base", a, n}, {"size", b, n}};
        CHECK(wave_same(app, "region_overlaps", ra, 2, &over, 1, false),
              "Mockingbird: region_overlaps map %u (%lld), Trebago = KRON = C", r,
              (long long) over);
    }
    {
        /* the edges: touching regions, one byte over, out of order, empty
         * regions, a region ending exactly at 2^48 */
        static const int64_t t1b[3] = {0, 4096, 8192}, t1s[3] = {4096, 4096, 4096};
        static const int64_t t2b[3] = {0, 4095, 8192}, t2s[3] = {4096, 4098, 4096};
        static const int64_t t3b[3] = {0, 8192, 4096}, t3s[3] = {4096, 4096, 4096};
        static const int64_t t4b[3] = {0, 0, 0}, t4s[3] = {0, 0, 0};
        static const int64_t t5b[2] = {0, MOCK_ADDR_MAX - 4096},
                             t5s[2] = {MOCK_ADDR_MAX - 4095, 4096};
        static const int64_t t6b[2] = {0, MOCK_ADDR_MAX}, t6s[2] = {MOCK_ADDR_MAX, 0};
        const int64_t *eb[6] = {t1b, t2b, t3b, t4b, t5b, t6b},
                      *es[6] = {t1s, t2s, t3s, t4s, t5s, t6s};
        static const uint32_t en[6] = {3, 3, 3, 3, 2, 2};
        static const int64_t ew[6] = {0, 2, 1, 0, 1, 0};
        for (uint32_t i = 0; i < 6; i++) {
            int64_t over = -1;
            zxapp_arg_t ra[2] = {{"base", eb[i], en[i]}, {"size", es[i], en[i]}};
            CHECK(mockingbird_region_overlaps(eb[i], es[i], en[i], &over) && over == ew[i] &&
                      wave_same(app, "region_overlaps", ra, 2, &ew[i], 1, false),
                  "Mockingbird: region_overlaps edge %u: %lld", i, (long long) over);
        }
        int64_t over = 0;
        int64_t nb[2] = {0, 4096}, neg[2] = {4096, -1}, far[2] = {0, MOCK_ADDR_MAX},
                one[2] = {4096, 1}, low[2] = {-1, 4096}, big[2] = {4096, MOCK_ADDR_MAX + 1};
        CHECK(!mockingbird_region_overlaps(nb, neg, 2, &over) &&
                  !mockingbird_region_overlaps(far, one, 2, &over) &&
                  !mockingbird_region_overlaps(low, one, 2, &over) &&
                  !mockingbird_region_overlaps(nb, big, 2, &over) &&
                  !mockingbird_region_overlaps(nb, one, 0, &over),
              "Mockingbird: the C refuses outside the address space, and an empty map");
        zxapp_arg_t p1[2] = {{"base", nb, 2}, {"size", neg, 2}};
        CHECK(wave_lp(app, "region_overlaps", p1, 2) == ZXAPP_BOTH,
              "Mockingbird: a negative size: Trebago refuses, KRON answers: BOTH");
        zxapp_arg_t p2[2] = {{"base", far, 2}, {"size", one, 2}};
        CHECK(wave_lp(app, "region_overlaps", p2, 2) == ZXAPP_BOTH,
              "Mockingbird: a region past 2^48: BOTH");
        zxapp_arg_t p3[2] = {{"base", low, 2}, {"size", one, 2}};
        CHECK(wave_lp(app, "region_overlaps", p3, 2) == ZXAPP_BOTH,
              "Mockingbird: a negative base: BOTH");
    }

    /* run_cycles (MR3): runs across Sutra's pictures, Sutra = Trebago = C */
    for (uint32_t r = 0; r < 2; r++) {
        for (uint32_t i = 0; i < WAVE_ROWS; i++) {
            a[i] = wrand(0, r ? 1000 : MOCK_STEPS_MAX);
            b[i] = wrand(0, MOCK_COST_MAX);
            c[i] = wrand(0, r ? 10 : MOCK_STALL_MAX);
            CHECK(mockingbird_run_cycles(a[i], b[i], c[i], &want[i]), "Mockingbird: run row %u", i);
        }
        zxapp_arg_t ca[3] = {
            {"steps", a, WAVE_ROWS}, {"cost", b, WAVE_ROWS}, {"stall", c, WAVE_ROWS}};
        CHECK(wave_same(app, "run_cycles", ca, 3, want, WAVE_ROWS, false),
              "Mockingbird: run_cycles sweep %u, Sutra = Trebago = C", r);
    }
    {
        static const int64_t es[4] = {0, 1, 1000, MOCK_STEPS_MAX}, ec[4] = {0, 1, 4, MOCK_COST_MAX},
                             el[4] = {0, 0, 7, MOCK_STALL_MAX},
                             ew[4] = {0, 1, 4007, INT64_C(9999999990000)};
        for (uint32_t i = 0; i < 4; i++) {
            int64_t o = -1;
            CHECK(mockingbird_run_cycles(es[i], ec[i], el[i], &o) && o == ew[i],
                  "Mockingbird: the C run rule at edge %u", i);
        }
        zxapp_arg_t ca[3] = {{"steps", es, 4}, {"cost", ec, 4}, {"stall", el, 4}};
        CHECK(wave_same(app, "run_cycles", ca, 3, ew, 4, false),
              "Mockingbird: run_cycles at 0 and at every picture's most");
        int64_t o = 0;
        CHECK(!mockingbird_run_cycles(MOCK_STEPS_MAX + 1, 1, 0, &o) &&
                  !mockingbird_run_cycles(1, MOCK_COST_MAX + 1, 0, &o) &&
                  !mockingbird_run_cycles(1, 1, MOCK_STALL_MAX + 1, &o) &&
                  !mockingbird_run_cycles(-1, 1, 0, &o) && !mockingbird_run_cycles(1, -1, 0, &o) &&
                  !mockingbird_run_cycles(1, 1, -1, &o),
              "Mockingbird: the C refuses past the pictures and below 0");
        int64_t one[2] = {1, 1}, past[2] = {1, MOCK_STEPS_MAX + 1}, z2[2] = {0, 0};
        zxapp_arg_t pa[3] = {{"steps", past, 2}, {"cost", one, 2}, {"stall", z2, 2}};
        CHECK(wave_lp(app, "run_cycles", pa, 3) == ZXAPP_BOTH,
              "Mockingbird: 10^9 steps: Sutra refuses, Trebago answers: BOTH");
        int64_t costly[2] = {1, MOCK_COST_MAX + 1};
        zxapp_arg_t pc[3] = {{"steps", one, 2}, {"cost", costly, 2}, {"stall", z2, 2}};
        CHECK(wave_lp(app, "run_cycles", pc, 3) == ZXAPP_BOTH,
              "Mockingbird: a cost past 9(4): BOTH");
        int64_t neg[2] = {1, -1};
        zxapp_arg_t pn[3] = {{"steps", neg, 2}, {"cost", one, 2}, {"stall", z2, 2}};
        zxapp_num_t out[2];
        uint32_t n = 0;
        int32_t lp = -1;
        CHECK(zxapp_call_witnessed(app, &H, "run_cycles", pn, 3, out, 2, &n, &lp) != ZXAPP_OK,
              "Mockingbird: a negative step count is refused by both");
    }

    /* emu_limits_ok (MR4): Eve's ranges, at and past every edge, and a sweep */
    {
        static const int64_t er[9] = {1,     MOCK_RAM_MAX, 0, MOCK_RAM_MAX + 1, 65536, 65536,
                                      65536, 65536,        -1};
        static const int64_t ed[9] = {0, 64, 0, 0, 65, -1, 0, 0, 0};
        static const int64_t ec[9] = {0, MOCK_CYCLES_MAX, 0, 0, 0, 0, MOCK_CYCLES_MAX + 1, -1, 0};
        for (uint32_t i = 0; i < 9; i++) want[i] = T_(mockingbird_limits_ok(er[i], ed[i], ec[i]));
        CHECK(want[0] == ZXAPP_TRUE && want[1] == ZXAPP_TRUE && want[2] == ZXAPP_FALSE &&
                  want[3] == ZXAPP_FALSE && want[6] == ZXAPP_FALSE,
              "Mockingbird: emu_limits_ok, the C's own edges");
        zxapp_arg_t la[3] = {{"ram", er, 9}, {"devices", ed, 9}, {"cycles", ec, 9}};
        CHECK(wave_same(app, "emu_limits_ok", la, 3, want, 9, true),
              "Mockingbird: emu_limits_ok edges (Eve = C)");
        for (uint32_t i = 0; i < WAVE_ROWS; i++) {
            a[i] = wrand(-5, MOCK_RAM_MAX + 5);
            b[i] = wrand(-3, 70);
            c[i] = wrand(-5, MOCK_CYCLES_MAX + 5);
            if (i % 4 == 0) a[i] = wrand(-3, 3);
            if (i % 4 == 1) c[i] = MOCK_CYCLES_MAX + wrand(-2, 2);
            want[i] = T_(mockingbird_limits_ok(a[i], b[i], c[i]));
        }
        zxapp_arg_t sa[3] = {
            {"ram", a, WAVE_ROWS}, {"devices", b, WAVE_ROWS}, {"cycles", c, WAVE_ROWS}};
        CHECK(wave_same(app, "emu_limits_ok", sa, 3, want, WAVE_ROWS, true),
              "Mockingbird: emu_limits_ok sweep (Eve = C)");
    }

    /* emu_device_ok (MR5): MYRA's digit widths, i2 i2 i15 i15 i3 */
    {
        for (uint32_t i = 0; i < WAVE_ROWS; i++) {
            a[i] = wrand(-199, 199);
            b[i] = wrand(-199, 199);
            c[i] = wrand(-INT64_C(1999999999999999), INT64_C(1999999999999999));
            d[i] = wrand(-INT64_C(1999999999999999), INT64_C(1999999999999999));
            e[i] = wrand(-1999, 1999);
            want[i] = T_(mockingbird_device_record_ok(a[i], b[i], c[i], d[i], e[i]));
        }
        zxapp_arg_t da[5] = {{"device", a, WAVE_ROWS},
                             {"kind", b, WAVE_ROWS},
                             {"base", c, WAVE_ROWS},
                             {"size", d, WAVE_ROWS},
                             {"irq", e, WAVE_ROWS}};
        CHECK(wave_same(app, "emu_device_ok", da, 5, want, WAVE_ROWS, true),
              "Mockingbird: emu_device_ok sweep (MYRA = C)");
        static const int64_t ea[7] = {0, 99, 100, -99, 0, 0, 0}, eb[7] = {0, 99, 1, -99, 100, 1, 1},
                             ec[7] = {0, INT64_C(999999999999999),  0, -1,
                                      0, INT64_C(1000000000000000), 0},
                             ed[7] = {0, INT64_C(999999999999999), 1, -1, 1, 1, 1},
                             ee[7] = {0, 999, 1, -999, 1, 1, 1000};
        static const int64_t ew[7] = {ZXAPP_TRUE,  ZXAPP_TRUE,  ZXAPP_FALSE, ZXAPP_TRUE,
                                      ZXAPP_FALSE, ZXAPP_FALSE, ZXAPP_FALSE};
        for (uint32_t i = 0; i < 7; i++)
            CHECK(T_(mockingbird_device_record_ok(ea[i], eb[i], ec[i], ed[i], ee[i])) == ew[i],
                  "Mockingbird: the C record rule at edge %u", i);
        zxapp_arg_t xa[5] = {
            {"device", ea, 7}, {"kind", eb, 7}, {"base", ec, 7}, {"size", ed, 7}, {"irq", ee, 7}};
        CHECK(wave_same(app, "emu_device_ok", xa, 5, ew, 7, true),
              "Mockingbird: emu_device_ok at 0, the widest values either side, and one past each");
    }
}
