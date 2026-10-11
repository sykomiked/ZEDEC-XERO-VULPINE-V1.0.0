// Copyright (c) 2024-2026 Michael Laurence Curzi and 36N9 Genetics, LLC
// SPDX-License-Identifier: Apache-2.0
//
// test_zxv.cpp — tests of zxv.hpp against the real libzxv and the shared
// answer key sdk/conformance/vectors.json. No network, no framework.
//
// Usage: test_zxv <conformance dir>   (holds vectors.json and fixtures/)
#include <zxv.hpp>

#include "mini_json.hpp"

#include <csignal>
#include <cstring>
#include <fstream>
#include <iostream>
#include <set>
#include <sstream>

#if defined(__unix__) || defined(__APPLE__)
#    include <sys/wait.h>
#    include <unistd.h>
#    define ZXV_TEST_HAS_FORK 1
#else
#    define ZXV_TEST_HAS_FORK 0
#endif

namespace {

int g_checks = 0, g_failures = 0;
std::string g_dir;
mini_json::Value g_vec;

#define CHECK(cond)                                                                    \
    do {                                                                               \
        ++g_checks;                                                                    \
        if (!(cond)) {                                                                 \
            ++g_failures;                                                              \
            std::cout << "FAIL " << __FILE__ << ":" << __LINE__ << ": " #cond "\n";    \
        }                                                                              \
    } while (0)

#define CHECK_THROWS_AS(expr, type)                                                    \
    do {                                                                               \
        ++g_checks;                                                                    \
        bool caught_ = false;                                                          \
        try {                                                                          \
            (void) (expr);                                                             \
        } catch (const type&) {                                                        \
            caught_ = true;                                                            \
        } catch (...) {                                                                \
        }                                                                              \
        if (!caught_) {                                                                \
            ++g_failures;                                                              \
            std::cout << "FAIL " << __FILE__ << ":" << __LINE__ << ": " #expr          \
                      << " did not throw " #type "\n";                                 \
        }                                                                              \
    } while (0)

std::vector<std::uint8_t> hex(const std::string& h) { return zxv::from_hex(h); }

std::string read_file(const std::string& p)
{
    std::ifstream f(p, std::ios::binary);
    std::stringstream ss;
    ss << f.rdbuf();
    return ss.str();
}

zxv::Rational rat(const mini_json::Value& pair) { return zxv::Rational::of(pair[0].i64(), pair[1].i64()); }

// ---------------------------------------------------------------- constexpr
// Compile-time proofs: exact arithmetic and wire packing in constant
// expressions (gcc/clang).
#if ZXV_HAS_INT128
static_assert(zxv::Rational::of(1, 10) + zxv::Rational::of(2, 10) == zxv::Rational::of(3, 10));
static_assert(zxv::Rational::of(6, -4).num() == -3 && zxv::Rational::of(6, -4).den() == 2);
static_assert(zxv::Rational::of(1, 3) > zxv::Rational::of(333333, 1000000));
static_assert(!zxv::Rational(INT64_MAX).checked_mul(2).has_value());
#endif
static_assert((zxv::Truth::both & zxv::Truth::true_) == zxv::Truth::both);
static_assert(~zxv::Truth::true_ == zxv::Truth::false_);
static_assert(zxv::Ubh168Frame::make_tag(3, 2, true) == 123);
static_assert(zxv::Ubh168Frame::pack(123, {0x01020304, 0x05060708, 0x090A0B0C}, {0x11121314, 0x15161718}).bytes[1] == 0x04);
static_assert(!std::is_convertible_v<double, zxv::Rational>, "no implicit float entry");
static_assert(!std::is_constructible_v<zxv::Rational, float>, "no float constructor");
static_assert(!std::is_convertible_v<zxv::Truth, bool>, "Truth never converts to bool");
static_assert(!std::is_copy_constructible_v<zxv::Context> && std::is_move_constructible_v<zxv::Context>);
static_assert(!std::is_copy_constructible_v<zxv::SecretKey>);
static_assert(!std::is_base_of_v<std::exception, zxv::FatalError>, "FatalError stands alone");
static_assert(std::is_base_of_v<zxv::RefusedError, zxv::BudgetExhausted>);

// ---------------------------------------------------------------- library
void test_library_and_status()
{
    const auto v = zxv::version();
    CHECK(v.major == 1 && v.minor >= 1);
    CHECK(v.major == static_cast<std::uint32_t>(g_vec["abi"][0].i64()));
    zxv::ensure_compatible();
    CHECK(zxv::has_feature(zxv::feature::rational | zxv::feature::caps));
    CHECK(!zxv::has_feature(zxv::feature::lora));
    for (const auto& s : g_vec["status"].items) {
        const auto code = static_cast<std::int32_t>(s["code"].i64());
        CHECK(zxv::status_name(code) == s["name"].str());
        CHECK(static_cast<std::int64_t>(zxv::status_class(code)) == s["class"].i64());
        if (code == 0) continue;
        // Every negative status maps to the documented exception type.
        try {
            zxv::detail::raise(code, "probe");
        } catch (const zxv::FatalError& e) {
            CHECK(zxv::status_class(code) == zxv::StatusClass::fatal && e.status() == code);
        } catch (const zxv::UsageError& e) {
            CHECK(zxv::status_class(code) == zxv::StatusClass::usage && e.status_name() == s["name"].str());
        } catch (const zxv::RefusedError& e) {
            CHECK(zxv::status_class(code) == zxv::StatusClass::refused && e.detail() == "probe");
        } catch (const zxv::SystemError&) {
            CHECK(zxv::status_class(code) == zxv::StatusClass::system);
        }
    }
    CHECK_THROWS_AS(zxv::detail::raise(ZXV_E_CAP, ""), zxv::CapabilityDenied);
    CHECK_THROWS_AS(zxv::detail::raise(ZXV_E_BUDGET, ""), zxv::BudgetExhausted);
    CHECK_THROWS_AS(zxv::detail::raise(ZXV_E_FUNDS, ""), zxv::InsufficientFunds);
    CHECK_THROWS_AS(zxv::detail::raise(ZXV_E_VERIFY, ""), zxv::VerificationFailed);
    CHECK_THROWS_AS(zxv::detail::raise(ZXV_E_NOTIMPL, ""), zxv::NotImplemented);
    CHECK_THROWS_AS(zxv::detail::raise(ZXV_E_FLOAT, ""), zxv::FloatRejected);
    CHECK_THROWS_AS(zxv::detail::raise(-1234, ""), zxv::SystemError); // unknown = SYSTEM (R8)
}

// ---------------------------------------------------------------- rational
void test_rational()
{
    using zxv::Rational;
    for (const auto& c : g_vec["rational_ops"].items) {
        const Rational a = rat(c["a"]), b = rat(c["b"]);
        CHECK(a + b == rat(c["add"]));
        CHECK(a - b == rat(c["sub"]));
        CHECK(a * b == rat(c["mul"]));
        CHECK(a / b == rat(c["div"]));
        const auto cmp = a <=> b;
        CHECK((cmp < 0 ? -1 : cmp > 0 ? 1 : 0) == c["cmp"].i64());
        // the host constexpr path agrees with the C library
        std::int64_t n = 0, d = 0;
        CHECK(zxv_rat_add(a.num(), a.den(), b.num(), b.den(), &n, &d) == ZXV_OK && (a + b) == Rational::of(n, d));
    }
    for (const auto& c : g_vec["rational_parse"].items) {
        const auto st = c["status"].i64();
        if (st == 0) {
            const Rational r = Rational::parse(c["text"].str());
            CHECK(r == rat(c["value"]));
            CHECK(r.to_string() == c["format"].str());
        } else {
            try {
                (void) Rational::parse(c["text"].str());
                CHECK(false);
            } catch (const zxv::UsageError& e) {
                CHECK(e.status() == st);
            }
        }
    }
    for (const auto& c : g_vec["ieee754"].items) {
        const double x = std::bit_cast<double>(std::stoull(c["bits"].str(), nullptr, 16));
        const auto st = c["status"].i64();
        if (st == 0) {
            CHECK(Rational::from_double_exact(x) == rat(c["value"]));
        } else if (st == ZXV_E_FLOAT) {
            CHECK_THROWS_AS(Rational::from_double_exact(x), zxv::FloatRejected);
        } else {
            try {
                (void) Rational::from_double_exact(x);
                CHECK(false);
            } catch (const zxv::UsageError& e) {
                CHECK(e.status() == st);
            }
        }
    }
    for (const auto& c : g_vec["q16"].items) {
        const Rational r = rat(c["value"]);
        if (c["status"].i64() == 0) {
            CHECK(r.to_q16() == c["q16"].i64());
        } else {
            CHECK_THROWS_AS(r.to_q16(), zxv::UsageError);
        }
    }
    // overflow is detected, never wrapped; checked forms return nullopt
    CHECK_THROWS_AS(Rational(INT64_MAX) * Rational(2), zxv::UsageError);
    CHECK(!Rational(INT64_MAX).checked_add(1).has_value());
    CHECK_THROWS_AS(Rational::of(1, 0), zxv::UsageError);
    CHECK_THROWS_AS(Rational(1) / Rational(0), zxv::UsageError);
    CHECK_THROWS_AS(Rational(std::int64_t{INT64_MIN}), zxv::UsageError);
    CHECK(Rational::from_q16(45875).to_q16() == 45875);
    CHECK((-Rational::of(1, 3)).abs() == Rational::of(1, 3));
    // fuzz: host arithmetic equals the C library, including overflow edges
    std::uint64_t s = 0x9E3779B97F4A7C15ull;
    auto rnd = [&] {
        s ^= s << 13;
        s ^= s >> 7;
        s ^= s << 17;
        const int shift = static_cast<int>(s % 63);
        auto v = static_cast<std::int64_t>((s >> 1) >> shift);
        return (s & 1) ? -v : v;
    };
    int agree = 0;
    for (int i = 0; i < 20000; ++i) {
        std::int64_t an = rnd(), ad = rnd(), bn = rnd(), bd = rnd();
        if (ad == 0) ad = 1;
        if (bd == 0) bd = 3;
        const Rational a = Rational::of(an, ad), b = Rational::of(bn, bd);
        using fn = std::int32_t (*)(std::int64_t, std::int64_t, std::int64_t, std::int64_t, std::int64_t*, std::int64_t*);
        const std::pair<fn, std::optional<Rational>> ops[] = {{zxv_rat_add, a.checked_add(b)},
                                                              {zxv_rat_sub, a.checked_sub(b)},
                                                              {zxv_rat_mul, a.checked_mul(b)},
                                                              {zxv_rat_div, a.checked_div(b)}};
        for (const auto& [f, host] : ops) {
            std::int64_t n = 0, d = 0;
            const std::int32_t st = f(a.num(), a.den(), b.num(), b.den(), &n, &d);
            if (st == ZXV_OK ? (host && *host == Rational::of(n, d)) : !host) ++agree;
        }
        std::int32_t c = 0;
        zxv_rat_cmp(a.num(), a.den(), b.num(), b.den(), &c);
        const auto h = a <=> b;
        if ((h < 0 ? -1 : h > 0 ? 1 : 0) == c) ++agree;
    }
    CHECK(agree == 20000 * 5);
}

// ---------------------------------------------------------------- truth
void test_truth()
{
    using zxv::Truth;
    const auto& lp = g_vec["lpres"];
    for (std::size_t a = 0; a < 4; ++a) {
        const Truth ta = zxv::truth_from_c(static_cast<std::int32_t>(a));
        CHECK(zxv::to_string(ta) == lp["names"][a].str());
        CHECK(zxv::to_c(~ta) == lp["not"][a].i64());
        for (std::size_t b = 0; b < 4; ++b) {
            const Truth tb = zxv::truth_from_c(static_cast<std::int32_t>(b));
            CHECK(zxv::to_c(ta & tb) == lp["and"][a][b].i64());
            CHECK(zxv::to_c(ta | tb) == lp["or"][a][b].i64());
            std::int32_t r = 0;
            CHECK(zxv_lp_and(static_cast<std::int32_t>(a), static_cast<std::int32_t>(b), &r) == ZXV_OK &&
                  r == zxv::to_c(ta & tb));
        }
    }
    CHECK(zxv::truth_from_evidence(2, 1) == Truth::both);
    CHECK(zxv::truth_from_evidence(0, 0) == Truth::neither);
    CHECK(!zxv::is_certainly_true(Truth::both));
    CHECK(zxv::to_bool(Truth::both, false, false) == false);
    CHECK(zxv::to_bool(Truth::neither, false, true) == true);
    CHECK_THROWS_AS(zxv::require_true(Truth::both, "gate"), zxv::RefusedError);
    CHECK_THROWS_AS(zxv::truth_from_c(7), zxv::UsageError);
}

// ---------------------------------------------------------------- caps, ledger, fatal
void test_caps_ledger_fatal()
{
    zxv::Context ctx;
    zxv::Capability root = ctx.root_capability(zxv::caps::all, 7);
    zxv::Capability ro = root.attenuate(zxv::caps::ledger_read);
    CHECK(ro.check(zxv::caps::ledger_read));
    CHECK(!ro.check(zxv::caps::ledger_post));
    CHECK(!ro.attenuate(zxv::caps::all).check(zxv::caps::ledger_post)); // never widens
    CHECK_THROWS_AS(ctx.root_capability(std::uint64_t{1} << 40), zxv::UsageError);
    CHECK(root.info().process_id == 7 && root.info().attestation == zxv::Truth::true_);

    root.set_attestation(zxv::Truth::both);
    try {
        root.require(zxv::caps::ledger_read);
        CHECK(false);
    } catch (const zxv::CapabilityDenied& e) {
        CHECK(e.detail().find("contradictory") != std::string::npos);
    }
    root.set_attestation(zxv::Truth::true_);

    ctx.enable_currency(root, "NGN", 2);
    CHECK(ctx.enabled_currencies() == std::vector<std::string>{"NGN"});
    zxv::Ledger ledger(root);
    const auto issuer = ledger.open_account("issuer", "NGN", zxv::AccountKind::issuer);
    const auto alice = ledger.open_account("alice", "NGN");
    const auto bob = ledger.open_account("bob", "NGN");
    CHECK(ledger.post(issuer, alice, 500, "NGN", "r1") == 1);
    ledger.post(alice, bob, 120, "NGN", "r2");
    CHECK_THROWS_AS(ledger.post(bob, alice, 1000, "NGN", "r3"), zxv::InsufficientFunds);
    CHECK_THROWS_AS(ledger.post(issuer, alice, 1, "NGN", "r1"), zxv::UsageError); // duplicate ref
    zxv::Ledger reader(ro);
    CHECK_THROWS_AS(reader.post(issuer, alice, 1, "NGN", "r4"), zxv::CapabilityDenied);
    const auto b = reader.balance(alice);
    CHECK(b.debit == 500 && b.credit == 120 && b.equity == 380);
    const auto j = reader.journal(alice);
    CHECK(j.size() == 2 && j[0].amount == 500 && j[1].amount == -120 && j[1].reference == "r2" && j[1].counterparty == bob);
    CHECK(reader.account_name(bob) == "bob" && reader.account_currency(bob) == "NGN");
    ledger.check();

    // ISO 20022 render through a LEDGER_READ capability
    zxv::Message msg(ro, zxv::MxKind::pacs008);
    msg.set(zxv::MxField::msg_id, "M1").set(zxv::MxField::cre_dt_tm, "2026-10-11T00:00:00Z")
        .set(zxv::MxField::debtor_name, "Alice & Co").set(zxv::MxField::debtor_acct, "A1")
        .set(zxv::MxField::creditor_name, "Bob").set(zxv::MxField::creditor_acct, "B1")
        .set(zxv::MxField::end_to_end_id, "E2E1").set(zxv::MxField::ccy, "NGN")
        .set_amount(zxv::MxAmount::settlement, 12075, 2);
    const std::string xml = msg.render();
    CHECK(xml.find("Alice &amp; Co") != std::string::npos && xml.find("120.75") != std::string::npos);
    CHECK_THROWS_AS(zxv::Message(ro, zxv::MxKind::pacs002), zxv::NotImplemented);
    CHECK(zxv::iso4217("NGN").numeric == 566);
    CHECK(zxv::card_check_digit(zxv::CardNetwork::dragon, "4111111111111111"));

    // Revocation flows to descendants.
    root.revoke();
    CHECK(!ro.check(zxv::caps::ledger_read));

    // Fail closed: halt -> FatalError, handle dead, outside std::exception.
    zxv::Capability c2 = ctx.root_capability();
    zxv::Ledger l2(c2);
    ctx.halt("test halt");
    CHECK(ctx.is_poisoned());
    bool fatal = false, swallowed_by_std = false;
    try {
        try {
            (void) l2.balance(alice);
        } catch (const std::exception&) {
            swallowed_by_std = true;
        }
    } catch (const zxv::FatalError& e) {
        fatal = e.status() == ZXV_E_POISONED;
    }
    CHECK(fatal && !swallowed_by_std);
    // later calls are refused before reaching C
    bool again = false;
    try {
        l2.check();
    } catch (const zxv::FatalError&) {
        again = true;
    }
    CHECK(again);
}

void test_fatal_abort_policy()
{
#if ZXV_TEST_HAS_FORK
    std::cout.flush();
    const pid_t pid = fork();
    if (pid == 0) {
        zxv::set_fatal_policy(zxv::FatalPolicy::abort);
        zxv::Context ctx;
        auto cap = ctx.root_capability();
        ctx.halt("abort policy test");
        try {
            zxv::Ledger(cap).check();
        } catch (...) {
        }
        _exit(0); // reached only if the policy failed to abort
    }
    int status = 0;
    waitpid(pid, &status, 0);
    CHECK(WIFSIGNALED(status) && WTERMSIG(status) == SIGABRT);
#endif
}

// ---------------------------------------------------------------- attestations, budgets
void test_attestations_budgets()
{
    zxv::Context ctx;
    auto cap = ctx.root_capability();
    zxv::Attestations reg(cap);
    const auto i = reg.attest("model.gguf", zxv::Subject::model, zxv::Truth::true_, "loader");
    CHECK(reg.gate());
    reg.evidence(i, true);
    reg.evidence(i, false);
    const auto st = reg.state(i);
    CHECK(st.truth == zxv::Truth::both && st.positive == 2 && st.negative == 1);
    CHECK(!reg.gate());
    CHECK(reg.find("model.gguf") == i && !reg.find("nope"));
    // ABI 1.2: creating a registry needs caps::attest, and so does using it.
    CHECK_THROWS_AS(zxv::Attestations(cap.attenuate(zxv::caps::ledger_read)), zxv::CapabilityDenied);
    {
        auto at = cap.attenuate(zxv::caps::attest);
        zxv::Attestations reg2(at);
        at.revoke();
        CHECK_THROWS_AS(reg2.attest("x", zxv::Subject::source, zxv::Truth::true_, "a"), zxv::CapabilityDenied);
    }

    zxv::Budgets budgets(cap);
    auto b = budgets.create("tokens", zxv::Resource::tokens, 10);
    int denied = 0;
    b.on_exhausted([&](std::uint32_t) { ++denied; });
    b.consume(zxv::Rational::of(7, 2));
    b.consume(zxv::Rational::of(13, 2));
    CHECK_THROWS_AS(b.consume(zxv::Rational::of(1, 3)), zxv::BudgetExhausted);
    CHECK(denied == 1);
    CHECK_THROWS_AS(b.consume(-1), zxv::UsageError); // RMAG_INVALID: never retry
    auto s = b.status();
    CHECK(s.remaining == 0 && s.used == 10 && s.epoch == 0);
    b.release(zxv::Rational::of(1, 2));
    s = b.status();
    CHECK(s.remaining == zxv::Rational::of(1, 2) && s.epoch == 1);

    // async: a waiter blocks on exhaustion and completes after a release
    auto fut = b.consume_async(2);
    CHECK(fut.wait_for(std::chrono::milliseconds(50)) == std::future_status::timeout);
    b.release(3);
    CHECK(fut.wait_for(std::chrono::seconds(5)) == std::future_status::ready);
    fut.get();
    // a growing limit also wakes waiters
    auto fut2 = b.consume_async(5);
    CHECK(fut2.wait_for(std::chrono::milliseconds(30)) == std::future_status::timeout);
    b.set_limit(20);
    fut2.get();
    // INVALID fails immediately; timeout and cancellation
    CHECK_THROWS_AS(b.consume_async(-3).get(), zxv::UsageError);
    CHECK_THROWS_AS(b.consume_async(100, {}, std::chrono::milliseconds(20)).get(), zxv::BudgetExhausted);
    zxv::stop_source stop;
    auto fut3 = b.consume_async(100, stop.get_token());
    stop.request_stop();
    CHECK_THROWS_AS(fut3.get(), zxv::OperationCancelled);
    // destroying the owner cancels pending waits
    std::future<void> fut4;
    {
        zxv::Budgets other(cap);
        auto ob = other.create("o", zxv::Resource::energy, 0);
        fut4 = ob.consume_async(1);
    }
    CHECK_THROWS_AS(fut4.get(), zxv::OperationCancelled);
}

// ---------------------------------------------------------------- phase
void test_phase()
{
    zxv::Context ctx;
    auto cap = ctx.root_capability();
    zxv::PhaseClock clock(cap);
    CHECK(zxv::phase_cycle == static_cast<std::uint64_t>(g_vec["phase_cycle"].i64()));
    auto ev = clock.defer(zxv::phase_cycle);
    auto fut = ev.await_cleared();
    CHECK(clock.next_due() == zxv::phase_cycle);
    auto a = clock.advance(zxv::phase_cycle - 1);
    CHECK(a.cleared == 0 && ev.state() == zxv::EventState::pending);
    CHECK(fut.wait_for(std::chrono::milliseconds(0)) == std::future_status::timeout);
    a = clock.advance(1);
    CHECK(a.cleared == 1 && a.now == 27720 && ev.state() == zxv::EventState::cleared);
    CHECK(fut.wait_for(std::chrono::milliseconds(0)) == std::future_status::ready);
    CHECK(!clock.next_due());

    auto adm = clock.admit(6, 1, zxv::step::s0_pending);
    CHECK(adm.decision == zxv::Decision::defer && adm.token == 0);
    clock.set_health(6, zxv::Health::healthy);
    adm = clock.admit(6, 2, zxv::step::requires_health);
    CHECK(adm.decision == zxv::Decision::admit && adm.token != 0);

    auto ev2 = clock.defer(clock.now() + 100);
    ev2.cancel();
    CHECK_THROWS_AS(ev2.await_cleared().get(), zxv::OperationCancelled);
    CHECK_THROWS_AS(zxv::PhaseClock(cap.attenuate(zxv::caps::ledger_read)), zxv::CapabilityDenied);
    {
        auto ph = cap.attenuate(zxv::caps::phase);
        zxv::PhaseClock c3(ph);
        ph.revoke();
        CHECK_THROWS_AS(c3.advance(1), zxv::CapabilityDenied);
    }

    // real-time driver: 100,000 ticks/s clears an event 2,000 ticks out
    auto until = clock.until_tick(clock.now() + 2000);
    clock.start_realtime(100000);
    CHECK(until.wait_for(std::chrono::seconds(5)) == std::future_status::ready);
    clock.stop_realtime();

    // a pending waiter fails when the clock is destroyed
    std::shared_future<void> orphan;
    {
        zxv::PhaseClock c2(cap);
        orphan = c2.until_tick(10);
    }
    CHECK_THROWS_AS(orphan.get(), zxv::OperationCancelled);
}

// ---------------------------------------------------------------- wire
void test_wire()
{
    for (const auto& c : g_vec["ubh168"].items) {
        const auto tag = zxv::Ubh168Frame::make_tag(static_cast<std::uint32_t>(c["trunk"].i64()),
                                                    static_cast<std::uint32_t>(c["shell"].i64()), c["sync"].i64() != 0);
        CHECK(tag == c["tag"].i64());
        const std::array<std::uint32_t, 3> sp{static_cast<std::uint32_t>(c["s_plus"][0].u64()),
                                             static_cast<std::uint32_t>(c["s_plus"][1].u64()),
                                             static_cast<std::uint32_t>(c["s_plus"][2].u64())};
        const std::array<std::uint32_t, 2> sm{static_cast<std::uint32_t>(c["s_minus"][0].u64()),
                                             static_cast<std::uint32_t>(c["s_minus"][1].u64())};
        const auto f = zxv::Ubh168Frame::pack(tag, sp, sm);
        CHECK(zxv::to_hex(f.bytes) == c["frame"].str());          // host, byte for byte
        CHECK(f == zxv::Ubh168Frame::pack_native(tag, sp, sm));    // == the C library
        CHECK(f.s_plus() == sp && f.s_minus() == sm && f.tag() == tag);
        CHECK(static_cast<std::int64_t>(f.resolve()) == c["truth"].i64());
    }
    for (std::uint32_t t = 0; t < g_vec["ubh168_idle"].size(); ++t) {
        const auto idle = zxv::Ubh168Frame::idle(t);
        CHECK(zxv::to_hex(idle.bytes) == g_vec["ubh168_idle"][t].str());
        CHECK(idle.resolve() == zxv::WireTruth::neutral);
    }
    CHECK_THROWS_AS(zxv::Ubh168Frame::make_tag(10, 0, false), zxv::UsageError);
}

// ---------------------------------------------------------------- swarm
void test_swarm()
{
    const auto& v = g_vec["swarm"];
    zxv::Context ctx;
    auto cap = ctx.root_capability();
    zxv::Swarm sw(cap, static_cast<std::uint32_t>(v["levels"].i64()), v["tokens_per_cycle"].u64());
    for (const auto& m : v["models"].items)
        sw.register_model(static_cast<std::uint32_t>(m[0].i64()), static_cast<std::uint32_t>(m[1].i64()));
    sw.set_mood(static_cast<char32_t>(v["mood"][0].i64()), static_cast<std::uint32_t>(v["mood"][1].i64()));
    for (const auto& f : v["feelings"].items)
        sw.set_feeling(static_cast<std::uint32_t>(f[0].i64()), static_cast<char32_t>(f[1].i64()),
                       static_cast<std::uint32_t>(f[2].i64()));
    sw.begin_cycle();
    for (const auto& a : v["allotments"].items) {
        const auto al = sw.allotment(static_cast<std::uint32_t>(a[0].i64()));
        CHECK(al.allotted == a[1].u64() && al.imaginary == a[2].u64());
    }
    const auto first = sw.allotment(100).allotted;
    CHECK(!sw.consume(100, 10).exhausted);
    const auto g = sw.consume(100, first);
    CHECK(g.exhausted && g.granted == first - 10);
    CHECK(sw.end_cycle() > 0);
    for (std::uint32_t d = 0; d < v["level_capacity"].size(); ++d)
        CHECK(zxv::Swarm::level_capacity(d) == v["level_capacity"][d].u64());
    CHECK_THROWS_AS(sw.set_mood(U'\U0001F600', 1), zxv::UsageError); // not in the palette
    (void) zxv::Swarm::emotion_profile(U'\U0001F914');
}

// ---------------------------------------------------------------- hashes and crypto
void test_hash_crypto()
{
    for (const auto& c : g_vec["hash"].items) {
        const std::string& in = c["utf8"].str();
        CHECK(zxv::to_hex(zxv::Hashes::keccak256(in)) == c["keccak256"].str());
        CHECK(zxv::to_hex(zxv::Hashes::sha3_256(in)) == c["sha3_256"].str());
        CHECK(zxv::to_hex(zxv::Hashes::shake256(in, 64)) == c["shake256_64"].str());
    }
    std::array<std::uint8_t, 32> r1{}, r2{};
    zxv::random_bytes(r1);
    zxv::random_bytes(r2);
    CHECK(r1 != r2);

    zxv::Context ctx;
    auto cap = ctx.root_capability();
    auto kem_only = cap.attenuate(zxv::caps::crypto_kem);

    const auto& e = g_vec["ed25519"];
    zxv::Ed25519 ed(cap);
    auto sk = zxv::SecretKey::from_bytes(hex(e["sk"].str()));
    const auto pk = ed.public_key(sk);
    CHECK(zxv::to_hex(pk) == e["pk"].str());
    auto sig = ed.sign(sk, zxv::as_bytes(e["msg_utf8"].str()));
    CHECK(zxv::to_hex(sig) == e["sig"].str());
    CHECK(ed.verify(pk, zxv::as_bytes(e["msg_utf8"].str()), sig));
    sig[0] ^= 1;
    CHECK(!ed.verify(pk, zxv::as_bytes(e["msg_utf8"].str()), sig));
    CHECK_THROWS_AS(zxv::Ed25519(kem_only).sign(sk, zxv::as_bytes("x")), zxv::CapabilityDenied);

    const auto& d = g_vec["mldsa87"];
    zxv::MlDsa87 dsa(cap);
    auto kp = dsa.keygen_seed(hex(d["seed"].str()));
    CHECK(zxv::to_hex(zxv::Hashes::sha3_256(kp.public_key)) == d["pk_sha3_256"].str());
    CHECK(zxv::to_hex(zxv::Hashes::sha3_256(kp.secret_key.bytes())) == d["sk_sha3_256"].str());
    const auto msg = zxv::as_bytes(d["msg_utf8"].str());
    const auto dctx = zxv::as_bytes(d["ctx_utf8"].str());
    const auto dsig = dsa.sign_deterministic(kp.secret_key, msg, dctx);
    CHECK(zxv::to_hex(zxv::Hashes::sha3_256(dsig)) == d["sig_deterministic_sha3_256"].str());
    CHECK(dsa.verify(kp.public_key, msg, dsig, dctx));
    CHECK(!dsa.verify(kp.public_key, msg, dsig, zxv::as_bytes("zxw")));
    const auto hedged = dsa.sign(kp.secret_key, msg, dctx);
    CHECK(dsa.verify(kp.public_key, msg, hedged, dctx));

    const auto& k = g_vec["mlkem1024"];
    zxv::MlKem1024 kem(kem_only);
    auto kkp = kem.keygen_derand(hex(k["d"].str()), hex(k["z"].str()));
    CHECK(zxv::to_hex(zxv::Hashes::sha3_256(kkp.encapsulation_key)) == k["ek_sha3_256"].str());
    auto enc = kem.encapsulate_derand(kkp.encapsulation_key, hex(k["m"].str()));
    CHECK(zxv::to_hex(zxv::Hashes::sha3_256(enc.ciphertext)) == k["ct_sha3_256"].str());
    CHECK(zxv::to_hex(enc.shared_secret.bytes()) == k["ss"].str());
    auto ss = kem.decapsulate(kkp.decapsulation_key, enc.ciphertext);
    CHECK(zxv::to_hex(ss.bytes()) == k["ss"].str());
    auto fresh = kem.keygen();
    auto e2 = kem.encapsulate(fresh.encapsulation_key);
    CHECK(kem.decapsulate(fresh.decapsulation_key, e2.ciphertext).export_bytes_unsafe() ==
          e2.shared_secret.export_bytes_unsafe());
    auto bad = fresh.encapsulation_key;
    std::fill(bad.begin(), bad.begin() + 32, 0xFF);
    CHECK_THROWS_AS(kem.encapsulate(bad), zxv::UsageError);

    // SecretKey wipes
    auto w = zxv::SecretKey::from_bytes(hex("0102030405"));
    w.wipe();
    CHECK(w.export_bytes_unsafe() == std::vector<std::uint8_t>(5, 0));
}

// ---------------------------------------------------------------- engine
struct Engine {
    zxv::Context ctx;
    zxv::Capability cap = ctx.root_capability();
    zxv::Model model = zxv::Model::open(cap, g_dir + "/fixtures/tiny-qwen2.gguf", g_dir + "/fixtures/tiny-tokenizer.gguf");
};

std::vector<std::int32_t> ints(const mini_json::Value& a)
{
    std::vector<std::int32_t> v;
    for (const auto& x : a.items) v.push_back(static_cast<std::int32_t>(x.i64()));
    return v;
}

void test_engine()
{
    if (!zxv::has_feature(zxv::feature::tensor)) {
        std::cout << "skip engine: built without the engine\n";
        return;
    }
    const auto& gv = g_vec["generate"];
    const std::string prompt = read_file(g_dir + "/fixtures/prompt.txt");
    Engine e;
    CHECK(e.model.info(zxv::ModelInfo::n_vocab) == 1200);
    CHECK(e.model.tokenize(prompt) == ints(gv["prompt_tokens"]));
    CHECK(e.model.detokenize(e.model.tokenize(prompt)) == prompt);
    CHECK_THROWS_AS(zxv::Model::open(e.cap.attenuate(zxv::caps::ledger_read), g_dir + "/fixtures/tiny-qwen2.gguf"),
                    zxv::CapabilityDenied);
    CHECK_THROWS_AS(zxv::Model::open(e.cap, "/nonexistent.gguf"), zxv::SystemError);

    auto s = e.model.new_session(static_cast<std::uint32_t>(gv["n_ctx"].i64()));
    zxv::GenerateOptions greedy;
    greedy.max_tokens = static_cast<std::uint32_t>(gv["greedy"]["max_tokens"].i64());
    auto r = s.generate(prompt, greedy);
    CHECK(r.tokens == ints(gv["greedy"]["tokens"]));
    CHECK(r.stop == zxv::StopReason::max_tokens && r.prompt_tokens == gv["prompt_tokens"].size());
    CHECK(r.text == e.model.detokenize(r.tokens));

    const auto& sv = gv["sampled"];
    zxv::GenerateOptions sampled;
    sampled.max_tokens = static_cast<std::uint32_t>(sv["max_tokens"].i64());
    sampled.temperature = zxv::Rational::from_q16(static_cast<std::int32_t>(sv["temperature_q16"].i64()));
    sampled.top_k = static_cast<std::uint32_t>(sv["top_k"].i64());
    sampled.top_p = zxv::Rational::from_q16(static_cast<std::int32_t>(sv["top_p_q16"].i64()));
    sampled.repeat_penalty = zxv::Rational::from_q16(static_cast<std::int32_t>(sv["repeat_penalty_q16"].i64()));
    sampled.repeat_last_n = static_cast<std::uint32_t>(sv["repeat_last_n"].i64());
    sampled.seed = sv["seed"].u64();
    s.reset();
    CHECK(s.generate(prompt, sampled).tokens == ints(sv["tokens"]));
    CHECK(zxv::Rational::of(7, 10).to_q16() == sv["temperature_q16"].i64()); // 0.7 exactly

    // callback stop, and exceptions crossing the callback
    s.reset();
    int seen = 0;
    r = s.generate(prompt, greedy, [&](const zxv::Token&) { return ++seen < 3; });
    CHECK(r.stop == zxv::StopReason::callback && r.tokens.size() == 3);
    s.reset();
    CHECK_THROWS_AS(s.generate(prompt, greedy, [](const zxv::Token&) -> bool { throw std::logic_error("x"); }),
                    std::logic_error);

    // iterator-style stream gives the greedy tokens
    s.reset();
    std::vector<std::int32_t> streamed;
    {
        auto st = s.stream(prompt, greedy);
        CHECK_THROWS_AS(s.reset(), zxv::UsageError); // busy while the stream is open
        for (const zxv::Token& t : st) streamed.push_back(t.id);
        CHECK(st.finish().stop == zxv::StopReason::max_tokens);
    }
    CHECK(streamed == ints(gv["greedy"]["tokens"]));
    // abandoning a stream early stops generation cleanly
    s.reset();
    {
        auto st = s.stream(prompt, greedy);
        CHECK(st.next().has_value());
    }
    s.reset();

    // budget binding: 8 tokens beyond the prompt stops early; then refuses up front
    zxv::Budgets budgets(e.cap);
    auto b = budgets.create("gen", zxv::Resource::tokens, static_cast<std::int64_t>(gv["prompt_tokens"].size()) + 8);
    s.bind_budget(b);
    r = s.generate(prompt, greedy);
    CHECK(r.stop == zxv::StopReason::budget && r.tokens.size() == 8);
    s.reset();
    CHECK_THROWS_AS(s.generate(prompt, greedy), zxv::BudgetExhausted);
    s.unbind_budget();

    CHECK_THROWS_AS(e.model.load_lora("x.gguf", 1), zxv::NotImplemented);
}

// Deterministic lockstep: two independent "machines" run the same frames
// with the same seeds and inputs and must produce identical tokens.
void test_lockstep()
{
    if (!zxv::has_feature(zxv::feature::tensor)) return;
    Engine a, b;
    auto sa = a.model.new_session(128), sb = b.model.new_session(128);
    zxv::GenerateOptions o;
    o.max_tokens = 4;
    o.temperature = zxv::Rational::of(7, 10);
    o.top_k = 40;
    bool same = true;
    std::set<std::vector<std::int32_t>> distinct;
    for (std::uint64_t frame = 0; frame < 6; ++frame) {
        const std::string input = "frame " + std::to_string(frame) + ": player moved north";
        o.seed = frame * 0x9E3779B97F4A7C15ull + 1;
        sa.reset();
        sb.reset();
        const auto ta = sa.generate(input, o).tokens, tb = sb.generate(input, o).tokens;
        same = same && ta == tb;
        distinct.insert(ta);
    }
    CHECK(same);
    CHECK(distinct.size() > 1);
}

// ---------------------------------------------------------------- evm
void test_evm()
{
    const auto& v = g_vec["evm_state"];
    zxv::Context ctx;
    auto cap = ctx.root_capability();
    zxv::StateChannel ch(cap);
    zxv::Digest32 id{};
    const auto idb = hex(v["channel_id"].str());
    std::copy(idb.begin(), idb.end(), id.begin());
    std::vector<zxv::Address> parties;
    std::vector<std::int64_t> balances;
    for (std::size_t i = 0; i < v["addresses"].size(); ++i) {
        zxv::Address a{};
        const auto ab = hex(v["addresses"][i].str());
        std::copy(ab.begin(), ab.end(), a.begin());
        parties.push_back(a);
        balances.push_back(v["balances"][i].i64());
    }
    const auto st = ch.encode_state(id, v["nonce"].u64(), parties, balances);
    CHECK(zxv::to_hex(st.encoding) == v["encoding"].str());
    CHECK(zxv::to_hex(st.digest) == v["digest"].str());
    CHECK(zxv::Hashes::keccak256(st.encoding) == st.digest);

    ctx.enable_currency(cap, "USD", 2);
    zxv::Ledger ledger(cap);
    const std::uint32_t acc[2] = {ledger.open_account("a", "USD", zxv::AccountKind::issuer), ledger.open_account("b", "USD")};
    ledger.post(acc[0], acc[1], 250, "USD", "x");
    const std::int64_t bal[2] = {-250, 250};
    const auto l = ch.ledger_state(id, 1, "USD", acc, std::span(parties).first(2));
    CHECK(l.digest == ch.encode_state(id, 1, std::span(parties).first(2), bal).digest);
    CHECK_THROWS_AS(zxv::StateChannel(cap.attenuate(zxv::caps::ledger_read)).encode_state(id, 1, parties, balances),
                    zxv::CapabilityDenied);
}

// ---------------------------------------------------------------- ABI 1.2 cap forms
void test_cap_forms()
{
    zxv::Context ctx;
    auto root = ctx.root_capability();
    auto post = root.attenuate(zxv::caps::ledger_post);
    auto read = root.attenuate(zxv::caps::ledger_read);

    // LEDGER_POST: currencies and opening accounts
    ctx.enable_currency(post, "NGN");
    ctx.enable_currency(post, "GHS", 2);
    ctx.register_private_unit(post, "VFV", 2);
    CHECK((ctx.enabled_currencies() == std::vector<std::string>{"NGN", "GHS", "VFV"}));
    CHECK_THROWS_AS(ctx.enable_currency(read, "KES"), zxv::CapabilityDenied);
    CHECK_THROWS_AS(ctx.enable_currency(read, "KES", 2), zxv::CapabilityDenied);
    CHECK_THROWS_AS(ctx.register_private_unit(read, "VFX", 2), zxv::CapabilityDenied);
    zxv::Ledger writer(post), reader(read);
    const auto bank = writer.open_account("bank", "NGN", zxv::AccountKind::issuer);
    const auto ada = writer.open_account("ada", "NGN");
    CHECK_THROWS_AS(reader.open_account("x", "NGN"), zxv::CapabilityDenied);
    writer.post(bank, ada, 7, "NGN", "R-1");

    // LEDGER_READ: account metadata, journals and messages
    CHECK(reader.account_count() == 2);
    CHECK(reader.account_name(ada) == "ada" && reader.account_currency(ada) == "NGN");
    CHECK(reader.account_kind(bank) == zxv::AccountKind::issuer && reader.account_kind(ada) == zxv::AccountKind::holder);
    CHECK(reader.journal(ada).size() == 1 && reader.journal(ada)[0].reference == "R-1");
    CHECK_THROWS_AS(writer.account_count(), zxv::CapabilityDenied);
    CHECK_THROWS_AS(writer.account_name(ada), zxv::CapabilityDenied);
    CHECK_THROWS_AS(writer.account_currency(ada), zxv::CapabilityDenied);
    CHECK_THROWS_AS(writer.account_kind(ada), zxv::CapabilityDenied);
    CHECK_THROWS_AS(writer.journal(ada), zxv::CapabilityDenied);
    CHECK_THROWS_AS(zxv::Message(post, zxv::MxKind::pacs008), zxv::CapabilityDenied);
    {
        auto mcap = root.attenuate(zxv::caps::ledger_read);
        zxv::Message m(mcap, zxv::MxKind::pacs008);
        m.set(zxv::MxField::msg_id, "M-1");
        mcap.set_attestation(zxv::Truth::neither); // re-checked on every message call
        CHECK_THROWS_AS(m.set(zxv::MxField::msg_id, "M-2"), zxv::CapabilityDenied);
    }
    {
        // the message keeps its capability alive after the Capability object is gone
        std::optional<zxv::Message> m;
        {
            auto tmp = root.attenuate(zxv::caps::ledger_read);
            m.emplace(tmp, zxv::MxKind::pacs008);
        }
        m->set(zxv::MxField::msg_id, "M-3");
    }

    // handle creation: BUDGET, PHASE, ATTEST, SWARM
    CHECK_THROWS_AS(zxv::Budgets{read}, zxv::CapabilityDenied);
    CHECK_THROWS_AS(zxv::PhaseClock{read}, zxv::CapabilityDenied);
    CHECK_THROWS_AS(zxv::Attestations{read}, zxv::CapabilityDenied);
    CHECK_THROWS_AS(zxv::Swarm(read, 2, 10), zxv::CapabilityDenied);
    zxv::Budgets{root.attenuate(zxv::caps::budget)};
    zxv::PhaseClock{root.attenuate(zxv::caps::phase)};
    zxv::Attestations{root.attenuate(zxv::caps::attest)};
    zxv::Swarm(root.attenuate(zxv::caps::swarm), 2, 10);
    CHECK(true);
}

void test_cap_from_another_context()
{
    zxv::Context ctx, other;
    auto foreign = other.root_capability();
    for (int which = 0; which < 3; ++which) {
        bool refused = false;
        try {
            if (which == 0) ctx.enable_currency(foreign, "NGN");
            if (which == 1) ctx.enable_currency(foreign, "NGN", 2);
            if (which == 2) ctx.register_private_unit(foreign, "VFV", 2);
        } catch (const zxv::CapabilityDenied& e) {
            refused = e.status() == ZXV_E_CAP && e.detail().find("different context") != std::string::npos;
        }
        CHECK(refused);
    }
    CHECK(ctx.enabled_currencies().empty());
    CHECK(other.enabled_currencies().empty()); // nothing leaked into the cap's own context
}

void test_deprecated_no_cap_forms()
{
    zxv::Context ctx;
#if defined(__GNUC__)
#    pragma GCC diagnostic push
#    pragma GCC diagnostic ignored "-Wdeprecated-declarations"
#elif defined(_MSC_VER)
#    pragma warning(push)
#    pragma warning(disable : 4996)
#endif
    ctx.enable_currency("NGN");
    ctx.enable_currency("GHS", 2);
    ctx.register_private_unit("VFV", 2);
    zxv::Message m(ctx, zxv::MxKind::pacs008);
    m.set(zxv::MxField::msg_id, "M-1");
#if defined(__GNUC__)
#    pragma GCC diagnostic pop
#elif defined(_MSC_VER)
#    pragma warning(pop)
#endif
    CHECK((ctx.enabled_currencies() == std::vector<std::string>{"NGN", "GHS", "VFV"}));
}

// ---------------------------------------------------------------- stop tokens
// zxv::stop_source / stop_token / stop_callback / jthread: the std types, or
// the bundled fallback (ZXV_STOP_TOKEN_FALLBACK); the same behaviour either way.
void test_stop_tokens()
{
    {
        zxv::stop_token none;
        CHECK(!none.stop_possible() && !none.stop_requested());
        int ran = 0;
        {
            zxv::stop_callback cb(none, [&] { ++ran; });
        }
        CHECK(ran == 0);
    }
    {
        zxv::stop_source src;
        auto tok = src.get_token();
        CHECK(tok.stop_possible() && !tok.stop_requested());
        int a = 0, b = 0;
        zxv::stop_callback ca(tok, [&] { ++a; });
        {
            zxv::stop_callback cb(tok, [&] { ++b; });
        } // deregistered: never runs
        CHECK(src.request_stop());
        CHECK(!src.request_stop()); // only the first request counts
        CHECK(tok.stop_requested() && a == 1 && b == 0);
        int late = 0;
        zxv::stop_callback cl(tok, [&] { ++late; }); // already stopped: runs at once
        CHECK(late == 1);
    }
    {
        // a callback registered on another thread runs on the requesting thread
        zxv::stop_source src;
        std::atomic<int> hits{0};
        std::thread::id ran_on;
        zxv::stop_callback cb(src.get_token(), [&] {
            ran_on = std::this_thread::get_id();
            ++hits;
        });
        std::thread([&] { src.request_stop(); }).join();
        CHECK(hits == 1 && ran_on != std::this_thread::get_id());
    }
    {
        // jthread: passes its token when the callable takes one; stops and joins on destruction
        std::atomic<bool> saw_stop{false};
        {
            zxv::jthread t([&](zxv::stop_token st) {
                while (!st.stop_requested()) std::this_thread::sleep_for(std::chrono::milliseconds(1));
                saw_stop = true;
            });
            CHECK(t.joinable());
        }
        CHECK(saw_stop);
        std::atomic<int> plain{0};
        {
            zxv::jthread t([&](int add) { plain += add; }, 5);
        }
        CHECK(plain == 5);
        zxv::jthread empty;
        CHECK(!empty.joinable());
        zxv::jthread moved([](zxv::stop_token) {});
        empty = std::move(moved);
        CHECK(empty.joinable() && !moved.joinable());
    }
    {
        // a stop wakes a condition_variable_any wait through a stop_callback
        std::mutex m;
        std::condition_variable_any cv;
        zxv::stop_source src;
        std::atomic<bool> woke{false};
        std::thread w([&] {
            auto tok = src.get_token();
            zxv::stop_callback wake(tok, [&] {
                std::lock_guard g(m);
                cv.notify_all();
            });
            std::unique_lock lk(m);
            cv.wait(lk, [&] { return tok.stop_requested(); });
            woke = true;
        });
        std::this_thread::sleep_for(std::chrono::milliseconds(20));
        src.request_stop();
        w.join();
        CHECK(woke);
    }
    std::cout << "     stop tokens: " << (ZXV_STOP_TOKEN_FALLBACK ? "bundled fallback" : "std") << "\n";
}

// ---------------------------------------------------------------- hexagram (ABI 1.3)
template <class E>
std::int64_t num(E e)
{
    return static_cast<std::int64_t>(e);
}

void test_hexagram()
{
    using zxv::HexAction, zxv::HexBase, zxv::HexPhase, zxv::HexPolarity, zxv::Hexagram;
    const auto& hx = g_vec["hex"];
    CHECK(zxv::has_feature(zxv::feature::hex));
    for (std::uint8_t i = 0; i < 4; ++i) {
        CHECK(zxv::to_string(static_cast<HexBase>(i)) == hx["base_names"][i].str());
        CHECK(zxv::to_string(static_cast<HexPolarity>(i)) == hx["polarity_names"][i].str());
        CHECK(zxv::to_string(static_cast<HexPhase>(i)) == hx["phase_names"][i].str());
        CHECK(num(zxv::next(static_cast<HexPhase>(i))) == hx["phase_next"][i].i64());
    }
    for (std::uint8_t i = 0; i < 5; ++i) CHECK(zxv::to_string(static_cast<HexAction>(i)) == hx["action_names"][i].str());
    const auto all = Hexagram::all();
    for (std::size_t i = 0; i < 64; ++i) {
        const auto& v = hx["tags"][i];
        const Hexagram h = Hexagram::from_tag(static_cast<std::uint8_t>(v["tag"].u64()));
        CHECK(h == all[i]);
        CHECK(h.name() == v["name"].str());
        CHECK(num(h.base()) == v["base"].i64() && num(h.polarity()) == v["polarity"].i64());
        CHECK(num(h.phase()) == v["phase"].i64() && num(h.action()) == v["action"].i64());
        CHECK(Hexagram(h.base(), h.polarity(), h.phase()) == h);
        CHECK(num((~h).tag()) == hx["not"][i].i64());
        for (std::size_t j = 0; j < 64; ++j) {
            CHECK(num((h & all[j]).tag()) == hx["and"][i][j].i64());
            CHECK(num((h | all[j]).tag()) == hx["or"][i][j].i64());
        }
        for (std::uint8_t p = 0; p < 4; ++p) {
            const std::int64_t want = hx["step"][i][p].i64();
            if (want < 0) {
                bool state = false;
                try {
                    (void) h.step(static_cast<HexPhase>(p));
                } catch (const zxv::RefusedError& e) {
                    state = e.status() == ZXV_E_STATE;
                }
                CHECK(state);
            } else {
                CHECK(num(h.step(static_cast<HexPhase>(p)).tag()) == want);
            }
        }
        for (std::uint8_t b = 0; b < 4; ++b) {
            for (std::uint8_t pol = 0; pol < 4; ++pol) {
                const std::int64_t want = hx["set"][i][b * 4u + pol].i64();
                if (want < 0)
                    CHECK_THROWS_AS(h.with_state(static_cast<HexBase>(b), static_cast<HexPolarity>(pol)),
                                    zxv::RefusedError);
                else
                    CHECK(num(h.with_state(static_cast<HexBase>(b), static_cast<HexPolarity>(pol)).tag()) == want);
            }
        }
        CHECK(h.forbid() == Hexagram(h.base(), HexPolarity::forbidden, h.phase()));
        CHECK(h.advance().phase() == zxv::next(h.phase()));
    }
    for (const auto& c : hx["polarity"].items)
        CHECK(num(Hexagram::polarity_from_evidence(
                  static_cast<std::uint32_t>(c["pos0"].u64()), static_cast<std::uint32_t>(c["neg0"].u64()),
                  static_cast<std::uint32_t>(c["pos1"].u64()), static_cast<std::uint32_t>(c["neg1"].u64()),
                  c["stale"].b)) == c["polarity"].i64());
    for (std::int32_t t = 0; t < 4; ++t) {
        const zxv::Truth tr = zxv::truth_from_c(t);
        const Hexagram h = Hexagram::from_truth(tr, HexPolarity::neutral, HexPhase::potential);
        CHECK(num(h.base()) == hx["lp_to_base"][static_cast<std::size_t>(t)].i64());
        CHECK(h.to_truth() == tr);
    }
    for (std::uint8_t b = 0; b < 4; ++b)
        CHECK(zxv::to_c(Hexagram(static_cast<HexBase>(b), HexPolarity::neutral, HexPhase::potential).to_truth()) ==
              hx["base_to_lp"][b].i64());
    // The numbering differs on purpose: Truth::true_ is 1, HexBase::true_ is 3.
    CHECK(zxv::to_c(zxv::Truth::true_) == 1 && num(HexBase::true_) == 3);

    for (std::uint8_t bad : {std::uint8_t{64}, std::uint8_t{100}, std::uint8_t{255}})
        CHECK_THROWS_AS(Hexagram::from_tag(bad), zxv::UsageError);
    CHECK_THROWS_AS(Hexagram(static_cast<HexBase>(4), HexPolarity::neutral, HexPhase::potential), zxv::UsageError);
    CHECK(Hexagram{}.name() == "false/entropic/potential");
    const Hexagram top(HexBase::true_, HexPolarity::negentropic, HexPhase::manifest);
    CHECK(top.tag() == 63);
    std::ostringstream os;
    os << top;
    CHECK(os.str() == "true/negentropic/manifest");
    Hexagram f(HexBase::true_, HexPolarity::forbidden, HexPhase::potential);
    CHECK(f.action() == HexAction::rollback);
    for (int k = 0; k < 4; ++k) {
        f = f.advance();
        CHECK(f.is_forbidden());
    }
    CHECK((f & top).is_forbidden() && (f | top).is_forbidden() && (~f).is_forbidden());
    CHECK_THROWS_AS(f.with_state(HexBase::true_, HexPolarity::neutral), zxv::RefusedError);
    CHECK(f.with_state(HexBase::false_, HexPolarity::forbidden).base() == HexBase::false_);
}

void test_quarantine_log()
{
    using zxv::Hexagram, zxv::QuarantineLog;
    const auto& q = g_vec["hex"]["qlog"];
    {
        zxv::Context ctx;
        const zxv::Capability cap = ctx.root_capability(zxv::caps::all);
        QuarantineLog log(cap, 3);
        CHECK(log.count() == 0);
        std::array<std::uint8_t, 32> prev{};
        CHECK(log.head() == prev);
        for (const auto& r : q["records"].items) {
            std::array<std::uint8_t, 32> subj{};
            const auto s = hex(r["subject"].str());
            std::copy(s.begin(), s.end(), subj.begin());
            const auto tag = Hexagram::from_tag(static_cast<std::uint8_t>(r["tag"].u64()));
            const std::uint64_t seq = log.append(tag, static_cast<std::uint8_t>(r["reason"].u64()),
                                                 static_cast<std::uint32_t>(r["submitter"].u64()), subj);
            CHECK(seq == r["seq"].u64());
            const auto rec = log.record(static_cast<std::uint32_t>(seq));
            const auto want = hex(r["hash"].str());
            CHECK(std::equal(want.begin(), want.end(), rec.hash.begin()));
            CHECK(rec.prev == prev && rec.subject == subj && rec.tag == tag);
            CHECK(rec.reason == r["reason"].u64() && rec.submitter == r["submitter"].u64());
            prev = rec.hash;
        }
        const auto head = hex(q["head"].str());
        const auto h = log.head();
        CHECK(std::equal(head.begin(), head.end(), h.begin()));
        CHECK(log.records().size() == 3 && log.records()[2].seq == 2);
        log.verify();
        bool full = false;
        try {
            (void) log.append(Hexagram::from_tag(0x3B), 0, 0, std::array<std::uint8_t, 32>{});
        } catch (const zxv::RefusedError& e) {
            full = e.status() == ZXV_E_CAPACITY;
        }
        CHECK(full);
    }
    {
        zxv::Context ctx;
        const zxv::Capability cap = ctx.root_capability(zxv::caps::all);
        const zxv::Capability no_attest = cap.attenuate(zxv::caps::all & ~zxv::caps::attest);
        CHECK_THROWS_AS(QuarantineLog(no_attest, 4), zxv::CapabilityDenied);
        CHECK_THROWS_AS(QuarantineLog(cap, 0), zxv::UsageError);
        CHECK_THROWS_AS(QuarantineLog(cap, QuarantineLog::max_records + 1), zxv::UsageError);
        QuarantineLog log(cap, 4);
        const std::array<std::uint8_t, 32> zero{};
        const Hexagram plain(zxv::HexBase::true_, zxv::HexPolarity::neutral, zxv::HexPhase::emerging);
        CHECK_THROWS_AS(log.append(plain, 1, 1, zero), zxv::UsageError);
        const Hexagram forb(zxv::HexBase::both, zxv::HexPolarity::forbidden, zxv::HexPhase::emerging);
        CHECK(log.append(forb, 9, 0xFFFFFFFFu, zero) == 0);
        CHECK(log.record(0).submitter == 0xFFFFFFFFu);
        ctx.halt("test");
        CHECK_THROWS_AS(log.count(), zxv::FatalError);
    }
}

} // namespace

int main(int argc, char** argv)
{
    g_dir = argc > 1 ? argv[1] : "../conformance";
    g_vec = mini_json::load(g_dir + "/vectors.json");
    const std::pair<const char*, void (*)()> tests[] = {
        {"library_and_status", test_library_and_status},
        {"rational", test_rational},
        {"truth", test_truth},
        {"caps_ledger_fatal", test_caps_ledger_fatal},
        {"fatal_abort_policy", test_fatal_abort_policy},
        {"attestations_budgets", test_attestations_budgets},
        {"phase", test_phase},
        {"wire", test_wire},
        {"swarm", test_swarm},
        {"hash_crypto", test_hash_crypto},
        {"engine", test_engine},
        {"lockstep", test_lockstep},
        {"evm", test_evm},
        {"cap_forms", test_cap_forms},
        {"cap_from_another_context", test_cap_from_another_context},
        {"deprecated_no_cap_forms", test_deprecated_no_cap_forms},
        {"stop_tokens", test_stop_tokens},
        {"hexagram", test_hexagram},
        {"quarantine_log", test_quarantine_log},
    };
    for (const auto& [name, fn] : tests) {
        const int before = g_failures;
        try {
            fn();
        } catch (const zxv::FatalError& e) {
            ++g_failures;
            std::cout << "FAIL " << name << ": unexpected FatalError " << e.what() << "\n";
        } catch (const std::exception& e) {
            ++g_failures;
            std::cout << "FAIL " << name << ": unexpected exception " << e.what() << "\n";
        }
        std::cout << (g_failures == before ? "ok   " : "FAIL ") << name << "\n";
    }
    std::cout << "test_zxv: " << g_checks << " checks, " << g_failures << " failures\n";
    return g_failures ? 1 : 0;
}
