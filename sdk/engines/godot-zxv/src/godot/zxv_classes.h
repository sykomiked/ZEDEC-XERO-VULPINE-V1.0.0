// Copyright (c) 2024-2026 Michael Laurence Curzi and 36N9 Genetics, LLC
// SPDX-License-Identifier: Apache-2.0
//
// zxv_classes.h — the Godot 4 classes: ZxvRational, ZxvHexagram and
// ZxvLockstep, thin wrappers over src/core (which holds every rule).
//
// Godot idiom for failures: a method that cannot produce its value pushes
// an error (visible in the debugger) and returns null, or returns a
// non-zero status int (ZxvLockstep.OK is 0); last_error() gives the text.
#ifndef ZXV_GODOT_CLASSES_H
#define ZXV_GODOT_CLASSES_H

#include <godot_cpp/classes/ref_counted.hpp>
#include <godot_cpp/variant/callable.hpp>
#include <godot_cpp/variant/packed_byte_array.hpp>
#include <godot_cpp/variant/string.hpp>
#include <godot_cpp/variant/variant.hpp>

#include "../core/zxv_gd_core.hpp"

namespace zxv_godot {

using godot::PackedByteArray;
using godot::Ref;
using godot::String;
using godot::Variant;

// An exact rational. Built from two ints or from exact text; a float is
// refused (Godot would otherwise truncate 0.5 to 0 for an int parameter).
class ZxvRational : public godot::RefCounted {
    GDCLASS(ZxvRational, godot::RefCounted)

  public:
    static Ref<ZxvRational> wrap(const zxvgd::Rational &v);
    const zxvgd::Rational &value() const { return value_; }

    static Ref<ZxvRational> from_ints(const Variant &num, const Variant &den);
    static Ref<ZxvRational> parse(const Variant &text);
    // Bound only to refuse: it always pushes an error and returns null.
    static Ref<ZxvRational> from_float(const Variant &x);
    static String last_error();

    int64_t numerator() const { return value_.num(); }
    int64_t denominator() const { return value_.den(); }
    bool is_zero() const { return value_.is_zero(); }
    String text() const;
    String fraction() const;
    String _to_string() const { return text(); }

    Ref<ZxvRational> add(const Ref<ZxvRational> &b) const;
    Ref<ZxvRational> sub(const Ref<ZxvRational> &b) const;
    Ref<ZxvRational> mul(const Ref<ZxvRational> &b) const;
    Ref<ZxvRational> div(const Ref<ZxvRational> &b) const;
    Ref<ZxvRational> neg() const;
    int64_t compare(const Ref<ZxvRational> &b) const;
    bool equals(const Ref<ZxvRational> &b) const;

    // LOSSY, for drawing only: the nearest float. Never feed it back into
    // game state; the simulation must only ever see the exact value.
    double to_float_lossy() const;

  protected:
    static void _bind_methods();

  private:
    zxvgd::Rational value_;
};

// The 64-state hexagram tag (ABI 1.3), immutable.
class ZxvHexagram : public godot::RefCounted {
    GDCLASS(ZxvHexagram, godot::RefCounted)

  public:
    enum {
        BASE_FALSE = ZXV_HEX_BASE_FALSE,
        BASE_NEITHER = ZXV_HEX_BASE_NEITHER,
        BASE_BOTH = ZXV_HEX_BASE_BOTH,
        BASE_TRUE = ZXV_HEX_BASE_TRUE,
        POL_ENTROPIC = ZXV_HEX_POL_ENTROPIC,
        POL_NEUTRAL = ZXV_HEX_POL_NEUTRAL,
        POL_FORBIDDEN = ZXV_HEX_POL_FORBIDDEN,
        POL_NEGENTROPIC = ZXV_HEX_POL_NEGENTROPIC,
        PHASE_POTENTIAL = ZXV_HEX_PHASE_POTENTIAL,
        PHASE_EMERGING = ZXV_HEX_PHASE_EMERGING,
        PHASE_DISSOLVING = ZXV_HEX_PHASE_DISSOLVING,
        PHASE_MANIFEST = ZXV_HEX_PHASE_MANIFEST,
        ACT_PROCEED = ZXV_HEX_ACT_PROCEED,
        ACT_WAIT = ZXV_HEX_ACT_WAIT,
        ACT_REFUSE = ZXV_HEX_ACT_REFUSE,
        ACT_ESCALATE = ZXV_HEX_ACT_ESCALATE,
        ACT_ROLLBACK = ZXV_HEX_ACT_ROLLBACK,
        TRUTH_NEITHER = ZXV_LP_NEITHER,
        TRUTH_TRUE = ZXV_LP_TRUE,
        TRUTH_FALSE = ZXV_LP_FALSE,
        TRUTH_BOTH = ZXV_LP_BOTH,
    };

    static Ref<ZxvHexagram> wrap(const zxvgd::Hexagram &h);
    const zxvgd::Hexagram &value() const { return value_; }

    static Ref<ZxvHexagram> from_tag(int64_t tag);
    static Ref<ZxvHexagram> from_fields(int64_t base, int64_t polarity, int64_t phase);
    static Ref<ZxvHexagram> from_truth(int64_t truth, int64_t polarity, int64_t phase);

    int64_t tag() const { return value_.tag(); }
    int64_t base() const { return value_.base(); }
    int64_t polarity() const { return value_.polarity(); }
    int64_t phase() const { return value_.phase(); }
    bool is_forbidden() const { return value_.is_forbidden(); }
    int64_t action() const { return value_.action(); }
    String action_name() const { return String(value_.action_name()); }
    String name() const { return String(value_.name()); }
    int64_t to_truth() const { return value_.to_truth(); }
    String _to_string() const { return name(); }

    Ref<ZxvHexagram> step(int64_t phase) const;
    Ref<ZxvHexagram> advance() const { return wrap(value_.advance()); }
    Ref<ZxvHexagram> with_state(int64_t base, int64_t polarity) const;
    Ref<ZxvHexagram> forbid() const { return wrap(value_.forbid()); }
    Ref<ZxvHexagram> and_(const Ref<ZxvHexagram> &b) const;
    Ref<ZxvHexagram> or_(const Ref<ZxvHexagram> &b) const;
    Ref<ZxvHexagram> not_() const { return wrap(value_.not_()); }
    bool equals(const Ref<ZxvHexagram> &b) const;

  protected:
    static void _bind_methods();

  private:
    zxvgd::Hexagram value_;
};

// One deterministic lockstep simulation (emu-state).
class ZxvLockstep : public godot::RefCounted {
    GDCLASS(ZxvLockstep, godot::RefCounted)

  public:
    enum {
        OK = ZXV_OK,
        E_ARG = ZXV_E_ARG,
        E_BUFFER = ZXV_E_BUFFER,
        E_RANGE = ZXV_E_RANGE,
        E_STATE = ZXV_E_STATE,
        E_DUPLICATE = ZXV_E_DUPLICATE,
        E_CAPACITY = ZXV_E_CAPACITY,
        E_BUDGET = ZXV_E_BUDGET,
        E_FORMAT = ZXV_E_FORMAT,
        E_NOT_FOUND = ZXV_E_NOT_FOUND,
        TRUTH_NEITHER = ZXV_LP_NEITHER,
        TRUTH_TRUE = ZXV_LP_TRUE,
        TRUTH_FALSE = ZXV_LP_FALSE,
    };

    int64_t setup(int64_t players, int64_t rat_slots, int64_t hex_slots, int64_t input_bytes,
                  int64_t history, bool predict);
    int64_t set_tick_budget(const Ref<ZxvRational> &limit);
    int64_t start();

    int64_t clock_set_step(const Ref<ZxvRational> &seconds, int64_t max_catchup);
    int64_t clock_feed_usec(int64_t usec); // ticks due now (negative: a status)
    Ref<ZxvRational> clock_alpha() const;

    int64_t submit_input(int64_t player, int64_t tick, const PackedByteArray &bytes);
    PackedByteArray input(int64_t player) const;
    bool input_predicted(int64_t player) const;
    bool ready() const { return core_.ready(); }
    int64_t tick() const { return static_cast<int64_t>(core_.tick()); }

    int64_t begin() { return core_.begin(); }
    int64_t commit() { return core_.commit(); }
    int64_t abort() { return core_.abort(); }
    // step(tick: int) -> int (0 = OK; null also counts as OK)
    int64_t advance(const godot::Callable &step);

    Ref<ZxvRational> get_rational(int64_t slot) const;
    int64_t set_rational(int64_t slot, const Ref<ZxvRational> &v);
    int64_t add_rational(int64_t slot, const Ref<ZxvRational> &v);
    int64_t mul_rational(int64_t slot, const Ref<ZxvRational> &v);
    Ref<ZxvHexagram> get_hexagram(int64_t slot) const;
    int64_t set_hexagram(int64_t slot, const Ref<ZxvHexagram> &h);
    int64_t charge(const Ref<ZxvRational> &amount);

    PackedByteArray state_hash(int64_t tick) const;
    bool hash_is_final(int64_t tick) const;
    int64_t confirmed_tick() { return static_cast<int64_t>(core_.confirmed_tick()); }
    int64_t compare(int64_t tick, const PackedByteArray &remote);
    int64_t desync_tick() const;           // -1: none
    int64_t rollback_pending_tick() const; // -1: none
    int64_t rollback(int64_t to_tick);
    int64_t rollback_replay(const godot::Callable &step);

    PackedByteArray export_state() const;
    int64_t import_state(const PackedByteArray &bytes);

    String last_error() const { return String(zxvgd::last_error().c_str()); }
    static String status_name(int64_t status);

  protected:
    static void _bind_methods();

  private:
    zxvgd::Lockstep::Step bridge(const godot::Callable &step);
    zxvgd::Lockstep core_;
};

} // namespace zxv_godot

#endif // ZXV_GODOT_CLASSES_H
