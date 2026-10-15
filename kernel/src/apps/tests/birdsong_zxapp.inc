/* Copyright (c) 2024-2026 Michael Laurence Curzi and 36N9 Genetics, LLC */
/* SPDX-License-Identifier: Apache-2.0 */
/* Birdsong (slot 55): gain (@num.birdsong_gain, KRON and C++ witnessed),
 * clip_ms (@num.birdsong_clip_ms, Trebago), rec_ok (@logic.birdsong_rec_ok,
 * Eve) and memo_ok (@logic.birdsong_memo_ok, MYRA), each row against the
 * rule as birdsong_model.c runs it (written out here, since this file
 * cannot reach the model; tests/birdsong_apps.inc checks the model). */

static int64_t bs_gain(int64_t s, int64_t level) /* birdsong_gain */
{
    if (s < -32768 || s > 32767 || level < 0 || level > 1000000) return -32769;
    int64_t p = s * level, q = p / 10000;
    if (p % 10000 != 0 && p < 0) q -= 1; /* toward minus infinity */
    return q > 32767 ? 32767 : q < -32768 ? -32768 : q;
}

static int64_t bs_gcd(int64_t a, int64_t b)
{
    while (b) {
        int64_t t = a % b;
        a = b;
        b = t;
    }
    return a;
}

static int64_t bs_rec_ok(int64_t rate, int64_t ch, int64_t frames) /* birdsong_rec_ok */
{
    return T_(rate >= 8000 && rate <= 48000 && ch >= 1 && ch <= 2 && frames >= 0 &&
              frames <= 480000);
}

static bool bs_digits(int64_t v, int64_t top)
{
    return v > -top && v < top;
}

APP_TEST(birdsong, 5, 55)
{
    static int64_t s[WAVE_ROWS], want[WAVE_ROWS], c[WAVE_ROWS], f[WAVE_ROWS], m[WAVE_ROWS],
        t[WAVE_ROWS];

    /* gain: a sweep of whole 16-bit buffers at unity, silence, a boost that
     * clips, a cut whose negative products floor, and a random level */
    static const int64_t levels[5] = {10000, 0, 25000, 3333, -1};
    for (uint32_t r = 0; r < 5; r++) {
        int64_t lv[1] = {levels[r] < 0 ? wrand(0, 1000000) : levels[r]};
        for (uint32_t i = 0; i < WAVE_ROWS; i++) {
            s[i] = wrand(-32768, 32767);
            want[i] = bs_gain(s[i], lv[0]);
        }
        zxapp_arg_t ga[2] = {{"sample", s, WAVE_ROWS}, {"level", lv, 1}};
        CHECK(wave_same(app, "gain", ga, 2, want, WAVE_ROWS, false),
              "Birdsong: gain at level %lld, KRON = C++ = C", (long long) lv[0]);
    }
    /* the edges: 0, the 16-bit ends, the floor of -1/10000, the largest level */
    {
        static const int64_t es[8] = {0, 32767, -32768, 1, -1, -1, 32767, -32768};
        static const int64_t el[8] = {10000, 10000, 10000, 1, 1, 10000, 1000000, 1000000};
        static const int64_t ew[8] = {0, 32767, -32768, 0, -1, -1, 32767, -32768};
        for (uint32_t i = 0; i < 8; i++) {
            int64_t sv[2] = {es[i], es[i]}, lv[1] = {el[i]}, w[2] = {ew[i], ew[i]};
            CHECK(bs_gain(es[i], el[i]) == ew[i], "Birdsong: the C rule at edge %u", i);
            zxapp_arg_t ea[2] = {{"sample", sv, 2}, {"level", lv, 1}};
            CHECK(wave_same(app, "gain", ea, 2, w, 2, false),
                  "Birdsong: gain edge %u (%lld at %lld)", i, (long long) es[i], (long long) el[i]);
        }
    }
    /* past the domain: KRON still answers (clipped), the C++ refuses (-32769) */
    {
        int64_t big[2] = {100, 32768}, unity[1] = {10000};
        zxapp_arg_t ba[2] = {{"sample", big, 2}, {"level", unity, 1}};
        CHECK(wave_lp(app, "gain", ba, 2) == ZXAPP_BOTH,
              "Birdsong: a sample past 16 bits: KRON clips, C++ refuses: BOTH");
        int64_t low[2] = {-32769, 0};
        zxapp_arg_t la[2] = {{"sample", low, 2}, {"level", unity, 1}};
        CHECK(wave_lp(app, "gain", la, 2) == ZXAPP_BOTH, "Birdsong: a sample under 16 bits: BOTH");
        int64_t sv[2] = {100, -200}, neg[1] = {-10000}, over[1] = {1000001};
        zxapp_arg_t na[2] = {{"sample", sv, 2}, {"level", neg, 1}};
        CHECK(wave_lp(app, "gain", na, 2) == ZXAPP_BOTH, "Birdsong: a negative level: BOTH");
        zxapp_arg_t oa[2] = {{"sample", sv, 2}, {"level", over, 1}};
        CHECK(wave_lp(app, "gain", oa, 2) == ZXAPP_BOTH, "Birdsong: a level past 1000000: BOTH");
    }

    /* clip_ms: exact rationals (den is not 1), so zxapp_call against the
     * C's own gcd-reduced num/den; whole milliseconds also through wave_same */
    {
        static const int64_t rates[4] = {44100, 48000, 8000, 22050};
        for (uint32_t r = 0; r < 4; r++) {
            int64_t rate[1] = {rates[r]};
            for (uint32_t i = 0; i < WAVE_ROWS; i++) s[i] = wrand(0, 480000);
            s[0] = 0;
            s[1] = 1;
            zxapp_arg_t ca[2] = {{"samples", s, WAVE_ROWS}, {"rate", rate, 1}};
            zxapp_num_t o[WAVE_ROWS];
            uint32_t k = 0;
            int rc = zxapp_call(app, &H, "clip_ms", ca, 2, o, WAVE_ROWS, &k);
            uint32_t good = 0;
            for (uint32_t i = 0; rc == ZXAPP_OK && k == WAVE_ROWS && i < WAVE_ROWS; i++) {
                int64_t n = s[i] * 1000, g = bs_gcd(n, rate[0]);
                good += o[i].num == n / g && o[i].den == rate[0] / g;
            }
            CHECK(good == WAVE_ROWS, "Birdsong: clip_ms at %lld Hz, Trebago = C (%u of %u rows)",
                  (long long) rate[0], good, (unsigned) WAVE_ROWS);
        }
        int64_t rate[1] = {8000};
        for (uint32_t i = 0; i < WAVE_ROWS; i++) {
            s[i] = 8 * wrand(0, 60000);
            want[i] = s[i] / 8;
        }
        zxapp_arg_t wa[2] = {{"samples", s, WAVE_ROWS}, {"rate", rate, 1}};
        CHECK(wave_same(app, "clip_ms", wa, 2, want, WAVE_ROWS, false),
              "Birdsong: whole milliseconds at 8000 Hz (Trebago, unwitnessed)");
        /* the edges: the largest count that fits, one past it, a negative
         * count (Trebago computes it; the model refuses it), a rate of 0 */
        int64_t edge[2] = {INT64_MAX / 1000, -441}, r441[1] = {44100};
        zxapp_arg_t ea[2] = {{"samples", edge, 2}, {"rate", r441, 1}};
        zxapp_num_t o[2];
        uint32_t k = 0;
        int64_t g = bs_gcd(INT64_MAX / 1000 * 1000, 44100);
        CHECK(zxapp_call(app, &H, "clip_ms", ea, 2, o, 2, &k) == ZXAPP_OK && k == 2 &&
                  o[0].num == INT64_MAX / 1000 * 1000 / g && o[0].den == 44100 / g &&
                  o[1].num == -10 && o[1].den == 1,
              "Birdsong: clip_ms at the largest count, and a negative one");
        int64_t past[2] = {INT64_MAX / 1000 + 1, 1};
        zxapp_arg_t pa[2] = {{"samples", past, 2}, {"rate", r441, 1}};
        CHECK(zxapp_call(app, &H, "clip_ms", pa, 2, o, 2, &k) != ZXAPP_OK,
              "Birdsong: a count whose ms does not fit int64 is refused, never wrapped");
        int64_t zero[1] = {0}, one[2] = {1, 2};
        zxapp_arg_t za[2] = {{"samples", one, 2}, {"rate", zero, 1}};
        CHECK(zxapp_call(app, &H, "clip_ms", za, 2, o, 2, &k) != ZXAPP_OK,
              "Birdsong: a rate of 0 is refused");
    }

    /* rec_ok: Eve's ranges, swept across and just past every edge */
    {
        static const int64_t er[12] = {7999,  8000,  48000, 48001, 16000, 16000,
                                       16000, 16000, 16000, 16000, 16000, -8000};
        static const int64_t ec[12] = {1, 1, 2, 2, 0, 1, 2, 3, 1, 1, 1, 1};
        static const int64_t ef[12] = {0, 0, 480000, 0, 10, 10, 10, 10, -1, 480000, 480001, 0};
        for (uint32_t i = 0; i < 12; i++) {
            m[i] = er[i];
            c[i] = ec[i];
            f[i] = ef[i];
            want[i] = bs_rec_ok(er[i], ec[i], ef[i]);
        }
        zxapp_arg_t ra[3] = {{"rate", m, 12}, {"channels", c, 12}, {"frames", f, 12}};
        CHECK(wave_same(app, "rec_ok", ra, 3, want, 12, true), "Birdsong: rec_ok edges (Eve)");
        for (uint32_t i = 0; i < WAVE_ROWS; i++) {
            m[i] = wrand(0, 60000);
            c[i] = wrand(-1, 3);
            f[i] = wrand(-10, 500000);
            want[i] = bs_rec_ok(m[i], c[i], f[i]);
        }
        zxapp_arg_t sa[3] = {
            {"rate", m, WAVE_ROWS}, {"channels", c, WAVE_ROWS}, {"frames", f, WAVE_ROWS}};
        CHECK(wave_same(app, "rec_ok", sa, 3, want, WAVE_ROWS, true),
              "Birdsong: rec_ok sweep (Eve = C)");
    }

    /* memo_ok: MYRA's digit widths, i9 i5 i1 i9 i12 */
    {
        for (uint32_t i = 0; i < WAVE_ROWS; i++) {
            s[i] = wrand(0, 1999999999);
            m[i] = wrand(0, 199999);
            c[i] = wrand(0, 19);
            f[i] = wrand(0, 1999999999);
            t[i] = wrand(0, INT64_C(1999999999999));
            want[i] =
                T_(bs_digits(s[i], 1000000000) && bs_digits(m[i], 100000) && bs_digits(c[i], 10) &&
                   bs_digits(f[i], 1000000000) && bs_digits(t[i], INT64_C(1000000000000)));
        }
        zxapp_arg_t ma[5] = {{"memo", s, WAVE_ROWS},
                             {"rate", m, WAVE_ROWS},
                             {"channels", c, WAVE_ROWS},
                             {"frames", f, WAVE_ROWS},
                             {"mtime", t, WAVE_ROWS}};
        CHECK(wave_same(app, "memo_ok", ma, 5, want, WAVE_ROWS, true),
              "Birdsong: memo_ok sweep (MYRA = C)");
        int64_t em[5] = {0, 999999999, 1000000000, -999999999, -1000000000},
                er[5] = {0, 99999, 48000, -99999, 1}, ec[5] = {0, 9, 2, -9, 1},
                ef[5] = {0, 999999999, 1, -1, 1}, et[5] = {0, INT64_C(999999999999), 1, -1, 1},
                ew[5] = {ZXAPP_TRUE, ZXAPP_TRUE, ZXAPP_FALSE, ZXAPP_TRUE, ZXAPP_FALSE};
        zxapp_arg_t xa[5] = {{"memo", em, 5},
                             {"rate", er, 5},
                             {"channels", ec, 5},
                             {"frames", ef, 5},
                             {"mtime", et, 5}};
        CHECK(wave_same(app, "memo_ok", xa, 5, ew, 5, true),
              "Birdsong: memo_ok at 0, the widest values either side, and 10-digit memos");
    }
}
