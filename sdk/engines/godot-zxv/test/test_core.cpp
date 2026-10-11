// Copyright (c) 2024-2026 Michael Laurence Curzi and 36N9 Genetics, LLC
// SPDX-License-Identifier: Apache-2.0
//
// test_core.cpp — tests the C++ layer under the Godot classes, without
// Godot: exact rationals, hexagram tags, and two lockstep peers (one
// predicting the other's late inputs) agreeing hash for hash.
#include <cstdio>
#include <cstring>
#include <type_traits>
#include <utility>

#include "zxv_gd_core.hpp"

using zxvgd::Hexagram;
using zxvgd::Lockstep;
using zxvgd::Rational;

static int g_checks, g_fail;

#define CHECK(cond, what)                                                                   \
    do {                                                                                    \
        g_checks++;                                                                         \
        if (!(cond)) {                                                                      \
            g_fail++;                                                                       \
            std::fprintf(stderr, "FAIL %s:%d: %s [%s]\n", __FILE__, __LINE__, what,         \
                         zxvgd::last_error().c_str());                                      \
        }                                                                                   \
    } while (0)
#define OK(expr) CHECK((expr) == ZXV_OK, #expr)
#define IS(expr, st) CHECK((expr) == (st), #expr " != " #st)

// A floating-point value never converts to a Rational (deleted constructor).
static_assert(!std::is_constructible<Rational, decltype(0.5)>::value, "double must not convert");
static_assert(!std::is_constructible<Rational, decltype(0.5f)>::value, "float must not convert");
static_assert(!std::is_convertible<decltype(0.5), Rational>::value, "no implicit conversion");
static_assert(std::is_nothrow_move_constructible<Lockstep>::value, "Lockstep moves");
static_assert(!std::is_copy_constructible<Lockstep>::value, "Lockstep is move-only");

static Rational R(long long n, long long d) {
    Rational r;
    Rational::from_ints(n, d, &r);
    return r;
}

static void test_rational() {
    Rational a, b, c;
    OK(Rational::parse("0.1", &a));
    OK(Rational::parse("0.2", &b));
    OK(a.add(b, &c));
    CHECK(c.to_string() == "0.3", "0.1 + 0.2 is exactly 0.3");
    CHECK(c.num() == 3 && c.den() == 10, "3/10");
    CHECK(c.to_fraction_string() == "3/10", "fraction string");
    OK(Rational::from_ints(2, -6, &a));
    CHECK(a.num() == -1 && a.den() == 3, "reduced, sign on top");
    CHECK(a.to_string() == "-1/3", "non-terminating stays a fraction");
    OK(Rational::parse("3/4", &a));
    CHECK(a == R(3, 4), "parse fraction");
    OK(Rational::parse("-12.75", &a));
    CHECK(a == R(-51, 4), "parse decimal");
    IS(Rational::parse("1e3", &a), ZXV_E_FORMAT);
    IS(Rational::parse("nan", &a), ZXV_E_FORMAT);
    IS(Rational::parse(" 1", &a), ZXV_E_FORMAT);
    IS(Rational::from_ints(1, 0, &a), ZXV_E_ARG);
    IS(Rational::from_ints(1, 1, nullptr), ZXV_E_ARG);
    CHECK(!zxvgd::last_error().empty(), "detail text");
    IS(R(1, 1).div(Rational(), &a), ZXV_E_ARG);
    Rational big = R(INT64_MAX, 1);
    IS(big.add(R(1, 1), &a), ZXV_E_RANGE);
    IS(big.mul(R(2, 1), &a), ZXV_E_RANGE);
    OK(R(1, 3).sub(R(1, 2), &a));
    CHECK(a == R(-1, 6), "1/3 - 1/2");
    OK(R(2, 3).mul(R(3, 4), &a));
    CHECK(a == R(1, 2), "2/3 * 3/4");
    OK(R(1, 3).neg(&a));
    CHECK(a == R(-1, 3), "neg");
    CHECK(R(1, 3).compare(R(1, 2)) == -1 && R(1, 2).compare(R(2, 4)) == 0 &&
              R(2, 3).compare(R(1, 2)) == 1,
          "compare is exact");
    CHECK(Rational().is_zero() && Rational().den() == 1, "default is 0/1");
    // 1/3 added three times is exactly one.
    Rational s;
    for (int i = 0; i < 3; i++) s.add(R(1, 3), &s);
    CHECK(s == R(1, 1), "three thirds");
}

static void test_hexagram() {
    Hexagram h;
    for (int t = 0; t < 64; t++) {
        OK(Hexagram::from_tag(t, &h));
        Hexagram g;
        OK(Hexagram::from_fields(h.base(), h.polarity(), h.phase(), &g));
        CHECK(g.tag() == t, "fields round trip");
        CHECK(std::strcmp(h.name(), zxv_hex_name(static_cast<uint8_t>(t))) == 0, "name from libzxv");
        CHECK(h.is_forbidden() == (h.action() == ZXV_HEX_ACT_ROLLBACK), "forbidden <-> rollback");
        Hexagram n = h.advance();
        CHECK(n.base() == h.base() && n.polarity() == h.polarity() && n.phase() != h.phase(),
              "advance moves the phase only");
        CHECK(h.not_().not_() == h, "not is an involution");
    }
    IS(Hexagram::from_tag(64, &h), ZXV_E_ARG);
    IS(Hexagram::from_tag(-1, &h), ZXV_E_ARG);
    IS(Hexagram::from_fields(4, 0, 0, &h), ZXV_E_ARG);
    OK(Hexagram::from_fields(ZXV_HEX_BASE_TRUE, ZXV_HEX_POL_NEGENTROPIC, ZXV_HEX_PHASE_MANIFEST, &h));
    CHECK(h.tag() == 63, "true/negentropic/manifest is 63");
    CHECK(std::strcmp(h.name(), "true/negentropic/manifest") == 0, "name");
    CHECK(h.action() == ZXV_HEX_ACT_PROCEED && std::strcmp(h.action_name(), "proceed") == 0, "proceed");
    CHECK(h.to_truth() == ZXV_LP_TRUE, "base TRUE is Truth TRUE (1), not 3");
    // the four-valued bridge, never a cast: lp -> base is [1, 3, 0, 2]
    const int lp_to_base[4] = {1, 3, 0, 2};
    for (int lp = 0; lp < 4; lp++) {
        OK(Hexagram::from_truth(lp, ZXV_HEX_POL_NEUTRAL, ZXV_HEX_PHASE_POTENTIAL, &h));
        CHECK(h.base() == lp_to_base[lp] && h.to_truth() == lp, "lp <-> base");
    }
    IS(Hexagram::from_truth(4, 0, 0, &h), ZXV_E_ARG);
    // one step at a time; forbidden is absorbing
    OK(Hexagram::from_fields(ZXV_HEX_BASE_TRUE, ZXV_HEX_POL_NEUTRAL, ZXV_HEX_PHASE_POTENTIAL, &h));
    Hexagram g;
    IS(h.step(ZXV_HEX_PHASE_MANIFEST, &g), ZXV_E_STATE);
    OK(h.step(ZXV_HEX_PHASE_EMERGING, &g));
    Hexagram f = g.forbid();
    CHECK(f.is_forbidden() && f.action() == ZXV_HEX_ACT_ROLLBACK, "forbid");
    IS(f.with_state(ZXV_HEX_BASE_TRUE, ZXV_HEX_POL_NEUTRAL, &g), ZXV_E_STATE);
    CHECK(f.and_(h).is_forbidden() && f.or_(h).is_forbidden() && f.not_().is_forbidden(),
          "forbidden absorbs and/or/not");
    // pending owner decision stays in libzxv: BOTH AND NEITHER = NEITHER today
    Hexagram both, neither;
    OK(Hexagram::from_fields(ZXV_HEX_BASE_BOTH, ZXV_HEX_POL_NEUTRAL, 0, &both));
    OK(Hexagram::from_fields(ZXV_HEX_BASE_NEITHER, ZXV_HEX_POL_NEUTRAL, 0, &neither));
    uint8_t want = 0;
    zxv_hex_and(static_cast<uint8_t>(both.tag()), static_cast<uint8_t>(neither.tag()), &want);
    CHECK(both.and_(neither).tag() == want, "and comes from libzxv");
}

// The game: rat slot p = player p's position (moves 1/3 per tick), rat slot
// P = a tide (+ (tick mod 7)/5), hex slot p = the player's claim.
static int32_t step_game(Lockstep &ls, uint64_t tick, uint32_t players) {
    int32_t rc;
    for (uint32_t p = 0; p < players; p++) {
        std::vector<uint8_t> in;
        if ((rc = ls.input(p, &in, nullptr)) != ZXV_OK) return rc;
        if ((rc = ls.charge(R(1, 1))) != ZXV_OK) return rc;
        long long dir = in[0] == 0 ? 0 : (in[0] < 128 ? 1 : -1);
        if (dir != 0 && (rc = ls.add_rational(p, R(dir, 3))) != ZXV_OK) return rc;
        Hexagram h;
        if ((rc = ls.get_hexagram(p, &h)) != ZXV_OK) return rc;
        if (in[1] & 2u) h = h.forbid();
        else if ((in[1] & 1u) && !h.is_forbidden())
            h.with_state(ZXV_HEX_BASE_TRUE, ZXV_HEX_POL_NEGENTROPIC, &h);
        if ((rc = ls.set_hexagram(p, h.advance())) != ZXV_OK) return rc;
    }
    return ls.add_rational(players, R(static_cast<long long>(tick % 7u), 5));
}

static std::vector<uint8_t> input_for(uint32_t p, uint64_t t) {
    uint64_t x = (t * 2654435761u) ^ (static_cast<uint64_t>(p) * 40503u);
    return {static_cast<uint8_t>(x % 3u == 0 ? 0 : (x % 3u == 1 ? 1 : 0xFF)),
            static_cast<uint8_t>((x >> 3) % 5u == 0 ? 1u : (p == 1 && t == 9 ? 2u : 0u))};
}

static int32_t make_game(Lockstep &ls, uint32_t players, bool predict) {
    int32_t rc = ls.create(players, players + 1, players, 2, 16, predict);
    if (rc != ZXV_OK) return rc;
    for (uint32_t p = 0; p < players; p++) {
        Hexagram h;
        Hexagram::from_fields(ZXV_HEX_BASE_NEITHER, ZXV_HEX_POL_NEUTRAL, ZXV_HEX_PHASE_POTENTIAL, &h);
        if ((rc = ls.set_rational(p, R(p, 2))) != ZXV_OK) return rc;
        if ((rc = ls.set_hexagram(p, h)) != ZXV_OK) return rc;
    }
    return ls.start();
}

static void test_lockstep() {
    const uint32_t P = 3;
    const uint64_t T = 40, LAG = 4;
    Lockstep a, b;
    IS(a.start(), ZXV_E_STATE); // not created
    OK(make_game(a, P, true));
    OK(make_game(b, P, false));
    OK(a.set_tick_budget(R(P, 1))); // exactly one unit per player per tick
    auto step = [&](Lockstep &ls, uint64_t t) { return step_game(ls, t, P); };
    CHECK(!b.ready(), "strict peer waits for inputs");
    IS(b.advance(step), ZXV_E_STATE);

    unsigned rollbacks = 0;
    for (uint64_t t = 1; t <= T + LAG; t++) {
        if (t <= T) {
            for (uint32_t p = 0; p < P; p++) OK(b.submit_input(p, t, input_for(p, t)));
            OK(a.submit_input(0, t, input_for(0, t))); // a's local player
        }
        if (t > LAG)
            for (uint32_t p = 1; p < P; p++) OK(a.submit_input(p, t - LAG, input_for(p, t - LAG)));
        uint64_t pt = 0;
        if (a.rollback_pending(&pt)) rollbacks++;
        OK(a.rollback_replay(step));
        if (t <= T) {
            CHECK(a.ready() && b.ready(), "both ready");
            OK(a.advance(step));
            OK(b.advance(step));
        }
    }
    CHECK(rollbacks > 0, "the lagging inputs caused rollbacks");
    CHECK(a.tick() == T && b.tick() == T, "both at T");
    CHECK(a.confirmed_tick() == T, "a is fully confirmed");
    std::vector<uint8_t> ha, hb;
    bool fin = false;
    for (uint64_t t = T - 10; t <= T; t++) {
        OK(a.hash(t, &ha, &fin));
        OK(b.hash(t, &hb, nullptr));
        CHECK(ha == hb && fin, "same final hash on both peers");
        int lp = -1;
        OK(a.compare(t, hb, &lp));
        CHECK(lp == ZXV_LP_TRUE, "compare TRUE");
    }
    uint64_t dt = 0;
    CHECK(!a.desync(&dt), "no desync");
    int lp = -1;
    IS(a.compare(T, std::vector<uint8_t>(31, 0), &lp), ZXV_E_ARG);
    OK(a.compare(T, std::vector<uint8_t>(32, 0), &lp));
    CHECK(lp == ZXV_LP_FALSE && a.desync(&dt) && dt == T, "a wrong hash is a desync");

    Rational pos, tide;
    OK(a.get_rational(0, &pos));
    OK(b.get_rational(P, &tide));
    // sum over t = 1..40 of (t mod 7)/5 = (5 * 21 + 1+2+3+4+5)/5 = 120/5 = 24
    CHECK(tide == R(24, 1), "tide is exact");
    CHECK(pos.den() == 1 || pos.den() == 3, "positions are thirds");

    // budget: one more charge than the limit refuses the tick
    OK(a.submit_input(0, T + 1, input_for(0, T + 1)));
    auto greedy = [&](Lockstep &ls, uint64_t t) {
        int32_t rc = step_game(ls, t, P);
        return rc != ZXV_OK ? rc : ls.charge(R(1, 1));
    };
    IS(a.advance(greedy), ZXV_E_BUDGET);
    CHECK(a.tick() == T, "refused tick did not advance");
    OK(a.advance(step));

    // a failing step aborts and keeps its own detail text
    auto bad = [](Lockstep &, uint64_t) -> int32_t { return ZXV_E_INVARIANT; };
    OK(b.submit_input(0, T + 1, input_for(0, T + 1)));
    for (uint32_t p = 1; p < P; p++) OK(b.submit_input(p, T + 1, input_for(p, T + 1)));
    IS(b.advance(bad), ZXV_E_INVARIANT);
    CHECK(zxvgd::last_error().find("ZXV_E_INVARIANT") != std::string::npos, "step status named");
    CHECK(b.tick() == T, "aborted");
    IS(b.advance(Lockstep::Step()), ZXV_E_ARG);

    // state transfer
    std::vector<uint8_t> bytes;
    OK(b.export_state(&bytes));
    Lockstep c;
    OK(c.create(P, P + 1, P, 2, 16, false));
    OK(c.import_state(bytes));
    CHECK(c.tick() == T, "imported tick");
    OK(c.hash(T, &ha, &fin));
    OK(b.hash(T, &hb, nullptr));
    CHECK(ha == hb && fin, "imported state hashes the same");
    bytes[0] ^= 1;
    IS(c.import_state(bytes), ZXV_E_FORMAT);

    // moves keep the sim alive and leave the source empty
    Lockstep moved(std::move(c));
    CHECK(moved.valid() && !c.valid(), "move");
    CHECK(moved.tick() == T, "moved sim intact");
    IS(c.begin(), ZXV_E_STATE);

    // the clock: 1/60 s steps fed in microseconds
    OK(moved.clock_set_step(R(1, 60), 4));
    uint32_t due = 0;
    uint64_t dropped = 0;
    OK(moved.clock_feed_usec(33334, &due, &dropped));
    CHECK(due == 2 && dropped == 0, "33.334 ms = 2 ticks");
    Rational alpha;
    OK(moved.clock_alpha(&alpha));
    CHECK(alpha.compare(Rational()) > 0 && alpha.compare(R(1, 1)) < 0, "alpha in (0, 1)");
    OK(moved.clock_feed_usec(1000000, &due, &dropped));
    CHECK(due == 4 && dropped == 56, "catch-up capped at 4");
}

int main() {
    if (zxvgd::check_library() != ZXV_OK) {
        std::fprintf(stderr, "libzxv 1.3+ required: %s\n", zxvgd::last_error().c_str());
        return 1;
    }
    test_rational();
    test_hexagram();
    test_lockstep();
    std::printf("test_core: %d checks, %d failed\n", g_checks, g_fail);
    return g_fail ? 1 : 0;
}
