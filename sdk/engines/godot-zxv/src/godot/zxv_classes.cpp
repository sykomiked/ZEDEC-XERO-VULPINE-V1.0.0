// Copyright (c) 2024-2026 Michael Laurence Curzi and 36N9 Genetics, LLC
// SPDX-License-Identifier: Apache-2.0
#include "zxv_classes.h"

#include <string>
#include <vector>

#include <godot_cpp/core/class_db.hpp>
#include <godot_cpp/variant/array.hpp>
#include <godot_cpp/variant/utility_functions.hpp>

using namespace godot;

namespace zxv_godot {

namespace {

void report(int32_t rc, const char *where) {
    if (rc == ZXV_OK) return;
    UtilityFunctions::push_error(String(where) + ": " + String(zxvgd::status_name(rc)) + " (" +
                                 String(zxvgd::last_error().c_str()) + ")");
}

std::vector<uint8_t> to_bytes(const PackedByteArray &a) {
    return std::vector<uint8_t>(a.ptr(), a.ptr() + a.size());
}

PackedByteArray to_packed(const std::vector<uint8_t> &v) {
    PackedByteArray a;
    a.resize(static_cast<int64_t>(v.size()));
    for (size_t i = 0; i < v.size(); i++) a.set(static_cast<int64_t>(i), v[i]);
    return a;
}

bool slot_ok(int64_t v) { return v >= 0 && v <= 0xFFFFFFFFll; }

} // namespace

// ---- ZxvRational -------------------------------------------------------

Ref<ZxvRational> ZxvRational::wrap(const zxvgd::Rational &v) {
    Ref<ZxvRational> r;
    r.instantiate();
    r->value_ = v;
    return r;
}

Ref<ZxvRational> ZxvRational::from_ints(const Variant &num, const Variant &den) {
    if (num.get_type() != Variant::INT || den.get_type() != Variant::INT) {
        UtilityFunctions::push_error(
            "ZxvRational.from_ints takes two ints; a float is refused (use parse(\"0.5\"))");
        return Ref<ZxvRational>();
    }
    zxvgd::Rational v;
    int32_t rc = zxvgd::Rational::from_ints(static_cast<int64_t>(num), static_cast<int64_t>(den), &v);
    report(rc, "ZxvRational.from_ints");
    return rc == ZXV_OK ? wrap(v) : Ref<ZxvRational>();
}

Ref<ZxvRational> ZxvRational::parse(const Variant &text) {
    if (text.get_type() != Variant::STRING && text.get_type() != Variant::STRING_NAME) {
        UtilityFunctions::push_error("ZxvRational.parse takes a String such as \"12.75\" or \"3/4\"");
        return Ref<ZxvRational>();
    }
    String s = text;
    zxvgd::Rational v;
    int32_t rc = zxvgd::Rational::parse(std::string(s.utf8().get_data()), &v);
    report(rc, "ZxvRational.parse");
    return rc == ZXV_OK ? wrap(v) : Ref<ZxvRational>();
}

Ref<ZxvRational> ZxvRational::from_float(const Variant &) {
    UtilityFunctions::push_error(
        "ZxvRational.from_float is refused: a float is not exact. Use from_ints(num, den) or "
        "parse(\"text\") so every peer starts from the same value.");
    return Ref<ZxvRational>();
}

String ZxvRational::last_error() { return String(zxvgd::last_error().c_str()); }

String ZxvRational::text() const { return String(value_.to_string().c_str()); }
String ZxvRational::fraction() const { return String(value_.to_fraction_string().c_str()); }

#define ZXV_GODOT_RAT_OP(name)                                                  \
    Ref<ZxvRational> ZxvRational::name(const Ref<ZxvRational> &b) const {      \
        if (b.is_null()) {                                                      \
            UtilityFunctions::push_error("ZxvRational." #name ": null operand"); \
            return Ref<ZxvRational>();                                          \
        }                                                                       \
        zxvgd::Rational out;                                                    \
        int32_t rc = value_.name(b->value_, &out);                              \
        report(rc, "ZxvRational." #name);                                       \
        return rc == ZXV_OK ? wrap(out) : Ref<ZxvRational>();                   \
    }
ZXV_GODOT_RAT_OP(add)
ZXV_GODOT_RAT_OP(sub)
ZXV_GODOT_RAT_OP(mul)
ZXV_GODOT_RAT_OP(div)
#undef ZXV_GODOT_RAT_OP

Ref<ZxvRational> ZxvRational::neg() const {
    zxvgd::Rational out;
    int32_t rc = value_.neg(&out);
    report(rc, "ZxvRational.neg");
    return rc == ZXV_OK ? wrap(out) : Ref<ZxvRational>();
}

int64_t ZxvRational::compare(const Ref<ZxvRational> &b) const {
    if (b.is_null()) {
        UtilityFunctions::push_error("ZxvRational.compare: null operand");
        return 0;
    }
    return value_.compare(b->value_);
}

bool ZxvRational::equals(const Ref<ZxvRational> &b) const {
    return b.is_valid() && value_ == b->value_;
}

double ZxvRational::to_float_lossy() const {
    // The one place a float is made, at the very edge, for drawing.
    return static_cast<double>(value_.num()) / static_cast<double>(value_.den());
}

void ZxvRational::_bind_methods() {
    ClassDB::bind_static_method("ZxvRational", D_METHOD("from_ints", "num", "den"), &ZxvRational::from_ints);
    ClassDB::bind_static_method("ZxvRational", D_METHOD("parse", "text"), &ZxvRational::parse);
    ClassDB::bind_static_method("ZxvRational", D_METHOD("from_float", "x"), &ZxvRational::from_float);
    ClassDB::bind_static_method("ZxvRational", D_METHOD("last_error"), &ZxvRational::last_error);
    ClassDB::bind_method(D_METHOD("numerator"), &ZxvRational::numerator);
    ClassDB::bind_method(D_METHOD("denominator"), &ZxvRational::denominator);
    ClassDB::bind_method(D_METHOD("is_zero"), &ZxvRational::is_zero);
    ClassDB::bind_method(D_METHOD("text"), &ZxvRational::text);
    ClassDB::bind_method(D_METHOD("fraction"), &ZxvRational::fraction);
    ClassDB::bind_method(D_METHOD("add", "other"), &ZxvRational::add);
    ClassDB::bind_method(D_METHOD("sub", "other"), &ZxvRational::sub);
    ClassDB::bind_method(D_METHOD("mul", "other"), &ZxvRational::mul);
    ClassDB::bind_method(D_METHOD("div", "other"), &ZxvRational::div);
    ClassDB::bind_method(D_METHOD("neg"), &ZxvRational::neg);
    ClassDB::bind_method(D_METHOD("compare", "other"), &ZxvRational::compare);
    ClassDB::bind_method(D_METHOD("equals", "other"), &ZxvRational::equals);
    ClassDB::bind_method(D_METHOD("to_float_lossy"), &ZxvRational::to_float_lossy);
}

// ---- ZxvHexagram -------------------------------------------------------

Ref<ZxvHexagram> ZxvHexagram::wrap(const zxvgd::Hexagram &h) {
    Ref<ZxvHexagram> r;
    r.instantiate();
    r->value_ = h;
    return r;
}

Ref<ZxvHexagram> ZxvHexagram::from_tag(int64_t tag) {
    zxvgd::Hexagram h;
    int32_t rc = zxvgd::Hexagram::from_tag(static_cast<int>(tag < 0 || tag > 63 ? -1 : tag), &h);
    report(rc, "ZxvHexagram.from_tag");
    return rc == ZXV_OK ? wrap(h) : Ref<ZxvHexagram>();
}

Ref<ZxvHexagram> ZxvHexagram::from_fields(int64_t base, int64_t polarity, int64_t phase) {
    zxvgd::Hexagram h;
    int32_t rc = (base < 0 || base > 3 || polarity < 0 || polarity > 3 || phase < 0 || phase > 3)
                     ? zxvgd::Hexagram::from_fields(-1, 0, 0, &h)
                     : zxvgd::Hexagram::from_fields(static_cast<int>(base), static_cast<int>(polarity),
                                                    static_cast<int>(phase), &h);
    report(rc, "ZxvHexagram.from_fields");
    return rc == ZXV_OK ? wrap(h) : Ref<ZxvHexagram>();
}

Ref<ZxvHexagram> ZxvHexagram::from_truth(int64_t truth, int64_t polarity, int64_t phase) {
    zxvgd::Hexagram h;
    int32_t rc = (truth < 0 || truth > 3 || polarity < 0 || polarity > 3 || phase < 0 || phase > 3)
                     ? zxvgd::Hexagram::from_truth(-1, 0, 0, &h)
                     : zxvgd::Hexagram::from_truth(static_cast<int>(truth), static_cast<int>(polarity),
                                                   static_cast<int>(phase), &h);
    report(rc, "ZxvHexagram.from_truth");
    return rc == ZXV_OK ? wrap(h) : Ref<ZxvHexagram>();
}

Ref<ZxvHexagram> ZxvHexagram::step(int64_t phase_) const {
    zxvgd::Hexagram h;
    int32_t rc = value_.step(phase_ < 0 || phase_ > 3 ? -1 : static_cast<int>(phase_), &h);
    report(rc, "ZxvHexagram.step");
    return rc == ZXV_OK ? wrap(h) : Ref<ZxvHexagram>();
}

Ref<ZxvHexagram> ZxvHexagram::with_state(int64_t base_, int64_t polarity_) const {
    zxvgd::Hexagram h;
    int32_t rc = (base_ < 0 || base_ > 3 || polarity_ < 0 || polarity_ > 3)
                     ? value_.with_state(-1, 0, &h)
                     : value_.with_state(static_cast<int>(base_), static_cast<int>(polarity_), &h);
    report(rc, "ZxvHexagram.with_state");
    return rc == ZXV_OK ? wrap(h) : Ref<ZxvHexagram>();
}

Ref<ZxvHexagram> ZxvHexagram::and_(const Ref<ZxvHexagram> &b) const {
    if (b.is_null()) {
        UtilityFunctions::push_error("ZxvHexagram.and_: null operand");
        return Ref<ZxvHexagram>();
    }
    return wrap(value_.and_(b->value_));
}

Ref<ZxvHexagram> ZxvHexagram::or_(const Ref<ZxvHexagram> &b) const {
    if (b.is_null()) {
        UtilityFunctions::push_error("ZxvHexagram.or_: null operand");
        return Ref<ZxvHexagram>();
    }
    return wrap(value_.or_(b->value_));
}

bool ZxvHexagram::equals(const Ref<ZxvHexagram> &b) const { return b.is_valid() && value_ == b->value_; }

void ZxvHexagram::_bind_methods() {
    ClassDB::bind_static_method("ZxvHexagram", D_METHOD("from_tag", "tag"), &ZxvHexagram::from_tag);
    ClassDB::bind_static_method("ZxvHexagram", D_METHOD("from_fields", "base", "polarity", "phase"),
                                &ZxvHexagram::from_fields);
    ClassDB::bind_static_method("ZxvHexagram", D_METHOD("from_truth", "truth", "polarity", "phase"),
                                &ZxvHexagram::from_truth);
    ClassDB::bind_method(D_METHOD("tag"), &ZxvHexagram::tag);
    ClassDB::bind_method(D_METHOD("base"), &ZxvHexagram::base);
    ClassDB::bind_method(D_METHOD("polarity"), &ZxvHexagram::polarity);
    ClassDB::bind_method(D_METHOD("phase"), &ZxvHexagram::phase);
    ClassDB::bind_method(D_METHOD("is_forbidden"), &ZxvHexagram::is_forbidden);
    ClassDB::bind_method(D_METHOD("action"), &ZxvHexagram::action);
    ClassDB::bind_method(D_METHOD("action_name"), &ZxvHexagram::action_name);
    ClassDB::bind_method(D_METHOD("name"), &ZxvHexagram::name);
    ClassDB::bind_method(D_METHOD("to_truth"), &ZxvHexagram::to_truth);
    ClassDB::bind_method(D_METHOD("step", "phase"), &ZxvHexagram::step);
    ClassDB::bind_method(D_METHOD("advance"), &ZxvHexagram::advance);
    ClassDB::bind_method(D_METHOD("with_state", "base", "polarity"), &ZxvHexagram::with_state);
    ClassDB::bind_method(D_METHOD("forbid"), &ZxvHexagram::forbid);
    ClassDB::bind_method(D_METHOD("and_", "other"), &ZxvHexagram::and_);
    ClassDB::bind_method(D_METHOD("or_", "other"), &ZxvHexagram::or_);
    ClassDB::bind_method(D_METHOD("not_"), &ZxvHexagram::not_);
    ClassDB::bind_method(D_METHOD("equals", "other"), &ZxvHexagram::equals);
    BIND_CONSTANT(BASE_FALSE);
    BIND_CONSTANT(BASE_NEITHER);
    BIND_CONSTANT(BASE_BOTH);
    BIND_CONSTANT(BASE_TRUE);
    BIND_CONSTANT(POL_ENTROPIC);
    BIND_CONSTANT(POL_NEUTRAL);
    BIND_CONSTANT(POL_FORBIDDEN);
    BIND_CONSTANT(POL_NEGENTROPIC);
    BIND_CONSTANT(PHASE_POTENTIAL);
    BIND_CONSTANT(PHASE_EMERGING);
    BIND_CONSTANT(PHASE_DISSOLVING);
    BIND_CONSTANT(PHASE_MANIFEST);
    BIND_CONSTANT(ACT_PROCEED);
    BIND_CONSTANT(ACT_WAIT);
    BIND_CONSTANT(ACT_REFUSE);
    BIND_CONSTANT(ACT_ESCALATE);
    BIND_CONSTANT(ACT_ROLLBACK);
    BIND_CONSTANT(TRUTH_NEITHER);
    BIND_CONSTANT(TRUTH_TRUE);
    BIND_CONSTANT(TRUTH_FALSE);
    BIND_CONSTANT(TRUTH_BOTH);
}

// ---- ZxvLockstep -------------------------------------------------------

int64_t ZxvLockstep::setup(int64_t players, int64_t rat_slots, int64_t hex_slots,
                           int64_t input_bytes, int64_t history, bool predict) {
    if (!slot_ok(players) || !slot_ok(rat_slots) || !slot_ok(hex_slots) || !slot_ok(input_bytes) ||
        !slot_ok(history)) {
        UtilityFunctions::push_error("ZxvLockstep.setup: arguments out of range");
        return ZXV_E_ARG;
    }
    int32_t rc = core_.create(static_cast<uint32_t>(players), static_cast<uint32_t>(rat_slots),
                              static_cast<uint32_t>(hex_slots), static_cast<uint32_t>(input_bytes),
                              static_cast<uint32_t>(history), predict);
    report(rc, "ZxvLockstep.setup");
    return rc;
}

int64_t ZxvLockstep::set_tick_budget(const Ref<ZxvRational> &limit) {
    if (limit.is_null()) return ZXV_E_ARG;
    return core_.set_tick_budget(limit->value());
}

int64_t ZxvLockstep::start() { return core_.start(); }

int64_t ZxvLockstep::clock_set_step(const Ref<ZxvRational> &seconds, int64_t max_catchup) {
    if (seconds.is_null() || max_catchup < 1 || max_catchup > 1024) return ZXV_E_ARG;
    return core_.clock_set_step(seconds->value(), static_cast<uint32_t>(max_catchup));
}

int64_t ZxvLockstep::clock_feed_usec(int64_t usec) {
    uint32_t due = 0;
    int32_t rc = core_.clock_feed_usec(usec, &due, nullptr);
    return rc == ZXV_OK ? static_cast<int64_t>(due) : rc;
}

Ref<ZxvRational> ZxvLockstep::clock_alpha() const {
    zxvgd::Rational a;
    int32_t rc = core_.clock_alpha(&a);
    return rc == ZXV_OK ? ZxvRational::wrap(a) : Ref<ZxvRational>();
}

int64_t ZxvLockstep::submit_input(int64_t player, int64_t tick_, const PackedByteArray &bytes) {
    if (!slot_ok(player) || tick_ < 0) return ZXV_E_ARG;
    return core_.submit_input(static_cast<uint32_t>(player), static_cast<uint64_t>(tick_), to_bytes(bytes));
}

PackedByteArray ZxvLockstep::input(int64_t player) const {
    std::vector<uint8_t> v;
    if (!slot_ok(player) || core_.input(static_cast<uint32_t>(player), &v, nullptr) != ZXV_OK)
        return PackedByteArray();
    return to_packed(v);
}

bool ZxvLockstep::input_predicted(int64_t player) const {
    std::vector<uint8_t> v;
    bool p = false;
    return slot_ok(player) && core_.input(static_cast<uint32_t>(player), &v, &p) == ZXV_OK && p;
}

zxvgd::Lockstep::Step ZxvLockstep::bridge(const godot::Callable &step) {
    return [step](zxvgd::Lockstep &, uint64_t tick_) -> int32_t {
        Array args;
        args.push_back(static_cast<int64_t>(tick_));
        Variant r = step.callv(args);
        if (r.get_type() == Variant::NIL) return ZXV_OK;
        if (r.get_type() != Variant::INT) return ZXV_E_ARG;
        int64_t v = r;
        return v == 0 ? ZXV_OK : static_cast<int32_t>(v < -99 ? ZXV_E_INTERNAL : v);
    };
}

int64_t ZxvLockstep::advance(const godot::Callable &step) {
    if (!step.is_valid()) return ZXV_E_ARG;
    return core_.advance(bridge(step));
}

Ref<ZxvRational> ZxvLockstep::get_rational(int64_t slot) const {
    zxvgd::Rational v;
    if (!slot_ok(slot) || core_.get_rational(static_cast<uint32_t>(slot), &v) != ZXV_OK)
        return Ref<ZxvRational>();
    return ZxvRational::wrap(v);
}

int64_t ZxvLockstep::set_rational(int64_t slot, const Ref<ZxvRational> &v) {
    if (!slot_ok(slot) || v.is_null()) return ZXV_E_ARG;
    return core_.set_rational(static_cast<uint32_t>(slot), v->value());
}

int64_t ZxvLockstep::add_rational(int64_t slot, const Ref<ZxvRational> &v) {
    if (!slot_ok(slot) || v.is_null()) return ZXV_E_ARG;
    return core_.add_rational(static_cast<uint32_t>(slot), v->value());
}

int64_t ZxvLockstep::mul_rational(int64_t slot, const Ref<ZxvRational> &v) {
    if (!slot_ok(slot) || v.is_null()) return ZXV_E_ARG;
    return core_.mul_rational(static_cast<uint32_t>(slot), v->value());
}

Ref<ZxvHexagram> ZxvLockstep::get_hexagram(int64_t slot) const {
    zxvgd::Hexagram h;
    if (!slot_ok(slot) || core_.get_hexagram(static_cast<uint32_t>(slot), &h) != ZXV_OK)
        return Ref<ZxvHexagram>();
    return ZxvHexagram::wrap(h);
}

int64_t ZxvLockstep::set_hexagram(int64_t slot, const Ref<ZxvHexagram> &h) {
    if (!slot_ok(slot) || h.is_null()) return ZXV_E_ARG;
    return core_.set_hexagram(static_cast<uint32_t>(slot), h->value());
}

int64_t ZxvLockstep::charge(const Ref<ZxvRational> &amount) {
    if (amount.is_null()) return ZXV_E_ARG;
    return core_.charge(amount->value());
}

PackedByteArray ZxvLockstep::state_hash(int64_t tick_) const {
    std::vector<uint8_t> h;
    if (tick_ < 0 || core_.hash(static_cast<uint64_t>(tick_), &h, nullptr) != ZXV_OK)
        return PackedByteArray();
    return to_packed(h);
}

bool ZxvLockstep::hash_is_final(int64_t tick_) const {
    std::vector<uint8_t> h;
    bool fin = false;
    return tick_ >= 0 && core_.hash(static_cast<uint64_t>(tick_), &h, &fin) == ZXV_OK && fin;
}

int64_t ZxvLockstep::compare(int64_t tick_, const PackedByteArray &remote) {
    int lp = ZXV_LP_NEITHER;
    if (tick_ < 0) return ZXV_LP_NEITHER;
    int32_t rc = core_.compare(static_cast<uint64_t>(tick_), to_bytes(remote), &lp);
    report(rc, "ZxvLockstep.compare");
    return rc == ZXV_OK ? lp : ZXV_LP_NEITHER;
}

int64_t ZxvLockstep::desync_tick() const {
    uint64_t t = 0;
    return core_.desync(&t) ? static_cast<int64_t>(t) : -1;
}

int64_t ZxvLockstep::rollback_pending_tick() const {
    uint64_t t = 0;
    return core_.rollback_pending(&t) ? static_cast<int64_t>(t) : -1;
}

int64_t ZxvLockstep::rollback(int64_t to_tick) {
    if (to_tick < 0) return ZXV_E_ARG;
    return core_.rollback(static_cast<uint64_t>(to_tick));
}

int64_t ZxvLockstep::rollback_replay(const godot::Callable &step) {
    if (!step.is_valid()) return ZXV_E_ARG;
    return core_.rollback_replay(bridge(step));
}

PackedByteArray ZxvLockstep::export_state() const {
    std::vector<uint8_t> v;
    if (core_.export_state(&v) != ZXV_OK) return PackedByteArray();
    return to_packed(v);
}

int64_t ZxvLockstep::import_state(const PackedByteArray &bytes) {
    return core_.import_state(to_bytes(bytes));
}

String ZxvLockstep::status_name(int64_t status) {
    return String(zxvgd::status_name(static_cast<int32_t>(status < -999 ? -999 : status)));
}

void ZxvLockstep::_bind_methods() {
    ClassDB::bind_method(D_METHOD("setup", "players", "rat_slots", "hex_slots", "input_bytes", "history", "predict"),
                         &ZxvLockstep::setup);
    ClassDB::bind_method(D_METHOD("set_tick_budget", "limit"), &ZxvLockstep::set_tick_budget);
    ClassDB::bind_method(D_METHOD("start"), &ZxvLockstep::start);
    ClassDB::bind_method(D_METHOD("clock_set_step", "seconds", "max_catchup"), &ZxvLockstep::clock_set_step);
    ClassDB::bind_method(D_METHOD("clock_feed_usec", "usec"), &ZxvLockstep::clock_feed_usec);
    ClassDB::bind_method(D_METHOD("clock_alpha"), &ZxvLockstep::clock_alpha);
    ClassDB::bind_method(D_METHOD("submit_input", "player", "tick", "bytes"), &ZxvLockstep::submit_input);
    ClassDB::bind_method(D_METHOD("input", "player"), &ZxvLockstep::input);
    ClassDB::bind_method(D_METHOD("input_predicted", "player"), &ZxvLockstep::input_predicted);
    ClassDB::bind_method(D_METHOD("ready"), &ZxvLockstep::ready);
    ClassDB::bind_method(D_METHOD("tick"), &ZxvLockstep::tick);
    ClassDB::bind_method(D_METHOD("begin"), &ZxvLockstep::begin);
    ClassDB::bind_method(D_METHOD("commit"), &ZxvLockstep::commit);
    ClassDB::bind_method(D_METHOD("abort"), &ZxvLockstep::abort);
    ClassDB::bind_method(D_METHOD("advance", "step"), &ZxvLockstep::advance);
    ClassDB::bind_method(D_METHOD("get_rational", "slot"), &ZxvLockstep::get_rational);
    ClassDB::bind_method(D_METHOD("set_rational", "slot", "value"), &ZxvLockstep::set_rational);
    ClassDB::bind_method(D_METHOD("add_rational", "slot", "value"), &ZxvLockstep::add_rational);
    ClassDB::bind_method(D_METHOD("mul_rational", "slot", "value"), &ZxvLockstep::mul_rational);
    ClassDB::bind_method(D_METHOD("get_hexagram", "slot"), &ZxvLockstep::get_hexagram);
    ClassDB::bind_method(D_METHOD("set_hexagram", "slot", "value"), &ZxvLockstep::set_hexagram);
    ClassDB::bind_method(D_METHOD("charge", "amount"), &ZxvLockstep::charge);
    ClassDB::bind_method(D_METHOD("state_hash", "tick"), &ZxvLockstep::state_hash);
    ClassDB::bind_method(D_METHOD("hash_is_final", "tick"), &ZxvLockstep::hash_is_final);
    ClassDB::bind_method(D_METHOD("confirmed_tick"), &ZxvLockstep::confirmed_tick);
    ClassDB::bind_method(D_METHOD("compare", "tick", "remote_hash"), &ZxvLockstep::compare);
    ClassDB::bind_method(D_METHOD("desync_tick"), &ZxvLockstep::desync_tick);
    ClassDB::bind_method(D_METHOD("rollback_pending_tick"), &ZxvLockstep::rollback_pending_tick);
    ClassDB::bind_method(D_METHOD("rollback", "to_tick"), &ZxvLockstep::rollback);
    ClassDB::bind_method(D_METHOD("rollback_replay", "step"), &ZxvLockstep::rollback_replay);
    ClassDB::bind_method(D_METHOD("export_state"), &ZxvLockstep::export_state);
    ClassDB::bind_method(D_METHOD("import_state", "bytes"), &ZxvLockstep::import_state);
    ClassDB::bind_method(D_METHOD("last_error"), &ZxvLockstep::last_error);
    ClassDB::bind_static_method("ZxvLockstep", D_METHOD("status_name", "status"), &ZxvLockstep::status_name);
    BIND_CONSTANT(OK);
    BIND_CONSTANT(E_ARG);
    BIND_CONSTANT(E_BUFFER);
    BIND_CONSTANT(E_RANGE);
    BIND_CONSTANT(E_STATE);
    BIND_CONSTANT(E_DUPLICATE);
    BIND_CONSTANT(E_CAPACITY);
    BIND_CONSTANT(E_BUDGET);
    BIND_CONSTANT(E_FORMAT);
    BIND_CONSTANT(E_NOT_FOUND);
    BIND_CONSTANT(TRUTH_NEITHER);
    BIND_CONSTANT(TRUTH_TRUE);
    BIND_CONSTANT(TRUTH_FALSE);
}

} // namespace zxv_godot
