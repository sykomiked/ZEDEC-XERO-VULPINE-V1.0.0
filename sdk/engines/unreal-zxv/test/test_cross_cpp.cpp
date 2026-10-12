// Copyright (c) 2024-2026 Michael Laurence Curzi and 36N9 Genetics, LLC
// SPDX-License-Identifier: Apache-2.0
//
// test_cross_cpp.cpp — the Unreal plugin's C++ layer (zxvue, no exceptions)
// and the header-only C++ SDK (sdk/cpp/include/zxv.hpp, exceptions) must
// agree value for value: the same rationals, the same refusals on
// overflow, the same text, and the same hexagram rules for all 64 tags.
// This is the only target built with exceptions, because zxv.hpp throws.
#include <cstdint>
#include <cstdio>
#include <optional>
#include <string>

#include <zxv.hpp>

#include "ZxvCore/zxv_ue_core.hpp"

static int g_checks, g_fail;

#define CHECK(cond, what)                                                          \
    do {                                                                           \
        g_checks++;                                                                \
        if (!(cond)) {                                                             \
            g_fail++;                                                              \
            std::fprintf(stderr, "FAIL %s:%d: %s\n", __FILE__, __LINE__, what);    \
        }                                                                          \
    } while (0)

// A small deterministic generator (no host randomness, no floats).
static std::uint64_t g_state = 0x9E3779B97F4A7C15ull;
static std::uint64_t next_u64() {
    g_state ^= g_state << 13;
    g_state ^= g_state >> 7;
    g_state ^= g_state << 17;
    return g_state;
}

// Mostly small values, sometimes values near the int64 edge.
static std::int64_t pick_num() {
    std::uint64_t r = next_u64();
    switch (r % 4) {
    case 0: return static_cast<std::int64_t>(r % 41) - 20;
    case 1: return static_cast<std::int64_t>((r >> 8) % 2000001) - 1000000;
    case 2: return INT64_MAX - static_cast<std::int64_t>((r >> 8) % 1000);
    default: return -(INT64_MAX - static_cast<std::int64_t>((r >> 8) % 1000));
    }
}
static std::int64_t pick_den() {
    std::uint64_t r = next_u64();
    switch (r % 3) {
    case 0: return static_cast<std::int64_t>(r % 12) + 1;
    case 1: return static_cast<std::int64_t>((r >> 8) % 1000000) + 1;
    default: return INT64_MAX - static_cast<std::int64_t>((r >> 8) % 1000);
    }
}

static bool same(const zxvue::Rational &a, const zxv::Rational &b) {
    return a.num() == b.num() && a.den() == b.den();
}

// One binary operation: either both give the same value, or both refuse.
template <class UeOp, class CppOp>
static void agree(const char *what, UeOp ue_op, CppOp cpp_op) {
    zxvue::Rational ue;
    std::int32_t rc = ue_op(&ue);
    std::optional<zxv::Rational> cpp = cpp_op();
    if (rc == ZXV_OK) CHECK(cpp.has_value() && same(ue, *cpp), what);
    else CHECK(!cpp.has_value() && (rc == ZXV_E_RANGE || rc == ZXV_E_ARG), what);
}

static void test_rationals() {
    const int N = 400;
    int refused = 0;
    for (int i = 0; i < N; i++) {
        const std::int64_t an = pick_num(), ad = pick_den(), bn = pick_num(), bd = pick_den();
        zxvue::Rational a, b;
        if (zxvue::Rational::from_ints(an, ad, &a) != ZXV_OK ||
            zxvue::Rational::from_ints(bn, bd, &b) != ZXV_OK) {
            CHECK(false, "valid inputs construct");
            continue;
        }
        const zxv::Rational ca = zxv::Rational::of(an, ad), cb = zxv::Rational::of(bn, bd);
        CHECK(same(a, ca) && same(b, cb), "reduction agrees");
        agree("add", [&](zxvue::Rational *o) { return a.add(b, o); }, [&] { return ca.checked_add(cb); });
        agree("sub", [&](zxvue::Rational *o) { return a.sub(b, o); }, [&] { return ca.checked_sub(cb); });
        agree("mul", [&](zxvue::Rational *o) { return a.mul(b, o); }, [&] { return ca.checked_mul(cb); });
        agree("div", [&](zxvue::Rational *o) { return a.div(b, o); }, [&] { return ca.checked_div(cb); });
        zxvue::Rational s;
        if (a.add(b, &s) != ZXV_OK) refused++;
        const int c = a.compare(b);
        const auto cc = ca <=> cb;
        CHECK((c < 0) == (cc < 0) && (c == 0) == (cc == 0) && (c > 0) == (cc > 0), "compare agrees");
        CHECK(a.to_string() == ca.to_string(), "text agrees");
    }
    CHECK(refused > 0, "the edge values did make some sums overflow");
    // parse agrees, including refusals
    static const char *const texts[] = {"0.1", "-12.75", "3/4", "6/8", "42", "1e3", "nan", " 1",
                                        "9223372036854775807", "9223372036854775808", "0.001"};
    for (const char *t : texts) {
        zxvue::Rational ue;
        const std::int32_t rc = zxvue::Rational::parse(t, &ue);
        bool cpp_ok = true;
        zxv::Rational cpp;
        try {
            cpp = zxv::Rational::parse(t);
        } catch (const zxv::Exception &e) {
            cpp_ok = false;
            CHECK(e.status() == rc, "the same refusal status");
        }
        CHECK((rc == ZXV_OK) == cpp_ok, t);
        if (cpp_ok) CHECK(same(ue, cpp), t);
    }
    // q16 agrees
    for (int i = 0; i < 50; i++) {
        const std::int64_t n = static_cast<std::int64_t>(next_u64() % 200001) - 100000;
        const std::int64_t d = static_cast<std::int64_t>(next_u64() % 999) + 1;
        zxvue::Rational ue;
        zxvue::Rational::from_ints(n, d, &ue);
        std::int32_t q = 0;
        const std::int32_t rc = ue.to_q16(&q);
        bool cpp_ok = true;
        std::int32_t cq = 0;
        try {
            cq = zxv::Rational::of(n, d).to_q16();
        } catch (const zxv::Exception &) {
            cpp_ok = false;
        }
        CHECK((rc == ZXV_OK) == cpp_ok && (!cpp_ok || q == cq), "q16 agrees");
    }
}

static void test_hexagrams() {
    for (int t = 0; t < 64; t++) {
        zxvue::Hexagram ue;
        zxvue::Hexagram::from_tag(t, &ue);
        const zxv::Hexagram cpp = zxv::Hexagram::from_tag(static_cast<std::uint8_t>(t));
        CHECK(ue.base() == static_cast<int>(cpp.base()) && ue.polarity() == static_cast<int>(cpp.polarity()) &&
                  ue.phase() == static_cast<int>(cpp.phase()),
              "fields agree");
        CHECK(ue.action() == static_cast<int>(cpp.action()), "action agrees");
        CHECK(std::string(ue.name()) == std::string(cpp.name()), "name agrees");
        CHECK(ue.to_truth() == static_cast<int>(cpp.to_truth()), "truth agrees");
        CHECK(ue.advance().tag() == cpp.advance().tag(), "advance agrees");
        CHECK(ue.forbid().tag() == cpp.forbid().tag(), "forbid agrees");
        CHECK(ue.not_().tag() == (~cpp).tag(), "not agrees");
        bool ok = true;
        for (int u = 0; u < 64; u++) {
            zxvue::Hexagram uo;
            zxvue::Hexagram::from_tag(u, &uo);
            const zxv::Hexagram co = zxv::Hexagram::from_tag(static_cast<std::uint8_t>(u));
            ok = ok && ue.and_(uo).tag() == (cpp & co).tag() && ue.or_(uo).tag() == (cpp | co).tag();
        }
        CHECK(ok, "and/or agree with all 64 partners");
    }
}

int main() {
    if (zxvue::check_library() != ZXV_OK) {
        std::fprintf(stderr, "libzxv 1.3+ required: %s\n", zxvue::last_error().c_str());
        return 1;
    }
    try {
        zxv::ensure_compatible();
        test_rationals();
        test_hexagrams();
    } catch (const zxv::Exception &e) {
        std::fprintf(stderr, "unexpected zxv exception: %s\n", e.what());
        return 1;
    }
    std::printf("test_cross_cpp: %d checks, %d failed\n", g_checks, g_fail);
    return g_fail ? 1 : 0;
}
