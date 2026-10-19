// Copyright (c) 2024-2026 Michael Laurence Curzi and 36N9 Genetics, LLC
// SPDX-License-Identifier: Apache-2.0
//
// test_zxv_abi15.cpp — tests of the ABI 1.5 groups of zxv.hpp (treaty, cid,
// aipi, coin/token, fee, vhub, freight, web4, paradox) against the real
// libzxv and the shared answer key sdk/conformance/vectors.json, plus handle
// lifetimes, the buffer-growth path, error mapping, capability refusals and
// a poisoned context. No network, no framework.
//
// Usage: test_zxv_abi15 <conformance dir>   (holds vectors.json)
#include <zxv.hpp>

#include "mini_json.hpp"

#include <iostream>
#include <map>
#include <stdexcept>

namespace cid = zxv::cid;

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

const V& J(const char* k) { return g_vec[k]; }
Bytes hex(const std::string& h) { return zxv::from_hex(h); }
std::string hx(std::span<const std::uint8_t> b) { return zxv::to_hex(b); }
Bytes utf8(const std::string& s) { return Bytes(s.begin(), s.end()); }
std::span<const std::uint8_t> sv(std::string_view s) { return zxv::as_bytes(s); }
Bytes ramp(std::size_t n)
{
    Bytes b(n);
    for (std::size_t i = 0; i < n; ++i) b[i] = static_cast<std::uint8_t>(i & 255u);
    return b;
}
bool has(const V& v, const std::string& k) { return v.kind == V::object && v.fields.count(k) != 0; }
bool is_null(const V& v) { return v.kind == V::null; }
std::uint8_t u8(const V& v) { return static_cast<std::uint8_t>(v.u64()); }
std::uint32_t u32(const V& v) { return static_cast<std::uint32_t>(v.u64()); }
std::int32_t i32(const V& v) { return static_cast<std::int32_t>(v.i64()); }
zxv::Digest32 fill32(std::uint8_t b)
{
    zxv::Digest32 d{};
    d.fill(b);
    return d;
}
zxv::Digest32 d32(const std::string& h)
{
    const Bytes b = hex(h);
    zxv::Digest32 d{};
    std::copy(b.begin(), b.end(), d.begin());
    return d;
}

// =====================================================================
// status_abi15, features2, caps
// =====================================================================
void test_status_abi15()
{
    CHECK(zxv::version().minor >= 5);
    CHECK(zxv::header_abi_minor == 5);
    zxv::ensure_compatible();
    const std::uint32_t all2 = zxv::feature2::treaty | zxv::feature2::aipi | zxv::feature2::coin |
                               zxv::feature2::fee | zxv::feature2::vhub | zxv::feature2::freight |
                               zxv::feature2::web4 | zxv::feature2::paradox;
    CHECK((zxv::features2() & all2) == all2);
    CHECK(zxv::has_feature2(zxv::feature2::paradox));
    CHECK((zxv::caps::all_1_5 & (zxv::caps::aipi | zxv::caps::mint | zxv::caps::paradox)) ==
          (zxv::caps::aipi | zxv::caps::mint | zxv::caps::paradox));
    CHECK((zxv::caps::all_1_5 & zxv::caps::all_1_4) == zxv::caps::all_1_4);

    for (const auto& s : J("status_abi15").items) {
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
    // Each new status has its own exception type, under its class.
    CHECK_THROWS_AS(zxv::detail::raise(ZXV_E_DENOM, ""), zxv::DenominationError);
    CHECK_THROWS_AS(zxv::detail::raise(ZXV_E_TREATY, ""), zxv::TreatyError);
    CHECK_THROWS_AS(zxv::detail::raise(ZXV_E_CID, ""), zxv::CidMismatch);
    CHECK_THROWS_AS(zxv::detail::raise(ZXV_E_CONSENT, ""), zxv::ConsentDenied);
    CHECK_THROWS_AS(zxv::detail::raise(ZXV_E_ERASURE, ""), zxv::ErasureError);
    CHECK_THROWS_AS(zxv::detail::raise(ZXV_E_PEG, ""), zxv::PegError);
    CHECK_THROWS_AS(zxv::detail::raise(ZXV_E_LIMIT, ""), zxv::LimitRefused);
    CHECK_THROWS_AS(zxv::detail::raise(ZXV_E_PARADOX_STEP, ""), zxv::ParadoxStepLimit);
    CHECK_THROWS_AS(zxv::detail::raise(ZXV_E_PARADOX_OWNER, ""), zxv::OwnerApprovalRequired);
    static_assert(std::is_base_of_v<zxv::UsageError, zxv::DenominationError>);
    static_assert(std::is_base_of_v<zxv::UsageError, zxv::TreatyError>);
    static_assert(std::is_base_of_v<zxv::RefusedError, zxv::CidMismatch>);
    static_assert(std::is_base_of_v<zxv::RefusedError, zxv::ConsentDenied>);
    static_assert(std::is_base_of_v<zxv::RefusedError, zxv::ErasureError>);
    static_assert(std::is_base_of_v<zxv::RefusedError, zxv::PegError>);
    static_assert(std::is_base_of_v<zxv::RefusedError, zxv::LimitRefused>);
    static_assert(std::is_base_of_v<zxv::RefusedError, zxv::ParadoxStepLimit>);
    static_assert(std::is_base_of_v<zxv::RefusedError, zxv::OwnerApprovalRequired>);
    // The mapping is reached through real calls too.
    CHECK_THROWS_AS(zxv::coin::rung(4), zxv::DenominationError);
    CHECK_THROWS_AS(zxv::treaty::Treaty::from_sutra("TREATY"), zxv::TreatyError);
    CHECK_THROWS_AS(zxv::cid::Cid::parse("z"), zxv::CidMismatch);
}

// =====================================================================
// treaty
// =====================================================================
void test_treaty()
{
    namespace tr = zxv::treaty;
    const V& t = J("treaty");
    CHECK(tr::record_bytes == static_cast<std::size_t>(t["rec_len"].u64()));
    CHECK(tr::result_record_bytes == static_cast<std::size_t>(t["result_len"].u64()));
    for (const auto& c : t["sutra"].items) {
        auto a = tr::Treaty::from_sutra(c["sutra"].str());
        CHECK(a.is_valid());
        CHECK(hx(a.record()) == c["record"].str());
        CHECK(a.sutra() == c["reemit"].str());
        CHECK(a.name() == c["name"].str());
        CHECK(a.asset() == c["asset"].str());
        CHECK(static_cast<std::int64_t>(a.op()) == c["op"].i64());
        CHECK(a.party_count() == u32(c["party_count"]));
        CHECK(a.obligation_count() == u32(c["owe_count"]));
        const auto ps = a.parties();
        CHECK(ps.size() == c["parties"].items.size());
        for (std::size_t i = 0; i < ps.size() && i < c["parties"].items.size(); ++i) {
            const V& p = c["parties"][i];
            CHECK(ps[i].id == p["id"].str());
            CHECK(ps[i].deposit == p["deposit"].i64());
            CHECK(ps[i].claim == p["claim"].i64());
            CHECK(ps[i].weight_num == u32(p["weight"][0]) && ps[i].weight_den == u32(p["weight"][1]));
        }
        const auto os = a.obligations();
        CHECK(os.size() == c["owes"].items.size());
        for (std::size_t i = 0; i < os.size() && i < c["owes"].items.size(); ++i) {
            const V& o = c["owes"][i];
            CHECK(os[i].from == u32(o["from"]) && os[i].to == u32(o["to"]) && os[i].amount == o["amount"].i64());
        }
        auto b = tr::Treaty::from_record(hex(c["record"].str()));
        CHECK((a == b) == c["record_parses_equal"].b);
        CHECK(b.sutra() == c["reemit"].str());
    }
    for (const auto& c : t["reject"].items) {
        CHECK_ST(tr::Treaty::from_sutra(c["sutra"].str()), c["status"].str());
        try {
            (void) tr::Treaty::from_sutra(c["sutra"].str());
            CHECK(false);
        } catch (const zxv::TreatyError& e) {
            CHECK(e.fault_code() == i32(c["fault"]));
            CHECK(e.offset() == u32(c["offset"]));
            CHECK(static_cast<std::int32_t>(tr::fault_of(e)) == i32(c["fault"]));
        }
    }
    const Bytes base = hex(t["sutra"][0]["record"].str());
    for (const auto& c : t["reject_record"].items) {
        Bytes r = base;
        r[static_cast<std::size_t>(c["patch_offset"].u64())] = hex(c["patch_byte"].str())[0];
        CHECK_ST(tr::Treaty::from_record(r), c["status"].str());
        try {
            (void) tr::Treaty::from_record(r);
        } catch (const zxv::TreatyError& e) {
            CHECK(e.fault_code() == i32(c["fault"]) && e.offset() == u32(c["offset"]));
        }
        // A rejected parse leaves the handle empty but usable.
        tr::Treaty x;
        CHECK_THROWS_AS(x.parse_record(r), zxv::TreatyError);
        CHECK(x.fault() != tr::Fault::none || !x.is_valid());
    }
    for (const auto& c : t["pic"].items) {
        const std::string op = c["op"].str();
        const std::int64_t a = c["a"].i64(), b = c["b"].i64();
        if (op == "div") {
            CHECK_ST(tr::pic_div(a, b), c["status"].str());
            if (c["status"].str() == "ZXV_OK") {
                const auto d = tr::pic_div(a, b);
                CHECK(d.quotient == c["out"].i64() && d.remainder == c["rem"].i64());
            }
            continue;
        }
        auto f = op == "add" ? &tr::pic_add : op == "sub" ? &tr::pic_sub : &tr::pic_mul;
        CHECK_ST(f(a, b), c["status"].str());
        if (c["status"].str() == "ZXV_OK") CHECK(f(a, b) == c["out"].i64());
    }
    for (const auto& c : t["run"].items) {
        const auto tt = tr::Treaty::from_sutra(c["sutra"].str());
        CHECK_ST(tt.run(), c["status"].str());
        if (c["status"].str() != "ZXV_OK") continue;
        const auto r = tt.run();
        CHECK(r.party_count() == u32(c["parties"]));
        CHECK(r.leg_count() == u32(c["legs"]));
        CHECK(r.fees() == c["fees"].i64());
        if (has(c, "party")) {
            const auto ps = r.parties();
            CHECK(ps.size() == c["party"].items.size());
            for (std::size_t i = 0; i < ps.size() && i < c["party"].items.size(); ++i) {
                const V& p = c["party"][i];
                CHECK(ps[i].net == p["net"].i64() && ps[i].balance == p["balance"].i64() &&
                      ps[i].dividend == p["dividend"].i64());
            }
        }
        if (has(c, "leg")) {
            const auto ls = r.legs();
            CHECK(ls.size() == c["leg"].items.size());
            for (std::size_t i = 0; i < ls.size() && i < c["leg"].items.size(); ++i) {
                const V& l = c["leg"][i];
                CHECK(ls[i].from == u32(l["from"]) && ls[i].to == u32(l["to"]) && ls[i].amount == l["amount"].i64());
            }
        }
        CHECK(hx(r.record()) == c["result_record"].str());
        CHECK(r.record().size() == tr::result_record_bytes);
    }
}

// =====================================================================
// cid
// =====================================================================
void test_cid()
{
    const V& c = J("cid");
    for (const auto& e : c["compute"].items) {
        const auto x = cid::Cid::compute(utf8(e["utf8"].str()), u32(e["codec"]));
        CHECK(hx(x.bytes()) == e["cid"].str());
        CHECK(x.to_text() == e["text_default"].str());
        CHECK(x.to_text(cid::Base::base58btc) == e["text_58btc"].str());
        const auto i = x.info();
        CHECK(i.version == u32(e["version"]) && i.hash == u32(e["hash"]) && i.codec == u32(e["codec"]));
        CHECK(hx(i.digest) == e["digest"].str());
        CHECK(cid::Cid::parse(e["text_default"].str()) == x);
        CHECK(cid::Cid::parse(e["text_58btc"].str()) == x);
        CHECK(x.matches(utf8(e["utf8"].str())));
    }
    for (const auto& e : c["unixfs"].items) {
        const Bytes in = has(e, "ramp") ? ramp(static_cast<std::size_t>(e["len"].u64())) : utf8(e["utf8"].str());
        const auto x = cid::Cid::unixfs(in);
        CHECK(hx(x.bytes()) == e["cid"].str());
        CHECK(x.to_text() == e["text_default"].str());
        if (has(e, "codec")) CHECK(x.info().codec == u32(e["codec"]));
    }
    for (const auto& e : c["text"].items) {
        const auto x = cid::Cid::parse(e["text"].str());
        CHECK(hx(x.bytes()) == e["cid"].str());
        CHECK(x.to_text() == e["text_default"].str());
        const std::string t58 = e["text_58btc"].str();
        if (t58.rfind("ZXV_E_", 0) == 0) CHECK_ST(x.to_text(cid::Base::base58btc), t58);
        else CHECK(x.to_text(cid::Base::base58btc) == t58);
        const auto i = x.info();
        CHECK(i.version == u32(e["version"]) && i.codec == u32(e["codec"]) && i.hash == u32(e["hash"]));
        CHECK(hx(i.digest) == e["digest"].str());
        CHECK(hx(x.to_v1().bytes()) == e["v1"].str());
        CHECK(x.to_v1().to_text() == e["v1_text"].str());
        CHECK(hx(zxv::aipi::provider_key(x)) == e["provider_key"].str());
        std::ostringstream os;
        os << x;
        CHECK(os.str() == e["text_default"].str());
    }
    for (const auto& e : c["text_reject"].items) CHECK_ST(cid::Cid::parse(e["text"].str()), e["status"].str());
    for (const auto& e : c["verify"].items) {
        CHECK_ST(cid::Cid::from_bytes(hex(e["cid"].str())).verify(utf8(e["utf8"].str())), e["status"].str());
    }
    CHECK(cid::Cid().empty());
    CHECK_ST(cid::Cid::from_bytes(Bytes(cid::max_bytes + 1, 1)), "ZXV_E_CID");
}

// =====================================================================
// aipi
// =====================================================================
cid::Cid aipi_ref(const std::string& ref)
{
    const V& a = J("aipi");
    if (ref == "module_cid") return zxv::cid::Cid::from_bytes(hex(a["module_cid"].str()));
    // "fn[i].fn_cid" or "call[i].call_cid"
    const auto lb = ref.find('['), rb = ref.find(']'), dot = ref.find('.', rb);
    const std::string arr = ref.substr(0, lb), field = ref.substr(dot + 1);
    const std::size_t idx = std::stoul(ref.substr(lb + 1, rb - lb - 1));
    return zxv::cid::Cid::from_bytes(hex(a[arr][idx][field].str()));
}
void test_aipi()
{
    namespace ai = zxv::aipi;
    const V& a = J("aipi");
    const auto module = cid::Cid::from_bytes(hex(a["module_cid"].str()));
    const auto contract = cid::Cid::from_bytes(hex(a["contract_cid"].str()));
    CHECK(cid::Cid::compute(utf8(a["module_utf8"].str())) == module);
    CHECK(cid::Cid::compute(utf8(a["contract_utf8"].str())) == contract);
    for (const auto& e : a["fn"].items) {
        ai::FunctionSpec f;
        f.intent = static_cast<ai::Intent>(u32(e["intent"]));
        f.verify = static_cast<ai::Verify>(u32(e["verify"]));
        f.privacy = static_cast<ai::Privacy>(u32(e["privacy"]));
        f.determinism = static_cast<ai::Determinism>(u32(e["determinism"]));
        f.quorum_n = u32(e["quorum_n"]);
        f.quorum_k = u32(e["quorum_k"]);
        f.fuel_max = e["fuel_max"].u64();
        f.module = module;
        if (e["contract"].b) f.contract = contract;
        f.entry = e["entry"].str();
        f.checker = is_null(e["checker"]) ? "" : e["checker"].str();
        f.certifier = is_null(e["certifier"]) ? "" : e["certifier"].str();
        CHECK_ST(ai::encode_function(f), e["status"].str());
        if (e["status"].str() != "ZXV_OK") continue;
        const Bytes rec = ai::encode_function(f);
        CHECK(hx(rec) == e["record"].str());
        const auto fc = ai::function_cid(rec);
        CHECK(hx(fc.bytes()) == e["fn_cid"].str());
        CHECK(fc.to_text(cid::Base::base58btc) == e["fn_cid_text"].str());
        const auto d = ai::decode_function(rec);
        CHECK(d.intent == f.intent && d.verify == f.verify && d.privacy == f.privacy &&
              d.determinism == f.determinism && d.quorum_n == f.quorum_n && d.quorum_k == f.quorum_k &&
              d.fuel_max == f.fuel_max && d.module == module && d.contract.has_value() == f.contract.has_value() &&
              d.entry == f.entry && d.checker == f.checker && d.certifier == f.certifier);
        CHECK(ai::encode_function(d) == rec);
    }
    for (const auto& e : a["call"].items) {
        const auto fn = aipi_ref(e["fn"].str());
        const Bytes in = utf8(e["input_utf8"].str());
        const std::string want = has(e, "status") ? e["status"].str() : "ZXV_OK";
        CHECK_ST(ai::encode_call(fn, in), want);
        CHECK_ST(ai::call_cid(fn, in), want);
        if (want != "ZXV_OK") continue;
        const Bytes rec = ai::encode_call(fn, in);
        CHECK(hx(rec) == e["record"].str());
        const auto cc = ai::call_cid(fn, in);
        CHECK(hx(cc.bytes()) == e["call_cid"].str());
        CHECK(cc.to_text(cid::Base::base58btc) == e["call_cid_text"].str());
        const auto d = ai::decode_call(rec);
        CHECK(d.function == fn && d.input == in);
    }
    for (const auto& e : a["claim"].items) {
        const auto call = aipi_ref(e["call"].str());
        ai::ExecutorId ex{};
        const Bytes xb = hex(e["executor"].str());
        std::copy(xb.begin(), xb.end(), ex.begin());
        const Bytes out = utf8(e["output_utf8"].str()), cert = utf8(e["cert_utf8"].str());
        const Bytes rec = ai::encode_claim(call, ex, out, cert);
        CHECK(hx(rec) == e["record"].str());
        const auto d = ai::decode_claim(rec);
        CHECK(d.call == call && d.executor == ex && d.output == out && d.cert == cert);
    }
    CHECK_ST(ai::decode_function(Bytes{1, 2, 3}), "ZXV_E_FORMAT");

    // An offline node: put, get, publish, load, plan, info.
    zxv::Context ctx;
    const auto cap = ctx.root_capability(zxv::caps::all_1_5);
    ai::Node node(cap, 1u << 20, 64);
    CHECK(node.put(utf8(a["module_utf8"].str())) == module);
    CHECK(node.get(module) == utf8(a["module_utf8"].str()));
    const Bytes fn0 = hex(a["fn"][0]["record"].str());
    const auto fc0 = node.publish(fn0);
    CHECK(hx(fc0.bytes()) == a["fn"][0]["fn_cid"].str());
    CHECK(node.load(fc0) == fn0);
    CHECK(node.plan(fc0) == ai::Route::local);
    ai::Node empty(cap, 1u << 20, 8); // holds no module: nothing can run here
    const auto fe0 = empty.publish(fn0);
    CHECK(empty.plan(fe0, sv("x")) == ai::Route::unavailable);
    CHECK_ST(node.publish(hex(a["fn"][1]["record"].str())), "ZXV_E_STATE"); // privacy local needs a node key
    CHECK_ST(node.get(contract), "ZXV_E_NOT_FOUND");
    CHECK_ST(node.put(utf8("secret"), true), "ZXV_E_STATE"); // no node key: no private blocks
    const auto info = node.info();
    CHECK(info.blocks == 2 && info.gets_verified >= 1 && info.forbidden == 0);
    // With a node key, private blocks are sealed, and never re-published as public.
    ai::Node keyed(cap, 1u << 20, 16, fill32(0x5A));
    const auto pc = keyed.put(utf8("private"), true);
    CHECK(keyed.get(pc) == utf8("private"));
    CHECK_ST(keyed.put(utf8("private"), false), "ZXV_E_STATE");
}

// =====================================================================
// coin / token
// =====================================================================
zxv::token::Counts counts_of(const V& sparse)
{
    zxv::token::Counts c{};
    for (const auto& p : sparse.items) c[static_cast<std::size_t>(p[0].u64())] = p[1].u64();
    return c;
}

void test_coin()
{
    namespace coin = zxv::coin;
    const V& c = J("coin");
    CHECK(coin::rungs == u32(c["rungs"]));
    CHECK(coin::top_face == c["top_face"].u64());
    for (const auto& e : c["face"].items) {
        CHECK_ST(coin::face(u32(e["rung"])), e["status"].str());
        if (has(e, "face")) CHECK(coin::face(u32(e["rung"])) == e["face"].u64());
    }
    for (const auto& e : c["rung_of"].items) {
        CHECK_ST(coin::rung(e["face"].u64()), e["status"].str());
        if (has(e, "rung")) CHECK(coin::rung(e["face"].u64()) == u32(e["rung"]));
    }
    for (const auto& e : c["split"].items) {
        CHECK_ST(coin::split_count(e["amount"].u64()), e["status"].str());
        if (e["status"].str() != "ZXV_OK") continue;
        CHECK(coin::split_count(e["amount"].u64()) == static_cast<std::size_t>(e["count"].u64()));
        if (has(e, "rungs")) {
            const auto r = coin::split(e["amount"].u64());
            CHECK(r.size() == e["rungs"].items.size());
            bool same = r.size() == e["rungs"].items.size();
            for (std::size_t i = 0; same && i < r.size(); ++i) same = r[i] == u32(e["rungs"][i]);
            CHECK(same);
        }
    }
}

void test_token()
{
    namespace tk = zxv::token;
    const V& c = J("coin");
    for (const auto& e : c["token_face"].items) {
        CHECK_ST(tk::face(u32(e["tier"])), e["status"].str());
        if (!has(e, "face")) continue;
        const auto f = tk::face(u32(e["tier"]));
        CHECK(f.face == e["face"].u64() && f.is_super == e["super"].b);
    }
    for (const auto& e : c["token_tier"].items) {
        CHECK_ST(tk::tier(e["face"].u64()), e["status"].str());
        if (has(e, "tier")) CHECK(tk::tier(e["face"].u64()) == u32(e["tier"]));
    }
    for (const auto& e : c["token_value"].items) {
        CHECK_ST(tk::value(counts_of(e["counts"])), e["status"].str());
        if (!has(e, "value")) continue;
        const auto v = tk::value(counts_of(e["counts"]));
        CHECK(v.value == e["value"].u64() && v.pieces == e["pieces"].u64());
    }
    for (const auto& e : c["token_canonical"].items) {
        CHECK_ST(tk::canonical(e["value"].u64(), u32(e["octane"])), e["status"].str());
        if (!has(e, "counts")) continue;
        const auto k = tk::canonical(e["value"].u64(), u32(e["octane"]));
        CHECK(k == counts_of(e["counts"]));
        CHECK(tk::value(k).pieces == e["pieces"].u64() && tk::value(k).value == e["value"].u64());
        CHECK(tk::is_canonical(k, u32(e["octane"])));
    }
    for (const auto& e : c["token_is_canonical"].items) {
        CHECK_ST(tk::is_canonical(counts_of(e["counts"]), u32(e["octane"])), e["status"].str());
        if (has(e, "canonical")) CHECK(tk::is_canonical(counts_of(e["counts"]), u32(e["octane"])) == e["canonical"].b);
    }
    for (const auto& e : c["token_compress"].items) {
        auto k = counts_of(e["counts"]);
        CHECK_ST(tk::compress(k, u32(e["octane"])), e["status"].str());
        if (has(e, "out")) CHECK(k == counts_of(e["out"]));
    }
    for (const auto& e : c["token_fuse_split"].items) {
        auto k = counts_of(e["counts"]);
        if (e["op"].str() == "fuse") CHECK_ST(tk::fuse(k, u32(e["octane"]), u32(e["tier"])), e["status"].str());
        else CHECK_ST(tk::split(k, u32(e["tier"])), e["status"].str());
        CHECK(k == counts_of(e["out"])); // unchanged on refusal
    }
    for (const auto& e : c["token_code"].items) {
        CHECK_ST(tk::code_bits(e["value"].u64()), e["status"].str());
        if (!has(e, "bits")) continue;
        const std::uint32_t bits = u32(e["bits"]);
        CHECK(tk::code_bits(e["value"].u64()) == bits);
        Bytes buf(tk::code_max_bytes, 0);
        std::uint32_t bit = 0;
        tk::code_put(buf, bit, e["value"].u64());
        CHECK(bit == bits);
        buf.resize((bits + 7) / 8);
        CHECK(hx(buf) == e["bytes"].str());
        std::string code;
        for (std::uint32_t i = 0; i < bits; ++i) code += ((static_cast<std::uint32_t>(buf[i / 8]) >> (i % 8)) & 1u) ? '1' : '0';
        CHECK(code == e["code"].str());
        std::uint32_t rb = 0;
        CHECK(tk::code_get(buf, rb) == e["value"].u64() && rb == bits);
    }
    {
        const V& s = c["token_code_stream"];
        Bytes buf(64, 0);
        std::uint32_t bit = 0;
        for (const auto& v : s["values"].items) tk::code_put(buf, bit, v.u64());
        CHECK(bit == u32(s["bits"]));
        buf.resize((bit + 7) / 8);
        CHECK(hx(buf) == s["bytes"].str());
        std::uint32_t rb = 0;
        bool same = true;
        for (const auto& v : s["values"].items) same = same && tk::code_get(buf, rb) == v.u64();
        CHECK(same && rb == bit);
        // A full buffer refuses the next code and leaves `bit` alone.
        std::uint32_t at = bit;
        Bytes tiny = buf;
        CHECK_ST(tk::code_put(tiny, at, 18446744073709551614ull), "ZXV_E_BUFFER");
        CHECK(at == bit);
    }
    for (const auto& e : c["token_code_get_reject"].items) {
        std::uint32_t b = u32(e["bit"]);
        const Bytes buf = hex(e["bytes"].str());
        CHECK_ST(tk::code_get(buf, b), e["status"].str());
        CHECK(b == u32(e["bit"]));
    }

    // A wallet (caps::mint).
    zxv::Context ctx;
    const auto cap = ctx.root_capability(zxv::caps::all_1_5);
    tk::Wallet w(cap, 7, tk::octane::midgrade), v(cap, 8, tk::octane::plain);
    w.mint(1000);
    CHECK(w.info().value == 1000 && w.info().model_id == 7 && w.info().octane == tk::octane::midgrade);
    CHECK(tk::is_canonical(w.counts(), tk::octane::midgrade));
    w.spend(1);
    CHECK(w.info().value == 999);
    w.transfer_to(v, 99);
    CHECK(w.info().value == 900 && v.info().value == 99 && v.counts()[0] == 99);
    CHECK_ST(v.spend(100), "ZXV_E_FUNDS");
    CHECK_ST(w.transfer_to(v, 901), "ZXV_E_FUNDS");
    CHECK(w.info().value == 900 && v.info().value == 99);
    v.set_octane(tk::octane::racing);
    CHECK(v.info().value == 99 && v.info().pieces < 99);
    CHECK_ST(v.set_octane(64), "ZXV_E_RANGE");
    CHECK_ST(tk::Wallet(cap, 0, 64), "ZXV_E_RANGE");
}

// =====================================================================
// fee
// =====================================================================
void test_fee()
{
    namespace fee = zxv::fee;
    const V& f = J("fee");
    CHECK(fee::num == f["num"].u64() && fee::den == f["den"].u64());
    for (const auto& e : f["assure"].items) CHECK(fee::assure(e["gross"].u64()) == e["fee"].u64());
    for (const auto& c : f["carry"].items) {
        fee::Carry carry(c["rem_in"].u64());
        std::uint64_t rem = c["rem_in"].u64();
        for (const auto& s : c["steps"].items) {
            CHECK_ST(fee::assure_carry(rem, s["gross"].u64()), s["status"].str());
            const auto ch = fee::assure_carry(rem, s["gross"].u64());
            CHECK(ch.fee == s["fee"].u64() && ch.remainder == s["rem"].u64());
            rem = ch.remainder;
            CHECK(carry.charge(s["gross"].u64()) == s["fee"].u64());
            CHECK(carry.remainder() == s["rem"].u64());
        }
    }
    for (const auto& e : f["carry_reject"].items) {
        CHECK_ST(fee::assure_carry(e["rem_in"].u64(), e["gross"].u64()), e["status"].str());
        fee::Carry carry(e["rem_in"].u64());
        CHECK_ST(carry.charge(e["gross"].u64()), e["status"].str());
        CHECK(carry.remainder() == e["rem_in"].u64()); // unchanged on refusal
    }
    for (std::size_t i = 0; i < f["buckets"].items.size(); ++i)
        CHECK(fee::to_string(static_cast<fee::Bucket>(i)) == f["buckets"][i].str());
    for (const auto& e : f["split"].items) {
        const auto s = fee::split(e["fee"].u64());
        bool same = true;
        std::uint64_t sum = 0;
        for (std::size_t i = 0; i < 4; ++i) {
            same = same && s[i] == e["parts"][i].u64();
            sum += s[i];
        }
        CHECK(same && sum == e["fee"].u64());
    }
    const V& p = f["policy_default"];
    const auto d = fee::default_policy();
    for (std::size_t i = 0; i < 4; ++i) CHECK(d.percent[i] == u32(p["pct"][i]));
    CHECK(d.allow_below == (p["allow_below"].u64() != 0));
    for (std::size_t i = 0; i < 4; ++i)
        CHECK(d.rates[i] == (fee::Ratio{p["rate"][i][0].u64(), p["rate"][i][1].u64()}));
    CHECK(d.multiplier == (fee::Ratio{p["multiplier"][0].u64(), p["multiplier"][1].u64()}));
    CHECK(d.commons_cap == (fee::Ratio{p["commons_cap"][0].u64(), p["commons_cap"][1].u64()}));
}

// =====================================================================
// vhub
// =====================================================================
void test_vhub()
{
    namespace vh = zxv::vhub;
    const V& x = J("vhub");
    for (const auto& e : x["peg_msg"].items) {
        const auto m = vh::peg_message(u8(e["slot"]), e["num"].u64(), e["den"].u64(), e["epoch"].u64());
        CHECK(hx(m) == e["msg"].str());
    }
    const V& s = x["scenario"];
    zxv::Context ctx;
    const auto cap = ctx.root_capability(zxv::caps::all_1_5);
    const zxv::MlDsa87 dsa(cap);
    std::vector<zxv::SigningKeyPair> keys;
    for (const auto& k : s["keys"].items) {
        keys.push_back(dsa.keygen_seed(hex(k["seed"].str())));
        CHECK(hx(zxv::Hashes::sha3_256(keys.back().public_key)) == k["pk_sha3_256"].str());
    }
    vh::Hub hub(cap, hex(s["hub_seed"].str()));
    for (const auto& c : s["currencies"].items) {
        const auto slot = u8(c["slot"]);
        const std::size_t key = has(c, "issuer_key") ? static_cast<std::size_t>(c["issuer_key"].u64()) : 0;
        const std::uint64_t pool = has(c, "pool_funding") ? c["pool_funding"].u64() : 0;
        const std::uint64_t back = has(c, "vbill_backing") ? c["vbill_backing"].u64() : 0;
        const std::uint8_t minor = has(c, "minor_units") ? u8(c["minor_units"]) : 2;
        CHECK_ST(hub.add_currency(slot, c["code"].str(), minor, keys[key].public_key, pool, back), c["status"].str());
        if (c["status"].str() != "ZXV_OK") continue;
        const auto i = hub.currency(slot);
        CHECK(i.issuer_account == u32(c["issuer_account"]) && i.pool_account == u32(c["pool_account"]) &&
              i.vfloat_account == u32(c["vfloat_account"]));
        CHECK(!i.has_peg());
    }
    CHECK_THROWS_AS(hub.add_currency(5, "USD", 2, keys[0].public_key), zxv::UsageError);
    for (const auto& p : s["pegs"].items) {
        const auto slot = u8(p["slot"]);
        const auto m = vh::peg_message(slot, p["num"].u64(), p["den"].u64(), p["epoch"].u64());
        const auto sig = dsa.sign_deterministic(keys[static_cast<std::size_t>(p["signer"].u64())].secret_key, m);
        CHECK(hx(zxv::Hashes::sha3_256(sig)) == p["sig_sha3_256"].str());
        CHECK_ST(hub.set_peg(slot, p["num"].u64(), p["den"].u64(), p["epoch"].u64(), sig), p["status"].str());
    }
    CHECK(hub.currency(0).has_peg() && hub.currency(0).peg_num == 350 && hub.currency(0).peg_epoch == 1);
    CHECK(!hub.currency(3).has_peg());
    std::vector<std::uint32_t> acct;
    for (const auto& a : s["accounts"].items) {
        acct.push_back(hub.open_account(u8(a["slot"]), u32(a["owner"])));
        CHECK(acct.back() == u32(a["account"]));
        if (has(a, "issued")) hub.issue(acct.back(), a["issued"].u64());
    }
    for (const auto& q : s["quotes"].items) {
        CHECK_ST(hub.quote(u8(q["from"]), u8(q["to"]), q["want"].u64()), q["status"].str());
        if (q["status"].str() != "ZXV_OK") continue;
        const auto r = hub.quote(u8(q["from"]), u8(q["to"]), q["want"].u64());
        CHECK(r.amount == q["amount"].u64() && r.vbills == q["vbills"].u64() && r.received == q["received"].u64());
    }
    for (const auto& w : s["swaps"].items) {
        if (has(w, "add_limit")) hub.add_limit(u8(w["add_limit"]["slot"]), w["add_limit"]["limit"].u64());
        vh::Swap r;
        CHECK_ST(r = hub.swap(u8(w["from"]), u8(w["to"]), u32(w["sender"]), u32(w["receiver"]), w["amount"].u64(),
                              w["tick"].u64()),
                 w["status"].str());
        if (w["status"].str() == "ZXV_OK") CHECK(r.vbills == w["vbills"].u64() && r.received == w["received"].u64());
    }
    CHECK(s["check"].str() == "ZXV_OK");
    CHECK(hub.check() == s["postings"].u64());
    for (const auto& b : s["balances"].items) {
        const auto r = hub.balance(u32(b["account"]));
        CHECK(r.debit == b["debit"].u64() && r.credit == b["credit"].u64() && r.equity == b["equity"].i64());
    }
    CHECK_ST(hub.balance(static_cast<std::uint32_t>(s["balances"].items.size())), "ZXV_E_NOT_FOUND");
    CHECK(hub.next_epoch() >= 1);
}

// =====================================================================
// freight
// =====================================================================
void test_freight()
{
    namespace fr = zxv::freight;
    const V& x = J("freight");
    for (const auto& e : x["header_serialize"].items) {
        fr::HeaderFields f;
        f.k = u8(e["k"]);
        f.flags = u8(e["flags"]);
        f.payload_len = static_cast<std::uint16_t>(e["payload_len"].u64());
        f.seq = static_cast<std::uint16_t>(e["seq"].u64());
        f.seq_count = static_cast<std::uint16_t>(e["seq_count"].u64());
        f.freight_id = e["freight_id"].u64();
        f.expand_limit = e["expand_limit"].u64();
        f.payload_sha256 = d32(e["payload_sha256"].str());
        CHECK_ST(fr::Header::from_fields(f), e["status"].str());
        if (e["status"].str() != "ZXV_OK") continue;
        const auto h = fr::Header::from_fields(f);
        CHECK(hx(h.bytes()) == e["header"].str());
        const auto g = fr::Header::parse(hex(e["header"].str())).fields();
        CHECK(g.version == fr::version && g.k == f.k && g.flags == f.flags && g.payload_len == f.payload_len &&
              g.seq == f.seq && g.seq_count == f.seq_count && g.freight_id == f.freight_id &&
              g.expand_limit == f.expand_limit && g.payload_sha256 == f.payload_sha256);
    }
    for (const auto& c : x["codec"].items) {
        const Bytes payload = hex(c["payload"].str());
        const auto h = fr::Header::for_payload(c["freight_id"].u64(), u8(c["k"]), payload);
        CHECK(hx(h.bytes()) == c["header"].str());
        const Bytes packets = fr::encode(h, payload);
        CHECK(hx(packets) == c["packets"].str());
        CHECK(packets.size() == fr::freight_bytes);
        for (const auto& d : c["decode"].items) {
            Bytes sub;
            for (const auto& r : d["rows"].items) {
                const auto row = static_cast<std::size_t>(r.u64());
                sub.insert(sub.end(), packets.begin() + static_cast<std::ptrdiff_t>(21 * row),
                           packets.begin() + static_cast<std::ptrdiff_t>(21 * row + 21));
            }
            if (has(d, "flip")) {
                const auto at = 21 * static_cast<std::size_t>(d["flip"]["index_in_rows"].u64()) +
                                static_cast<std::size_t>(d["flip"]["byte"].u64());
                sub[at] ^= hex(d["flip"]["xor"].str())[0];
            }
            CHECK_ST(fr::decode(h, sub), d["status"].str());
            if (has(d, "matches_payload")) CHECK((fr::decode(h, sub) == payload) == d["matches_payload"].b);
        }
    }
    {
        const Bytes r201 = ramp(201);
        for (const auto& e : x["header_init_reject"].items) {
            const std::uint64_t lim = has(e, "expand_limit") ? e["expand_limit"].u64() : 0;
            const std::uint8_t flags = has(e, "flags") ? u8(e["flags"]) : 0;
            CHECK_ST(fr::Header::for_payload(1, u8(e["k"]),
                                             std::span<const std::uint8_t>(r201).first(
                                                 static_cast<std::size_t>(e["len"].u64())),
                                             flags, lim),
                     e["status"].str());
        }
    }
    for (const auto& e : x["set_count"].items) {
        CHECK_ST(fr::set_count(e["len"].u64(), u8(e["k"])), e["status"].str());
        if (has(e, "count")) CHECK(fr::set_count(e["len"].u64(), u8(e["k"])) == u32(e["count"]));
    }
    {
        const V& sp = x["split"];
        const Bytes data = ramp(static_cast<std::size_t>(sp["len"].u64()));
        fr::Join join(1u << 16);
        for (const auto& m : sp["members"].items) {
            CHECK_ST(fr::split(sp["freight_id"].u64(), u8(sp["k"]), data, u32(m["seq"])), m["status"].str());
            if (m["status"].str() != "ZXV_OK") continue;
            const auto mem = fr::split(sp["freight_id"].u64(), u8(sp["k"]), data, u32(m["seq"]));
            CHECK(hx(mem.header.bytes()) == m["header"].str());
            CHECK(hx(zxv::Hashes::sha3_256(mem.packets)) == m["packets_sha3_256"].str());
            CHECK(!join.status().complete);
            join.add(mem);
        }
        const auto st = join.status();
        CHECK(st.complete && st.received == 3 && st.count == 3 && st.total == data.size());
        CHECK(join.result() == data);
    }
    for (const auto& e : x["ops_encode"].items) {
        const Bytes in = utf8(e["utf8"].str());
        const bool ws = e["with_sha"].b;
        CHECK_ST(fr::encode_ops(in, ws), e["status"].str());
        const Bytes ops = fr::encode_ops(in, ws);
        CHECK(hx(ops) == e["ops"].str());
        CHECK(fr::ops_bound(in.size(), ws) == e["bound"].u64());
        CHECK(ops.size() <= fr::ops_bound(in.size(), ws));
        CHECK_ST(fr::expand(ops, 256), e["expand_status"].str());
        CHECK(fr::expand(ops, 256) == in);
        CHECK(fr::peek(ops).total == in.size() && fr::peek(ops).has_sha == ws);
    }
    {
        const V& w = x["ops_writer"][0];
        fr::OpsWriter ow(w["declared_total"].u64(), 4096);
        for (const auto& op : w["ops_list"].items) {
            const std::string k = op["op"].str();
            if (k == "literal") CHECK_ST(ow.literal(utf8(op["utf8"].str())), op["status"].str());
            else if (k == "copy") CHECK_ST(ow.copy(op["distance"].u64(), op["len"].u64()), op["status"].str());
            else
                CHECK_ST(ow.seed(static_cast<fr::Generator>(op["gen"].u64()), hex(op["params"].str()), op["len"].u64()),
                         op["status"].str());
        }
        CHECK_ST(ow.finish(), w["finish_status"].str());
        const Bytes ops = ow.finish();
        CHECK(hx(ops) == w["ops"].str());
        CHECK_ST(fr::expand(ops, 1024), w["expand_status"].str());
        CHECK(hx(fr::expand(ops, 1024)) == w["expanded"].str());
        CHECK(fr::peek(ops).total == w["peek_total"].u64() && fr::peek(ops).has_sha == w["peek_has_sha"].b);
        CHECK_ST(fr::expand(ops, w["declared_total"].u64() - 1), w["expand_limit_too_small"].str());
        CHECK_ST(fr::expand(std::span<const std::uint8_t>(ops).first(ops.size() - 1), 1024),
                 w["expand_truncated"].str());
    }
    {
        const V& w = x["ops_writer"][1];
        fr::OpsWriter ow(w["declared_total"].u64(), 4096);
        std::string resolves;
        cid::Cid ref;
        for (const auto& op : w["ops_list"].items) {
            if (op["op"].str() == "literal") ow.literal(utf8(op["utf8"].str()));
            else {
                ref = cid::Cid::from_bytes(hex(op["cid"].str()));
                ow.cidref(ref, op["len"].u64());
                resolves = op["resolves_to_utf8"].str();
            }
        }
        const Bytes ops = ow.finish();
        CHECK(hx(ops) == w["ops"].str());
        CHECK_ST(fr::expand(ops, 1024), w["expand_without_resolver"].str());
        int calls = 0;
        const fr::Resolver resolver = [&](std::span<const std::uint8_t> c, std::span<std::uint8_t> out) {
            ++calls;
            if (!std::equal(c.begin(), c.end(), ref.bytes().begin(), ref.bytes().end())) return false;
            if (out.size() != resolves.size()) return false;
            std::copy(resolves.begin(), resolves.end(), out.begin());
            return true;
        };
        const Bytes got = fr::expand(ops, 1024, resolver);
        CHECK(std::string(got.begin(), got.end()) == w["expanded_with_resolver_utf8"].str());
        CHECK(calls >= 1);
        // A resolver that declines: ZXV_E_NOT_FOUND.
        CHECK_ST(fr::expand(ops, 1024, [](auto, auto) { return false; }), "ZXV_E_NOT_FOUND");
        // A throwing resolver: the exception never crosses C; expand rethrows it.
        bool rethrown = false;
        try {
            (void) fr::expand(ops, 1024, [](auto, auto) -> bool { throw std::runtime_error("resolver boom"); });
        } catch (const std::runtime_error& e) {
            rethrown = std::string(e.what()) == "resolver boom";
        }
        CHECK(rethrown);
    }
    for (const auto& e : x["gen_run"].items) {
        const auto gen = static_cast<fr::Generator>(e["gen"].u64());
        const Bytes params = hex(e["params"].str());
        const auto len = static_cast<std::size_t>(e["len"].u64());
        CHECK_ST(fr::generate(gen, params, len), e["status"].str());
        if (has(e, "out")) CHECK(hx(fr::generate(gen, params, len)) == e["out"].str());
    }
}

// =====================================================================
// web4
// =====================================================================
void test_web4()
{
    namespace w4 = zxv::web4;
    const V& x = J("web4");
    const std::size_t sigb = static_cast<std::size_t>(x["sig_bytes"].u64());
    CHECK(sigb == w4::sig_bytes);
    zxv::Context ctx;
    const auto cap = ctx.root_capability(zxv::caps::all_1_5);
    std::vector<w4::Agent> agents;
    std::map<std::string, std::size_t> by_name;
    auto body = [&](const Bytes& b) { return hx(std::span<const std::uint8_t>(b).first(b.size() - sigb)); };
    for (const auto& a : x["agents"].items) {
        const Bytes seed(32, hex(a["seed_byte"].str())[0]);
        const w4::Agent* node = is_null(a["node"]) ? nullptr : &agents[by_name.at(a["node"].str())];
        agents.emplace_back(cap, a["name"].str(), a["now_ms"].u64(), node, seed);
        by_name[a["name"].str()] = agents.size() - 1;
        const auto& ag = agents.back();
        CHECK(hx(ag.agent_id()) == a["agent_id"].str());
        CHECK(hx(ag.peer_id()) == a["peer_id"].str());
        CHECK(hx(zxv::Hashes::sha3_256(ag.public_key())) == a["pk_sha3_256"].str());
        Bytes card = ag.card();
        CHECK(card.size() == static_cast<std::size_t>(a["card_len"].u64()));
        CHECK(hx(zxv::Hashes::sha3_256(std::span<const std::uint8_t>(card).first(card.size() - sigb))) ==
              a["card_signed_body_sha3_256"].str());
        CHECK_ST(w4::verify_card(card), a["card_verify"].str());
        const auto ci = w4::verify_card(card);
        CHECK(ci.agent_id == ag.agent_id() && ci.peer_id == ag.peer_id() && ci.name == a["name"].str() &&
              ci.bound_ms == a["now_ms"].u64());
        card.back() ^= 1;
        CHECK_ST(w4::verify_card(card), a["card_verify_flipped_last_byte"].str());
    }
    const auto& alice = agents[by_name.at("alice")];
    auto& bob = agents[by_name.at("bob")];
    const auto& human = agents[by_name.at("human")];

    // Directory of cards.
    w4::Directory dir(8);
    for (const auto& a : agents) dir.add(a.card());
    CHECK(dir.count() == agents.size());
    CHECK(dir.find(bob.agent_id()) == std::optional<Bytes>(bob.card()) || dir.find(bob.agent_id()).has_value());
    CHECK(!dir.find(fill32(0xEE)).has_value());

    const V& ad = x["action_digest"];
    const auto from = d32(ad["from"].str()), to = d32(ad["to"].str());
    for (const auto& c : ad["cases"].items) {
        const Bytes p = has(c, "payload_ramp_len") ? ramp(static_cast<std::size_t>(c["payload_ramp_len"].u64()))
                                                   : utf8(c["payload_utf8"].str());
        CHECK_ST(w4::action_digest(c["tool"].str(), from, to, c["req_id"].u64(), p), c["status"].str());
        if (has(c, "digest")) CHECK(hx(w4::action_digest(c["tool"].str(), from, to, c["req_id"].u64(), p)) == c["digest"].str());
    }

    // Manifest.
    const V& m = x["manifest"];
    w4::Manifest man(u32(m["version"]), m["issued_ms"].u64(), m["expires_ms"].u64());
    for (const auto& t : m["tools"].items) {
        w4::Tool tool{t["name"].str(), t["price_vfv"].u64(), u32(t["rate_per_min"]), u32(t["burst"]), u32(t["flags"]),
                      u32(t["max_input"])};
        CHECK_ST(man.add_tool(tool), t["status"].str());
    }
    CHECK_ST(man.add_tool(w4::Tool{"pay", 1, 1, 1, 0, 0}), m["add_duplicate"].str());
    CHECK_ST(man.encode(), m["encode_unsigned"].str());
    CHECK(!man.info().is_signed);
    man.sign(agents[by_name.at(m["signer"].str())]);
    const Bytes enc = man.encode();
    CHECK(enc.size() == static_cast<std::size_t>(m["encoding_len"].u64()));
    CHECK(body(enc) == m["encoding_signed_body"].str());
    CHECK(man.to_json() == m["json"].str());
    CHECK(man.info().is_signed && man.info().tool_count == m["tools"].items.size());
    CHECK(man.find("profile.read") == std::optional<std::uint32_t>(1) && !man.find("nope").has_value());
    CHECK(man.tool(0).name == "pay" && man.tool(0).price_vfv == 150 && man.tools().size() == 3);
    CHECK_ST(w4::Manifest::decode(enc), m["decode"].str());
    const auto dec = w4::Manifest::decode(enc);
    CHECK(dec.info().agent_id == alice.agent_id());
    for (const auto& v : m["verify"].items)
        CHECK_ST(dec.verify(agents[by_name.at(v["key"].str())].public_key(), v["now_ms"].u64()), v["status"].str());

    // Consent.
    const V& c = x["consent"];
    const auto action = d32(ad["cases"][1]["digest"].str());
    std::array<std::uint8_t, 16> nonce{};
    {
        const Bytes nb = hex(c["nonce"].str());
        std::copy(nb.begin(), nb.end(), nonce.begin());
    }
    const auto& agent = agents[by_name.at(c["agent"].str())];
    const auto tok = agents[by_name.at(c["human"].str())].issue_consent(
        agent.agent_id(), action, u32(c["scope"]), c["max_vfv"].u64(), c["issued_ms"].u64(), c["expires_ms"].u64(), nonce);
    CHECK(tok.bytes().size() == static_cast<std::size_t>(c["token_len"].u64()));
    CHECK(body(Bytes(tok.bytes().begin(), tok.bytes().end())) == c["token_signed_body"].str());
    const auto ti = tok.info();
    CHECK(ti.human_id == human.agent_id() && ti.agent_id == agent.agent_id() && ti.action == action &&
          ti.scope == u32(c["scope"]) && ti.max_vfv == c["max_vfv"].u64() && ti.nonce == nonce);
    CHECK(w4::ConsentToken::from_bytes(tok.bytes()) == tok);
    auto nonce2 = nonce;
    nonce2[0] ^= 0xFF;
    const auto tok2 = human.issue_consent(agent.agent_id(), action, u32(c["scope"]), c["max_vfv"].u64(),
                                          c["issued_ms"].u64(), c["expires_ms"].u64(), nonce2);
    w4::ConsentLog log(16);
    for (const auto& k : c["check"].items) {
        const bool second = k["what"].str().find("second token") != std::string::npos;
        if (second) log.revoke(tok2, k["now_ms"].u64());
        CHECK_ST(log.check(second ? &tok2 : &tok, human.public_key(), agents[by_name.at(k["agent"].str())].agent_id(),
                           action, u32(k["need"]), k["amount"].u64(), k["now_ms"].u64()),
                 k["status"].str());
    }
    CHECK_THROWS_AS(log.check(nullptr, human.public_key(), alice.agent_id(), action, 1, 1, c["issued_ms"].u64() + 1),
                    zxv::ConsentDenied);
    CHECK(log.prune(c["issued_ms"].u64()) >= 1);  // nothing expired yet: entries remain
    CHECK(log.prune(c["expires_ms"].u64() + 1) == 0); // every token expired: none remain

    // Envelopes.
    const V& es = x["envelopes"];
    Bytes req;
    for (const auto& e : es.items) {
        auto& snd = agents[by_name.at(e["sender"].str())];
        const auto& rcv = agents[by_name.at(e["to"].str())];
        Bytes env;
        if (e["op"].str() == "seal") {
            CHECK_ST(env = snd.seal(static_cast<w4::MsgKind>(e["kind"].i64()), rcv.agent_id(), e["req_id"].u64(),
                                    u32(e["chunk"]), e["tool"].str(), u32(e["flags"]), utf8(e["payload_utf8"].str()),
                                    e["now_ms"].u64()),
                     e["status"].str());
            req = env;
        } else {
            CHECK_ST(env = snd.reply(req, static_cast<w4::MsgKind>(e["kind"].i64()), u32(e["chunk"]), u32(e["flags"]),
                                     utf8(e["payload_utf8"].str()), e["now_ms"].u64()),
                     e["status"].str());
        }
        CHECK(env.size() == static_cast<std::size_t>(e["env_len"].u64()));
        CHECK(body(env) == e["env_signed_body"].str());
        CHECK(w4::verify_envelope(env, snd.public_key()) == (e["verify_sender"].str() == "ZXV_OK"));
        if (has(e, "verify_other")) CHECK(w4::verify_envelope(env, rcv.public_key()) == (e["verify_other"].str() == "ZXV_OK"));
        const auto d = w4::decode_envelope(env);
        CHECK(static_cast<std::int64_t>(d.kind) == e["kind"].i64() && d.from == snd.agent_id() && d.to == rcv.agent_id());
        CHECK(w4::envelope_payload(env) == utf8(e["payload_utf8"].str()));
        CHECK(!w4::envelope_consent(env).has_value());
    }
    // A request carrying a consent token.
    auto& al = agents[by_name.at("alice")];
    const Bytes env = al.seal(w4::MsgKind::request, bob.agent_id(), 9, 0, "pay", 0, sv("amount=100"),
                              c["issued_ms"].u64() + 10, &tok);
    CHECK(w4::envelope_consent(env) == std::optional<w4::ConsentToken>(tok));
    CHECK((w4::decode_envelope(env).flags & w4::msg_flag::consent) != 0);
}

// =====================================================================
// paradox
// =====================================================================
void check_heads(const zxv::paradox::Book& b, const V& h)
{
    namespace px = zxv::paradox;
    const std::pair<const char*, px::Log> logs[] = {
        {"ops", px::Log::ops}, {"frontier", px::Log::frontier}, {"quarantine", px::Log::quarantine}};
    for (const auto& [name, which] : logs) {
        CHECK(b.log_count(which) == u32(h[name]["count"]));
        CHECK(hx(b.log_head(which)) == h[name]["head"].str());
    }
}

void test_paradox()
{
    namespace px = zxv::paradox;
    using zxv::Hexagram;
    const V& p = J("paradox");
    for (const auto& s : p["signature"].items) CHECK(hx(px::signature(s["utf8"].str())) == s["sig"].str());
    for (std::size_t k = 0; k < p["kind_names"].items.size(); ++k)
        CHECK(px::to_string(static_cast<px::Kind>(k)) == p["kind_names"][k].str());
    for (std::size_t k = 0; k < p["outcome_names"].items.size(); ++k)
        CHECK(px::to_string(static_cast<px::Outcome>(k)) == p["outcome_names"][k].str());
    for (std::size_t k = 0; k < p["node_state_names"].items.size(); ++k)
        CHECK(px::to_string(static_cast<px::NodeState>(k)) == p["node_state_names"][k].str());
    CHECK(px::to_string(static_cast<px::Kind>(99)) == "INVALID");
    for (std::uint8_t t = 0; t < 64; ++t) {
        const auto want = p["kind_of_tag"][t].i64();
        if (want < 0) CHECK_ST(px::kind_of_tag(Hexagram::from_tag(t)), "ZXV_E_ARG");
        else CHECK(static_cast<std::int64_t>(px::kind_of_tag(Hexagram::from_tag(t))) == want);
    }
    for (std::int32_t k = 0; k < 5; ++k)
        for (std::uint8_t t = 0; t < 64; ++t) {
            const auto want = p["action"][static_cast<std::size_t>(k)][t].i64();
            if (want < 0) CHECK_ST(px::action(static_cast<px::Kind>(k), Hexagram::from_tag(t)), "ZXV_E_ARG");
            else CHECK(static_cast<std::int64_t>(px::action(static_cast<px::Kind>(k), Hexagram::from_tag(t))) == want);
        }
    for (std::size_t o = 0; o < p["outcome_action"].items.size(); ++o)
        CHECK(static_cast<std::int64_t>(px::outcome_action(static_cast<px::Outcome>(o))) == p["outcome_action"][o].i64());
    CHECK_ST(px::outcome_action(static_cast<px::Outcome>(14)), "ZXV_E_ARG");

    zxv::Context ctx;
    const auto cap = ctx.root_capability(zxv::caps::all_1_5);
    px::Book book(cap, 64, 16);
    check_heads(book, p["heads_fresh"]);
    const auto sig_ref = [&](const V& v) -> std::optional<zxv::Digest32> {
        if (is_null(v)) return std::nullopt;
        CHECK(v.str() == "signature[2]");
        return d32(p["signature"][2]["sig"].str());
    };
    for (const auto& c : p["classify"].items) {
        if (has(c, "registry_add")) {
            const V& r = c["registry_add"];
            CHECK_ST(book.registry_add(*sig_ref(r["sig"]), static_cast<px::Kind>(r["kind"].i64()), u8(r["reason"]),
                                       u8(r["flags"]), r["owner_ok"].b),
                     r["status"].str());
            CHECK(book.registry_find(*sig_ref(r["sig"])) == std::optional<std::uint32_t>(u32(r["index"])));
            continue;
        }
        std::optional<px::Evidence> ev;
        if (!is_null(c["evidence"]))
            ev = px::Evidence{u32(c["evidence"][0]), u32(c["evidence"][1]), u32(c["evidence"][2]), u32(c["evidence"][3])};
        const auto r = book.classify(Hexagram::from_tag(u8(c["tag"])), ev, c["stale"].b, sig_ref(c["sig"]));
        CHECK(static_cast<std::int64_t>(r.kind) == c["kind"].i64());
        CHECK(r.tag.tag() == u8(c["out_tag"]));
        CHECK(r.reg_index == i32(c["reg_index"]));
        if (is_null(c["sig"])) {
            const auto f = px::classify(Hexagram::from_tag(u8(c["tag"])), ev, c["stale"].b);
            CHECK(f.kind == r.kind && f.tag == r.tag && f.reg_index == -1);
        }
    }
    for (const auto& r : p["registry"].items) {
        const auto sig = px::signature(r["utf8"].str());
        CHECK_ST(book.registry_add(sig, static_cast<px::Kind>(r["kind"].i64()), 2, u8(r["flags"]), r["owner_ok"].b),
                 r["status"].str());
        if (has(r, "index")) CHECK(book.registry_find(sig) == std::optional<std::uint32_t>(u32(r["index"])));
    }
    CHECK_THROWS_AS(book.registry_add(px::signature("money:other"), px::Kind::neutral, 2, px::flag::money, false),
                    zxv::OwnerApprovalRequired);
    {
        const auto e = book.registry_at(1);
        CHECK(e.kind == px::Kind::neutral && e.reason == 2 && e.flags == px::flag::money && e.source == px::src_seed);
        CHECK(e.signature == px::signature("money:drain"));
        CHECK_ST(book.registry_at(99), "ZXV_E_NOT_FOUND");
        CHECK(!book.registry_find(fill32(0)).has_value());
    }
    check_heads(book, p["heads_after_classify"]);

    // constructive
    {
        const V& c = p["constructive"];
        auto vec = [](const V& a) {
            std::vector<std::int64_t> v;
            for (const auto& x : a.items) v.push_back(x.i64());
            return v;
        };
        const auto lo = vec(c["lo"]), hi = vec(c["hi"]), init = vec(c["initial"]);
        px::Policy plain(cap, lo, hi, init), money(cap, lo, hi, init, px::flag::money);
        CHECK(plain.lower() == lo && plain.upper() == hi && plain.flags() == 0 && money.flags() == px::flag::money);
        for (const auto& s : c["steps"].items) {
            px::Policy& pol = s["policy"].str() == "money" ? money : plain;
            const auto kind = static_cast<px::Kind>(s["kind"].i64());
            const auto tag = Hexagram::from_tag(u8(s["tag"]));
            px::Outcome out{};
            if (is_null(s["synth"])) CHECK_ST(out = book.constructive(pol, kind, tag, u32(s["tension"])), s["status"].str());
            else CHECK_ST(out = book.constructive(pol, kind, tag, vec(s["synth"]), u32(s["tension"])), s["status"].str());
            if (has(s, "outcome")) CHECK(static_cast<std::int64_t>(out) == s["outcome"].i64());
            const auto st = pol.state();
            CHECK(st.values == vec(s["values"]) && st.previous == vec(s["prev"]) && st.armed == s["armed"].b &&
                  st.tension_at == u32(s["tension_at"]));
            if (has(s, "proposal")) CHECK(hx(pol.proposal(vec(s["synth"]))) == s["proposal"].str());
        }
        CHECK_ST(px::Policy(cap, lo, hi, std::vector<std::int64_t>{0, 0, 0}), "ZXV_E_RANGE");
        CHECK_ST(px::Policy(cap, lo, hi, std::vector<std::int64_t>{1}), "ZXV_E_ARG");
        CHECK_ST(book.constructive(plain, px::Kind::constructive, Hexagram::from_tag(47), std::vector<std::int64_t>{1}, 0),
                 "ZXV_E_ARG");
    }
    // destructive
    for (const auto& d : p["destructive"].items) {
        Bytes state = utf8(d["state_utf8"].str());
        const Bytes ck = utf8(d["checkpoint_utf8"].str());
        px::Containment c;
        c.steps = u32(d["steps"]);
        c.step_limit = u32(d["step_limit"]);
        c.spent = u32(d["spent"]);
        c.budget = u32(d["budget"]);
        for (const auto& t : d["trace"].items) c.trace.push_back(t.u64());
        c.invariant_ok = d["invariant_ok"].b;
        c.pattern = utf8(d["pattern_utf8"].str());
        c.submitter = u32(d["submitter"]);
        px::Contained r;
        CHECK_ST(r = book.destructive(static_cast<px::Kind>(d["kind"].i64()), Hexagram::from_tag(u8(d["tag"])), state,
                                      ck, c),
                 d["status"].str());
        CHECK(static_cast<std::int64_t>(r.outcome) == d["outcome"].i64());
        CHECK(hx(state) == d["state_after"].str());
        CHECK(r.proof.has_value() == has(d, "repair_proof"));
        if (!r.proof) continue;
        CHECK(hx(r.proof->bytes()) == d["repair_proof"].str());
        CHECK(r.proof->verify() == (d["repair_verify"].str() == "ZXV_OK"));
        Bytes flipped(r.proof->bytes().begin(), r.proof->bytes().end());
        flipped[0] ^= 1;
        CHECK(px::RepairProof::from_bytes(flipped).verify() == (d["repair_verify_flipped_byte0"].str() == "ZXV_OK"));
        CHECK(r.proof->signature() == px::signature(d["pattern_utf8"].str()));
        CHECK(r.proof->submitter() == u32(d["submitter"]) && r.proof->state_length() == state.size());
        CHECK((r.proof->trips() & px::trip::steps) != 0 && (r.proof->trips() & px::trip::cycle) != 0);
        CHECK(r.proof->quarantine_index() == 0);
        const auto q = book.quarantine_record(0);
        CHECK(q.subject == r.proof->proof_hash() && q.submitter == u32(d["submitter"]) && q.reason == r.proof->trips());
        CHECK(q.tag.polarity() == zxv::HexPolarity::forbidden);
    }
    CHECK_ST(px::RepairProof::from_bytes(Bytes(10)), "ZXV_E_ARG");
    // neutral
    {
        const V& n = p["neutral"];
        px::Hold hold(cap, u32(n["capacity"]), u32(n["decisive"]));
        std::vector<zxv::Digest32> ids;
        for (const auto& b : n["branches"].items) {
            ids.push_back(d32(b.str()));
            hold.add(ids.back());
            hold.evidence(ids.back(), true);
        }
        hold.add(ids[0]); // the same id again is the same candidate
        CHECK(hold.info().count == ids.size() && hold.info().settled == -1);
        for (const auto& s : n["steps"].items) {
            if (has(s, "before")) continue;
            for (std::uint64_t k = 0; k < s["add_for_branch0"].u64(); ++k) hold.evidence(ids[0], true);
            px::Settlement r;
            const auto pick = i32(s["owner_pick"]);
            CHECK_ST(r = book.neutral(hold, static_cast<px::Kind>(s["kind"].i64()), Hexagram::from_tag(u8(s["tag"])),
                                      pick < 0 ? std::nullopt : std::optional<std::uint32_t>(static_cast<std::uint32_t>(pick))),
                     s["status"].str());
            CHECK(static_cast<std::int64_t>(r.outcome) == s["outcome"].i64());
            CHECK(r.settled == i32(s["settled"]));
        }
        CHECK(hold.info().settled == 0);
        CHECK(hold.branch(0).id == ids[0] && hold.branch(0).pos == 3 && hold.branch(1).pos == 1);
        CHECK_ST(hold.add(fill32(0x09)), "ZXV_E_STATE"); // settled
        CHECK_ST(hold.branch(9), "ZXV_E_NOT_FOUND");
    }
    // special, drafts, reviews
    {
        const V& s = p["special"];
        const auto sig = px::signature(s["sig_utf8"].str());
        CHECK(hx(sig) == s["sig"].str());
        px::FrontierEntry fe;
        CHECK_ST(fe = book.special(static_cast<px::Kind>(s["kind"].i64()), Hexagram::from_tag(u8(s["tag"])), sig,
                                   d32(s["subject"].str())),
                 s["status"].str());
        CHECK(static_cast<std::int64_t>(fe.outcome) == s["outcome"].i64());
        CHECK(fe.record == std::optional<std::uint32_t>(u32(s["rec"])));
        const auto fr0 = book.op_record(px::Log::frontier, 0);
        CHECK(fr0.a == sig && fr0.b == d32(s["subject"].str()));
        std::map<std::string, px::Draft> drafts;
        for (const char* name : {"draft", "draft_money"}) {
            const V& d = s[name];
            std::optional<px::Draft> dr;
            CHECK_ST(dr = book.draft(fe.record.value_or(0), static_cast<px::Kind>(d["kind"].i64()), u8(d["reason"]),
                                     u8(d["flags"])),
                     d["status"].str());
            if (!dr) continue;
            CHECK(hx(dr->bytes()) == d["bytes"].str());
            CHECK(dr->record() == u32(s["rec"]) && dr->signature() == sig &&
                  static_cast<std::int64_t>(dr->kind()) == d["kind"].i64() && dr->reason() == u8(d["reason"]) &&
                  dr->flags() == u8(d["flags"]));
            CHECK(px::Draft::from_bytes(hex(d["bytes"].str())) == *dr);
            drafts.emplace(name, *dr);
        }
        for (const auto& r : s["review"].items) {
            px::Outcome out{};
            CHECK_ST(out = book.review(drafts.at(r["draft"].str()), r["owner_ok"].b, r["consensus_ok"].b),
                     r["status"].str());
            CHECK(static_cast<std::int64_t>(out) == r["outcome"].i64());
        }
        CHECK(book.registry_count() == u32(s["registry_count_after"]));
        Bytes bad(drafts.at("draft").bytes().begin(), drafts.at("draft").bytes().end());
        bad[10] ^= 1; // the record hash no longer matches
        CHECK_THROWS_AS(book.review(px::Draft::from_bytes(bad), true, true), zxv::VerificationFailed);
        CHECK_ST(book.draft(99, px::Kind::neutral, 0), "ZXV_E_NOT_FOUND");
    }
    // graphs
    for (const auto& g : p["graphs"].items) {
        px::Graph graph(cap, u32(g["step_limit"]));
        auto id = [](const V& v) { return fill32(static_cast<std::uint8_t>(v.str()[0])); };
        for (const auto& n : g["nodes"].items)
            CHECK_ST(graph.add_node(id(n["id"]), static_cast<px::Kind>(n["kind"].i64()), Hexagram::from_tag(u8(n["tag"])),
                                    u32(n["mag"]), i32(n["dir"])),
                     n["status"].str());
        for (const auto& d : g["deps"].items) CHECK_ST(graph.add_dep(id(d["id"]), id(d["dep"])), d["status"].str());
        CHECK_ST(graph.node_at(0), "ZXV_E_STATE"); // before the first pass
        for (const auto& ps : g["passes"].items) {
            px::Outcome out = static_cast<px::Outcome>(-1);
            CHECK_ST(out = book.pass(graph), ps["status"].str());
            if (ps["status"].str() == "ZXV_OK") CHECK(static_cast<std::int64_t>(out) == ps["outcome"].i64());
            const auto i = graph.info();
            CHECK(i.passes == u32(ps["pass"]) && i.total_mag == u32(ps["total_mag"]) && i.total_dir == i32(ps["total_dir"]) &&
                  i.escalated == ps["escalated"].b && hx(i.chain) == ps["chain"].str());
            CHECK(i.nodes == g["nodes"].items.size());
            for (const auto& [k, v] : ps["states"].fields)
                CHECK(static_cast<std::int64_t>(graph.state(fill32(static_cast<std::uint8_t>(k[0])))) == v.i64());
        }
        // node_at walks the nodes in id order.
        const auto n0 = graph.node_at(0);
        CHECK(n0.id == id(g["nodes"][0]["id"]));
        CHECK_ST(graph.add_node(fill32(0x7A), px::Kind::none, Hexagram::from_tag(55), 0, 0), "ZXV_E_STATE");
    }
    check_heads(book, p["heads_final"]);
    CHECK_THROWS_AS([&] {
        px::Graph g(cap, 1);
        g.add_node(fill32('S'), px::Kind::special, Hexagram::from_tag(36), 1, 0);
        (void) book.pass(g);
    }(), zxv::ParadoxStepLimit);
    CHECK(book.log_count(px::Log::ops) == u32(p["heads_final"]["ops"]["count"]) + 1); // the step-limit pass is logged
}

/// Replay the paradox vector steps that write the book, in order, on a fresh
/// book, and compare the final heads, every log record and log_verify.
void test_paradox_logs()
{
    namespace px = zxv::paradox;
    using zxv::Hexagram;
    const V& p = J("paradox");
    zxv::Context ctx;
    const auto cap = ctx.root_capability(zxv::caps::all_1_5);
    px::Book book(cap, 64, 16);
    const auto sigL = px::signature("loop:A->B->A"), sigM = px::signature("money:drain");
    book.registry_add(sigL, px::Kind::destructive, 1);
    CHECK_ST(book.registry_add(sigM, px::Kind::neutral, 2, px::flag::money, false), "ZXV_E_PARADOX_OWNER");
    book.registry_add(sigM, px::Kind::neutral, 2, px::flag::money, true);
    CHECK_ST(book.registry_add(sigM, px::Kind::neutral, 2, px::flag::money, true), "ZXV_E_DUPLICATE");
    check_heads(book, p["heads_after_classify"]);
    const std::vector<std::int64_t> lo{0, 0, 10}, hi{100, 50, 20}, init{50, 25, 15};
    px::Policy plain(cap, lo, hi, init), money(cap, lo, hi, init, px::flag::money);
    const auto ct = Hexagram::from_tag(47), nt = Hexagram::from_tag(39);
    using I = std::vector<std::int64_t>;
    book.constructive(plain, px::Kind::constructive, ct, I{60, 20, 12}, 5);
    book.constructive(plain, px::Kind::constructive, ct, 3);
    book.constructive(plain, px::Kind::constructive, ct, I{200, 20, 12}, 3);
    book.constructive(plain, px::Kind::constructive, ct, I{55, 30, 18}, 4);
    book.constructive(plain, px::Kind::constructive, ct, 9);
    book.constructive(plain, px::Kind::neutral, nt, I{60, 20, 12}, 1);
    CHECK_ST(book.constructive(plain, px::Kind::neutral, ct, I{60, 20, 12}, 1), "ZXV_E_ARG");
    book.constructive(money, px::Kind::constructive, ct, I{60, 20, 12}, 1);
    {
        Bytes state(16, 'X');
        const Bytes ck(16, 'A');
        px::Containment c{10, 8, 3, 100, {1, 2, 3, 2}, true, utf8("loop:A->B->A"), 7};
        CHECK(book.destructive(px::Kind::destructive, Hexagram::from_tag(35), state, ck, c).outcome ==
              px::Outcome::contained);
        state.assign(16, 'X');
        px::Containment c2{1, 8, 0, 0, {}, true, {}, 7};
        CHECK(book.destructive(px::Kind::constructive, ct, state, ck, c2).outcome == px::Outcome::refused);
    }
    {
        px::Hold h(cap, 4, 2);
        const auto i1 = fill32(1), i2 = fill32(2);
        h.add(i1);
        h.add(i2);
        h.evidence(i1, true);
        h.evidence(i2, true);
        book.neutral(h, px::Kind::neutral, nt);
        book.neutral(h, px::Kind::neutral, Hexagram::from_tag(21));
        book.neutral(h, px::Kind::neutral, Hexagram::from_tag(43));
        book.neutral(h, px::Kind::constructive, ct);
        h.evidence(i1, true);
        h.evidence(i1, true);
        CHECK(book.neutral(h, px::Kind::neutral, nt).outcome == px::Outcome::settled);
    }
    {
        zxv::Digest32 subj{};
        for (std::size_t i = 0; i < 32; ++i) subj[i] = static_cast<std::uint8_t>(0x80 + i);
        const auto fe = book.special(px::Kind::special, Hexagram::from_tag(36), px::signature("frontier:new"), subj);
        const auto d = book.draft(*fe.record, px::Kind::destructive, 3, 0);
        const auto dm = book.draft(*fe.record, px::Kind::neutral, 4, px::flag::money);
        book.review(dm, false, true);
        book.review(d, false, false);
        book.review(d, false, true);
        book.review(d, true, false);
    }
    auto graph = [&](std::uint32_t limit, const std::string& names, const std::vector<px::Kind>& k,
                     const std::vector<std::uint8_t>& t, const std::vector<std::uint32_t>& m,
                     const std::vector<std::int32_t>& d, const std::string& deps) {
        px::Graph g(cap, limit);
        for (std::size_t i = 0; i < names.size(); ++i)
            g.add_node(fill32(static_cast<std::uint8_t>(names[i])), k[i], Hexagram::from_tag(t[i]), m[i], d[i]);
        for (std::size_t i = 0; i + 1 < deps.size(); i += 2)
            g.add_dep(fill32(static_cast<std::uint8_t>(deps[i])), fill32(static_cast<std::uint8_t>(deps[i + 1])));
        for (int pass = 0; pass < 4; ++pass) {
            px::Outcome o{};
            if (status_of([&] { o = book.pass(g); }) != ZXV_OK || o == px::Outcome::resolved) break;
        }
    };
    graph(4, "ABCD", {px::Kind::constructive, px::Kind::destructive, px::Kind::constructive, px::Kind::none},
          {47, 35, 47, 55}, {3, 5, 2, 0}, {1, -1, 1, 0}, "CBDA");
    graph(2, "EFG", {px::Kind::special, px::Kind::constructive, px::Kind::neutral}, {36, 47, 21}, {1, 4, 2},
          {0, 1, 0}, "FE");
    check_heads(book, p["heads_final"]);
    const std::pair<const char*, px::Log> logs[] = {
        {"ops", px::Log::ops}, {"frontier", px::Log::frontier}, {"quarantine", px::Log::quarantine}};
    for (const auto& [name, which] : logs) {
        const V& want = p["logs"][name];
        CHECK(book.log_count(which) == want.items.size());
        for (std::uint32_t i = 0; i < want.items.size(); ++i) CHECK(hx(book.log_record(which, i)) == want[i].str());
        CHECK_ST(book.log_record(which, static_cast<std::uint32_t>(want.items.size())), "ZXV_E_NOT_FOUND");
    }
    // Parsed records chain: each prev is the previous hash.
    bool chained = true;
    for (std::uint32_t i = 1; i < book.log_count(px::Log::ops); ++i)
        chained = chained && book.op_record(px::Log::ops, i).prev == book.op_record(px::Log::ops, i - 1).hash &&
                  book.op_record(px::Log::ops, i).seq == i;
    CHECK(chained);
    CHECK(book.op_record(px::Log::ops, book.log_count(px::Log::ops) - 1).hash == book.log_head(px::Log::ops));
    CHECK(book.quarantine_record(0).hash == book.log_head(px::Log::quarantine));
    CHECK_ST(book.op_record(px::Log::quarantine, 0), "ZXV_E_ARG");
    for (std::size_t w = 0; w < 3; ++w) CHECK_ST(book.log_verify(static_cast<px::Log>(w)), p["log_verify"][w].str());
}

// =====================================================================
// lifetimes, buffer growth, capabilities, poisoned context
// =====================================================================
void test_lifetimes()
{
    zxv::Context ctx;
    const auto cap = ctx.root_capability(zxv::caps::all_1_5);
    // A moved-from handle refuses use with UsageError (ZXV_E_ARG), never reaching C.
    zxv::paradox::Book a(cap, 4, 4);
    zxv::paradox::Book b(std::move(a));
    CHECK_THROWS_AS((void) a.log_count(zxv::paradox::Log::ops), zxv::UsageError);
    CHECK_ST((void) a.log_count(zxv::paradox::Log::ops), "ZXV_E_ARG");
    CHECK(b.log_count(zxv::paradox::Log::ops) == 0);
    CHECK(a.native_handle() == nullptr && b.native_handle() != nullptr);
    zxv::token::Wallet w(cap, 1, 0);
    zxv::token::Wallet w2 = std::move(w);
    CHECK_THROWS_AS(w.mint(1), zxv::UsageError);
    w2.mint(5);
    CHECK_THROWS_AS(w2.transfer_to(w, 1), zxv::UsageError);
    zxv::treaty::Treaty t = zxv::treaty::Treaty::from_sutra("TREATY N USD NET\nPARTY A 1 1 1/1\nEND\n");
    zxv::treaty::Treaty t2 = std::move(t);
    CHECK_THROWS_AS((void) t.sutra(), zxv::UsageError);
    CHECK(t2.is_valid());
    zxv::web4::Agent ag(cap, "temp", 1, nullptr, std::vector<std::uint8_t>(32, 7));
    zxv::web4::Agent ag2 = std::move(ag);
    CHECK_THROWS_AS((void) ag.agent_id(), zxv::UsageError);
    CHECK_THROWS_AS((void) ag.card(), zxv::UsageError);
    CHECK(ag2.card().size() > zxv::web4::sig_bytes);
    zxv::freight::Join j(1024);
    zxv::freight::Join j2(std::move(j));
    CHECK_THROWS_AS((void) j.status(), zxv::UsageError);
    zxv::vhub::Hub h(cap);
    zxv::vhub::Hub h2 = std::move(h);
    CHECK_THROWS_AS((void) h.check(), zxv::UsageError);
    zxv::aipi::Node n(cap, 1u << 20, 4);
    zxv::aipi::Node n2 = std::move(n);
    CHECK_THROWS_AS((void) n.info(), zxv::UsageError);
    zxv::paradox::Hold hd(cap, 2);
    zxv::paradox::Hold hd2 = std::move(hd);
    CHECK_THROWS_AS(b.neutral(hd, zxv::paradox::Kind::neutral, zxv::Hexagram::from_tag(39)), zxv::UsageError);
    // Handles outlive the Context object and the Capability object that made them.
    std::optional<zxv::paradox::Book> keep;
    {
        zxv::Context c2;
        auto k2 = c2.root_capability(zxv::caps::paradox);
        keep.emplace(k2, 4, 4);
    }
    CHECK(keep->log_count(zxv::paradox::Log::ops) == 0);
    keep->registry_add(zxv::paradox::signature("x"), zxv::paradox::Kind::neutral, 0);
    CHECK(keep->registry_count() == 1);
    // A moved-from capability cannot create a handle.
    auto cap2 = ctx.root_capability(zxv::caps::all_1_5);
    auto cap3 = std::move(cap2);
    CHECK_THROWS_AS(zxv::paradox::Book(cap2, 1, 1), zxv::UsageError);
    (void) cap3;
    // The type system keeps handles move-only.
    static_assert(!std::is_copy_constructible_v<zxv::paradox::Book> && std::is_move_constructible_v<zxv::paradox::Book>);
    static_assert(!std::is_copy_constructible_v<zxv::web4::Agent>);
    static_assert(!std::is_copy_constructible_v<zxv::token::Wallet>);
    static_assert(!std::is_copy_constructible_v<zxv::aipi::Node>);
    static_assert(!std::is_copy_constructible_v<zxv::vhub::Hub>);
    static_assert(!std::is_copy_constructible_v<zxv::freight::OpsWriter>);
}

void test_buffer_growth()
{
    // The two-call pattern: a buffer that is too small is grown once to the
    // reported length, for text and for bytes.
    const std::string in = "the quick brown fox jumps over the lazy dog; the quick brown fox";
    const auto direct = zxv::freight::encode_ops(zxv::as_bytes(in), true);
    int calls = 0;
    const auto grown = zxv::detail::read_bytes([&](std::uint8_t* b, std::size_t c, std::size_t* n) {
        ++calls;
        return zxv_freight_ops_encode(reinterpret_cast<const std::uint8_t*>(in.data()), in.size(), 1, b, c, n);
    }, nullptr, 1);
    CHECK(calls == 2 && grown == direct);
    calls = 0;
    const auto empty_first = zxv::detail::read_bytes([&](std::uint8_t* b, std::size_t c, std::size_t* n) {
        ++calls;
        return zxv_freight_ops_encode(reinterpret_cast<const std::uint8_t*>(in.data()), in.size(), 1, b, c, n);
    }, nullptr, 0);
    CHECK(calls == 2 && empty_first == direct);
    // Large results: a 1 MB UnixFS CID input, a 3360-byte freight decode, a
    // 64-entry treaty result record, a manifest (all past the 256-byte guess).
    const auto big = zxv::freight::split(5, 168, std::vector<std::uint8_t>(10000, 0x42), 0);
    zxv::freight::Join j(20000);
    for (std::uint32_t s = 0; s < zxv::freight::set_count(10000, 168); ++s)
        j.add(zxv::freight::split(5, 168, std::vector<std::uint8_t>(10000, 0x42), s));
    CHECK(j.result() == std::vector<std::uint8_t>(10000, 0x42));
    CHECK(zxv::freight::decode(big.header, big.packets).size() == zxv::freight::max_payload);
    const std::string t = zxv::treaty::Treaty::from_sutra(
                              "TREATY CORRIDOR-7 USD SETL\nPARTY ACME 5000000 4000 1/3\nPARTY BETA 3000000 3000 1/7\n"
                              "PARTY GAMMA 900000 0 11/21\nOWE ACME BETA 1200000\nOWE BETA GAMMA 700000\n"
                              "OWE GAMMA ACME 100000\nEND\n")
                              .sutra();
    CHECK(t.size() > 100);
    // Text two-call: the manifest JSON.
    zxv::web4::Manifest m(1, 0, 10);
    for (int i = 0; i < 16; ++i) m.add_tool({"tool" + std::to_string(i), 1, 1, 1, 0, 0});
    zxv::Context ctx;
    const auto cap = ctx.root_capability(zxv::caps::all_1_5);
    zxv::web4::Agent a(cap, "signer", 0);
    m.sign(a);
    CHECK(m.to_json().size() > 1000);
    CHECK_ST(m.add_tool({"tool16", 1, 1, 1, 0, 0}), "ZXV_E_CAPACITY");
    CHECK(m.encode().size() > zxv::web4::sig_bytes);
}

void test_capability_refusals()
{
    zxv::Context ctx;
    // A capability without AIPI, MINT or PARADOX gets the capability error.
    const auto weak = ctx.root_capability(zxv::caps::all_1_4);
    CHECK_THROWS_AS(zxv::aipi::Node(weak, 1u << 20, 4), zxv::CapabilityDenied);
    CHECK_ST(zxv::aipi::Node(weak, 1u << 20, 4), "ZXV_E_CAP");
    CHECK_THROWS_AS(zxv::token::Wallet(weak, 1, 0), zxv::CapabilityDenied);
    CHECK_THROWS_AS(zxv::paradox::Book(weak, 4, 4), zxv::CapabilityDenied);
    CHECK_THROWS_AS(zxv::paradox::Policy(weak, std::vector<std::int64_t>{0}, std::vector<std::int64_t>{1},
                                         std::vector<std::int64_t>{0}),
                    zxv::CapabilityDenied);
    CHECK_THROWS_AS(zxv::paradox::Hold(weak, 2), zxv::CapabilityDenied);
    CHECK_THROWS_AS(zxv::paradox::Graph(weak, 2), zxv::CapabilityDenied);
    // Handles made with a full capability keep it, so later calls succeed;
    // a handle made with ONLY the 1.5 bit refuses calls needing other bits.
    const auto mint_only = ctx.root_capability(zxv::caps::mint);
    zxv::token::Wallet w(mint_only, 1, 0);
    w.mint(10);
    CHECK(w.info().value == 10);
    const auto aipi_only = ctx.root_capability(zxv::caps::aipi);
    zxv::aipi::Node n(aipi_only, 1u << 20, 4);
    CHECK(n.put(zxv::as_bytes("x")).size() > 0);
    const auto pdx_only = ctx.root_capability(zxv::caps::paradox);
    zxv::paradox::Book b(pdx_only, 4, 4);
    b.registry_add(zxv::paradox::signature("y"), zxv::paradox::Kind::neutral, 0);
    CHECK(b.registry_count() == 1);
    // A handle from another context's capability is refused (no mixing).
    zxv::Context other;
    const auto ocap = other.root_capability(zxv::caps::all_1_5);
    zxv::paradox::Hold foreign(ocap, 2);
    CHECK_THROWS_AS(b.neutral(foreign, zxv::paradox::Kind::neutral, zxv::Hexagram::from_tag(39)), zxv::UsageError);
}

void test_poisoned_context()
{
    zxv::Context ctx;
    const auto cap = ctx.root_capability(zxv::caps::all_1_5);
    zxv::paradox::Book book(cap, 8, 8);
    zxv::token::Wallet w(cap, 1, 0);
    zxv::vhub::Hub hub(cap);
    zxv::web4::Agent ag(cap, "a", 0);
    w.mint(5);
    ctx.halt("test");
    CHECK(ctx.is_poisoned());
    // Every gated call on a handle of a halted context fails closed.
    CHECK_THROWS_AS(book.registry_add(zxv::paradox::signature("z"), zxv::paradox::Kind::neutral, 0), zxv::FatalError);
    CHECK_ST(book.registry_add(zxv::paradox::signature("z"), zxv::paradox::Kind::neutral, 0), "ZXV_E_POISONED");
    CHECK_ST((void) book.log_count(zxv::paradox::Log::ops), "ZXV_E_POISONED");
    CHECK_ST(w.mint(1), "ZXV_E_POISONED");
    CHECK_ST((void) w.info(), "ZXV_E_POISONED");
    CHECK_ST(hub.check(), "ZXV_E_POISONED");
    CHECK_ST((void) ag.card(), "ZXV_E_POISONED");
    CHECK_ST(zxv::paradox::Book(cap, 1, 1), "ZXV_E_POISONED");
    CHECK_ST(zxv::aipi::Node(cap, 1u << 20, 1), "ZXV_E_POISONED");
    // Pure functions are unaffected.
    CHECK(zxv::fee::assure(1125) == 1);
    CHECK(zxv::coin::face(5) == 8);
}

} // namespace

int main(int argc, char** argv)
{
    const std::string dir = argc > 1 ? argv[1] : "../conformance";
    g_vec = mini_json::load(dir + "/vectors.json");
    const std::pair<const char*, void (*)()> tests[] = {
        {"status_abi15", test_status_abi15}, {"treaty", test_treaty}, {"cid", test_cid},
        {"aipi", test_aipi},                 {"coin", test_coin},     {"token", test_token},
        {"fee", test_fee},                   {"vhub", test_vhub},     {"freight", test_freight},
        {"web4", test_web4},                 {"paradox", test_paradox}, {"paradox_logs", test_paradox_logs},
        {"lifetimes", test_lifetimes},       {"buffer_growth", test_buffer_growth},
        {"capability_refusals", test_capability_refusals}, {"poisoned_context", test_poisoned_context},
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
    std::cout << "test_zxv_abi15: " << g_checks << " checks, " << g_failures << " failures\n";
    return g_failures ? 1 : 0;
}
