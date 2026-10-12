// Copyright (c) 2024-2026 Michael Laurence Curzi and 36N9 Genetics, LLC
// SPDX-License-Identifier: Apache-2.0
//
// zxv_ue_core.hpp — the engine-neutral C++ layer under the Unreal plugin:
// exact rationals, hexagram tags, the lockstep sim and an integer
// fixed-step pacer, over libzxv (zxv_api.h) and emu-state (zxv_emu.h).
//
// It includes no Unreal header, so it is built and unit-tested on its own
// with CMake (test/test_core.cpp, test/test_cross_cpp.cpp). The UCLASS and
// USTRUCT wrappers in Source/ZxvUnreal call only this layer, and modules
// that depend on ZxvUnreal (for example a later checkout plugin) can use
// it directly from C++.
//
// Unreal builds modules without C++ exceptions by default, so nothing here
// throws: every fallible call returns a status (ZXV_OK or a negative
// ZXV_E_*) and writes its result through a pointer only on success;
// last_error() gives the detail text. No float or double appears in this
// layer (R11): a floating-point argument to a Rational constructor does not
// compile, and time enters only as integer microseconds.
#ifndef ZXV_UE_CORE_HPP
#define ZXV_UE_CORE_HPP

#include <cstdint>
#include <functional>
#include <string>
#include <type_traits>
#include <vector>

#include "zxv_api.h"
#include "zxv_emu.h"

namespace zxvue {

// Version of this layer (the plugin's VersionName is the same text).
inline constexpr int kVersionMajor = 0;
inline constexpr int kVersionMinor = 1;

// Detail text of the last failure on this thread (from this layer,
// emu-state or libzxv, whichever failed last).
std::string last_error();

// Static name of a status ("ZXV_E_RANGE"); "ZXV_E_UNKNOWN" for others.
const char *status_name(std::int32_t status);

// Checks the loaded libzxv: major 1, minor >= 3 (the hexagram tag).
std::int32_t check_library();

// A monotonic clock in integer microseconds (std::chrono::steady_clock).
// Only for pacing: never read it inside a step.
std::int64_t monotonic_usec();

// ---------------------------------------------------------------------
// An exact rational: int64 numerator over a positive int64 denominator,
// always reduced. Arithmetic runs in libzxv; overflow is ZXV_E_RANGE.
class Rational {
  public:
    Rational() = default; // 0/1

    // A float or double never converts: no rounding can sneak in here.
    template <typename F, typename = std::enable_if_t<std::is_floating_point<F>::value>>
    Rational(F) = delete;

    // num/den reduced. den == 0 is ZXV_E_ARG; INT64_MIN is refused by libzxv.
    static std::int32_t from_ints(std::int64_t num, std::int64_t den, Rational *out);
    // "12.75", "-0.1", "3/4", "42" exactly; exponents, "nan", spaces:
    // ZXV_E_FORMAT; too large: ZXV_E_RANGE.
    static std::int32_t parse(const std::string &text, Rational *out);
    // An integer that is known to be valid (|v| < 2^63); for constants.
    static Rational of_int(std::int32_t v);

    std::int64_t num() const { return num_; }
    std::int64_t den() const { return den_; }
    bool is_zero() const { return num_ == 0; }
    bool is_integer() const { return den_ == 1; }
    int sign() const { return (num_ > 0) - (num_ < 0); }

    // Exact text: a terminating decimal when one exists ("12.75"), else "n/d".
    std::string to_string() const;
    // Always "n/d" (the form the Unreal struct and Unity store).
    std::string to_fraction_string() const;

    std::int32_t add(const Rational &b, Rational *out) const;
    std::int32_t sub(const Rational &b, Rational *out) const;
    std::int32_t mul(const Rational &b, Rational *out) const;
    std::int32_t div(const Rational &b, Rational *out) const;
    std::int32_t neg(Rational *out) const;
    std::int32_t abs(Rational *out) const;
    // The largest integer <= value / the smallest integer >= value. Exact.
    std::int64_t floor() const;
    std::int64_t ceil() const;
    // floor(value * scale) as an integer, for integer render units (for
    // example centimetres from metres with scale 100). ZXV_E_RANGE if the
    // product does not fit; scale must be > 0 (ZXV_E_ARG).
    std::int32_t to_scaled_floor(std::int64_t scale, std::int64_t *out) const;
    // Q16.16 fixed point, rounded half away from zero (zxv_rat_to_q16).
    std::int32_t to_q16(std::int32_t *out) const;
    // -1, 0 or +1; exact, no epsilon. Cannot fail for two valid values.
    int compare(const Rational &b) const;
    bool operator==(const Rational &b) const { return num_ == b.num_ && den_ == b.den_; }
    bool operator!=(const Rational &b) const { return !(*this == b); }
    bool operator<(const Rational &b) const { return compare(b) < 0; }
    bool operator>(const Rational &b) const { return compare(b) > 0; }
    bool operator<=(const Rational &b) const { return compare(b) <= 0; }
    bool operator>=(const Rational &b) const { return compare(b) >= 0; }
    Rational min(const Rational &b) const { return compare(b) <= 0 ? *this : b; }
    Rational max(const Rational &b) const { return compare(b) >= 0 ? *this : b; }

  private:
    std::int64_t num_ = 0;
    std::int64_t den_ = 1;
};

// ---------------------------------------------------------------------
// The 64-state hexagram tag (ABI 1.3); every rule is libzxv's zxv_hex_*.
class Hexagram {
  public:
    Hexagram() = default; // tag 0: false/entropic/potential

    static std::int32_t from_tag(int tag, Hexagram *out);
    static std::int32_t from_fields(int base, int polarity, int phase, Hexagram *out);
    // A four-valued Truth (ZXV_LP_*) as the base, through zxv_hex_from_lp.
    static std::int32_t from_truth(int lp_truth, int polarity, int phase, Hexagram *out);

    int tag() const { return tag_; }
    int base() const;
    int polarity() const;
    int phase() const;
    bool is_forbidden() const { return polarity() == ZXV_HEX_POL_FORBIDDEN; }
    int action() const;
    const char *name() const { return zxv_hex_name(tag_); }
    const char *action_name() const { return zxv_hex_action_name(action()); }
    int to_truth() const;

    // The same or the next phase only; a jump is ZXV_E_STATE.
    std::int32_t step(int phase, Hexagram *out) const;
    Hexagram advance() const; // the next phase; cannot fail
    // New base and polarity; a forbidden tag stays forbidden (ZXV_E_STATE).
    std::int32_t with_state(int base, int polarity, Hexagram *out) const;
    Hexagram forbid() const;
    Hexagram and_(const Hexagram &b) const;
    Hexagram or_(const Hexagram &b) const;
    Hexagram not_() const;
    bool operator==(const Hexagram &b) const { return tag_ == b.tag_; }
    bool operator!=(const Hexagram &b) const { return tag_ != b.tag_; }

  private:
    explicit Hexagram(std::uint8_t t) : tag_(t) {}
    std::uint8_t tag_ = 0;
};

// The shape of a sim; zxv_emu_create's limits apply (players 1..64, ...).
struct LockstepConfig {
    std::uint32_t players = 2;
    std::uint32_t rat_slots = 1;
    std::uint32_t hex_slots = 0;
    std::uint32_t input_bytes = 1;
    std::uint32_t history = 32;
    bool predict = false;
};

// ---------------------------------------------------------------------
// One emu-state sim (see sdk/engines/emu-state/include/zxv_emu.h).
// Move-only; destroys the sim, then its private budget context.
class Lockstep {
  public:
    using Step = std::function<std::int32_t(Lockstep &, std::uint64_t tick)>;

    Lockstep() = default;
    ~Lockstep();
    Lockstep(const Lockstep &) = delete;
    Lockstep &operator=(const Lockstep &) = delete;
    Lockstep(Lockstep &&o) noexcept;
    Lockstep &operator=(Lockstep &&o) noexcept;

    std::int32_t create(const LockstepConfig &cfg);
    bool valid() const { return sim_ != nullptr; }
    zxv_emu *raw() const { return sim_; }
    // The shape it was created with (ZXV_E_STATE before create).
    std::int32_t config(LockstepConfig *out) const;

    // A per-tick budget. The Lockstep owns a private libzxv context and a
    // capability attenuated to ZXV_CAP_BUDGET only; nothing else can use it.
    std::int32_t set_tick_budget(const Rational &limit);
    std::int32_t start();

    std::int32_t clock_set_step(const Rational &seconds, std::uint32_t max_catchup);
    // Elapsed wall time as integer microseconds (never a float DeltaTime).
    std::int32_t clock_feed_usec(std::int64_t usec, std::uint32_t *due, std::uint64_t *dropped);
    std::int32_t clock_alpha(Rational *out) const;

    std::int32_t submit_input(std::uint32_t player, std::uint64_t tick,
                              const std::vector<std::uint8_t> &bytes);
    std::int32_t input(std::uint32_t player, std::vector<std::uint8_t> *out,
                       bool *predicted) const;
    bool ready() const;
    std::uint64_t tick() const;

    std::int32_t begin();
    std::int32_t commit();
    std::int32_t abort();
    std::int32_t advance(const Step &step);

    std::int32_t get_rational(std::uint32_t slot, Rational *out) const;
    std::int32_t set_rational(std::uint32_t slot, const Rational &v);
    std::int32_t add_rational(std::uint32_t slot, const Rational &v);
    std::int32_t mul_rational(std::uint32_t slot, const Rational &v);
    std::int32_t get_hexagram(std::uint32_t slot, Hexagram *out) const;
    std::int32_t set_hexagram(std::uint32_t slot, const Hexagram &h);
    std::int32_t charge(const Rational &amount);

    std::int32_t hash(std::uint64_t tick, std::vector<std::uint8_t> *out, bool *final_) const;
    std::uint64_t confirmed_tick();
    // ZXV_LP_TRUE (equal), ZXV_LP_FALSE (desync), ZXV_LP_NEITHER (no verdict).
    std::int32_t compare(std::uint64_t tick, const std::vector<std::uint8_t> &remote, int *lp);
    bool desync(std::uint64_t *tick) const;
    bool rollback_pending(std::uint64_t *tick) const;
    std::int32_t rollback(std::uint64_t to_tick);
    std::int32_t rollback_replay(const Step &step);

    std::int32_t export_state(std::vector<std::uint8_t> *out) const;
    std::int32_t import_state(const std::vector<std::uint8_t> &bytes);

  private:
    void reset();
    zxv_emu *sim_ = nullptr;
    zxv_ctx *ctx_ = nullptr;
    zxv_cap *root_ = nullptr;
    zxv_cap *budget_cap_ = nullptr;
};

// ---------------------------------------------------------------------
// Feeds a Lockstep's fixed-step clock from absolute monotonic readings in
// integer microseconds (monotonic_usec(), or a test clock). The first
// reading only sets the origin. A reading earlier than the previous one is
// ZXV_E_RANGE and changes nothing.
class Pacer {
  public:
    std::int32_t pump(Lockstep &sim, std::int64_t now_usec, std::uint32_t *due,
                      std::uint64_t *dropped);
    void reset() { started_ = false; }
    bool started() const { return started_; }

  private:
    bool started_ = false;
    std::int64_t last_usec_ = 0;
};

} // namespace zxvue

#endif // ZXV_UE_CORE_HPP
