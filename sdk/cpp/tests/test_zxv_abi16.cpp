// Copyright (c) 2024-2026 Michael Laurence Curzi and 36N9 Genetics, LLC
// SPDX-License-Identifier: Apache-2.0
//
// test_zxv_abi16.cpp — tests of the ABI 1.6 vino group of zxv.hpp (Vino
// Embed: the play lane, payment requests, receipts and the daily meter)
// against the real libzxv and the shared answer key
// sdk/conformance/vectors.json (status_abi16 and vino), plus handle
// lifetimes, the voucher-list buffer path, error mapping, capability
// refusals and a poisoned context. No network, no framework.
//
// Usage: test_zxv_abi16 <conformance dir>   (holds vectors.json)
#include <zxv.hpp>

#include "mini_json.hpp"

#include <iostream>
#include <optional>
#include <stdexcept>
#include <type_traits>

namespace vino = zxv::vino;

namespace {

int g_checks = 0, g_failures = 0;
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

/// The status `f` ends with: 0 when it returns, else the thrown status.
template <class F>
std::int32_t status_of(F&& f)
{
    try {
        f();
    } catch (const zxv::Exception& e) {
        return e.status();
    } catch (const zxv::FatalError& e) {
        return e.status();
    }
    return ZXV_OK;
}

/// CHECK that `expr` ends with the status named `name` ("ZXV_OK", "ZXV_E_...").
#define CHECK_ST(expr, name)                                                           \
    do {                                                                               \
        ++g_checks;                                                                    \
        const std::int32_t got_ = status_of([&] { (void) (expr); });                  \
        const std::string want_ = (name);                                              \
        if (zxv::status_name(got_) != want_) {                                         \
            ++g_failures;                                                              \
            std::cout << "FAIL " << __FILE__ << ":" << __LINE__ << ": " #expr " -> "   \
                      << zxv::status_name(got_) << ", want " << want_ << "\n";         \
        }                                                                              \
    } while (0)

using Bytes = std::vector<std::uint8_t>;
using V = mini_json::Value;

const V& vino_vec() { return g_vec["vino"]; }
Bytes hex(const std::string& h) { return zxv::from_hex(h); }
std::string hx(std::span<const std::uint8_t> b) { return zxv::to_hex(b); }
bool has(const V& v, const std::string& k) { return v.kind == V::object && v.fields.count(k) != 0; }
std::uint32_t u32(const V& v) { return static_cast<std::uint32_t>(v.u64()); }
std::int32_t i32(const V& v) { return static_cast<std::int32_t>(v.i64()); }
template <std::size_t N>
std::array<std::uint8_t, N> arr(const std::string& h)
{
    const Bytes b = hex(h);
    std::array<std::uint8_t, N> a{};
    if (b.size() == N) std::copy(b.begin(), b.end(), a.begin());
    return a;
}

vino::Request request_of(const V& r)
{
    vino::Request q;
    q.lane = static_cast<vino::Lane>(i32(r["lane"]));
    q.flags = u32(r["flags"]);
    q.merchant = arr<32>(r["merchant"].str());
    q.nonce = arr<16>(r["nonce"].str());
    q.amount = r["amount"].u64();
    q.created_at = r["created_at"].u64();
    q.expires_at = r["expires_at"].u64();
    q.memo = r["memo"].str();
    return q;
}

vino::RequestBytes good_request() { return arr<160>(vino_vec()["requests"][0]["bytes"].str()); }

// =====================================================================
// status_abi16, constants
// =====================================================================
void test_status_abi16()
{
    CHECK(zxv::version().minor >= 6);
    CHECK(zxv::has_feature2(zxv::feature2::vino));
    CHECK(zxv::feature2::vino == 0x100u);
    CHECK(zxv::caps::vino == (std::uint64_t{1} << 15));
    CHECK(zxv::caps::all_1_6 == (std::uint64_t{1} << 16) - 1);
    CHECK((zxv::caps::all_1_6 & zxv::caps::all_1_5) == zxv::caps::all_1_5);
    CHECK((zxv::caps::all_1_5 & zxv::caps::vino) == 0);
    CHECK(vino::request_bytes == 160 && vino::request_id_bytes == 32 && vino::receipt_message_bytes == 88);
    CHECK(vino::wallet_public_key_bytes == 2592 && vino::wallet_secret_key_bytes == 4896 &&
          vino::receipt_signature_bytes == 4627);
    CHECK(vino::amount_max == (std::uint64_t{1} << 59));
    // 1<<15 is now a known capability bit; 1<<16 is the first unknown one.
    zxv::Context ctx;
    CHECK_ST(ctx.root_capability(zxv::caps::vino), "ZXV_OK");
    CHECK_ST(ctx.root_capability(zxv::caps::all_1_6), "ZXV_OK");
    CHECK_ST(ctx.root_capability(zxv::caps::all_1_6 + 1), "ZXV_E_ARG");

    for (const auto& s : g_vec["status_abi16"].items) {
        const auto code = i32(s["code"]);
        CHECK(zxv::status_name(code) == s["name"].str());
        CHECK(static_cast<std::int64_t>(zxv::status_class(code)) == s["class"].i64());
        try {
            zxv::detail::raise(code, "probe");
        } catch (const zxv::UsageError& e) {
            CHECK(s["class"].i64() == 1 && e.status() == code && e.status_name() == s["name"].str());
        } catch (const zxv::RefusedError& e) {
            CHECK(s["class"].i64() == 2 && e.status() == code && e.detail() == "probe");
        } catch (...) {
            CHECK(false);
        }
    }
    // Each new status maps to its own exception type, under its class.
    CHECK_THROWS_AS(zxv::detail::check(ZXV_E_RAIL), zxv::RailMismatch);
    CHECK_THROWS_AS(zxv::detail::check(ZXV_E_RAIL), zxv::RefusedError);
    CHECK_THROWS_AS(zxv::detail::check(ZXV_E_COVERAGE), zxv::CoverageRefused);
    CHECK_THROWS_AS(zxv::detail::check(ZXV_E_COVERAGE), zxv::RefusedError);
    CHECK_THROWS_AS(zxv::detail::check(ZXV_E_LANE), zxv::LaneError);
    CHECK_THROWS_AS(zxv::detail::check(ZXV_E_LANE), zxv::UsageError);
    CHECK_THROWS_AS(zxv::detail::check(ZXV_E_EXPIRED), zxv::RequestExpired);
    CHECK_THROWS_AS(zxv::detail::check(ZXV_E_EXPIRED), zxv::RefusedError);
    CHECK_THROWS_AS(zxv::detail::check(ZXV_E_CHANCE), zxv::PaidChanceRefused);
    CHECK_THROWS_AS(zxv::detail::check(ZXV_E_CHANCE), zxv::RefusedError);
    // A reserved code in the vino block is not one of them.
    CHECK(status_of([] { zxv::detail::check(-51); }) == -51);
    bool reserved_is_specific = false;
    try {
        zxv::detail::check(-51);
    } catch (const zxv::RailMismatch&) {
        reserved_is_specific = true;
    } catch (const zxv::PaidChanceRefused&) {
        reserved_is_specific = true;
    } catch (...) {
    }
    CHECK(!reserved_is_specific);
}

// =====================================================================
// payment requests
// =====================================================================
void test_requests()
{
    int ok = 0;
    for (const auto& r : vino_vec()["requests"].items) {
        const vino::Request q = request_of(r);
        const std::string want = r["status"].str();
        CHECK_ST(q.encode(), want);
        if (want != "ZXV_OK") continue;
        ++ok;
        const auto e = q.encode();
        CHECK(hx(e.bytes) == r["bytes"].str());
        CHECK(hx(e.id) == r["id"].str());
        CHECK(q.bytes() == e.bytes && q.id() == e.id);
        // The id is SHA-256 of the bytes (checked against the C hash).
        // Round trip: decode gives back the same value and id.
        const auto [back, id] = vino::Request::decode_with_id(e.bytes);
        CHECK(back == q);
        CHECK(id == e.id);
        CHECK(vino::Request::decode(e.bytes).memo == r["memo"].str());
    }
    CHECK(ok >= 6);

    // A memo with a NUL byte would be silently cut by C: refused up front.
    vino::Request nul = request_of(vino_vec()["requests"][0]);
    nul.memo = std::string("ab\0cd", 5);
    CHECK_ST(nul.encode(), "ZXV_E_ARG");
    // The exception types a developer would catch.
    vino::Request chance = request_of(vino_vec()["requests"][0]);
    chance.flags = vino::flag::chance;
    CHECK_THROWS_AS(chance.encode(), zxv::PaidChanceRefused);
    chance.lane = vino::Lane::play; // a chance outcome is fine on the play lane
    CHECK_ST(chance.encode(), "ZXV_OK");
    vino::Request bad_lane = request_of(vino_vec()["requests"][0]);
    bad_lane.lane = static_cast<vino::Lane>(3);
    CHECK_THROWS_AS(bad_lane.encode(), zxv::LaneError);

    // Decode refusals: the good request with one byte changed.
    const auto good = good_request();
    for (const auto& d : vino_vec()["decode"].items) {
        auto b = good;
        b[static_cast<std::size_t>(d["offset"].u64())] = static_cast<std::uint8_t>(d["value"].u64());
        CHECK_ST(vino::Request::decode(b), d["status"].str());
    }
    // Wrong lengths never reach C.
    CHECK_ST(vino::Request::decode(Bytes(159)), "ZXV_E_ARG");
    CHECK_ST(vino::Request::decode(Bytes(161)), "ZXV_E_ARG");
    CHECK_ST(vino::Request::decode(Bytes{}), "ZXV_E_ARG");
}

// =====================================================================
// receipts
// =====================================================================
void test_receipts()
{
    const V& rc = vino_vec()["receipt"];
    zxv::Context ctx;
    const auto cap = ctx.root_capability(zxv::caps::crypto_sign);
    zxv::MlDsa87 dsa(cap);
    const auto kp = dsa.keygen_seed(hex(rc["seed"].str()));
    CHECK(hx(zxv::Hashes::sha3_256(kp.public_key)) == rc["pk_sha3_256"].str());

    const auto req = good_request();
    const auto id = vino::Request::decode_with_id(req).second;
    const std::uint64_t approved = rc["approved_at"].u64();
    const auto msg = vino::receipt_message(id, kp.public_key, approved);
    CHECK(hx(msg) == rc["msg"].str());
    const auto sig = dsa.sign_deterministic(kp.secret_key, msg);
    CHECK(hx(sig) == rc["sig"].str());
    const Bytes vsig = hex(rc["sig"].str());

    for (const auto& c : rc["checks"].items) {
        Bytes s = vsig;
        const auto flip = c["flip_sig"].i64();
        if (flip >= 0) s[static_cast<std::size_t>(flip)] ^= 1;
        const auto at = c["approved_at"].u64(), now = c["now"].u64();
        CHECK_ST(vino::verify_receipt(req, kp.public_key, at, s, now), c["status"].str());
        CHECK(vino::receipt_is_valid(req, kp.public_key, at, s, now) == (c["status"].str() == "ZXV_OK"));
    }

    // The wallet's own (hedged) signature verifies offline too.
    const auto mine = vino::sign_receipt(cap, req, kp.secret_key, kp.public_key, approved);
    CHECK(mine.size() == vino::receipt_signature_bytes);
    CHECK_ST(vino::verify_receipt(req, kp.public_key, approved, mine, approved), "ZXV_OK");
    // Another wallet's key does not verify it.
    const auto other = dsa.keygen_seed(Bytes(32, 0x32));
    CHECK(!vino::receipt_is_valid(req, other.public_key, approved, mine, approved));
    // Signing outside the window is refused.
    CHECK_THROWS_AS(vino::sign_receipt(cap, req, kp.secret_key, kp.public_key, 1700000601), zxv::RequestExpired);
    CHECK_ST(vino::sign_receipt(cap, req, kp.secret_key, kp.public_key, 1699999999), "ZXV_E_EXPIRED");
    // A damaged request is refused with the decode error, both sides.
    auto bad = req;
    bad[0] = 'Y';
    CHECK_ST(vino::sign_receipt(cap, bad, kp.secret_key, kp.public_key, approved), "ZXV_E_FORMAT");
    CHECK_THROWS_AS((void) vino::receipt_is_valid(bad, kp.public_key, approved, vsig, approved), zxv::UsageError);
    // Signing needs CRYPTO_SIGN; a vino-only capability is refused.
    const auto vcap = ctx.root_capability(zxv::caps::vino);
    CHECK_THROWS_AS(vino::sign_receipt(vcap, req, kp.secret_key, kp.public_key, approved), zxv::CapabilityDenied);
    // Wrong-size buffers never reach C.
    CHECK_ST(vino::receipt_message(id, Bytes(2591), approved), "ZXV_E_ARG");
    CHECK_ST(vino::verify_receipt(req, kp.public_key, approved, Bytes(4626), approved), "ZXV_E_ARG");
    CHECK_ST(vino::verify_receipt(Bytes(100), kp.public_key, approved, vsig, approved), "ZXV_E_ARG");
    CHECK_ST(vino::sign_receipt(cap, req, zxv::SecretKey(10), kp.public_key, approved), "ZXV_E_ARG");
}

// =====================================================================
// the play scenario
// =====================================================================
void test_play()
{
    zxv::Context ctx;
    const auto cap = ctx.root_capability(zxv::caps::vino);
    vino::Play play(cap);
    const V& pv = vino_vec()["play"];
    for (const auto& s : pv["steps"].items) {
        const std::string op = s["op"].str(), want = s["status"].str();
        if (op == "open") {
            std::uint32_t got = 0;
            CHECK_ST(got = play.open(u32(s["owner"]), static_cast<vino::AccountKind>(i32(s["is_issuer"]))), want);
            if (want == "ZXV_OK") CHECK(got == u32(s["account"]));
        } else if (op == "back") {
            CHECK_ST(play.back(u32(s["issuer"]), s["amount"].u64()), want);
        } else if (op == "unback") {
            CHECK_ST(play.unback(u32(s["issuer"]), s["amount"].u64()), want);
        } else if (op == "issue") {
            std::uint64_t got = 0;
            CHECK_ST(got = play.issue(u32(s["issuer"]), u32(s["holder"]), s["face"].u64(),
                                      static_cast<vino::Rail>(i32(s["rail"]))),
                     want);
            if (want == "ZXV_OK") CHECK(got == s["id"].u64());
        } else if (op == "transfer") {
            CHECK_ST(play.transfer(s["voucher"].u64(), u32(s["from"]), u32(s["to"]),
                                   static_cast<vino::Rail>(i32(s["rail"]))),
                     want);
        } else if (op == "set_rail") {
            CHECK_ST(play.set_rail(s["voucher"].u64(), u32(s["holder"]), static_cast<vino::Rail>(i32(s["rail"]))),
                     want);
        } else if (op == "redeem") {
            CHECK_ST(play.redeem(s["voucher"].u64(), u32(s["holder"])), want);
        } else {
            std::cout << "unknown play op " << op << "\n";
            CHECK(false);
        }
    }
    for (const auto& a : pv["accounts"].items) {
        const auto info = play.account(u32(a["account"]));
        CHECK(info.owner == u32(a["owner"]));
        CHECK(info.is_issuer() == (a["is_issuer"].i64() == 1));
        CHECK(info.backing == a["backing"].u64());
        CHECK(info.faces == a["faces"].u64());
        CHECK(info.count == u32(a["count"]));
        std::vector<std::uint64_t> want;
        for (const auto& v : a["vouchers"].items) want.push_back(v.u64());
        CHECK(play.vouchers(u32(a["account"])) == want);
    }
    for (const auto& v : pv["vouchers"].items) {
        const auto info = play.voucher(v["id"].u64());
        CHECK(info.face == v["face"].u64());
        CHECK(info.issuer == u32(v["issuer"]));
        CHECK(info.holder == u32(v["holder"]));
        CHECK(static_cast<std::int32_t>(info.rail) == i32(v["rail"]));
        CHECK(static_cast<std::int32_t>(info.state) == i32(v["state"]));
        CHECK(info.is_active() == (i32(v["state"]) == 1));
    }
    play.check();
    CHECK_ST(play.voucher(999), "ZXV_E_NOT_FOUND");
    CHECK_ST(play.account(999), "ZXV_E_NOT_FOUND");
    CHECK_ST(play.vouchers(999), "ZXV_E_NOT_FOUND");

    // The exception types a developer would catch, on a fresh sandbox.
    vino::Play p2(cap);
    const auto shop = p2.open_issuer(1);
    const auto alice = p2.open_holder(2);
    const auto bob = p2.open_holder(3);
    CHECK(p2.account(shop).kind == vino::AccountKind::issuer);
    CHECK(p2.account(alice).kind == vino::AccountKind::holder);
    p2.back(shop, 18);
    CHECK_THROWS_AS(p2.issue(shop, alice, 13), zxv::CoverageRefused); // 10*18 < 18*13
    const auto v = p2.issue(shop, alice, 8);
    CHECK(p2.voucher(v).rail == vino::Rail::bearer); // default rail
    CHECK_THROWS_AS(p2.issue(shop, alice, 4), zxv::DenominationError);
    CHECK_THROWS_AS(p2.transfer(v, alice, bob, vino::Rail::ghost), zxv::RailMismatch);
    CHECK_THROWS_AS(p2.set_rail(v, alice, vino::Rail::bearer), zxv::RailMismatch);
    CHECK_THROWS_AS(p2.unback(shop, 18), zxv::CoverageRefused);
    CHECK_THROWS_AS(p2.unback(shop, 19), zxv::InsufficientFunds);
    p2.redeem(v, alice);
    CHECK_THROWS_AS(p2.redeem(v, alice), zxv::RefusedError);
    CHECK(p2.account(shop).faces == 0 && p2.account(shop).backing == 18);
    p2.unback(shop, 18);
}

// =====================================================================
// the meter scenario
// =====================================================================
void test_meter()
{
    zxv::Context ctx;
    const auto cap = ctx.root_capability(zxv::caps::vino);
    const V& mv = vino_vec()["meter"];
    vino::SpendMeter m(cap, mv["daily_cap"].u64());
    vino::SpendMeter m2(cap, mv["daily_cap"].u64()); // the same steps through admit()
    for (const auto& s : mv["steps"].items) {
        const std::string want = s["status"].str();
        const auto day = s["day"].u64(), amount = s["amount"].u64();
        vino::MeterDecision d;
        CHECK_ST(d = m.try_admit(day, amount), want == "ZXV_E_LIMIT" ? "ZXV_OK" : want);
        std::uint64_t left = 0;
        CHECK_ST(left = m2.admit(day, amount), want);
        if (want == "ZXV_OK") {
            CHECK(d.admitted && d.left == s["left"].u64() && left == s["left"].u64());
        } else if (want == "ZXV_E_LIMIT") {
            CHECK(!d.admitted && d.left == s["left"].u64());
        }
    }
    CHECK_THROWS_AS(m2.admit(100, 101), zxv::LimitRefused);
    CHECK_ST(vino::SpendMeter(cap, 0), "ZXV_E_RANGE");
    CHECK_ST(vino::SpendMeter(cap, vino::amount_max + 1), "ZXV_E_RANGE");
    CHECK_ST(vino::SpendMeter(cap, vino::amount_max), "ZXV_OK");
}

// =====================================================================
// lifetimes, buffers, capabilities, poisoned context
// =====================================================================
void test_lifetimes()
{
    zxv::Context ctx;
    const auto cap = ctx.root_capability(zxv::caps::vino);
    vino::Play p(cap);
    const auto i = p.open_issuer(1);
    vino::Play p2 = std::move(p);
    // Use after release (move) is refused before reaching C.
    CHECK_THROWS_AS(p.open_holder(2), zxv::UsageError);
    CHECK_ST(p.open_holder(2), "ZXV_E_ARG");
    CHECK_ST(p.check(), "ZXV_E_ARG");
    CHECK_ST(p.vouchers(0), "ZXV_E_ARG");
    CHECK(p.native_handle() == nullptr);
    CHECK(p2.account(i).is_issuer());
    vino::SpendMeter m(cap, 10);
    vino::SpendMeter m2 = std::move(m);
    CHECK_ST(m.admit(1, 1), "ZXV_E_ARG");
    CHECK_ST(m.try_admit(1, 1), "ZXV_E_ARG");
    CHECK(m2.admit(1, 1) == 9);
    // Handles outlive the Context and Capability objects that made them.
    std::optional<vino::Play> keep;
    std::optional<vino::SpendMeter> keep_m;
    {
        zxv::Context c2;
        auto k2 = c2.root_capability(zxv::caps::vino);
        keep.emplace(k2);
        keep_m.emplace(k2, 5);
    }
    const auto a = keep->open_issuer(7);
    keep->back(a, 100);
    CHECK(keep->account(a).backing == 100);
    CHECK(keep_m->admit(1, 5) == 0);
    keep.reset(); // destroyed after its context object, safely
    keep_m.reset();
    // A moved-from capability cannot create a handle.
    auto cap2 = ctx.root_capability(zxv::caps::vino);
    auto cap3 = std::move(cap2);
    CHECK_THROWS_AS(vino::Play{cap2}, zxv::UsageError);
    CHECK_THROWS_AS(vino::SpendMeter(cap2, 1), zxv::UsageError);
    CHECK_THROWS_AS(vino::sign_receipt(cap2, good_request(), zxv::SecretKey(vino::wallet_secret_key_bytes),
                                       Bytes(vino::wallet_public_key_bytes), 1),
                    zxv::UsageError);
    (void) cap3;
    static_assert(!std::is_copy_constructible_v<vino::Play> && std::is_move_constructible_v<vino::Play>);
    static_assert(!std::is_copy_constructible_v<vino::SpendMeter> && std::is_move_constructible_v<vino::SpendMeter>);
    // A request is a plain value.
    static_assert(std::is_copy_constructible_v<vino::Request>);
}

void test_voucher_buffer()
{
    // vouchers() hides the two-call pattern: an empty list, one id, and a
    // list long enough to need a second call at any guess.
    zxv::Context ctx;
    const auto cap = ctx.root_capability(zxv::caps::vino);
    vino::Play p(cap);
    const auto shop = p.open_issuer(1);
    const auto alice = p.open_holder(2);
    const auto bob = p.open_holder(3);
    CHECK(p.vouchers(alice).empty());
    CHECK(p.vouchers(shop).empty());
    p.back(shop, 1000000);
    std::vector<std::uint64_t> want;
    for (int k = 0; k < 600; ++k) want.push_back(p.issue(shop, alice, 1, k % 2 ? vino::Rail::ghost : vino::Rail::bearer));
    CHECK(p.vouchers(alice) == want);
    CHECK(p.vouchers(shop) == want);
    CHECK(p.account(alice).count == 600 && p.account(alice).faces == 600);
    // Moving some to bob keeps both lists ascending and exact.
    std::vector<std::uint64_t> a_want, b_want;
    for (std::size_t k = 0; k < want.size(); ++k) {
        if (k % 3 == 0) {
            p.transfer(want[k], alice, bob, k % 2 ? vino::Rail::ghost : vino::Rail::bearer);
            b_want.push_back(want[k]);
        } else {
            a_want.push_back(want[k]);
        }
    }
    CHECK(p.vouchers(alice) == a_want);
    CHECK(p.vouchers(bob) == b_want);
    // Redeemed vouchers leave the lists but stay readable by id.
    p.redeem(b_want.front(), bob);
    b_want.erase(b_want.begin());
    CHECK(p.vouchers(bob) == b_want);
    CHECK(p.voucher(want.front()).state == vino::VoucherState::redeemed);
    // The C two-call contract the wrapper relies on: a short buffer gets
    // ZXV_E_BUFFER and the count, and is left untouched.
    std::size_t n = 0;
    std::uint64_t one = 77;
    CHECK(zxv_vino_vouchers(cap.native_handle(), p.native_handle(), alice, &one, 1, &n) == ZXV_E_BUFFER);
    CHECK(n == a_want.size() && one == 77);
    p.check();
}

void test_capability_refusals()
{
    zxv::Context ctx;
    // A capability without VINO gets the capability error.
    const auto weak = ctx.root_capability(zxv::caps::all_1_5);
    CHECK_THROWS_AS(vino::Play{weak}, zxv::CapabilityDenied);
    CHECK_ST(vino::Play{weak}, "ZXV_E_CAP");
    CHECK_THROWS_AS(vino::SpendMeter(weak, 10), zxv::CapabilityDenied);
    // A handle keeps its capability: revoking it refuses later calls.
    auto vcap = ctx.root_capability(zxv::caps::vino);
    const auto child = vcap.attenuate(zxv::caps::vino);
    vino::Play p(child);
    vino::SpendMeter m(child, 10);
    const auto a = p.open_issuer(1);
    vcap.revoke();
    CHECK_ST(p.open_holder(2), "ZXV_E_CAP");
    CHECK_ST(p.account(a), "ZXV_E_CAP");
    CHECK_ST(p.vouchers(a), "ZXV_E_CAP");
    CHECK_ST(p.check(), "ZXV_E_CAP");
    CHECK_ST(m.admit(1, 1), "ZXV_E_CAP");
    CHECK_THROWS_AS(m.try_admit(1, 1), zxv::CapabilityDenied);
    // An unattested capability is refused too.
    auto att = ctx.root_capability(zxv::caps::vino);
    att.set_attestation(zxv::Truth::neither);
    CHECK_ST(vino::Play{att}, "ZXV_E_CAP");
    // The pure calls need no capability at all.
    CHECK_ST(vino::Request::decode(good_request()), "ZXV_OK");
}

void test_poisoned_context()
{
    zxv::Context ctx;
    const auto cap = ctx.root_capability(zxv::caps::all_1_6);
    vino::Play p(cap);
    vino::SpendMeter m(cap, 100);
    const auto shop = p.open_issuer(1);
    const auto alice = p.open_holder(2);
    p.back(shop, 100);
    const auto v = p.issue(shop, alice, 5);
    ctx.halt("test");
    CHECK(ctx.is_poisoned());
    // Every play and meter call on a halted context fails closed.
    CHECK_THROWS_AS(p.open_holder(3), zxv::FatalError);
    CHECK_ST(p.open_holder(3), "ZXV_E_POISONED");
    CHECK_ST(p.back(shop, 1), "ZXV_E_POISONED");
    CHECK_ST(p.issue(shop, alice, 1), "ZXV_E_POISONED");
    CHECK_ST(p.transfer(v, alice, alice, vino::Rail::bearer), "ZXV_E_POISONED");
    CHECK_ST(p.redeem(v, alice), "ZXV_E_POISONED");
    CHECK_ST(p.voucher(v), "ZXV_E_POISONED");
    CHECK_ST(p.account(shop), "ZXV_E_POISONED");
    CHECK_ST(p.vouchers(shop), "ZXV_E_POISONED");
    CHECK_ST(p.check(), "ZXV_E_POISONED");
    CHECK_ST(m.admit(1, 1), "ZXV_E_POISONED");
    CHECK_ST(m.try_admit(1, 1), "ZXV_E_POISONED");
    CHECK_ST(vino::Play{cap}, "ZXV_E_POISONED");
    CHECK_ST(vino::SpendMeter(cap, 1), "ZXV_E_POISONED");
    // The poisoned status is FATAL: not swallowed by catch (std::exception&).
    bool swallowed = false;
    try {
        try {
            p.check();
        } catch (const std::exception&) {
            swallowed = true;
        }
    } catch (const zxv::FatalError&) {
    }
    CHECK(!swallowed);
    // Pure functions are unaffected.
    CHECK_ST(vino::Request::decode(good_request()), "ZXV_OK");
}

} // namespace

int main(int argc, char** argv)
{
    const std::string dir = argc > 1 ? argv[1] : "../conformance";
    g_vec = mini_json::load(dir + "/vectors.json");
    if (!has(g_vec, "vino") || !has(g_vec, "status_abi16")) {
        std::cout << "vectors.json has no vino section\n";
        return 1;
    }
    const std::pair<const char*, void (*)()> tests[] = {
        {"status_abi16", test_status_abi16},
        {"requests", test_requests},
        {"receipts", test_receipts},
        {"play", test_play},
        {"meter", test_meter},
        {"lifetimes", test_lifetimes},
        {"voucher_buffer", test_voucher_buffer},
        {"capability_refusals", test_capability_refusals},
        {"poisoned_context", test_poisoned_context},
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
    std::cout << "test_zxv_abi16: " << g_checks << " checks, " << g_failures << " failures\n";
    return g_failures ? 1 : 0;
}
