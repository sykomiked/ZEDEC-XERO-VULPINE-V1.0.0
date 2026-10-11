// Copyright (c) 2024-2026 Michael Laurence Curzi and 36N9 Genetics, LLC
// SPDX-License-Identifier: Apache-2.0
#include "zxv_gd_core.hpp"

#include <utility>

namespace zxvgd {

namespace {

thread_local std::string g_detail;

std::int32_t set_detail(std::int32_t rc, const std::string &text) {
    g_detail = text;
    return rc;
}

// A libzxv call failed: keep its detail text.
std::int32_t zxv_fail(std::int32_t rc) {
    char buf[512];
    std::size_t n = 0;
    if (zxv_last_error(buf, sizeof buf, &n) == ZXV_OK)
        g_detail.assign(buf, n);
    else
        g_detail = status_name(rc);
    return rc;
}

// An emu-state call failed.
std::int32_t emu_fail(std::int32_t rc) {
    char buf[512];
    std::size_t n = 0;
    if (zxv_emu_last_error(buf, sizeof buf, &n) == ZXV_OK && n > 0)
        g_detail.assign(buf, n);
    else
        g_detail = status_name(rc);
    return rc;
}

std::int32_t emu_rc(std::int32_t rc) { return rc == ZXV_OK ? rc : emu_fail(rc); }

} // namespace

std::string last_error() { return g_detail; }

const char *status_name(std::int32_t status) {
    const char *n = zxv_status_name(status);
    return n ? n : "ZXV_E_UNKNOWN";
}

std::int32_t check_library() {
    std::uint32_t mj = 0, mn = 0, pt = 0;
    std::int32_t rc = zxv_version(&mj, &mn, &pt);
    if (rc != ZXV_OK) return zxv_fail(rc);
    if (mj != 1 || mn < 3)
        return set_detail(ZXV_E_NOTIMPL, "libzxv " + std::to_string(mj) + "." + std::to_string(mn) +
                                             " loaded; 1.3 or later (major 1) is required");
    return ZXV_OK;
}

// ---- Rational ---------------------------------------------------------

std::int32_t Rational::from_ints(std::int64_t num, std::int64_t den, Rational *out) {
    if (!out) return set_detail(ZXV_E_ARG, "out is null");
    std::int64_t n = 0, d = 1;
    std::int32_t rc = zxv_rat_normalize(num, den, &n, &d);
    if (rc != ZXV_OK) return zxv_fail(rc);
    out->num_ = n;
    out->den_ = d;
    return ZXV_OK;
}

std::int32_t Rational::parse(const std::string &text, Rational *out) {
    if (!out) return set_detail(ZXV_E_ARG, "out is null");
    std::int64_t n = 0, d = 1;
    std::int32_t rc = zxv_rat_parse(text.c_str(), &n, &d);
    if (rc != ZXV_OK) return zxv_fail(rc);
    out->num_ = n;
    out->den_ = d;
    return ZXV_OK;
}

std::string Rational::to_string() const {
    std::size_t need = 0;
    zxv_rat_format(num_, den_, nullptr, 0, &need);
    std::string s(need + 1, '\0');
    if (zxv_rat_format(num_, den_, &s[0], s.size(), &need) != ZXV_OK) return to_fraction_string();
    s.resize(need);
    return s;
}

std::string Rational::to_fraction_string() const {
    return std::to_string(num_) + "/" + std::to_string(den_);
}

#define ZXVGD_BINOP(name, fn)                                                  \
    std::int32_t Rational::name(const Rational &b, Rational *out) const {      \
        if (!out) return set_detail(ZXV_E_ARG, "out is null");                 \
        std::int64_t n = 0, d = 1;                                             \
        std::int32_t rc = fn(num_, den_, b.num_, b.den_, &n, &d);              \
        if (rc != ZXV_OK) return zxv_fail(rc);                                 \
        out->num_ = n;                                                         \
        out->den_ = d;                                                         \
        return ZXV_OK;                                                         \
    }
ZXVGD_BINOP(add, zxv_rat_add)
ZXVGD_BINOP(sub, zxv_rat_sub)
ZXVGD_BINOP(mul, zxv_rat_mul)
ZXVGD_BINOP(div, zxv_rat_div)
#undef ZXVGD_BINOP

std::int32_t Rational::neg(Rational *out) const {
    Rational zero;
    return zero.sub(*this, out);
}

int Rational::compare(const Rational &b) const {
    std::int32_t c = 0;
    if (zxv_rat_cmp(num_, den_, b.num_, b.den_, &c) != ZXV_OK) return 0; // unreachable for valid values
    return c;
}

// ---- Hexagram ---------------------------------------------------------

std::int32_t Hexagram::from_tag(int tag, Hexagram *out) {
    if (!out) return set_detail(ZXV_E_ARG, "out is null");
    if (tag < 0 || tag > 63) return set_detail(ZXV_E_ARG, "a hexagram tag is 0..63");
    out->tag_ = static_cast<std::uint8_t>(tag);
    return ZXV_OK;
}

std::int32_t Hexagram::from_fields(int base, int polarity, int phase, Hexagram *out) {
    if (!out) return set_detail(ZXV_E_ARG, "out is null");
    if (base < 0 || base > 3 || polarity < 0 || polarity > 3 || phase < 0 || phase > 3)
        return set_detail(ZXV_E_ARG, "base, polarity and phase are each 0..3");
    std::uint8_t t = 0;
    std::int32_t rc = zxv_hex_pack(static_cast<std::uint8_t>(base), static_cast<std::uint8_t>(polarity),
                                   static_cast<std::uint8_t>(phase), &t);
    if (rc != ZXV_OK) return zxv_fail(rc);
    out->tag_ = t;
    return ZXV_OK;
}

std::int32_t Hexagram::from_truth(int lp, int polarity, int phase, Hexagram *out) {
    std::uint8_t base = 0;
    std::int32_t rc = zxv_hex_from_lp(lp, &base);
    if (rc != ZXV_OK) return zxv_fail(rc);
    return from_fields(base, polarity, phase, out);
}

int Hexagram::base() const {
    std::uint8_t b = 0;
    zxv_hex_unpack(tag_, &b, nullptr, nullptr);
    return b;
}
int Hexagram::polarity() const {
    std::uint8_t p = 0;
    zxv_hex_unpack(tag_, nullptr, &p, nullptr);
    return p;
}
int Hexagram::phase() const {
    std::uint8_t p = 0;
    zxv_hex_unpack(tag_, nullptr, nullptr, &p);
    return p;
}
int Hexagram::action() const {
    std::int32_t a = 0;
    zxv_hex_action(tag_, &a);
    return a;
}
int Hexagram::to_truth() const {
    std::int32_t lp = 0;
    zxv_hex_to_lp(static_cast<std::uint8_t>(base()), &lp);
    return lp;
}

std::int32_t Hexagram::step(int phase_, Hexagram *out) const {
    if (!out) return set_detail(ZXV_E_ARG, "out is null");
    if (phase_ < 0 || phase_ > 3) return set_detail(ZXV_E_ARG, "a phase is 0..3");
    std::uint8_t t = 0;
    std::int32_t rc = zxv_hex_step(tag_, static_cast<std::uint8_t>(phase_), &t);
    if (rc != ZXV_OK) return zxv_fail(rc);
    out->tag_ = t;
    return ZXV_OK;
}

Hexagram Hexagram::advance() const {
    std::uint8_t next = 0, t = tag_;
    zxv_hex_phase_next(static_cast<std::uint8_t>(phase()), &next);
    zxv_hex_step(tag_, next, &t);
    return Hexagram(t);
}

std::int32_t Hexagram::with_state(int base_, int polarity_, Hexagram *out) const {
    if (!out) return set_detail(ZXV_E_ARG, "out is null");
    if (base_ < 0 || base_ > 3 || polarity_ < 0 || polarity_ > 3)
        return set_detail(ZXV_E_ARG, "base and polarity are each 0..3");
    std::uint8_t t = 0;
    std::int32_t rc = zxv_hex_set(tag_, static_cast<std::uint8_t>(base_),
                                  static_cast<std::uint8_t>(polarity_), &t);
    if (rc != ZXV_OK) return zxv_fail(rc);
    out->tag_ = t;
    return ZXV_OK;
}

Hexagram Hexagram::forbid() const {
    std::uint8_t t = tag_;
    zxv_hex_forbid(tag_, &t);
    return Hexagram(t);
}
Hexagram Hexagram::and_(const Hexagram &b) const {
    std::uint8_t t = 0;
    zxv_hex_and(tag_, b.tag_, &t);
    return Hexagram(t);
}
Hexagram Hexagram::or_(const Hexagram &b) const {
    std::uint8_t t = 0;
    zxv_hex_or(tag_, b.tag_, &t);
    return Hexagram(t);
}
Hexagram Hexagram::not_() const {
    std::uint8_t t = 0;
    zxv_hex_not(tag_, &t);
    return Hexagram(t);
}

// ---- Lockstep ---------------------------------------------------------

Lockstep::~Lockstep() { reset(); }

void Lockstep::reset() {
    // R6: the sim (and its rmag) before the caps, children before parents,
    // caps before their context.
    if (sim_) zxv_emu_destroy(sim_);
    if (budget_cap_) zxv_cap_destroy(budget_cap_);
    if (root_) zxv_cap_destroy(root_);
    if (ctx_) zxv_ctx_destroy(ctx_);
    sim_ = nullptr;
    budget_cap_ = nullptr;
    root_ = nullptr;
    ctx_ = nullptr;
}

Lockstep::Lockstep(Lockstep &&o) noexcept
    : sim_(std::exchange(o.sim_, nullptr)), ctx_(std::exchange(o.ctx_, nullptr)),
      root_(std::exchange(o.root_, nullptr)), budget_cap_(std::exchange(o.budget_cap_, nullptr)) {}

Lockstep &Lockstep::operator=(Lockstep &&o) noexcept {
    if (this != &o) {
        reset();
        sim_ = std::exchange(o.sim_, nullptr);
        ctx_ = std::exchange(o.ctx_, nullptr);
        root_ = std::exchange(o.root_, nullptr);
        budget_cap_ = std::exchange(o.budget_cap_, nullptr);
    }
    return *this;
}

#define ZXVGD_NEED_SIM()                                                               \
    do {                                                                               \
        if (!sim_) return set_detail(ZXV_E_STATE, "the lockstep sim is not created"); \
    } while (0)

std::int32_t Lockstep::create(std::uint32_t players, std::uint32_t rat_slots,
                              std::uint32_t hex_slots, std::uint32_t input_bytes,
                              std::uint32_t history, bool predict) {
    std::int32_t rc = check_library();
    if (rc != ZXV_OK) return rc;
    zxv_emu *s = nullptr;
    rc = zxv_emu_create(players, rat_slots, hex_slots, input_bytes, history,
                        predict ? ZXV_EMU_F_PREDICT : 0u, &s);
    if (rc != ZXV_OK) return emu_fail(rc);
    reset();
    sim_ = s;
    return ZXV_OK;
}

std::int32_t Lockstep::set_tick_budget(const Rational &limit) {
    ZXVGD_NEED_SIM();
    std::int32_t rc;
    if (!ctx_) {
        if ((rc = zxv_ctx_create(&ctx_)) != ZXV_OK) return zxv_fail(rc);
        if ((rc = zxv_cap_create(ctx_, 1, nullptr, ZXV_CAP_ALL, &root_)) != ZXV_OK ||
            (rc = zxv_cap_set_attestation(root_, ZXV_LP_TRUE)) != ZXV_OK ||
            (rc = zxv_cap_attenuate(root_, ZXV_CAP_BUDGET, &budget_cap_)) != ZXV_OK) {
            zxv_fail(rc);
            if (budget_cap_) zxv_cap_destroy(budget_cap_);
            if (root_) zxv_cap_destroy(root_);
            zxv_ctx_destroy(ctx_);
            budget_cap_ = nullptr;
            root_ = nullptr;
            ctx_ = nullptr;
            return rc;
        }
    }
    return emu_rc(zxv_emu_set_tick_budget(sim_, budget_cap_, limit.num(), limit.den()));
}

std::int32_t Lockstep::start() {
    ZXVGD_NEED_SIM();
    return emu_rc(zxv_emu_start(sim_));
}

std::int32_t Lockstep::clock_set_step(const Rational &seconds, std::uint32_t max_catchup) {
    ZXVGD_NEED_SIM();
    return emu_rc(zxv_emu_clock_set_step(sim_, seconds.num(), seconds.den(), max_catchup));
}

std::int32_t Lockstep::clock_feed_usec(std::int64_t usec, std::uint32_t *due,
                                       std::uint64_t *dropped) {
    ZXVGD_NEED_SIM();
    return emu_rc(zxv_emu_clock_feed(sim_, usec, 1000000, due, dropped));
}

std::int32_t Lockstep::clock_alpha(Rational *out) const {
    ZXVGD_NEED_SIM();
    if (!out) return set_detail(ZXV_E_ARG, "out is null");
    std::int64_t n = 0, d = 1;
    std::int32_t rc = zxv_emu_clock_alpha(sim_, &n, &d);
    if (rc != ZXV_OK) return emu_fail(rc);
    return Rational::from_ints(n, d, out);
}

std::int32_t Lockstep::submit_input(std::uint32_t player, std::uint64_t tick,
                                    const std::vector<std::uint8_t> &bytes) {
    ZXVGD_NEED_SIM();
    return emu_rc(zxv_emu_input_submit(sim_, player, tick, bytes.data(), bytes.size()));
}

std::int32_t Lockstep::input(std::uint32_t player, std::vector<std::uint8_t> *out,
                             bool *predicted) const {
    ZXVGD_NEED_SIM();
    if (!out) return set_detail(ZXV_E_ARG, "out is null");
    std::size_t need = 0;
    std::int32_t rc = zxv_emu_input_get(sim_, player, nullptr, 0, &need, nullptr);
    if (rc != ZXV_E_BUFFER) return emu_fail(rc);
    out->assign(need, 0);
    std::int32_t pred = 0;
    rc = zxv_emu_input_get(sim_, player, out->data(), out->size(), &need, &pred);
    if (rc != ZXV_OK) return emu_fail(rc);
    if (predicted) *predicted = pred != 0;
    return ZXV_OK;
}

bool Lockstep::ready() const {
    std::int32_t r = 0;
    return sim_ && zxv_emu_ready(sim_, &r) == ZXV_OK && r != 0;
}

std::uint64_t Lockstep::tick() const {
    std::uint64_t t = 0;
    if (sim_) zxv_emu_tick(sim_, &t);
    return t;
}

std::int32_t Lockstep::begin() {
    ZXVGD_NEED_SIM();
    return emu_rc(zxv_emu_begin(sim_));
}
std::int32_t Lockstep::commit() {
    ZXVGD_NEED_SIM();
    return emu_rc(zxv_emu_commit(sim_));
}
std::int32_t Lockstep::abort() {
    ZXVGD_NEED_SIM();
    return emu_rc(zxv_emu_abort(sim_));
}

// begin / step / commit-or-abort, keeping whichever detail text belongs to
// the failure (the step's own, or emu-state's).
std::int32_t Lockstep::advance(const Step &step) {
    ZXVGD_NEED_SIM();
    if (!step) return set_detail(ZXV_E_ARG, "step is empty");
    std::int32_t rc = zxv_emu_begin(sim_);
    if (rc != ZXV_OK) return emu_fail(rc);
    g_detail.clear();
    rc = step(*this, tick() + 1);
    if (rc != ZXV_OK) {
        if (g_detail.empty())
            g_detail = std::string("the step returned ") + status_name(rc);
        zxv_emu_abort(sim_);
        return rc;
    }
    return emu_rc(zxv_emu_commit(sim_));
}

std::int32_t Lockstep::get_rational(std::uint32_t slot, Rational *out) const {
    ZXVGD_NEED_SIM();
    if (!out) return set_detail(ZXV_E_ARG, "out is null");
    std::int64_t n = 0, d = 1;
    std::int32_t rc = zxv_emu_rat_get(sim_, slot, &n, &d);
    if (rc != ZXV_OK) return emu_fail(rc);
    return Rational::from_ints(n, d, out);
}
std::int32_t Lockstep::set_rational(std::uint32_t slot, const Rational &v) {
    ZXVGD_NEED_SIM();
    return emu_rc(zxv_emu_rat_set(sim_, slot, v.num(), v.den()));
}
std::int32_t Lockstep::add_rational(std::uint32_t slot, const Rational &v) {
    ZXVGD_NEED_SIM();
    return emu_rc(zxv_emu_rat_add(sim_, slot, v.num(), v.den()));
}
std::int32_t Lockstep::mul_rational(std::uint32_t slot, const Rational &v) {
    ZXVGD_NEED_SIM();
    return emu_rc(zxv_emu_rat_mul(sim_, slot, v.num(), v.den()));
}
std::int32_t Lockstep::get_hexagram(std::uint32_t slot, Hexagram *out) const {
    ZXVGD_NEED_SIM();
    if (!out) return set_detail(ZXV_E_ARG, "out is null");
    std::uint8_t t = 0;
    std::int32_t rc = zxv_emu_hex_get(sim_, slot, &t);
    if (rc != ZXV_OK) return emu_fail(rc);
    return Hexagram::from_tag(t, out);
}
std::int32_t Lockstep::set_hexagram(std::uint32_t slot, const Hexagram &h) {
    ZXVGD_NEED_SIM();
    return emu_rc(zxv_emu_hex_set(sim_, slot, static_cast<std::uint8_t>(h.tag())));
}
std::int32_t Lockstep::charge(const Rational &amount) {
    ZXVGD_NEED_SIM();
    return emu_rc(zxv_emu_charge(sim_, amount.num(), amount.den()));
}

std::int32_t Lockstep::hash(std::uint64_t tick_, std::vector<std::uint8_t> *out,
                            bool *final_) const {
    ZXVGD_NEED_SIM();
    if (!out) return set_detail(ZXV_E_ARG, "out is null");
    std::uint8_t h[ZXV_EMU_HASH_BYTES];
    std::int32_t fin = 0;
    std::int32_t rc = zxv_emu_hash(sim_, tick_, h, &fin);
    if (rc != ZXV_OK) return emu_fail(rc);
    out->assign(h, h + sizeof h);
    if (final_) *final_ = fin != 0;
    return ZXV_OK;
}

std::uint64_t Lockstep::confirmed_tick() {
    std::uint64_t c = 0;
    if (sim_) zxv_emu_confirmed_tick(sim_, &c);
    return c;
}

std::int32_t Lockstep::compare(std::uint64_t tick_, const std::vector<std::uint8_t> &remote,
                               int *lp) {
    ZXVGD_NEED_SIM();
    if (!lp) return set_detail(ZXV_E_ARG, "lp is null");
    if (remote.size() != ZXV_EMU_HASH_BYTES)
        return set_detail(ZXV_E_ARG, "a state hash is 32 bytes");
    std::int32_t v = ZXV_LP_NEITHER;
    std::int32_t rc = zxv_emu_compare(sim_, tick_, remote.data(), &v);
    if (rc != ZXV_OK) return emu_fail(rc);
    *lp = v;
    return ZXV_OK;
}

bool Lockstep::desync(std::uint64_t *tick_) const {
    std::uint64_t t = 0;
    std::int32_t has = 0;
    if (!sim_ || zxv_emu_desync(sim_, &t, &has) != ZXV_OK || !has) return false;
    if (tick_) *tick_ = t;
    return true;
}

bool Lockstep::rollback_pending(std::uint64_t *tick_) const {
    std::uint64_t t = 0;
    std::int32_t p = 0;
    if (!sim_ || zxv_emu_rollback_pending(sim_, &t, &p) != ZXV_OK || !p) return false;
    if (tick_) *tick_ = t;
    return true;
}

std::int32_t Lockstep::rollback(std::uint64_t to_tick) {
    ZXVGD_NEED_SIM();
    return emu_rc(zxv_emu_rollback(sim_, to_tick));
}

std::int32_t Lockstep::rollback_replay(const Step &step) {
    ZXVGD_NEED_SIM();
    if (!step) return set_detail(ZXV_E_ARG, "step is empty");
    std::uint64_t pending = 0;
    if (!rollback_pending(&pending)) return ZXV_OK;
    const std::uint64_t target = tick();
    std::int32_t rc = rollback(pending - 1);
    while (rc == ZXV_OK && tick() < target) rc = advance(step);
    return rc;
}

std::int32_t Lockstep::export_state(std::vector<std::uint8_t> *out) const {
    ZXVGD_NEED_SIM();
    if (!out) return set_detail(ZXV_E_ARG, "out is null");
    std::size_t need = 0;
    std::int32_t rc = zxv_emu_state_export(sim_, nullptr, 0, &need);
    if (rc != ZXV_E_BUFFER) return emu_fail(rc);
    out->assign(need, 0);
    rc = zxv_emu_state_export(sim_, out->data(), out->size(), &need);
    if (rc != ZXV_OK) return emu_fail(rc);
    return ZXV_OK;
}

std::int32_t Lockstep::import_state(const std::vector<std::uint8_t> &bytes) {
    ZXVGD_NEED_SIM();
    return emu_rc(zxv_emu_state_import(sim_, bytes.data(), bytes.size()));
}

#undef ZXVGD_NEED_SIM

} // namespace zxvgd
