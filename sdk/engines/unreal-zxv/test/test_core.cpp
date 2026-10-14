// Copyright (c) 2024-2026 Michael Laurence Curzi and 36N9 Genetics, LLC
// SPDX-License-Identifier: Apache-2.0
//
// test_core.cpp — tests the C++ layer under the Unreal plugin, without
// Unreal: exact rationals and their refusals, every hexagram tag, two (and
// three) lockstep peers agreeing hash for hash, overflow refused inside a
// tick, budgets, desync, state transfer, the clock and the pacer. Built
// with -fno-exceptions, like an Unreal module.
#include <cstdio>
#include <cstring>
#include <string>
#include <type_traits>
#include <utility>
#include <vector>

#include "ZxvCore/zxv_ue_core.hpp"

using zxvue::Hexagram;
using zxvue::Lockstep;
using zxvue::LockstepConfig;
using zxvue::Pacer;
using zxvue::Rational;

static int g_checks, g_fail;

#define CHECK(cond, what)                                                                   \
    do {                                                                                    \
        g_checks++;                                                                         \
        if (!(cond)) {                                                                      \
            g_fail++;                                                                       \
            std::fprintf(stderr, "FAIL %s:%d: %s [%s]\n", __FILE__, __LINE__, what,         \
                         zxvue::last_error().c_str());                                      \
        }                                                                                   \
    } while (0)
#define OK(expr) CHECK((expr) == ZXV_OK, #expr)
#define IS(expr, st) CHECK((expr) == (st), #expr " != " #st)

// A floating-point value never converts to a Rational (deleted constructor).
static_assert(!std::is_constructible<Rational, decltype(0.5)>::value, "double must not convert");
static_assert(!std::is_constructible<Rational, decltype(0.5f)>::value, "float must not convert");
static_assert(!std::is_convertible<decltype(0.5), Rational>::value, "no implicit conversion");
static_assert(!std::is_convertible<long long, Rational>::value, "integers go through from_ints");
static_assert(std::is_nothrow_move_constructible<Lockstep>::value, "Lockstep moves");
static_assert(std::is_nothrow_move_assignable<Lockstep>::value, "Lockstep move-assigns");
static_assert(!std::is_copy_constructible<Lockstep>::value, "Lockstep is move-only");
static_assert(!std::is_copy_assignable<Lockstep>::value, "Lockstep is move-only");
static_assert(sizeof(Hexagram) == 1, "a hexagram is one byte");

static Rational R(long long n, long long d) {
    Rational r;
    Rational::from_ints(n, d, &r);
    return r;
}

// ---------------------------------------------------------------------
// Rationals

struct ParseCase {
    const char *text;
    long long num, den;
};
static const ParseCase kParse[] = {
    {"0", 0, 1},          {"42", 42, 1},          {"-42", -42, 1},
    {"0.5", 1, 2},        {"-0.5", -1, 2},        {"12.75", 51, 4},
    {"-12.75", -51, 4},   {"0.1", 1, 10},         {"0.3", 3, 10},
    {"3/4", 3, 4},        {"-3/4", -3, 4},        {"6/8", 3, 4},
    {"10/5", 2, 1},       {"1/3", 1, 3},          {"0.125", 1, 8},
    {"100", 100, 1},      {"0.001", 1, 1000},     {"7/1", 7, 1},
    {"9223372036854775807", INT64_MAX, 1},        {"-9223372036854775807", -INT64_MAX, 1},
};

struct FormatCase {
    long long num, den;
    const char *text;
};
static const FormatCase kFormat[] = {
    {0, 1, "0"},     {1, 2, "0.5"},     {-1, 2, "-0.5"},  {51, 4, "12.75"},
    {1, 3, "1/3"},   {-1, 3, "-1/3"},   {2, 3, "2/3"},    {1, 8, "0.125"},
    {7, 1, "7"},     {-7, 1, "-7"},     {1, 7, "1/7"},    {3, 10, "0.3"},
    {1, 6, "1/6"},   {5, 16, "0.3125"}, {22, 7, "22/7"},  {1, 1000, "0.001"},
};

static const char *const kBadText[] = {
    "1e3", "nan", "NaN", "inf", " 1", "1 ", "", "abc", "1/0", "0x10", "1..2", "--1", "+-1", "1/", "/2", "1.2.3",
};

struct ArithCase {
    long long an, ad, bn, bd;
    long long sum_n, sum_d, diff_n, diff_d, prod_n, prod_d;
};
static const ArithCase kArith[] = {
    {1, 3, 1, 6, 1, 2, 1, 6, 1, 18},
    {1, 2, 1, 2, 1, 1, 0, 1, 1, 4},
    {-1, 2, 1, 3, -1, 6, -5, 6, -1, 6},
    {2, 3, 3, 4, 17, 12, -1, 12, 1, 2},
    {5, 1, -5, 1, 0, 1, 10, 1, -25, 1},
    {7, 10, 3, 10, 1, 1, 2, 5, 21, 100},
    {1, 7, 1, 11, 18, 77, 4, 77, 1, 77},
    {0, 1, 9, 4, 9, 4, -9, 4, 0, 1},
    {-3, 8, -5, 8, -1, 1, 1, 4, 15, 64},
    {1000000, 1, 1, 1000000, 1000000000001LL, 1000000, 999999999999LL, 1000000, 1, 1},
};

struct FloorCase {
    long long num, den, floor_, ceil_;
};
static const FloorCase kFloor[] = {
    {0, 1, 0, 0},    {7, 2, 3, 4},     {-7, 2, -4, -3},  {6, 3, 2, 2},
    {-6, 3, -2, -2}, {1, 3, 0, 1},     {-1, 3, -1, 0},   {99, 100, 0, 1},
    {-99, 100, -1, 0}, {INT64_MAX, 1, INT64_MAX, INT64_MAX}, {-INT64_MAX, 2, -4611686018427387904LL, -4611686018427387903LL},
};

static void test_rational_parse_format() {
    for (const ParseCase &c : kParse) {
        Rational r;
        OK(Rational::parse(c.text, &r));
        CHECK(r.num() == c.num && r.den() == c.den, c.text);
    }
    for (const FormatCase &c : kFormat) {
        Rational r;
        OK(Rational::from_ints(c.num, c.den, &r));
        CHECK(r.to_string() == c.text, c.text);
        Rational back;
        OK(Rational::parse(r.to_string(), &back));
        CHECK(back == r, "exact text round-trips");
        OK(Rational::parse(r.to_fraction_string(), &back));
        CHECK(back == r, "fraction text round-trips");
    }
    for (const char *bad : kBadText) {
        Rational r = R(5, 7);
        std::int32_t rc = Rational::parse(bad, &r);
        CHECK(rc == ZXV_E_FORMAT || rc == ZXV_E_ARG, bad);
        CHECK(r == R(5, 7), "a refused parse leaves the output alone");
    }
    Rational r;
    IS(Rational::parse("9223372036854775808", &r), ZXV_E_RANGE);
    IS(Rational::parse("-9223372036854775808", &r), ZXV_E_RANGE);
    IS(Rational::parse("1/9223372036854775808", &r), ZXV_E_RANGE);
    IS(Rational::parse("1", nullptr), ZXV_E_ARG);
    CHECK(!zxvue::last_error().empty(), "a refusal leaves detail text");
}

static void test_rational_arith() {
    Rational a, b, c;
    OK(Rational::parse("0.1", &a));
    OK(Rational::parse("0.2", &b));
    OK(a.add(b, &c));
    CHECK(c.to_string() == "0.3", "0.1 + 0.2 is exactly 0.3");
    for (const ArithCase &k : kArith) {
        Rational x = R(k.an, k.ad), y = R(k.bn, k.bd), o;
        OK(x.add(y, &o));
        CHECK(o == R(k.sum_n, k.sum_d), "sum");
        OK(x.sub(y, &o));
        CHECK(o == R(k.diff_n, k.diff_d), "difference");
        OK(x.mul(y, &o));
        CHECK(o == R(k.prod_n, k.prod_d), "product");
        OK(y.add(x, &c));
        OK(x.add(y, &o));
        CHECK(o == c, "addition commutes");
        if (!y.is_zero()) {
            OK(o.sub(y, &c));
            CHECK(c == x, "(x + y) - y == x");
            OK(x.mul(y, &o));
            OK(o.div(y, &c));
            CHECK(c == x, "(x * y) / y == x");
        }
        CHECK(o.den() > 0, "denominator stays positive");
    }
    OK(Rational::from_ints(2, -6, &a));
    CHECK(a.num() == -1 && a.den() == 3, "reduced, sign on top");
    OK(Rational::from_ints(-4, -8, &a));
    CHECK(a == R(1, 2), "two negatives");
    OK(Rational::from_ints(0, -5, &a));
    CHECK(a.num() == 0 && a.den() == 1, "zero is 0/1");
    IS(Rational::from_ints(1, 0, &a), ZXV_E_ARG);
    IS(Rational::from_ints(1, 1, nullptr), ZXV_E_ARG);
    CHECK(Rational().is_zero() && Rational().den() == 1, "default is 0/1");
    // 1/3 added three times is exactly one; 1/10 ten times too.
    Rational s;
    for (int i = 0; i < 3; i++) OK(s.add(R(1, 3), &s));
    CHECK(s == R(1, 1), "three thirds");
    s = Rational();
    for (int i = 0; i < 10; i++) OK(s.add(R(1, 10), &s));
    CHECK(s == R(1, 1) && s.is_integer(), "ten tenths");
    OK(R(1, 3).neg(&a));
    CHECK(a == R(-1, 3), "neg");
    OK(R(-5, 3).abs(&a));
    CHECK(a == R(5, 3), "abs of a negative");
    OK(R(5, 3).abs(&a));
    CHECK(a == R(5, 3), "abs of a positive");
    IS(R(1, 1).abs(nullptr), ZXV_E_ARG);
    CHECK(R(-2, 3).sign() == -1 && Rational().sign() == 0 && R(2, 3).sign() == 1, "sign");
    CHECK(R(1, 3).compare(R(1, 2)) == -1 && R(1, 2).compare(R(2, 4)) == 0 &&
              R(2, 3).compare(R(1, 2)) == 1,
          "compare is exact");
    CHECK(R(1, 3) < R(1, 2) && R(1, 2) > R(1, 3) && R(1, 2) <= R(2, 4) && R(2, 4) >= R(1, 2),
          "ordering operators");
    CHECK(R(1, 3).min(R(1, 2)) == R(1, 3) && R(1, 3).max(R(1, 2)) == R(1, 2), "min/max");
    // Neighbours that a float could not tell apart are still ordered.
    CHECK(R(INT64_MAX - 2, INT64_MAX - 1) < R(INT64_MAX - 1, INT64_MAX),
          "1 - 1/(2^63-2) < 1 - 1/(2^63-1)");
    CHECK(Rational::of_int(-12) == R(-12, 1) && Rational::of_int(0).is_zero(), "of_int");
}

// Overflow is refused (ZXV_E_RANGE), never wrapped or rounded, and the
// output is left alone.
static void test_rational_overflow() {
    const Rational keep = R(5, 7);
    const Rational big = R(INT64_MAX, 1), small = R(-INT64_MAX, 1), tiny = R(1, INT64_MAX);
    Rational a = keep;
    IS(big.add(R(1, 1), &a), ZXV_E_RANGE);
    CHECK(a == keep, "add: output untouched");
    IS(small.sub(R(1, 1), &a), ZXV_E_RANGE);
    CHECK(a == keep, "sub: output untouched");
    IS(big.mul(R(2, 1), &a), ZXV_E_RANGE);
    CHECK(a == keep, "mul: output untouched");
    IS(big.div(R(1, 2), &a), ZXV_E_RANGE);
    CHECK(a == keep, "div: output untouched");
    IS(tiny.mul(R(1, 2), &a), ZXV_E_RANGE);
    IS(tiny.add(R(1, INT64_MAX - 1), &a), ZXV_E_RANGE);
    IS(R(1, 1).div(Rational(), &a), ZXV_E_ARG);
    CHECK(a == keep, "division by zero: output untouched");
    IS(Rational::from_ints(INT64_MIN, 1, &a), ZXV_E_RANGE);
    IS(Rational::from_ints(1, INT64_MIN, &a), ZXV_E_RANGE);
    CHECK(a == keep, "INT64_MIN refused");
    OK(big.neg(&a));
    CHECK(a == small, "-(2^63-1) fits");
    OK(big.sub(big, &a));
    CHECK(a.is_zero(), "max - max");
    OK(big.add(small, &a));
    CHECK(a.is_zero(), "max + -max");
    // Growing denominators: distinct primes overflow eventually, with a refusal.
    static const long long primes[] = {2, 3, 5, 7, 11, 13, 17, 19, 23, 29, 31, 37, 41, 43, 47, 53, 59, 61, 67, 71};
    Rational acc;
    std::int32_t rc = ZXV_OK;
    int added = 0;
    for (long long p : primes) {
        Rational next;
        rc = acc.add(R(1, p), &next);
        if (rc != ZXV_OK) break;
        acc = next;
        added++;
    }
    IS(rc, ZXV_E_RANGE);
    CHECK(added == 15, "1/2 + ... + 1/47 fits; adding 1/53 is refused");
    CHECK(acc.den() > 0 && acc.num() > 0, "the last good value is kept");

    for (const FloorCase &c : kFloor) {
        Rational r = R(c.num, c.den);
        CHECK(r.floor() == c.floor_, "floor");
        CHECK(r.ceil() == c.ceil_, "ceil");
    }
    std::int64_t v = 0;
    OK(R(1234, 1000).to_scaled_floor(100, &v));
    CHECK(v == 123, "1.234 m = 123 cm (floor)");
    OK(R(-1234, 1000).to_scaled_floor(100, &v));
    CHECK(v == -124, "-1.234 m = -124 cm (floor)");
    OK(R(1, 3).to_scaled_floor(3, &v));
    CHECK(v == 1, "1/3 * 3");
    IS(R(1, 1).to_scaled_floor(0, &v), ZXV_E_ARG);
    IS(R(1, 1).to_scaled_floor(-1, &v), ZXV_E_ARG);
    IS(R(1, 1).to_scaled_floor(1, nullptr), ZXV_E_ARG);
    v = 77;
    IS(big.to_scaled_floor(2, &v), ZXV_E_RANGE);
    CHECK(v == 77, "a refused scale leaves the output alone");
    std::int32_t q = 0;
    OK(R(1, 2).to_q16(&q));
    CHECK(q == 32768, "1/2 is Q16 32768");
    OK(R(-3, 4).to_q16(&q));
    CHECK(q == -49152, "-3/4 is Q16 -49152");
    IS(R(40000, 1).to_q16(&q), ZXV_E_RANGE);
    IS(R(1, 1).to_q16(nullptr), ZXV_E_ARG);
}

// ---------------------------------------------------------------------
// Hexagrams: every tag, every rule from libzxv

static void test_hexagram() {
    Hexagram h;
    for (int t = 0; t < 64; t++) {
        OK(Hexagram::from_tag(t, &h));
        CHECK(h.tag() == t, "tag kept");
        Hexagram g;
        OK(Hexagram::from_fields(h.base(), h.polarity(), h.phase(), &g));
        CHECK(g == h, "fields round trip");
        std::uint8_t b = 9, p = 9, ph = 9;
        zxv_hex_unpack(static_cast<std::uint8_t>(t), &b, &p, &ph);
        CHECK(h.base() == b && h.polarity() == p && h.phase() == ph, "fields from libzxv");
        CHECK(std::strcmp(h.name(), zxv_hex_name(static_cast<std::uint8_t>(t))) == 0, "name from libzxv");
        std::int32_t act = -1;
        zxv_hex_action(static_cast<std::uint8_t>(t), &act);
        CHECK(h.action() == act, "action from libzxv");
        CHECK(h.is_forbidden() == (h.action() == ZXV_HEX_ACT_ROLLBACK), "forbidden <-> rollback");
        Hexagram n = h.advance();
        CHECK(n.base() == h.base() && n.polarity() == h.polarity() && n.phase() != h.phase(),
              "advance moves the phase only");
        CHECK(h.advance().advance().advance().advance() == h, "four steps make a cycle");
        CHECK(h.not_().not_() == h, "not is an involution");
        CHECK(h.forbid().is_forbidden() && h.forbid().forbid() == h.forbid(), "forbid is idempotent");
        Hexagram same;
        OK(h.step(h.phase(), &same));
        CHECK(same == h, "a step to the same phase");
        int lp = h.to_truth();
        Hexagram back;
        OK(Hexagram::from_truth(lp, h.polarity(), h.phase(), &back));
        CHECK(back == h, "truth round trip");
        // and/or against every other tag, compared with libzxv directly
        bool and_ok = true, or_ok = true;
        for (int u = 0; u < 64; u++) {
            Hexagram o;
            Hexagram::from_tag(u, &o);
            std::uint8_t wa = 0, wo = 0;
            zxv_hex_and(static_cast<std::uint8_t>(t), static_cast<std::uint8_t>(u), &wa);
            zxv_hex_or(static_cast<std::uint8_t>(t), static_cast<std::uint8_t>(u), &wo);
            and_ok = and_ok && h.and_(o).tag() == wa;
            or_ok = or_ok && h.or_(o).tag() == wo;
        }
        CHECK(and_ok, "and over all 64 partners matches libzxv");
        CHECK(or_ok, "or over all 64 partners matches libzxv");
    }
    IS(Hexagram::from_tag(64, &h), ZXV_E_ARG);
    IS(Hexagram::from_tag(-1, &h), ZXV_E_ARG);
    IS(Hexagram::from_tag(255, &h), ZXV_E_ARG);
    IS(Hexagram::from_tag(0, nullptr), ZXV_E_ARG);
    IS(Hexagram::from_fields(4, 0, 0, &h), ZXV_E_ARG);
    IS(Hexagram::from_fields(0, 4, 0, &h), ZXV_E_ARG);
    IS(Hexagram::from_fields(0, 0, 4, &h), ZXV_E_ARG);
    IS(Hexagram::from_fields(-1, 0, 0, &h), ZXV_E_ARG);
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
    IS(Hexagram::from_truth(-1, 0, 0, &h), ZXV_E_ARG);
    // one step at a time; forbidden is absorbing
    OK(Hexagram::from_fields(ZXV_HEX_BASE_TRUE, ZXV_HEX_POL_NEUTRAL, ZXV_HEX_PHASE_POTENTIAL, &h));
    Hexagram g;
    IS(h.step(ZXV_HEX_PHASE_MANIFEST, &g), ZXV_E_STATE);
    IS(h.step(4, &g), ZXV_E_ARG);
    IS(h.step(0, nullptr), ZXV_E_ARG);
    OK(h.step(ZXV_HEX_PHASE_EMERGING, &g));
    CHECK(g.phase() == ZXV_HEX_PHASE_EMERGING, "potential -> emerging");
    Hexagram f = g.forbid();
    CHECK(f.is_forbidden() && f.action() == ZXV_HEX_ACT_ROLLBACK, "forbid");
    IS(f.with_state(ZXV_HEX_BASE_TRUE, ZXV_HEX_POL_NEUTRAL, &g), ZXV_E_STATE);
    IS(h.with_state(4, 0, &g), ZXV_E_ARG);
    IS(h.with_state(0, 0, nullptr), ZXV_E_ARG);
    OK(h.with_state(ZXV_HEX_BASE_BOTH, ZXV_HEX_POL_ENTROPIC, &g));
    CHECK(g.base() == ZXV_HEX_BASE_BOTH && g.polarity() == ZXV_HEX_POL_ENTROPIC &&
              g.phase() == h.phase(),
          "with_state keeps the phase");
    CHECK(f.and_(h).is_forbidden() && f.or_(h).is_forbidden() && f.not_().is_forbidden(),
          "forbidden absorbs and/or/not");
    CHECK(Hexagram() == Hexagram() && Hexagram().tag() == 0, "default is tag 0");
}

// ---------------------------------------------------------------------
// Lockstep

// The game: rat slot p = player p's position (moves 1/3 per tick), rat slot
// P = a tide (+ (tick mod 7)/5), hex slot p = the player's claim.
static std::int32_t step_game(Lockstep &ls, std::uint64_t tick, std::uint32_t players) {
    std::int32_t rc;
    for (std::uint32_t p = 0; p < players; p++) {
        std::vector<std::uint8_t> in;
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

static std::vector<std::uint8_t> input_for(std::uint32_t p, std::uint64_t t) {
    std::uint64_t x = (t * 2654435761u) ^ (static_cast<std::uint64_t>(p) * 40503u);
    return {static_cast<std::uint8_t>(x % 3u == 0 ? 0 : (x % 3u == 1 ? 1 : 0xFF)),
            static_cast<std::uint8_t>((x >> 3) % 5u == 0 ? 1u : (p == 1 && t == 9 ? 2u : 0u))};
}

static LockstepConfig game_config(std::uint32_t players, bool predict) {
    LockstepConfig c;
    c.players = players;
    c.rat_slots = players + 1;
    c.hex_slots = players;
    c.input_bytes = 2;
    c.history = 16;
    c.predict = predict;
    return c;
}

static std::int32_t make_game(Lockstep &ls, std::uint32_t players, bool predict) {
    std::int32_t rc = ls.create(game_config(players, predict));
    if (rc != ZXV_OK) return rc;
    for (std::uint32_t p = 0; p < players; p++) {
        Hexagram h;
        Hexagram::from_fields(ZXV_HEX_BASE_NEITHER, ZXV_HEX_POL_NEUTRAL, ZXV_HEX_PHASE_POTENTIAL, &h);
        if ((rc = ls.set_rational(p, R(p, 2))) != ZXV_OK) return rc;
        if ((rc = ls.set_hexagram(p, h)) != ZXV_OK) return rc;
    }
    return ls.start();
}

static void test_unready() {
    Lockstep x;
    CHECK(!x.valid() && x.raw() == nullptr, "empty");
    LockstepConfig c;
    IS(x.config(&c), ZXV_E_STATE);
    IS(x.start(), ZXV_E_STATE);
    IS(x.begin(), ZXV_E_STATE);
    IS(x.commit(), ZXV_E_STATE);
    IS(x.abort(), ZXV_E_STATE);
    IS(x.set_tick_budget(R(1, 1)), ZXV_E_STATE);
    IS(x.clock_set_step(R(1, 60), 4), ZXV_E_STATE);
    std::uint32_t due = 0;
    IS(x.clock_feed_usec(1, &due, nullptr), ZXV_E_STATE);
    Rational r;
    IS(x.clock_alpha(&r), ZXV_E_STATE);
    IS(x.submit_input(0, 1, {0}), ZXV_E_STATE);
    std::vector<std::uint8_t> v;
    IS(x.input(0, &v, nullptr), ZXV_E_STATE);
    IS(x.get_rational(0, &r), ZXV_E_STATE);
    IS(x.set_rational(0, r), ZXV_E_STATE);
    IS(x.add_rational(0, r), ZXV_E_STATE);
    IS(x.mul_rational(0, r), ZXV_E_STATE);
    Hexagram h;
    IS(x.get_hexagram(0, &h), ZXV_E_STATE);
    IS(x.set_hexagram(0, h), ZXV_E_STATE);
    IS(x.charge(R(1, 1)), ZXV_E_STATE);
    IS(x.hash(0, &v, nullptr), ZXV_E_STATE);
    int lp = 0;
    IS(x.compare(0, std::vector<std::uint8_t>(32, 0), &lp), ZXV_E_STATE);
    IS(x.rollback(0), ZXV_E_STATE);
    IS(x.export_state(&v), ZXV_E_STATE);
    IS(x.import_state(v), ZXV_E_STATE);
    auto step = [](Lockstep &, std::uint64_t) -> std::int32_t { return ZXV_OK; };
    IS(x.advance(step), ZXV_E_STATE);
    IS(x.rollback_replay(step), ZXV_E_STATE);
    CHECK(!x.ready() && x.tick() == 0 && x.confirmed_tick() == 0, "inert queries");
    std::uint64_t t = 0;
    CHECK(!x.desync(&t) && !x.rollback_pending(&t), "no desync, no rollback");

    // the shape limits come from emu-state
    LockstepConfig bad = game_config(0, false);
    IS(x.create(bad), ZXV_E_ARG);
    bad = game_config(65, false);
    IS(x.create(bad), ZXV_E_ARG);
    bad = game_config(2, false);
    bad.input_bytes = 0;
    IS(x.create(bad), ZXV_E_ARG);
    bad.input_bytes = 257;
    IS(x.create(bad), ZXV_E_ARG);
    bad = game_config(2, false);
    bad.history = 1;
    IS(x.create(bad), ZXV_E_ARG);
    bad = game_config(2, false);
    bad.rat_slots = 0;
    bad.hex_slots = 0;
    IS(x.create(bad), ZXV_E_ARG);
    CHECK(!x.valid(), "a refused create leaves it empty");

    OK(x.create(game_config(3, true)));
    OK(x.config(&c));
    CHECK(c.players == 3 && c.rat_slots == 4 && c.hex_slots == 3 && c.input_bytes == 2 &&
              c.history == 16 && c.predict,
          "config round trip");
    IS(x.config(nullptr), ZXV_E_ARG);
    // setup: slot ranges and setters before start
    IS(x.set_rational(4, R(1, 1)), ZXV_E_ARG);
    IS(x.set_hexagram(3, Hexagram()), ZXV_E_ARG);
    IS(x.get_rational(0, nullptr), ZXV_E_ARG);
    IS(x.get_hexagram(0, nullptr), ZXV_E_ARG);
    IS(x.begin(), ZXV_E_STATE); // not started
    OK(x.start());
    IS(x.start(), ZXV_E_STATE);
    IS(x.set_rational(0, R(1, 1)), ZXV_E_STATE); // setters only inside a tick now
    IS(x.submit_input(3, 1, {0, 0}), ZXV_E_ARG);
    IS(x.submit_input(0, 1, {0}), ZXV_E_ARG);
    IS(x.submit_input(0, 0, {0, 0}), ZXV_E_ARG);
    IS(x.submit_input(0, 100, {0, 0}), ZXV_E_RANGE);
    OK(x.submit_input(0, 1, {1, 0}));
    OK(x.submit_input(0, 1, {1, 0}));
    IS(x.submit_input(0, 1, {2, 0}), ZXV_E_DUPLICATE);
    IS(x.input(0, nullptr, nullptr), ZXV_E_ARG);
    IS(x.input(0, &v, nullptr), ZXV_E_STATE); // outside a tick
}

// Two peers: A predicts the late inputs of players 1..P-1 and rolls back; B
// runs strict lockstep. A third peer C receives every input in reverse
// order of ticks inside a window. All end with the same hash at every tick.
static void test_two_peers() {
    const std::uint32_t P = 3;
    const std::uint64_t T = 60, LAG = 4;
    Lockstep a, b, c;
    OK(make_game(a, P, true));
    OK(make_game(b, P, false));
    OK(make_game(c, P, false));
    OK(a.set_tick_budget(R(P, 1))); // exactly one unit per player per tick
    auto step = [&](Lockstep &ls, std::uint64_t t) { return step_game(ls, t, P); };
    CHECK(!b.ready(), "strict peer waits for inputs");
    IS(b.advance(step), ZXV_E_STATE);
    std::vector<std::uint8_t> h0a, h0b;
    OK(a.hash(0, &h0a, nullptr));
    OK(b.hash(0, &h0b, nullptr));
    CHECK(h0a == h0b && h0a.size() == 32, "same tick-0 hash");

    unsigned rollbacks = 0, predicted_seen = 0;
    auto step_count = [&](Lockstep &ls, std::uint64_t t) {
        for (std::uint32_t p = 0; p < P; p++) {
            std::vector<std::uint8_t> in;
            bool pred = false;
            if (ls.input(p, &in, &pred) == ZXV_OK && pred) predicted_seen++;
        }
        return step_game(ls, t, P);
    };
    for (std::uint64_t t = 1; t <= T + LAG; t++) {
        if (t <= T) {
            for (std::uint32_t p = 0; p < P; p++) OK(b.submit_input(p, t, input_for(p, t)));
            OK(a.submit_input(0, t, input_for(0, t))); // a's local player
        }
        if (t > LAG)
            for (std::uint32_t p = 1; p < P; p++) OK(a.submit_input(p, t - LAG, input_for(p, t - LAG)));
        std::uint64_t pt = 0;
        if (a.rollback_pending(&pt)) {
            rollbacks++;
            CHECK(pt >= 1 && pt <= a.tick(), "pending tick within the simulated range");
        }
        OK(a.rollback_replay(step));
        CHECK(!a.rollback_pending(nullptr), "replay clears the pending rollback");
        if (t <= T) {
            CHECK(a.ready() && b.ready(), "both ready");
            OK(a.advance(step_count));
            OK(b.advance(step));
        }
    }
    // c: inputs for each block of 5 ticks arrive newest first, then it runs.
    for (std::uint64_t base = 1; base <= T; base += 5) {
        for (std::uint64_t t = base + 4; t + 1 > base; t--)
            for (std::uint32_t p = P; p-- > 0;) OK(c.submit_input(p, t, input_for(p, t)));
        for (int k = 0; k < 5; k++) OK(c.advance(step));
    }
    CHECK(predicted_seen > 0, "A did run ticks on predicted inputs");
    CHECK(rollbacks > 0, "the lagging inputs caused rollbacks");
    CHECK(a.tick() == T && b.tick() == T && c.tick() == T, "all at T");
    CHECK(a.confirmed_tick() == T, "a is fully confirmed");
    std::vector<std::uint8_t> ha, hb, hc;
    bool fin = false;
    unsigned agree = 0;
    for (std::uint64_t t = T - 15; t <= T; t++) {
        OK(a.hash(t, &ha, &fin));
        OK(b.hash(t, &hb, nullptr));
        OK(c.hash(t, &hc, nullptr));
        CHECK(ha == hb && hb == hc && fin, "same final hash on every peer");
        int lp = -1;
        OK(a.compare(t, hb, &lp));
        CHECK(lp == ZXV_LP_TRUE, "compare TRUE");
        OK(b.compare(t, hc, &lp));
        CHECK(lp == ZXV_LP_TRUE, "compare TRUE (b, c)");
        if (ha == hb) agree++;
    }
    CHECK(agree == 16, "16 kept ticks agree");
    IS(a.hash(1, &ha, nullptr), ZXV_E_NOT_FOUND); // outside the 16-tick ring
    IS(a.hash(T + 1, &ha, nullptr), ZXV_E_NOT_FOUND);
    IS(a.hash(T, nullptr, nullptr), ZXV_E_ARG);
    std::uint64_t dt = 0;
    CHECK(!a.desync(&dt), "no desync");
    for (std::uint32_t s = 0; s <= P; s++) {
        Rational ra, rb;
        OK(a.get_rational(s, &ra));
        OK(b.get_rational(s, &rb));
        CHECK(ra == rb, "every rational slot equal");
    }
    for (std::uint32_t s = 0; s < P; s++) {
        Hexagram xa, xb;
        OK(a.get_hexagram(s, &xa));
        OK(b.get_hexagram(s, &xb));
        CHECK(xa == xb, "every hexagram slot equal");
    }
    Hexagram claim1;
    OK(b.get_hexagram(1, &claim1));
    CHECK(claim1.is_forbidden(), "player 1 hit the wall at tick 9 and it stuck");

    int lp = -1;
    IS(a.compare(T, std::vector<std::uint8_t>(31, 0), &lp), ZXV_E_ARG);
    IS(a.compare(T, hb, nullptr), ZXV_E_ARG);
    OK(a.compare(1, hb, &lp)); // not kept: no verdict
    CHECK(lp == ZXV_LP_NEITHER, "a tick not kept gives NEITHER");
    OK(a.compare(T, std::vector<std::uint8_t>(32, 0), &lp));
    CHECK(lp == ZXV_LP_FALSE && a.desync(&dt) && dt == T, "a wrong hash is a desync");

    Rational pos, tide;
    OK(a.get_rational(0, &pos));
    OK(b.get_rational(P, &tide));
    // sum over t = 1..60 of (t mod 7)/5: 8 full weeks (21 each) + 1+2+3+4 = 178 -> 178/5
    CHECK(tide == R(178, 5), "tide is exact");
    CHECK(pos.den() == 1 || pos.den() == 3 || pos.den() == 2 || pos.den() == 6, "positions are sixths");

    // budget: one more charge than the limit refuses the tick
    OK(a.submit_input(0, T + 1, input_for(0, T + 1)));
    auto greedy = [&](Lockstep &ls, std::uint64_t t) {
        std::int32_t rc = step_game(ls, t, P);
        return rc != ZXV_OK ? rc : ls.charge(R(1, 1));
    };
    IS(a.advance(greedy), ZXV_E_BUDGET);
    CHECK(a.tick() == T, "refused tick did not advance");
    IS(a.set_tick_budget(R(-1, 1)), ZXV_E_RMAG_INVALID);
    OK(a.advance(step));
    CHECK(a.tick() == T + 1, "the honest step commits");

    // a failing step aborts and keeps its own detail text
    auto bad = [](Lockstep &, std::uint64_t) -> std::int32_t { return ZXV_E_INVARIANT; };
    for (std::uint32_t p = 0; p < P; p++) OK(b.submit_input(p, T + 1, input_for(p, T + 1)));
    IS(b.advance(bad), ZXV_E_INVARIANT);
    CHECK(zxvue::last_error().find("ZXV_E_INVARIANT") != std::string::npos, "step status named");
    CHECK(b.tick() == T, "aborted");
    IS(b.advance(Lockstep::Step()), ZXV_E_ARG);
    IS(b.rollback_replay(Lockstep::Step()), ZXV_E_ARG);

    // driving a tick by hand
    OK(b.begin());
    IS(b.begin(), ZXV_E_STATE);
    OK(b.add_rational(P, R(1, 1)));
    OK(b.abort());
    Rational after;
    OK(b.get_rational(P, &after));
    CHECK(after == tide, "abort restores the state");
    OK(b.begin());
    OK(step_game(b, T + 1, P));
    OK(b.commit());
    CHECK(b.tick() == T + 1, "manual commit");
    IS(b.commit(), ZXV_E_STATE);

    // rollback by hand: back to T, then forward again gives the same hash
    std::vector<std::uint8_t> before;
    OK(b.hash(T + 1, &before, nullptr));
    OK(b.rollback(T));
    CHECK(b.tick() == T, "rolled back");
    IS(b.rollback(T + 5), ZXV_E_NOT_FOUND);
    OK(b.advance(step));
    OK(b.hash(T + 1, &hb, nullptr));
    CHECK(hb == before, "re-simulating gives the same hash");

    // state transfer: a late joiner
    std::vector<std::uint8_t> bytes;
    OK(b.export_state(&bytes));
    CHECK(bytes.size() == 16 + 8 + 4 + 4 + (P + 1) * 16 + P, "canonical encoding size");
    CHECK(std::memcmp(bytes.data(), ZXV_EMU_DOMAIN, ZXV_EMU_DOMAIN_BYTES) == 0, "domain prefix");
    IS(b.export_state(nullptr), ZXV_E_ARG);
    Lockstep j;
    OK(j.create(game_config(P, false)));
    OK(j.import_state(bytes));
    CHECK(j.tick() == T + 1, "imported tick");
    OK(j.hash(T + 1, &ha, &fin));
    CHECK(ha == hb && fin, "imported state hashes the same");
    for (std::uint32_t p = 0; p < P; p++) {
        OK(j.submit_input(p, T + 2, input_for(p, T + 2)));
        OK(b.submit_input(p, T + 2, input_for(p, T + 2)));
    }
    OK(j.advance(step));
    OK(b.advance(step));
    OK(j.hash(T + 2, &ha, nullptr));
    OK(b.hash(T + 2, &hb, nullptr));
    CHECK(ha == hb, "the late joiner stays in sync");
    std::vector<std::uint8_t> broken = bytes;
    broken[0] ^= 1;
    IS(j.import_state(broken), ZXV_E_FORMAT);
    broken = bytes;
    broken.pop_back();
    IS(j.import_state(broken), ZXV_E_FORMAT);
    Lockstep other;
    OK(other.create(game_config(P + 1, false)));
    IS(other.import_state(bytes), ZXV_E_FORMAT);

    // moves keep the sim alive and leave the source empty
    Lockstep moved(std::move(j));
    CHECK(moved.valid() && !j.valid(), "move");
    CHECK(moved.tick() == T + 2, "moved sim intact");
    IS(j.begin(), ZXV_E_STATE);
    Lockstep assigned;
    OK(assigned.create(game_config(1, false)));
    assigned = std::move(moved);
    CHECK(assigned.valid() && !moved.valid() && assigned.tick() == T + 2, "move-assign");
    // the moved budget context still works
    Lockstep budgeted;
    OK(make_game(budgeted, 1, false));
    OK(budgeted.set_tick_budget(R(1, 1)));
    Lockstep budget_moved(std::move(budgeted));
    OK(budget_moved.submit_input(0, 1, {1, 0}));
    auto step1 = [](Lockstep &ls, std::uint64_t t) { return step_game(ls, t, 1); };
    OK(budget_moved.advance(step1));
    OK(budget_moved.submit_input(0, 2, {1, 0}));
    auto greedy1 = [](Lockstep &ls, std::uint64_t t) {
        std::int32_t rc = step_game(ls, t, 1);
        return rc != ZXV_OK ? rc : ls.charge(R(1, 2));
    };
    IS(budget_moved.advance(greedy1), ZXV_E_BUDGET);
}

// The same game run twice from scratch gives byte-identical hashes for
// every tick; a different input stream does not.
static void test_replay_determinism() {
    const std::uint32_t P = 2;
    auto step = [&](Lockstep &ls, std::uint64_t t) { return step_game(ls, t, P); };
    std::vector<std::vector<std::uint8_t>> first;
    for (int run = 0; run < 2; run++) {
        Lockstep ls;
        OK(make_game(ls, P, false));
        for (std::uint64_t t = 1; t <= 12; t++) {
            for (std::uint32_t p = 0; p < P; p++) OK(ls.submit_input(p, t, input_for(p, t)));
            OK(ls.advance(step));
            std::vector<std::uint8_t> h;
            OK(ls.hash(t, &h, nullptr));
            if (run == 0) first.push_back(h);
            else CHECK(first[t - 1] == h, "run 2 matches run 1 at this tick");
        }
    }
    Lockstep ls;
    OK(make_game(ls, P, false));
    for (std::uint32_t p = 0; p < P; p++) {
        std::vector<std::uint8_t> in = input_for(p, 1);
        in[0] = static_cast<std::uint8_t>(in[0] == 1 ? 0 : 1);
        OK(ls.submit_input(p, 1, in));
    }
    OK(ls.advance(step));
    std::vector<std::uint8_t> h;
    OK(ls.hash(1, &h, nullptr));
    CHECK(h != first[0], "different inputs give a different hash");
}

// Overflow inside a tick: the operation is refused, the step returns the
// status, the tick aborts and the state and hash are unchanged.
static void test_overflow_in_sim() {
    Lockstep ls;
    LockstepConfig c;
    c.players = 1;
    c.rat_slots = 2;
    c.hex_slots = 0;
    c.input_bytes = 1;
    c.history = 4;
    OK(ls.create(c));
    OK(ls.set_rational(0, R(INT64_MAX - 1, 1)));
    OK(ls.set_rational(1, R(1, 3)));
    OK(ls.start());
    std::vector<std::uint8_t> h0;
    OK(ls.hash(0, &h0, nullptr));
    OK(ls.submit_input(0, 1, {1}));
    auto push = [](Lockstep &s, std::uint64_t) -> std::int32_t {
        std::int32_t rc = s.add_rational(1, R(1, 3)); // fine
        if (rc != ZXV_OK) return rc;
        return s.add_rational(0, R(2, 1)); // past INT64_MAX: refused
    };
    IS(ls.advance(push), ZXV_E_RANGE);
    CHECK(ls.tick() == 0, "the tick did not advance");
    Rational v;
    OK(ls.get_rational(0, &v));
    CHECK(v == R(INT64_MAX - 1, 1), "slot 0 unchanged");
    OK(ls.get_rational(1, &v));
    CHECK(v == R(1, 3), "slot 1 rolled back with the tick");
    std::vector<std::uint8_t> h;
    OK(ls.hash(0, &h, nullptr));
    CHECK(h == h0, "hash unchanged");
    auto mul = [](Lockstep &s, std::uint64_t) { return s.mul_rational(0, R(3, 1)); };
    IS(ls.advance(mul), ZXV_E_RANGE);
    auto tiny = [](Lockstep &s, std::uint64_t) { return s.mul_rational(1, R(1, INT64_MAX)); };
    IS(ls.advance(tiny), ZXV_E_RANGE);
    auto ok = [](Lockstep &s, std::uint64_t) { return s.add_rational(0, R(1, 1)); };
    OK(ls.advance(ok));
    OK(ls.get_rational(0, &v));
    CHECK(v == R(INT64_MAX, 1), "exactly INT64_MAX is fine");
    OK(ls.submit_input(0, 2, {1}));
    IS(ls.advance(ok), ZXV_E_RANGE);
    CHECK(ls.tick() == 1, "one past is not");
}

// The clock is fed integer microseconds; the pacer turns absolute readings
// into elapsed time and refuses time running backwards.
static void test_clock_and_pacer() {
    Lockstep ls;
    OK(make_game(ls, 1, false));
    OK(ls.clock_set_step(R(1, 60), 4));
    IS(ls.clock_set_step(Rational(), 4), ZXV_E_ARG);
    IS(ls.clock_set_step(R(1, 60), 0), ZXV_E_ARG);
    std::uint32_t due = 0;
    std::uint64_t dropped = 0;
    OK(ls.clock_feed_usec(33334, &due, &dropped));
    CHECK(due == 2 && dropped == 0, "33.334 ms = 2 ticks");
    Rational alpha;
    OK(ls.clock_alpha(&alpha));
    CHECK(alpha > Rational() && alpha < R(1, 1), "alpha in (0, 1)");
    CHECK(alpha == R(1, 25000), "alpha is exact: 0.000004 s of 1/60 s");
    IS(ls.clock_alpha(nullptr), ZXV_E_ARG);
    OK(ls.clock_feed_usec(1000000, &due, &dropped));
    CHECK(due == 4 && dropped == 56, "catch-up capped at 4");
    IS(ls.clock_feed_usec(-1, &due, &dropped), ZXV_E_ARG);

    // 60 Hz at 1 ms granularity: 16667 us per frame drifts, the clock does not.
    Lockstep p;
    OK(make_game(p, 1, false));
    OK(p.clock_set_step(R(1, 60), 8));
    Pacer pacer;
    CHECK(!pacer.started(), "pacer starts empty");
    OK(pacer.pump(p, 5000000, &due, &dropped));
    CHECK(due == 0 && pacer.started(), "the first reading sets the origin");
    std::uint64_t total = 0;
    std::int64_t now = 5000000;
    for (int frame = 0; frame < 600; frame++) {
        now += (frame % 3 == 0) ? 16666 : 16667; // a jittery 60 Hz host
        OK(pacer.pump(p, now, &due, &dropped));
        total += due;
    }
    // 600 frames = 200 * (16666 + 2 * 16667) = 10000000 us = exactly 10 s
    CHECK(total == 600, "exactly 600 ticks in 10 s, no float drift");
    IS(pacer.pump(p, now - 1, &due, &dropped), ZXV_E_RANGE);
    OK(pacer.pump(p, now, &due, &dropped));
    CHECK(due == 0, "no time, no ticks");
    IS(pacer.pump(p, now, nullptr, nullptr), ZXV_E_ARG);
    Lockstep empty;
    IS(pacer.pump(empty, now, &due, nullptr), ZXV_E_STATE);
    pacer.reset();
    CHECK(!pacer.started(), "reset");
    OK(pacer.pump(p, INT64_MIN + 10, &due, nullptr));
    IS(pacer.pump(p, INT64_MAX, &due, nullptr), ZXV_E_RANGE);
    std::int64_t m1 = zxvue::monotonic_usec(), m2 = zxvue::monotonic_usec();
    CHECK(m2 >= m1, "the monotonic clock does not go backwards");
}

int main() {
    if (zxvue::check_library() != ZXV_OK) {
        std::fprintf(stderr, "libzxv 1.3+ required: %s\n", zxvue::last_error().c_str());
        return 1;
    }
    CHECK(std::strcmp(zxvue::status_name(ZXV_E_RANGE), "ZXV_E_RANGE") == 0, "status names");
    CHECK(std::strcmp(zxvue::status_name(ZXV_OK), "ZXV_OK") == 0, "ZXV_OK name");
    CHECK(std::strcmp(zxvue::status_name(-12345), "ZXV_E_UNKNOWN") == 0, "unknown status");
    test_rational_parse_format();
    test_rational_arith();
    test_rational_overflow();
    test_hexagram();
    test_unready();
    test_two_peers();
    test_replay_determinism();
    test_overflow_in_sim();
    test_clock_and_pacer();
    std::printf("test_core: %d checks, %d failed\n", g_checks, g_fail);
    return g_fail ? 1 : 0;
}
