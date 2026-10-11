// Copyright (c) 2024-2026 Michael Laurence Curzi and 36N9 Genetics, LLC
// SPDX-License-Identifier: Apache-2.0
//
// zxv.hpp — the header-only C++20 SDK for ZXV, over the C ABI zxv_api.h.
//
// One include, one namespace (`zxv`), one native dependency (libzxv).
// The rules this file follows are sdk/docs/SDK_DESIGN.md; the C rules it
// wraps (R1-R11) are in zxv_api.h. In C++ terms:
//
//   * Every C handle is owned by a move-only RAII class. Parents are kept
//     alive by their children (a Capability keeps its Context alive), so
//     destruction order (R6) can never be wrong.
//   * Every negative status becomes an exception from the zxv::Exception
//     family, picked by zxv_status_class. A FATAL status (the context
//     halted) throws zxv::FatalError, which deliberately derives from
//     nothing else, so `catch (const std::exception&)` cannot swallow it.
//   * No float or double crosses a public signature, except
//     Rational::from_double_exact.
//   * Gated objects hold a Capability and pass it on every call (R10).
//
// Thread safety: Context and Capability may be used from any thread (the
// C context is internally serialized). Budgets, PhaseClock, Attestations,
// Swarm and Model are safe to share across threads. A Session is used by
// one thread at a time (the SDK refuses concurrent use).
#ifndef ZXV_HPP
#define ZXV_HPP

#include <zxv_api.h>

#include <algorithm>
#include <array>
#include <atomic>
#include <bit>
#include <chrono>
#include <compare>
#include <concepts>
#include <condition_variable>
#include <cstddef>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <exception>
#include <functional>
#include <future>
#include <iterator>
#include <map>
#include <memory>
#include <mutex>
#include <optional>
#include <ostream>
#include <span>
#include <stdexcept>
#include <string>
#include <string_view>
#include <thread>
#include <utility>
#include <vector>
#include <version>

// std::jthread and <stop_token> are missing from some standard libraries
// that otherwise do C++20 (Apple's libc++ in Xcode 15 and 16). zxv.hpp then
// uses a small bundled stop_source / stop_token / stop_callback / jthread;
// -DZXV_FORCE_STOP_TOKEN_FALLBACK selects it everywhere (CI tests both).
#if defined(__cpp_lib_jthread) && !defined(ZXV_FORCE_STOP_TOKEN_FALLBACK)
#    define ZXV_STOP_TOKEN_FALLBACK 0
#    include <stop_token>
#else
/// 1 when zxv::jthread and friends are the bundled fallback, 0 when they
/// are the std types.
#    define ZXV_STOP_TOKEN_FALLBACK 1
#    include <list>
#    include <type_traits>
#endif

#if defined(__SIZEOF_INT128__)
#    define ZXV_HAS_INT128 1
/// Rational arithmetic is constexpr where the compiler has 128-bit integers
/// (gcc, clang); elsewhere (MSVC) it calls the C library at run time.
#    define ZXV_RAT_CONSTEXPR constexpr
#else
#    define ZXV_HAS_INT128 0
#    define ZXV_RAT_CONSTEXPR inline
#endif

namespace zxv {

// ===========================================================================
// Cooperative cancellation: std::stop_token and std::jthread, or a fallback
// ===========================================================================

#if !ZXV_STOP_TOKEN_FALLBACK
using std::jthread;
using std::nostopstate;
using std::nostopstate_t;
using std::stop_callback;
using std::stop_source;
using std::stop_token;
#else
namespace detail {
/// Shared state of a stop_source: an atomic flag, plus the registered
/// callbacks under a mutex. request_stop() runs them on the calling thread,
/// outside the mutex; a callback being destroyed waits for its own run to
/// finish when that run is on another thread (as std::stop_callback does).
class StopState {
public:
    using Fn = std::function<void()>;
    using Slot = std::list<Fn*>::iterator;

    bool stop_requested() const noexcept { return stopped_.load(std::memory_order_acquire); }

    bool request_stop()
    {
        std::unique_lock lk(m_);
        if (stopped_.load(std::memory_order_relaxed)) return false;
        stopped_.store(true, std::memory_order_release);
        runner_ = std::this_thread::get_id();
        while (!callbacks_.empty()) {
            running_ = callbacks_.front();
            callbacks_.pop_front();
            lk.unlock();
            (*running_)();
            lk.lock();
            running_ = nullptr;
            done_.notify_all();
        }
        return true;
    }

    /// Registers fn; false (nothing registered) if stop was already requested.
    bool add(Fn* fn, Slot& slot)
    {
        std::lock_guard lk(m_);
        if (stopped_.load(std::memory_order_relaxed)) return false;
        slot = callbacks_.insert(callbacks_.end(), fn);
        return true;
    }

    void remove(Fn* fn, Slot slot)
    {
        std::unique_lock lk(m_);
        if (!stopped_.load(std::memory_order_relaxed)) {
            callbacks_.erase(slot);
            return;
        }
        for (auto it = callbacks_.begin(); it != callbacks_.end(); ++it)
            if (*it == fn) {
                callbacks_.erase(it);
                return;
            }
        // Already run, or running now: wait unless it is running on this thread.
        if (running_ == fn && runner_ != std::this_thread::get_id())
            done_.wait(lk, [&] { return running_ != fn; });
    }

private:
    std::atomic<bool> stopped_{false};
    std::mutex m_;
    std::condition_variable done_;
    std::list<Fn*> callbacks_;
    Fn* running_ = nullptr;
    std::thread::id runner_;
};
} // namespace detail

/// Tag for a stop_source without a stop state (as std::nostopstate_t).
struct nostopstate_t {
    explicit nostopstate_t() = default;
};
/// The nostopstate tag value.
inline constexpr nostopstate_t nostopstate{};

/// Fallback std::stop_token: a view of a stop_source's state.
class stop_token {
public:
    stop_token() noexcept = default;
    /// True once the associated stop_source requested stop.
    [[nodiscard]] bool stop_requested() const noexcept { return state_ && state_->stop_requested(); }
    /// True if a stop can still be (or already was) requested.
    [[nodiscard]] bool stop_possible() const noexcept { return state_ != nullptr; }
    /// Same stop state (or both without one).
    friend bool operator==(const stop_token& a, const stop_token& b) noexcept { return a.state_ == b.state_; }
    /// Exchange states.
    void swap(stop_token& o) noexcept { state_.swap(o.state_); }

private:
    friend class stop_source;
    template <class Callback>
    friend class stop_callback;
    explicit stop_token(std::shared_ptr<detail::StopState> s) noexcept : state_(std::move(s)) {}
    std::shared_ptr<detail::StopState> state_;
};

/// Fallback std::stop_source: owns a stop state and requests the stop.
class stop_source {
public:
    stop_source() : state_(std::make_shared<detail::StopState>()) {}
    /// A source without a stop state.
    explicit stop_source(nostopstate_t) noexcept {}
    /// A token observing this source.
    [[nodiscard]] stop_token get_token() const noexcept { return stop_token(state_); }
    /// Request stop; true if this call made the request (runs the callbacks).
    bool request_stop() { return state_ && state_->request_stop(); }
    /// True once stop was requested.
    [[nodiscard]] bool stop_requested() const noexcept { return state_ && state_->stop_requested(); }
    /// True if this source has a stop state.
    [[nodiscard]] bool stop_possible() const noexcept { return state_ != nullptr; }
    /// Same stop state (or both without one).
    friend bool operator==(const stop_source& a, const stop_source& b) noexcept { return a.state_ == b.state_; }
    /// Exchange states.
    void swap(stop_source& o) noexcept { state_.swap(o.state_); }

private:
    std::shared_ptr<detail::StopState> state_;
};

/// Fallback std::stop_callback: runs `Callback` once when stop is requested
/// (at once, in the constructor, if it already was). Not copyable or movable.
template <class Callback>
class stop_callback {
public:
    /// The callback type.
    using callback_type = Callback;
    /// Register `cb` with the stop state of `st`.
    template <class C>
    explicit stop_callback(const stop_token& st, C&& cb) : cb_(std::forward<C>(cb)), fn_([this] { cb_(); })
    {
        if (st.state_ && st.state_->add(&fn_, slot_))
            state_ = st.state_;
        else if (st.stop_requested())
            cb_();
    }
    ~stop_callback()
    {
        if (state_) state_->remove(&fn_, slot_);
    }
    stop_callback(const stop_callback&) = delete;
    stop_callback& operator=(const stop_callback&) = delete;
    stop_callback(stop_callback&&) = delete;
    stop_callback& operator=(stop_callback&&) = delete;

private:
    Callback cb_;
    std::function<void()> fn_;
    std::shared_ptr<detail::StopState> state_;
    detail::StopState::Slot slot_{};
};
/// Deduce stop_callback<C> from the callable.
template <class C>
stop_callback(stop_token, C) -> stop_callback<C>;

/// Fallback std::jthread: passes its stop_token as the first argument when
/// the callable accepts one; requests stop and joins on destruction.
class jthread {
public:
    using id = std::thread::id; ///< thread id type
    jthread() noexcept : src_(nostopstate) {}
    /// Start a thread running f(token, args...) or f(args...).
    template <class F, class... Args>
        requires(!std::is_same_v<std::remove_cvref_t<F>, jthread>)
    explicit jthread(F&& f, Args&&... args)
    {
        if constexpr (std::is_invocable_v<std::decay_t<F>, stop_token, std::decay_t<Args>...>)
            t_ = std::thread(std::forward<F>(f), src_.get_token(), std::forward<Args>(args)...);
        else
            t_ = std::thread(std::forward<F>(f), std::forward<Args>(args)...);
    }
    jthread(jthread&&) noexcept = default; ///< move
    /// Move-assign: stops and joins the current thread first.
    jthread& operator=(jthread&& o) noexcept
    {
        if (this != &o) {
            stop_and_join();
            src_ = std::move(o.src_);
            t_ = std::move(o.t_);
        }
        return *this;
    }
    jthread(const jthread&) = delete;
    jthread& operator=(const jthread&) = delete;
    ~jthread() { stop_and_join(); }

    [[nodiscard]] bool joinable() const noexcept { return t_.joinable(); } ///< as std::thread
    void join() { t_.join(); }                                             ///< as std::thread
    void detach() { t_.detach(); }                                         ///< as std::thread
    [[nodiscard]] id get_id() const noexcept { return t_.get_id(); }      ///< as std::thread
    /// Request stop of the thread's stop_token.
    bool request_stop() noexcept
    {
        try {
            return src_.request_stop();
        } catch (...) { // a callback threw; std::stop_callback would terminate
            std::terminate();
        }
    }
    [[nodiscard]] stop_source get_stop_source() const noexcept { return src_; } ///< its stop_source
    [[nodiscard]] stop_token get_stop_token() const noexcept { return src_.get_token(); } ///< its token
    void swap(jthread& o) noexcept
    {
        std::swap(src_, o.src_);
        t_.swap(o.t_);
    } ///< exchange

private:
    void stop_and_join() noexcept
    {
        if (t_.joinable()) {
            request_stop();
            t_.join();
        }
    }
    stop_source src_; // declared first: the thread's token comes from it
    std::thread t_;
};
#endif

// ===========================================================================
// Version and features
// ===========================================================================

/// The ABI version this header was written against.
inline constexpr std::uint32_t header_abi_major = ZXV_ABI_VERSION_MAJOR;
/// Minimum ABI minor version this SDK needs (ABI 1.3: the hexagram tag;
/// 1.2 added the *_cap forms).
inline constexpr std::uint32_t header_abi_minor = 3;

/// One harmonic fundamental, in ticks (27,720 = lcm(1..11)).
inline constexpr std::uint64_t phase_cycle = ZXV_PHASE_CYCLE;

/// ABI version of a loaded library.
struct Version {
    std::uint32_t major = 0; ///< breaking-change counter
    std::uint32_t minor = 0; ///< additive-change counter
    std::uint32_t patch = 0; ///< fixes only
    /// Ordered comparison (major, then minor, then patch).
    friend constexpr auto operator<=>(const Version&, const Version&) = default;
};

/// Feature bits reported by zxv_features (ZXV_FEAT_*), as named constants.
namespace feature {
inline constexpr std::uint32_t ledger = ZXV_FEAT_LEDGER;       ///< triple-ledger accounts
inline constexpr std::uint32_t mx_pacs008 = ZXV_FEAT_MX_PACS008; ///< pacs.008 messages
inline constexpr std::uint32_t mx_camt053 = ZXV_FEAT_MX_CAMT053; ///< camt.053 statements
inline constexpr std::uint32_t iso4217 = ZXV_FEAT_ISO4217;     ///< ISO 4217 table
inline constexpr std::uint32_t card_check = ZXV_FEAT_CARD_CHECK; ///< PAN check digits
inline constexpr std::uint32_t rational = ZXV_FEAT_RATIONAL;   ///< exact rationals
inline constexpr std::uint32_t lpres = ZXV_FEAT_LPRES;         ///< four-valued logic
inline constexpr std::uint32_t caps = ZXV_FEAT_CAPS;           ///< capability handles
inline constexpr std::uint32_t rmag = ZXV_FEAT_RMAG;           ///< rational budgets
inline constexpr std::uint32_t phase = ZXV_FEAT_PHASE;         ///< phase ticks
inline constexpr std::uint32_t wire = ZXV_FEAT_WIRE;           ///< UBH-168 frames
inline constexpr std::uint32_t swarm = ZXV_FEAT_SWARM;         ///< swarm economy
inline constexpr std::uint32_t hash = ZXV_FEAT_HASH;           ///< SHA3, SHAKE, Keccak
inline constexpr std::uint32_t mlkem1024 = ZXV_FEAT_MLKEM1024; ///< FIPS 203
inline constexpr std::uint32_t mldsa87 = ZXV_FEAT_MLDSA87;     ///< FIPS 204
inline constexpr std::uint32_t ed25519 = ZXV_FEAT_ED25519;     ///< RFC 8032
inline constexpr std::uint32_t tensor = ZXV_FEAT_TENSOR;       ///< inference engine
inline constexpr std::uint32_t lora = ZXV_FEAT_LORA;           ///< LoRA (not built yet)
inline constexpr std::uint32_t evm = ZXV_FEAT_EVM;             ///< EVM state channels
inline constexpr std::uint32_t hex = ZXV_FEAT_HEX;             ///< hexagram tag, quarantine log (1.3)
} // namespace feature

/// Capability bits (ZXV_CAP_*). Combine with `|`.
namespace caps {
inline constexpr std::uint64_t ledger_read = ZXV_CAP_LEDGER_READ;   ///< read balances, journals
inline constexpr std::uint64_t ledger_post = ZXV_CAP_LEDGER_POST;   ///< post transfers
inline constexpr std::uint64_t crypto_sign = ZXV_CAP_CRYPTO_SIGN;   ///< sign (ML-DSA, Ed25519)
inline constexpr std::uint64_t crypto_kem = ZXV_CAP_CRYPTO_KEM;     ///< ML-KEM
inline constexpr std::uint64_t ai_inference = ZXV_CAP_AI_INFERENCE; ///< open models, generate
inline constexpr std::uint64_t budget = ZXV_CAP_BUDGET;             ///< rmag budgets
inline constexpr std::uint64_t phase = ZXV_CAP_PHASE;               ///< admit / defer / advance
inline constexpr std::uint64_t attest = ZXV_CAP_ATTEST;             ///< attestations
inline constexpr std::uint64_t swarm = ZXV_CAP_SWARM;               ///< swarm economy
inline constexpr std::uint64_t evm = ZXV_CAP_EVM;                   ///< state-channel encoding
inline constexpr std::uint64_t all = ZXV_CAP_ALL;                   ///< every bit above
} // namespace caps

// ===========================================================================
// Errors
// ===========================================================================

/// The class of a status code (zxv_status_class).
enum class StatusClass : std::int32_t {
    ok = ZXV_CLASS_OK,           ///< success
    usage = ZXV_CLASS_USAGE,     ///< caller bug; retrying the same call fails again
    refused = ZXV_CLASS_REFUSED, ///< declined; may succeed once the situation changes
    fatal = ZXV_CLASS_FATAL,     ///< context halted; never retry
    system = ZXV_CLASS_SYSTEM,   ///< environment (memory, I/O, internal)
};

/// Symbolic name of a status ("ZXV_E_FUNDS"); "ZXV_E_UNKNOWN" if unknown.
inline std::string_view status_name(std::int32_t status) noexcept
{
    return zxv_status_name(status);
}

/// Class of a status. Unknown negative codes are SYSTEM (R8).
inline StatusClass status_class(std::int32_t status) noexcept
{
    return static_cast<StatusClass>(zxv_status_class(status));
}

/// The calling thread's last error detail (R7). Diagnostic text only.
inline std::string last_error()
{
    std::size_t need = 0;
    if (zxv_last_error(nullptr, 0, &need) != ZXV_E_BUFFER) return {};
    std::string s(need, '\0');
    if (zxv_last_error(s.data(), need + 1, &need) != ZXV_OK) return {};
    s.resize(need);
    return s;
}

/// Base of every recoverable ZXV error (USAGE, REFUSED, SYSTEM classes).
/// Carries the status code, its symbolic name and the C detail text.
class Exception : public std::runtime_error {
public:
    /// Build from a status and the detail text from zxv_last_error.
    Exception(std::int32_t status, std::string detail)
        : std::runtime_error(std::string(zxv::status_name(status)) +
                             (detail.empty() ? "" : ": " + detail)),
          status_(status), detail_(std::move(detail))
    {
    }
    /// The negative ZXV_E_* status.
    [[nodiscard]] std::int32_t status() const noexcept { return status_; }
    /// The status's symbolic name ("ZXV_E_CAP").
    [[nodiscard]] std::string_view status_name() const noexcept { return zxv::status_name(status_); }
    /// The C library's detail text (may be empty). Never branch on it.
    [[nodiscard]] const std::string& detail() const noexcept { return detail_; }
    /// The status class.
    [[nodiscard]] StatusClass status_class() const noexcept { return zxv::status_class(status_); }

private:
    std::int32_t status_;
    std::string detail_;
};

/// USAGE class: a caller bug (bad argument, malformed text, wrong state of
/// the call itself). Fix the call; retrying unchanged fails again.
class UsageError : public Exception {
public:
    using Exception::Exception;
};

/// A NaN or infinity reached the float boundary (ZXV_E_FLOAT). Only
/// Rational::from_double_exact accepts floating point.
class FloatRejected : public UsageError {
public:
    using UsageError::UsageError;
};

/// REFUSED class: a well-formed request the platform declined. Handle it;
/// a retry may succeed after the situation changes.
class RefusedError : public Exception {
public:
    using Exception::Exception;
};

/// The capability lacks a bit, is revoked, or its attestation is not TRUE.
class CapabilityDenied : public RefusedError {
public:
    using RefusedError::RefusedError;
};

/// An rmag or swarm budget is exhausted (ZXV_E_BUDGET).
class BudgetExhausted : public RefusedError {
public:
    using RefusedError::RefusedError;
};

/// A holder account would go below zero (ZXV_E_FUNDS).
class InsufficientFunds : public RefusedError {
public:
    using RefusedError::RefusedError;
};

/// A signature or tag did not verify (ZXV_E_VERIFY).
class VerificationFailed : public RefusedError {
public:
    using RefusedError::RefusedError;
};

/// The feature is not built into this library (ZXV_E_NOTIMPL).
class NotImplemented : public RefusedError {
public:
    using RefusedError::RefusedError;
};

/// SYSTEM class: out of memory, I/O, internal or unknown status.
class SystemError : public Exception {
public:
    using Exception::Exception;
};

/// A wait was cancelled (stop token, cancel(), or its owner was destroyed).
/// Not a ZXV status; derives from std::runtime_error only.
class OperationCancelled : public std::runtime_error {
public:
    using std::runtime_error::runtime_error;
};

/// FATAL class: the context halted after a broken invariant (or
/// zxv_ctx_halt). Every later call on it fails; never retry.
///
/// FatalError deliberately derives from nothing, not even std::exception,
/// so generic `catch (const std::exception&)` handlers do not swallow it.
/// Only `catch (const zxv::FatalError&)` or `catch (...)` stop it. For a
/// truly uncatchable form, set FatalPolicy::abort.
class FatalError {
public:
    /// Build from a status and the detail text.
    FatalError(std::int32_t status, std::string detail)
        : status_(status), detail_(std::move(detail)),
          what_(std::string(zxv::status_name(status)) + (detail_.empty() ? "" : ": " + detail_))
    {
    }
    /// ZXV_E_POISONED or ZXV_E_INVARIANT.
    [[nodiscard]] std::int32_t status() const noexcept { return status_; }
    /// The status's symbolic name.
    [[nodiscard]] std::string_view status_name() const noexcept { return zxv::status_name(status_); }
    /// The C library's detail text.
    [[nodiscard]] const std::string& detail() const noexcept { return detail_; }
    /// "ZXV_E_POISONED: detail".
    [[nodiscard]] const char* what() const noexcept { return what_.c_str(); }

private:
    std::int32_t status_;
    std::string detail_;
    std::string what_;
};

/// What the SDK does when a call reports a FATAL status.
enum class FatalPolicy {
    throw_error, ///< mark the handle dead and throw zxv::FatalError (default)
    abort,       ///< print the detail to stderr and std::abort() immediately
};

namespace detail {
inline std::atomic<FatalPolicy>& fatal_policy_ref() noexcept
{
    static std::atomic<FatalPolicy> policy{FatalPolicy::throw_error};
    return policy;
}
} // namespace detail

/// Set the process-wide fatal policy. FatalPolicy::abort is the only truly
/// uncatchable form of fail-closed.
inline void set_fatal_policy(FatalPolicy p) noexcept { detail::fatal_policy_ref().store(p); }
/// The current process-wide fatal policy.
inline FatalPolicy fatal_policy() noexcept { return detail::fatal_policy_ref().load(); }

namespace detail {

/// Shared state of one zxv_ctx. Every child handle keeps it alive.
struct CtxCore {
    zxv_ctx* ctx = nullptr;
    std::atomic<bool> dead{false};
    explicit CtxCore(zxv_ctx* c) noexcept : ctx(c) {}
    CtxCore(const CtxCore&) = delete;
    CtxCore& operator=(const CtxCore&) = delete;
    ~CtxCore() { zxv_ctx_destroy(ctx); }
};

[[noreturn]] inline void raise_fatal(std::int32_t status, std::string detail)
{
    if (fatal_policy() == FatalPolicy::abort) {
        std::fprintf(stderr, "zxv: FATAL %s: %s (FatalPolicy::abort)\n", zxv_status_name(status),
                     detail.c_str());
        std::fflush(stderr);
        std::abort();
    }
    throw FatalError(status, std::move(detail));
}

/// Throw the exception for a negative status with an explicit detail.
[[noreturn]] inline void raise(std::int32_t status, std::string detail, CtxCore* core = nullptr)
{
    switch (static_cast<StatusClass>(zxv_status_class(status))) {
    case StatusClass::fatal:
        if (core) core->dead.store(true);
        raise_fatal(status, std::move(detail));
    case StatusClass::usage:
        if (status == ZXV_E_FLOAT) throw FloatRejected(status, std::move(detail));
        throw UsageError(status, std::move(detail));
    case StatusClass::refused:
        switch (status) {
        case ZXV_E_CAP: throw CapabilityDenied(status, std::move(detail));
        case ZXV_E_BUDGET: throw BudgetExhausted(status, std::move(detail));
        case ZXV_E_FUNDS: throw InsufficientFunds(status, std::move(detail));
        case ZXV_E_VERIFY: throw VerificationFailed(status, std::move(detail));
        case ZXV_E_NOTIMPL: throw NotImplemented(status, std::move(detail));
        default: throw RefusedError(status, std::move(detail));
        }
    case StatusClass::ok: // a non-negative status never reaches here
    case StatusClass::system:
    default: throw SystemError(status, std::move(detail));
    }
}

/// Map a C status to nothing (ZXV_OK) or an exception. A FATAL status marks
/// `core` dead so later calls fail before reaching C.
inline void check(std::int32_t status, CtxCore* core = nullptr)
{
    if (status == ZXV_OK) return;
    raise(status, last_error(), core);
}

/// Refuse a call on a context that already raised FATAL.
inline void ensure_alive(const CtxCore* core)
{
    if (core && core->dead.load())
        raise_fatal(ZXV_E_POISONED, "the context halted earlier; this handle is dead");
}

/// R4 two-call pattern for text results.
template <class F>
std::string read_text(F&& call, CtxCore* core = nullptr)
{
    std::size_t need = 0;
    std::int32_t st = call(static_cast<char*>(nullptr), std::size_t{0}, &need);
    if (st != ZXV_E_BUFFER) {
        check(st, core);
        return {};
    }
    std::string s(need, '\0');
    check(call(s.data(), need + 1, &need), core);
    s.resize(need);
    return s;
}

inline const std::uint8_t* bytes_of(std::string_view s) noexcept
{
    return reinterpret_cast<const std::uint8_t*>(s.data());
}

} // namespace detail

// ===========================================================================
// Library
// ===========================================================================

/// ABI version of the linked libzxv.
inline Version version()
{
    Version v;
    detail::check(zxv_version(&v.major, &v.minor, &v.patch));
    return v;
}

/// Bitmask of the features live in the linked libzxv (zxv::feature::*).
inline std::uint32_t features()
{
    std::uint32_t m = 0;
    detail::check(zxv_features(&m));
    return m;
}

/// True if every bit of `mask` (zxv::feature::*) is live in this build.
inline bool has_feature(std::uint32_t mask) { return (features() & mask) == mask; }

/// Refuse a library whose major version differs or whose minor is below 3
/// (SDK_DESIGN.md section 1). Called once, automatically, by Context.
inline void ensure_compatible()
{
    static const Version v = version();
    if (v.major != header_abi_major || v.minor < header_abi_minor)
        throw SystemError(ZXV_E_INTERNAL, "libzxv ABI " + std::to_string(v.major) + "." +
                                              std::to_string(v.minor) + " is not compatible with " +
                                              "this SDK (needs 1.3 or later 1.x)");
}

// ===========================================================================
// Truth: Belnap four-valued logic
// ===========================================================================

/// The kernel's four-valued truth (lpres). Numbered exactly as the C ABI.
/// BOTH is a held contradiction, not an error and not "true". There is no
/// implicit conversion to bool: pick a policy with to_bool, require_true or
/// is_certainly_true.
///
/// Forward compatibility: the kernel is adding an orthogonal 2-bit `mode`
/// beside these four states. It will arrive as a separate accessor or a
/// companion type, never by renumbering these enumerators.
enum class Truth : std::uint8_t {
    neither = ZXV_LP_NEITHER, ///< S0: unknown, unattested, pending
    true_ = ZXV_LP_TRUE,      ///< S+: attested present and valid
    false_ = ZXV_LP_FALSE,    ///< S-: attested absent or invalid
    both = ZXV_LP_BOTH,       ///< contradictory evidence, held
};

namespace detail {
inline constexpr std::array<std::uint8_t, 4> lp_not{0, 2, 1, 3};
inline constexpr std::array<std::array<std::uint8_t, 4>, 4> lp_and{
    {{0, 0, 2, 0}, {0, 1, 2, 3}, {2, 2, 2, 2}, {0, 3, 2, 3}}};
inline constexpr std::array<std::array<std::uint8_t, 4>, 4> lp_or{
    {{0, 1, 0, 3}, {1, 1, 1, 1}, {0, 1, 2, 3}, {3, 1, 3, 3}}};
constexpr std::size_t lp_index(Truth t) noexcept { return static_cast<std::size_t>(t) & 3u; }
} // namespace detail

/// Four-valued NOT (swaps TRUE and FALSE; NEITHER and BOTH are fixed).
constexpr Truth operator~(Truth a) noexcept
{
    return static_cast<Truth>(detail::lp_not[detail::lp_index(a)]);
}
/// Four-valued AND (Belnap's FOUR meet), identical to zxv_lp_and.
constexpr Truth operator&(Truth a, Truth b) noexcept
{
    return static_cast<Truth>(detail::lp_and[detail::lp_index(a)][detail::lp_index(b)]);
}
/// Four-valued OR (Belnap's FOUR join), identical to zxv_lp_or.
constexpr Truth operator|(Truth a, Truth b) noexcept
{
    return static_cast<Truth>(detail::lp_or[detail::lp_index(a)][detail::lp_index(b)]);
}

/// Truth from evidence counts: both -> BOTH, only positive -> TRUE, only
/// negative -> FALSE, none -> NEITHER (zxv_lp_from_evidence).
constexpr Truth truth_from_evidence(std::uint32_t positive, std::uint32_t negative) noexcept
{
    return positive && negative ? Truth::both
           : positive           ? Truth::true_
           : negative           ? Truth::false_
                                : Truth::neither;
}

/// Convert a C lp value; unknown values are a UsageError.
inline Truth truth_from_c(std::int32_t v)
{
    if (v < ZXV_LP_NEITHER || v > ZXV_LP_BOTH)
        throw UsageError(ZXV_E_ARG, "lp value " + std::to_string(v) + " is not 0..3");
    return static_cast<Truth>(v);
}

/// The C numbering of a Truth (for interop).
constexpr std::int32_t to_c(Truth t) noexcept { return static_cast<std::int32_t>(t); }

/// "NEITHER", "TRUE", "FALSE" or "BOTH".
constexpr std::string_view to_string(Truth t) noexcept
{
    constexpr std::array<std::string_view, 4> names{"NEITHER", "TRUE", "FALSE", "BOTH"};
    return names[detail::lp_index(t)];
}

/// True only for Truth::true_ (BOTH is not certainly true).
constexpr bool is_certainly_true(Truth t) noexcept { return t == Truth::true_; }
/// True only for Truth::false_.
constexpr bool is_certainly_false(Truth t) noexcept { return t == Truth::false_; }
/// True for TRUE or FALSE (no contradiction, no gap).
constexpr bool is_certain(Truth t) noexcept { return t == Truth::true_ || t == Truth::false_; }

/// Collapse to bool with an explicit policy for BOTH and NEITHER.
constexpr bool to_bool(Truth t, bool on_both, bool on_neither) noexcept
{
    switch (t) {
    case Truth::true_: return true;
    case Truth::false_: return false;
    case Truth::both: return on_both;
    case Truth::neither:
    default: return on_neither;
    }
}

/// Return normally only for TRUE; otherwise throw RefusedError naming the state.
inline void require_true(Truth t, std::string_view what = "value")
{
    if (t != Truth::true_)
        throw RefusedError(ZXV_E_STATE,
                           std::string(what) + " is " + std::string(to_string(t)) + ", not TRUE");
}

/// Stream as its name.
inline std::ostream& operator<<(std::ostream& os, Truth t) { return os << to_string(t); }

// ===========================================================================
// Rational
// ===========================================================================

/// An exact rational number: int64 numerator over a positive int64
/// denominator, always reduced. The numeric normal form of the kernel.
///
/// Construction is from integers (Rational{3}, Rational::of(3, 4)), exact
/// text (Rational::parse("12.75")), Q16.16 (from_q16), or one deliberately
/// named float entry point (from_double_exact). Constructing from a float
/// type does not compile.
///
/// Arithmetic never rounds and never wraps: the operators throw UsageError
/// (ZXV_E_RANGE) when a result does not fit; the checked_* forms return
/// std::nullopt instead. With gcc or clang everything except text and float
/// conversion is constexpr, and gives the same answer as the C library.
class Rational {
public:
    /// Zero.
    constexpr Rational() noexcept = default;

    /// An integer. INT64_MIN is refused (its negation is not an int64).
    template <std::integral I>
        requires(!std::same_as<I, bool>)
    constexpr Rational(I value) // NOLINT(google-explicit-constructor): integers are exact
    {
        if constexpr (std::is_signed_v<I> && sizeof(I) >= sizeof(std::int64_t)) {
            if (static_cast<std::int64_t>(value) == INT64_MIN)
                throw UsageError(ZXV_E_RANGE, "INT64_MIN has no int64 negation");
        }
        if constexpr (std::is_unsigned_v<I> && sizeof(I) >= sizeof(std::int64_t)) {
            if (value > static_cast<I>(INT64_MAX))
                throw UsageError(ZXV_E_RANGE, "value exceeds int64");
        }
        num_ = static_cast<std::int64_t>(value);
    }

    /// Floating point never converts implicitly; use from_double_exact.
    template <std::floating_point F>
    Rational(F) = delete;

    /// num/den, reduced. den == 0 is ZXV_E_ARG; INT64_MIN is ZXV_E_RANGE.
    static ZXV_RAT_CONSTEXPR Rational of(std::int64_t num, std::int64_t den)
    {
        if (den == 0) throw UsageError(ZXV_E_ARG, "zero denominator");
        if (num == INT64_MIN || den == INT64_MIN)
            throw UsageError(ZXV_E_RANGE, "INT64_MIN has no int64 negation");
#if ZXV_HAS_INT128
        auto r = make(num, den);
        if (!r) throw UsageError(ZXV_E_RANGE, "value cannot be normalized");
        return *r;
#else
        Rational r;
        detail::check(zxv_rat_normalize(num, den, &r.num_, &r.den_));
        return r;
#endif
    }

    /// q / 65536 exactly (Q16.16 fixed point, as the engine uses).
    static ZXV_RAT_CONSTEXPR Rational from_q16(std::int32_t q16) { return of(q16, 65536); }

    /// Parse "12.75", "-0.1", "3/4" or "42" exactly. Anything else
    /// (exponents, spaces, "NaN") is ZXV_E_FORMAT; too large is ZXV_E_RANGE.
    static Rational parse(const std::string& text)
    {
        Rational r;
        detail::check(zxv_rat_parse(text.c_str(), &r.num_, &r.den_));
        return r;
    }

    /// THE FLOAT BOUNDARY. The exact value of a double: 0.1 becomes
    /// 3602879701896397/36028797018963968, never 1/10. NaN and infinity
    /// throw FloatRejected; values needing more than 63 bits, UsageError.
    static Rational from_double_exact(double x)
    {
        Rational r;
        detail::check(zxv_rat_from_ieee754(std::bit_cast<std::uint64_t>(x), &r.num_, &r.den_));
        return r;
    }

    /// The numerator (sign carrier).
    [[nodiscard]] constexpr std::int64_t num() const noexcept { return num_; }
    /// The denominator (always > 0).
    [[nodiscard]] constexpr std::int64_t den() const noexcept { return den_; }
    /// True for 0.
    [[nodiscard]] constexpr bool is_zero() const noexcept { return num_ == 0; }
    /// True when den() == 1.
    [[nodiscard]] constexpr bool is_integer() const noexcept { return den_ == 1; }
    /// -1, 0 or +1.
    [[nodiscard]] constexpr int sign() const noexcept { return (num_ > 0) - (num_ < 0); }

    /// Exact decimal when one exists ("12.75"), else "n/d" ("1/3").
    [[nodiscard]] std::string to_string() const
    {
        return detail::read_text([&](char* b, std::size_t c, std::size_t* n) {
            return zxv_rat_format(num_, den_, b, c, n);
        });
    }

    /// Round to Q16.16 (half away from zero). ZXV_E_RANGE outside int32.
    [[nodiscard]] std::int32_t to_q16() const
    {
        std::int32_t q = 0;
        detail::check(zxv_rat_to_q16(num_, den_, &q));
        return q;
    }

    /// a + b, or nullopt if the result does not fit.
    [[nodiscard]] ZXV_RAT_CONSTEXPR std::optional<Rational> checked_add(Rational b) const noexcept
    {
#if ZXV_HAS_INT128
        const std::int64_t g = gcd64(den_, b.den_);
        const i128 d = static_cast<i128>(den_ / g) * b.den_;
        const i128 n = static_cast<i128>(num_) * (b.den_ / g) + static_cast<i128>(b.num_) * (den_ / g);
        return make(n, d);
#else
        return c_op(zxv_rat_add, b);
#endif
    }
    /// a - b, or nullopt if the result does not fit.
    [[nodiscard]] ZXV_RAT_CONSTEXPR std::optional<Rational> checked_sub(Rational b) const noexcept
    {
        b.num_ = -b.num_; // never INT64_MIN: |num| <= INT64_MAX always
        return checked_add(b);
    }
    /// a * b, or nullopt if the result does not fit.
    [[nodiscard]] ZXV_RAT_CONSTEXPR std::optional<Rational> checked_mul(Rational b) const noexcept
    {
#if ZXV_HAS_INT128
        const std::int64_t g1 = gcd64(num_, b.den_), g2 = gcd64(b.num_, den_);
        return make(static_cast<i128>(num_ / g1) * (b.num_ / g2),
                    static_cast<i128>(den_ / g2) * (b.den_ / g1));
#else
        return c_op(zxv_rat_mul, b);
#endif
    }
    /// a / b, or nullopt if b is zero or the result does not fit.
    [[nodiscard]] ZXV_RAT_CONSTEXPR std::optional<Rational> checked_div(Rational b) const noexcept
    {
        if (b.num_ == 0) return std::nullopt;
        Rational inv;
        inv.num_ = b.num_ < 0 ? -b.den_ : b.den_;
        inv.den_ = b.num_ < 0 ? -b.num_ : b.num_;
        return checked_mul(inv);
    }

    /// Exact sum; throws UsageError (ZXV_E_RANGE) on overflow.
    friend ZXV_RAT_CONSTEXPR Rational operator+(Rational a, Rational b) { return a.or_range(a.checked_add(b), "add"); }
    /// Exact difference; throws UsageError (ZXV_E_RANGE) on overflow.
    friend ZXV_RAT_CONSTEXPR Rational operator-(Rational a, Rational b) { return a.or_range(a.checked_sub(b), "sub"); }
    /// Exact product; throws UsageError (ZXV_E_RANGE) on overflow.
    friend ZXV_RAT_CONSTEXPR Rational operator*(Rational a, Rational b) { return a.or_range(a.checked_mul(b), "mul"); }
    /// Exact quotient; throws UsageError: ZXV_E_ARG for b == 0, ZXV_E_RANGE on overflow.
    friend ZXV_RAT_CONSTEXPR Rational operator/(Rational a, Rational b)
    {
        if (b.num_ == 0) throw UsageError(ZXV_E_ARG, "division by zero");
        return a.or_range(a.checked_div(b), "div");
    }
    /// Negation (always exact).
    constexpr Rational operator-() const noexcept
    {
        Rational r = *this;
        r.num_ = -r.num_;
        return r;
    }
    /// Absolute value (always exact).
    [[nodiscard]] constexpr Rational abs() const noexcept { return num_ < 0 ? -*this : *this; }

    /// In-place forms of the operators.
    ZXV_RAT_CONSTEXPR Rational& operator+=(Rational b) { return *this = *this + b; }
    /// In-place subtraction.
    ZXV_RAT_CONSTEXPR Rational& operator-=(Rational b) { return *this = *this - b; }
    /// In-place multiplication.
    ZXV_RAT_CONSTEXPR Rational& operator*=(Rational b) { return *this = *this * b; }
    /// In-place division.
    ZXV_RAT_CONSTEXPR Rational& operator/=(Rational b) { return *this = *this / b; }

    /// Equality of reduced forms (exact).
    friend constexpr bool operator==(Rational a, Rational b) noexcept = default;
    /// Exact total order; no epsilon.
    friend ZXV_RAT_CONSTEXPR std::strong_ordering operator<=>(Rational a, Rational b) noexcept
    {
#if ZXV_HAS_INT128
        const i128 l = static_cast<i128>(a.num_) * b.den_, r = static_cast<i128>(b.num_) * a.den_;
        return l <=> r;
#else
        std::int32_t c = 0;
        zxv_rat_cmp(a.num_, a.den_, b.num_, b.den_, &c);
        return c <=> 0;
#endif
    }

private:
#if ZXV_HAS_INT128
    __extension__ typedef __int128 i128;
    static constexpr std::int64_t gcd64(std::int64_t a, std::int64_t b) noexcept
    {
        if (a < 0) a = -a;
        if (b < 0) b = -b;
        while (b) {
            const std::int64_t t = a % b;
            a = b;
            b = t;
        }
        return a ? a : 1;
    }
    /// The kernel's reduce(): 128-bit reduce, then require it fits int64.
    static constexpr std::optional<Rational> make(i128 n, i128 d) noexcept
    {
        if (d == 0) return std::nullopt;
        Rational r;
        if (n == 0) return r;
        if (d < 0) {
            n = -n;
            d = -d;
        }
        i128 a = n < 0 ? -n : n, b = d;
        while (b) {
            const i128 t = a % b;
            a = b;
            b = t;
        }
        n /= a;
        d /= a;
        if (n > INT64_MAX || n < -static_cast<i128>(INT64_MAX) || d > INT64_MAX) return std::nullopt;
        r.num_ = static_cast<std::int64_t>(n);
        r.den_ = static_cast<std::int64_t>(d);
        return r;
    }
#else
    using c_fn = std::int32_t (*)(std::int64_t, std::int64_t, std::int64_t, std::int64_t,
                                  std::int64_t*, std::int64_t*);
    std::optional<Rational> c_op(c_fn f, Rational b) const noexcept
    {
        Rational r;
        if (f(num_, den_, b.num_, b.den_, &r.num_, &r.den_) != ZXV_OK) return std::nullopt;
        return r;
    }
#endif
    static ZXV_RAT_CONSTEXPR Rational or_range(std::optional<Rational> r, const char* op)
    {
        if (!r) throw UsageError(ZXV_E_RANGE, std::string(op) + " overflows 64-bit numerator/denominator");
        return *r;
    }

    std::int64_t num_ = 0;
    std::int64_t den_ = 1;
};

/// Stream as Rational::to_string().
inline std::ostream& operator<<(std::ostream& os, const Rational& r) { return os << r.to_string(); }

// ===========================================================================
// Context and Capability
// ===========================================================================

class Capability;
namespace detail {
struct CapCore;
} // namespace detail

/// One platform instance: a ledger, its currencies, and everything created
/// from it. Move-only. Its C handle lives until the last child (capability,
/// budget, model...) is gone, so destruction order cannot be wrong.
class Context {
public:
    /// Create an empty context (checks the library version first).
    Context()
    {
        ensure_compatible();
        zxv_ctx* c = nullptr;
        detail::check(zxv_ctx_create(&c));
        core_ = std::make_shared<detail::CtxCore>(c);
    }
    Context(Context&&) noexcept = default;            ///< move
    Context& operator=(Context&&) noexcept = default; ///< move-assign
    Context(const Context&) = delete;
    Context& operator=(const Context&) = delete;
    ~Context() = default;

    /// A root capability for this context. `bits` is a mask of zxv::caps;
    /// `identity` is the 21-octet (168-bit) identity, zeros if omitted.
    /// Attestation starts TRUE.
    [[nodiscard]] Capability root_capability(std::uint64_t bits = caps::all,
                                             std::uint32_t process_id = 0,
                                             std::optional<std::array<std::uint8_t, ZXV_IDENTITY_BYTES>> identity = {}) const;

    /// True once the context halted (a broken invariant or halt()).
    [[nodiscard]] bool is_poisoned() const
    {
        if (core_->dead.load()) return true;
        std::int32_t p = 0;
        detail::check(zxv_ctx_is_poisoned(core_->ctx, &p));
        return p != 0;
    }

    /// Halt the context on purpose (an operator kill switch). Afterwards
    /// every call fails with FatalError. `reason` <= 150 bytes.
    void halt(const std::string& reason = {})
    {
        detail::check(zxv_ctx_halt(core_->ctx, reason.empty() ? nullptr : reason.c_str()), core_.get());
    }

    /// Enable an ISO 4217 currency for ledger accounts ("NGN") through
    /// `cap`, which needs caps::ledger_post. A capability of another
    /// context throws CapabilityDenied (ZXV_E_CAP).
    void enable_currency(const Capability& cap, const std::string& alpha);
    /// Enable a currency asserting its minor units, through `cap`
    /// (caps::ledger_post); a mismatch with the ISO table is ZXV_E_CURRENCY.
    void enable_currency(const Capability& cap, const std::string& alpha, std::uint8_t minor_units);
    /// Register a platform-private unit ("VFV") through `cap`
    /// (caps::ledger_post). Never valid in ISO 20022.
    void register_private_unit(const Capability& cap, const std::string& code, std::uint8_t minor_units);

    /// Enable an ISO 4217 currency for ledger accounts ("NGN").
    [[deprecated("ABI 1.2: pass a capability with caps::ledger_post: enable_currency(cap, alpha)")]]
    void enable_currency(const std::string& alpha)
    {
        call([&] { return zxv_ccy_enable(core_->ctx, alpha.c_str()); });
    }
    /// Enable a currency asserting its minor units (for builds without the
    /// ISO 4217 table; a mismatch with the table is ZXV_E_CURRENCY).
    [[deprecated("ABI 1.2: pass a capability with caps::ledger_post: enable_currency(cap, alpha, minor_units)")]]
    void enable_currency(const std::string& alpha, std::uint8_t minor_units)
    {
        call([&] { return zxv_ccy_enable_with_minor(core_->ctx, alpha.c_str(), minor_units); });
    }
    /// Register a platform-private unit ("VFV"). Never valid in ISO 20022.
    [[deprecated("ABI 1.2: pass a capability with caps::ledger_post: register_private_unit(cap, code, minor_units)")]]
    void register_private_unit(const std::string& code, std::uint8_t minor_units)
    {
        call([&] { return zxv_ccy_register_private(core_->ctx, code.c_str(), minor_units); });
    }
    /// The currencies enabled in this context, in enable order.
    [[nodiscard]] std::vector<std::string> enabled_currencies() const
    {
        std::uint32_t n = 0;
        call([&] { return zxv_ccy_enabled_count(core_->ctx, &n); });
        std::vector<std::string> out;
        for (std::uint32_t i = 0; i < n; ++i)
            out.push_back(detail::read_text(
                [&](char* b, std::size_t c, std::size_t* l) { return zxv_ccy_enabled_at(core_->ctx, i, b, c, l); },
                core_.get()));
        return out;
    }

    /// The raw C handle, for interop with code that calls zxv_api.h
    /// directly. Owned by this Context; never destroy it.
    [[nodiscard]] zxv_ctx* native_handle() const noexcept { return core_->ctx; }

private:
    friend class Capability;
    friend class Message;
    template <class F>
    void call(F&& f) const
    {
        detail::ensure_alive(core_.get());
        detail::check(f(), core_.get());
    }
    /// The cap's core, after refusing a capability of another context (the
    /// C *_cap forms act on the capability's own context, not on this one).
    const detail::CapCore& own(const Capability& cap) const;
    std::shared_ptr<detail::CtxCore> core_;
};

namespace detail {
struct CapCore {
    zxv_cap* cap = nullptr;
    std::shared_ptr<CtxCore> ctx;
    std::shared_ptr<CapCore> parent; // the C parent must outlive its child
    CapCore(zxv_cap* c, std::shared_ptr<CtxCore> x, std::shared_ptr<CapCore> p) noexcept
        : cap(c), ctx(std::move(x)), parent(std::move(p))
    {
    }
    CapCore(const CapCore&) = delete;
    CapCore& operator=(const CapCore&) = delete;
    ~CapCore() { zxv_cap_destroy(cap); }

    /// Run a gated C call: refuse on a dead context, map the status.
    template <class F>
    void call(F&& f) const
    {
        ensure_alive(ctx.get());
        check(f(), ctx.get());
    }
};
} // namespace detail

/// Snapshot of a capability (zxv_cap_info).
struct CapabilityInfo {
    std::uint64_t bits = 0;         ///< zxv::caps mask
    std::uint32_t process_id = 0;   ///< the process id it was created with
    Truth attestation = Truth::neither; ///< gated calls need TRUE
    bool revoked = false;           ///< revoked itself or through an ancestor
};

/// A capability: what a part of the program may do. Gated SDK objects hold
/// one and pass it on every call. Narrow it with attenuate(); it can never
/// be widened. Move-only; the underlying handle lives as long as any object
/// holding it.
///
/// Honest limit: inside one process this is least-privilege discipline,
/// not a sandbox (code in the same address space can create its own).
class Capability {
public:
    Capability(Capability&&) noexcept = default;            ///< move
    Capability& operator=(Capability&&) noexcept = default; ///< move-assign
    Capability(const Capability&) = delete;
    Capability& operator=(const Capability&) = delete;
    ~Capability() = default;

    /// A child holding (these bits & keep). Revoking this revokes the child.
    [[nodiscard]] Capability attenuate(std::uint64_t keep) const
    {
        zxv_cap* c = nullptr;
        core_->call([&] { return zxv_cap_attenuate(core_->cap, keep, &c); });
        return Capability(std::make_shared<detail::CapCore>(c, core_->ctx, core_));
    }

    /// Permanently revoke this capability and every descendant.
    void revoke() { core_->call([&] { return zxv_cap_revoke(core_->cap); }); }

    /// Set the attestation. Anything but TRUE blocks gated calls; BOTH
    /// reports "contradictory attestation".
    void set_attestation(Truth t)
    {
        core_->call([&] { return zxv_cap_set_attestation(core_->cap, to_c(t)); });
    }

    /// Current bits, process id, attestation and revocation.
    [[nodiscard]] CapabilityInfo info() const
    {
        std::uint64_t bits = 0;
        std::uint32_t pid = 0;
        std::int32_t att = 0, rev = 0;
        core_->call([&] { return zxv_cap_info(core_->cap, &bits, &pid, &att, &rev); });
        return {bits, pid, truth_from_c(att), rev != 0};
    }

    /// True if a call needing `needed` would pass (live, bits set, attested).
    [[nodiscard]] bool check(std::uint64_t needed) const
    {
        detail::ensure_alive(core_->ctx.get());
        const std::int32_t st = zxv_cap_check(core_->cap, needed);
        if (st == ZXV_E_CAP) return false;
        detail::check(st, core_->ctx.get());
        return true;
    }

    /// Like check(), but throws CapabilityDenied with the C detail.
    void require(std::uint64_t needed) const
    {
        core_->call([&] { return zxv_cap_check(core_->cap, needed); });
    }

    /// The raw C handle (owned by this object; never destroy it).
    [[nodiscard]] const zxv_cap* native_handle() const noexcept { return core_->cap; }

    /// Internal: the shared core (used by gated SDK objects).
    [[nodiscard]] const std::shared_ptr<detail::CapCore>& core() const noexcept { return core_; }

private:
    friend class Context;
    explicit Capability(std::shared_ptr<detail::CapCore> c) noexcept : core_(std::move(c)) {}
    std::shared_ptr<detail::CapCore> core_;
};

inline const detail::CapCore& Context::own(const Capability& cap) const
{
    detail::ensure_alive(core_.get());
    if (cap.core()->ctx != core_) throw CapabilityDenied(ZXV_E_CAP, "the capability belongs to a different context");
    return *cap.core();
}

inline void Context::enable_currency(const Capability& cap, const std::string& alpha)
{
    const detail::CapCore& c = own(cap);
    c.call([&] { return zxv_ccy_enable_cap(c.cap, alpha.c_str()); });
}

inline void Context::enable_currency(const Capability& cap, const std::string& alpha, std::uint8_t minor_units)
{
    const detail::CapCore& c = own(cap);
    c.call([&] { return zxv_ccy_enable_with_minor_cap(c.cap, alpha.c_str(), minor_units); });
}

inline void Context::register_private_unit(const Capability& cap, const std::string& code, std::uint8_t minor_units)
{
    const detail::CapCore& c = own(cap);
    c.call([&] { return zxv_ccy_register_private_cap(c.cap, code.c_str(), minor_units); });
}

inline Capability Context::root_capability(std::uint64_t bits, std::uint32_t process_id,
                                           std::optional<std::array<std::uint8_t, ZXV_IDENTITY_BYTES>> identity) const
{
    zxv_cap* c = nullptr;
    call([&] { return zxv_cap_create(core_->ctx, process_id, identity ? identity->data() : nullptr, bits, &c); });
    return Capability(std::make_shared<detail::CapCore>(c, core_, nullptr));
}

// ===========================================================================
// Currencies (process-wide ISO 4217 reference data)
// ===========================================================================

/// One ISO 4217 entry.
struct CurrencyInfo {
    std::string alpha;          ///< "NGN"
    std::uint16_t numeric = 0;  ///< 566
    std::uint8_t minor_units = 0; ///< 2; 255 = not applicable
    std::uint32_t flags = 0;    ///< ZXV_CCYF_*
    std::string name;           ///< "Naira"
};

/// Look up an ISO 4217 currency by alpha code. NOT_FOUND throws UsageError.
inline CurrencyInfo iso4217(const std::string& alpha)
{
    CurrencyInfo c;
    c.alpha = alpha;
    detail::check(zxv_iso4217_by_alpha(alpha.c_str(), &c.numeric, &c.minor_units, &c.flags));
    c.name = detail::read_text([&](char* b, std::size_t n, std::size_t* l) {
        return zxv_iso4217_name(alpha.c_str(), b, n, l);
    });
    return c;
}

/// Card network for check-digit validation.
enum class CardNetwork : std::int32_t {
    phoenix = ZXV_NET_PHOENIX,         ///< Damm check digit
    dragon = ZXV_NET_DRAGON,           ///< Luhn check digit
    thunderbird = ZXV_NET_THUNDERBIRD, ///< Verhoeff check digit
};

/// True if `pan` (decimal digits) has a valid check digit for `network`.
inline bool card_check_digit(CardNetwork network, const std::string& pan)
{
    std::int32_t ok = 0;
    detail::check(zxv_card_check_digit(static_cast<std::int32_t>(network), pan.c_str(), &ok));
    return ok != 0;
}

// ===========================================================================
// Ledger
// ===========================================================================

/// Kind of ledger account.
enum class AccountKind : std::int32_t {
    holder = ZXV_ACCT_HOLDER, ///< may never go below zero
    issuer = ZXV_ACCT_ISSUER, ///< may carry a CREDIT (777) balance
};

/// Largest amount in one posting and largest |balance| (2^53 - 1).
inline constexpr std::int64_t amount_max = ZXV_AMOUNT_MAX;

/// Rail balances of an account in exact minor units.
struct Balance {
    std::int64_t debit = 0;  ///< 555 NCR: sum received
    std::int64_t credit = 0; ///< 777 NRE: sum sent
    std::int64_t equity = 0; ///< 888 PNS: debit - credit (spendable)
};

/// One journal line of an account.
struct JournalLine {
    std::uint64_t entry_id = 0;      ///< platform entry id (shared by both sides)
    std::int64_t amount = 0;         ///< + received / - sent, minor units
    std::uint32_t counterparty = 0;  ///< the other account
    std::string reference;           ///< the unique reference
};

/// The triple ledger of a context, used through a capability. Amounts are
/// exact int64 minor units (cents, kobo...). Posting and opening accounts
/// need caps::ledger_post; balances, account metadata and journals need
/// caps::ledger_read. Every call is the capability-gated *_cap C form.
class Ledger {
public:
    /// A ledger view acting with `cap`.
    explicit Ledger(const Capability& cap) : cap_(cap.core()) {}

    /// Open an account in an enabled currency. `name` <= 63 bytes.
    std::uint32_t open_account(const std::string& name, const std::string& ccy,
                               AccountKind kind = AccountKind::holder)
    {
        std::uint32_t id = 0;
        cap_->call([&] { return zxv_ledger_open_account_cap(cap_->cap, name.c_str(), ccy.c_str(),
                                                            static_cast<std::int32_t>(kind), &id); });
        return id;
    }

    /// Post a balanced transfer (amount > 0). `reference` (<= 35 bytes) must
    /// be unique. Returns the entry id. A holder going below zero throws
    /// InsufficientFunds.
    std::uint64_t post(std::uint32_t from, std::uint32_t to, std::int64_t amount,
                       const std::string& ccy, const std::string& reference)
    {
        std::uint64_t e = 0;
        cap_->call([&] { return zxv_ledger_post_cap(cap_->cap, from, to, amount, ccy.c_str(), reference.c_str(), &e); });
        return e;
    }

    /// Rail balances of an account.
    [[nodiscard]] Balance balance(std::uint32_t account) const
    {
        Balance b;
        cap_->call([&] { return zxv_ledger_balance_cap(cap_->cap, account, &b.debit, &b.credit, &b.equity); });
        return b;
    }

    /// Verify every invariant. A failure halts the context (FatalError).
    void check() const { cap_->call([&] { return zxv_ledger_check_cap(cap_->cap); }); }

    /// Journal of one account, oldest first (needs caps::ledger_read).
    [[nodiscard]] std::vector<JournalLine> journal(std::uint32_t account) const
    {
        std::uint32_t n = 0;
        cap_->call([&] { return zxv_ledger_entry_count_cap(cap_->cap, account, &n); });
        std::vector<JournalLine> out(n);
        for (std::uint32_t i = 0; i < n; ++i) {
            JournalLine& l = out[i];
            l.reference = detail::read_text(
                [&](char* b, std::size_t c, std::size_t* len) {
                    return zxv_ledger_entry_at_cap(cap_->cap, account, i, &l.entry_id, &l.amount,
                                                   &l.counterparty, b, c, len);
                },
                cap_->ctx.get());
        }
        return out;
    }

    /// Currency of an account (needs caps::ledger_read).
    [[nodiscard]] std::string account_currency(std::uint32_t account) const
    {
        detail::ensure_alive(cap_->ctx.get());
        return detail::read_text([&](char* b, std::size_t c, std::size_t* l) {
            return zxv_ledger_account_ccy_cap(cap_->cap, account, b, c, l);
        }, cap_->ctx.get());
    }

    /// Name of an account (needs caps::ledger_read).
    [[nodiscard]] std::string account_name(std::uint32_t account) const
    {
        detail::ensure_alive(cap_->ctx.get());
        return detail::read_text([&](char* b, std::size_t c, std::size_t* l) {
            return zxv_ledger_account_name_cap(cap_->cap, account, b, c, l);
        }, cap_->ctx.get());
    }

    /// Kind of an account (needs caps::ledger_read).
    [[nodiscard]] AccountKind account_kind(std::uint32_t account) const
    {
        std::int32_t k = 0;
        cap_->call([&] { return zxv_ledger_account_kind_cap(cap_->cap, account, &k); });
        return static_cast<AccountKind>(k);
    }

    /// Number of accounts in the context (needs caps::ledger_read).
    [[nodiscard]] std::uint32_t account_count() const
    {
        std::uint32_t n = 0;
        cap_->call([&] { return zxv_ledger_account_count_cap(cap_->cap, &n); });
        return n;
    }

private:
    std::shared_ptr<detail::CapCore> cap_;
};

// ===========================================================================
// ISO 20022 messages
// ===========================================================================

/// ISO 20022 message kinds (only pacs.008 and camt.053 are live).
enum class MxKind : std::int32_t {
    pacs008 = ZXV_MX_PACS008, ///< FIToFICstmrCdtTrf pacs.008.001.09
    camt053 = ZXV_MX_CAMT053, ///< BkToCstmrStmt camt.053.001.08
    pacs002 = ZXV_MX_PACS002, ///< pending
    pacs004 = ZXV_MX_PACS004, ///< pending
    pacs009 = ZXV_MX_PACS009, ///< pending
    camt056 = ZXV_MX_CAMT056, ///< pending
    camt029 = ZXV_MX_CAMT029, ///< pending
};

/// Text fields of a message.
enum class MxField : std::int32_t {
    msg_id = ZXV_FLD_MSG_ID, cre_dt_tm = ZXV_FLD_CRE_DT_TM, debtor_name = ZXV_FLD_DEBTOR_NAME,
    debtor_acct = ZXV_FLD_DEBTOR_ACCT, creditor_name = ZXV_FLD_CREDITOR_NAME,
    creditor_acct = ZXV_FLD_CREDITOR_ACCT, end_to_end_id = ZXV_FLD_END_TO_END_ID,
    ccy = ZXV_FLD_CCY, acct_id = ZXV_FLD_ACCT_ID,
};

/// Amount fields of a message.
enum class MxAmount : std::int32_t {
    settlement = ZXV_AMT_SETTLEMENT, ///< pacs008 IntrBkSttlmAmt
    opening = ZXV_AMT_OPENING,       ///< camt053 OPBD
    closing = ZXV_AMT_CLOSING,       ///< camt053 CLBD
};

/// One ISO 20022 message under construction. Move-only; not thread-safe.
/// Made through a capability (caps::ledger_read), which the library
/// re-checks on every call and which the message keeps alive.
class Message {
public:
    /// Create a message of `kind` through `cap` (caps::ledger_read). Pending
    /// kinds throw NotImplemented.
    Message(const Capability& cap, MxKind kind) : ctx_(cap.core()->ctx), cap_(cap.core())
    {
        zxv_msg* m = nullptr;
        cap_->call([&] { return zxv_msg_create_cap(cap_->cap, static_cast<std::int32_t>(kind), &m); });
        msg_.reset(m);
    }
    /// Create a message of `kind` in `ctx`. Pending kinds throw NotImplemented.
    [[deprecated("ABI 1.2: create messages through a capability with caps::ledger_read: Message(cap, kind)")]]
    Message(const Context& ctx, MxKind kind) : ctx_(ctx_core(ctx))
    {
        detail::ensure_alive(ctx_.get());
        zxv_msg* m = nullptr;
        detail::check(zxv_msg_create(ctx_->ctx, static_cast<std::int32_t>(kind), &m), ctx_.get());
        msg_.reset(m);
    }
    /// Set a text field (plain UTF-8; the serializer escapes XML).
    Message& set(MxField field, const std::string& utf8)
    {
        detail::check(zxv_msg_set_text(msg_.get(), static_cast<std::int32_t>(field), utf8.c_str()), ctx_.get());
        return *this;
    }
    /// Set an amount: exact units with `frac_digits` decimal places.
    Message& set_amount(MxAmount field, std::int64_t units, std::uint8_t frac_digits)
    {
        detail::check(zxv_msg_set_amount(msg_.get(), static_cast<std::int32_t>(field), units, frac_digits), ctx_.get());
        return *this;
    }
    /// pacs008 GrpHdr/NbOfTxs (default 1).
    Message& set_tx_count(std::uint32_t n)
    {
        detail::check(zxv_msg_set_tx_count(msg_.get(), n), ctx_.get());
        return *this;
    }
    /// Append a camt053 entry (magnitude, credit=true / debit=false, status).
    Message& add_entry(std::int64_t units, std::uint8_t frac_digits, bool credit,
                       std::int32_t status = ZXV_STS_BOOK)
    {
        detail::check(zxv_msg_add_entry(msg_.get(), units, frac_digits, credit ? ZXV_CRDT : ZXV_DBIT, status),
                      ctx_.get());
        return *this;
    }
    /// Serialize to XML; validates mandatory fields and currency.
    [[nodiscard]] std::string render() const
    {
        return detail::read_text([&](char* b, std::size_t c, std::size_t* l) {
            return zxv_msg_render(msg_.get(), b, c, l);
        }, ctx_.get());
    }

private:
    struct Del {
        void operator()(zxv_msg* m) const noexcept { zxv_msg_destroy(m); }
    };
    static std::shared_ptr<detail::CtxCore> ctx_core(const Context& c);
    std::shared_ptr<detail::CtxCore> ctx_; // declared first: destroyed after msg_
    std::shared_ptr<detail::CapCore> cap_; // the cap a *_cap message re-checks; outlives msg_
    std::unique_ptr<zxv_msg, Del> msg_;
};

// ===========================================================================
// Attestations
// ===========================================================================

/// Subject kinds of an attestation (the kernel's lpres_subject_type_t).
enum class Subject : std::int32_t {
    source = 0, binary = 1, config = 2, dependency = 3, dataset = 4, device = 5, model = 6,
    identity = 7, policy = 8, build = 9, artifact = 10, network = 11, sensor = 12,
    actuator = 13, firmware = 14,
};

/// State of one attestation.
struct AttestationState {
    Truth truth = Truth::neither;   ///< follows truth_from_evidence
    std::uint32_t positive = 0;     ///< positive evidence count
    std::uint32_t negative = 0;     ///< negative evidence count
};

/// An attestation registry (lpres). Writes need caps::attest. The gate is
/// clear only when every attestation is certain: no BOTH, no NEITHER.
class Attestations {
public:
    /// A registry in the capability's context (needs caps::attest).
    explicit Attestations(const Capability& cap) : cap_(cap.core())
    {
        zxv_lpres* r = nullptr;
        cap_->call([&] { return zxv_lpres_create_cap(cap_->cap, &r); });
        reg_.reset(r);
    }
    /// Record an attestation. name, attester <= 31 bytes; digest <= 32 bytes.
    std::uint32_t attest(const std::string& name, Subject subject, Truth state, const std::string& attester,
                         std::span<const std::uint8_t> digest = {})
    {
        std::uint32_t i = 0;
        cap_->call([&] {
            return zxv_lpres_attest(cap_->cap, reg_.get(), name.c_str(), static_cast<std::int32_t>(subject),
                                    to_c(state), attester.c_str(), digest.empty() ? nullptr : digest.data(),
                                    static_cast<std::uint32_t>(digest.size()), &i);
        });
        return i;
    }
    /// Add one piece of evidence.
    void evidence(std::uint32_t index, bool positive)
    {
        cap_->call([&] { return zxv_lpres_evidence(cap_->cap, reg_.get(), index, positive ? 1 : 0); });
    }
    /// Index of a named attestation, or nullopt.
    [[nodiscard]] std::optional<std::uint32_t> find(const std::string& name) const
    {
        std::uint32_t i = 0;
        const std::int32_t st = zxv_lpres_find(reg_.get(), name.c_str(), &i);
        if (st == ZXV_E_NOT_FOUND) return std::nullopt;
        detail::check(st, cap_->ctx.get());
        return i;
    }
    /// State and evidence counts of one attestation.
    [[nodiscard]] AttestationState state(std::uint32_t index) const
    {
        std::int32_t s = 0;
        AttestationState a;
        cap_->call([&] { return zxv_lpres_state(reg_.get(), index, &s, &a.positive, &a.negative); });
        a.truth = truth_from_c(s);
        return a;
    }
    /// True if every attestation is TRUE or FALSE. Actuators need a clear gate.
    [[nodiscard]] bool gate() const
    {
        std::int32_t clear = 0;
        cap_->call([&] { return zxv_lpres_gate(reg_.get(), &clear); });
        return clear != 0;
    }

private:
    struct Del {
        void operator()(zxv_lpres* r) const noexcept { zxv_lpres_destroy(r); }
    };
    std::shared_ptr<detail::CapCore> cap_;
    std::unique_ptr<zxv_lpres, Del> reg_;
};

// ===========================================================================
// Hexagram (ABI 1.3)
// ===========================================================================

/// The four-valued base of a Hexagram (bits 5-4), numbered by line pattern.
/// NOT Truth's numbering (TRUE is 1 there and 3 here): convert only with
/// Hexagram::to_truth and Hexagram::from_truth.
enum class HexBase : std::uint8_t {
    false_ = ZXV_HEX_BASE_FALSE, neither = ZXV_HEX_BASE_NEITHER, both = ZXV_HEX_BASE_BOTH,
    true_ = ZXV_HEX_BASE_TRUE,
};
/// The polarity of a Hexagram's evidence (bits 3-2). forbidden is absorbing.
enum class HexPolarity : std::uint8_t {
    entropic = ZXV_HEX_POL_ENTROPIC,       ///< margin shrinking, or stale
    neutral = ZXV_HEX_POL_NEUTRAL,         ///< margin unchanged
    forbidden = ZXV_HEX_POL_FORBIDDEN,     ///< rolled back, quarantined, never replayed
    negentropic = ZXV_HEX_POL_NEGENTROPIC, ///< margin growing
};
/// The lifecycle phase of a Hexagram (bits 1-0). The cycle is
/// potential -> emerging -> manifest -> dissolving -> potential.
enum class HexPhase : std::uint8_t {
    potential = ZXV_HEX_PHASE_POTENTIAL, emerging = ZXV_HEX_PHASE_EMERGING,
    dissolving = ZXV_HEX_PHASE_DISSOLVING, manifest = ZXV_HEX_PHASE_MANIFEST,
};
/// What happens next to work with a given Hexagram.
enum class HexAction : std::uint8_t {
    proceed = ZXV_HEX_ACT_PROCEED, wait = ZXV_HEX_ACT_WAIT, refuse = ZXV_HEX_ACT_REFUSE,
    escalate = ZXV_HEX_ACT_ESCALATE, rollback = ZXV_HEX_ACT_ROLLBACK,
};

/// "false", "neither", "both" or "true".
constexpr std::string_view to_string(HexBase b) noexcept
{
    constexpr std::array<std::string_view, 4> n{"false", "neither", "both", "true"};
    return n[static_cast<std::size_t>(b) & 3u];
}
/// "entropic", "neutral", "forbidden" or "negentropic".
constexpr std::string_view to_string(HexPolarity p) noexcept
{
    constexpr std::array<std::string_view, 4> n{"entropic", "neutral", "forbidden", "negentropic"};
    return n[static_cast<std::size_t>(p) & 3u];
}
/// "potential", "emerging", "dissolving" or "manifest".
constexpr std::string_view to_string(HexPhase p) noexcept
{
    constexpr std::array<std::string_view, 4> n{"potential", "emerging", "dissolving", "manifest"};
    return n[static_cast<std::size_t>(p) & 3u];
}
/// "proceed", "wait", "refuse", "escalate" or "rollback" (zxv_hex_action_name).
inline std::string_view to_string(HexAction a) noexcept
{
    return zxv_hex_action_name(static_cast<std::int32_t>(a));
}

namespace detail {
template <class F>
std::uint8_t hex_out(F&& f)
{
    std::uint8_t o = 0;
    check(f(&o));
    return o;
}
} // namespace detail

/// The next phase on the cycle (zxv_hex_phase_next).
inline HexPhase next(HexPhase p)
{
    return static_cast<HexPhase>(
        detail::hex_out([&](std::uint8_t* o) { return zxv_hex_phase_next(static_cast<std::uint8_t>(p), o); }));
}

/// The 64-state hexagram tag of ABI 1.3 (the kernel's lpres_hex): an
/// immutable value tagging one unit of work with a four-valued HexBase, the
/// HexPolarity of its evidence and the HexPhase of its lifecycle, packed as
/// tag = base << 4 | polarity << 2 | phase (0..63). A default-constructed
/// Hexagram is tag 0 (false/entropic/potential).
///
/// Every rule is computed by libzxv. There is no conversion to bool and no
/// implicit one to Truth: use to_truth().
///
///     zxv::Hexagram h{zxv::HexBase::true_, zxv::HexPolarity::negentropic, zxv::HexPhase::potential};
///     h.action();                        // HexAction::proceed
///     h = h.advance();                   // emerging
///     h.step(zxv::HexPhase::dissolving); // throws RefusedError (ZXV_E_STATE): a jump
class Hexagram {
public:
    /// Number of tags (ZXV_HEX_STATES).
    static constexpr std::uint32_t states = ZXV_HEX_STATES;

    /// Tag 0.
    constexpr Hexagram() noexcept = default;

    /// The tag from its three fields (zxv_hex_pack).
    Hexagram(HexBase base, HexPolarity polarity, HexPhase phase)
        : tag_(detail::hex_out([&](std::uint8_t* o) {
              return zxv_hex_pack(static_cast<std::uint8_t>(base), static_cast<std::uint8_t>(polarity),
                                  static_cast<std::uint8_t>(phase), o);
          }))
    {
    }

    /// The tag with this byte value; UsageError (ZXV_E_ARG) above 63.
    static Hexagram from_tag(std::uint8_t tag)
    {
        detail::check(zxv_hex_unpack(tag, nullptr, nullptr, nullptr));
        return Hexagram(tag, 0);
    }

    /// All 64 tags, in tag order.
    static std::array<Hexagram, 64> all() noexcept
    {
        std::array<Hexagram, 64> a{};
        for (std::size_t i = 0; i < a.size(); ++i) a[i] = Hexagram(static_cast<std::uint8_t>(i), 0);
        return a;
    }

    /// A tag whose base is `t` (zxv_hex_from_lp).
    static Hexagram from_truth(Truth t, HexPolarity polarity, HexPhase phase)
    {
        const auto b = detail::hex_out([&](std::uint8_t* o) { return zxv_hex_from_lp(to_c(t), o); });
        return Hexagram(static_cast<HexBase>(b), polarity, phase);
    }

    /// Polarity from evidence counts before (pos0, neg0) and after (pos1,
    /// neg1): stale gives entropic; otherwise the margin |pos - neg|
    /// growing gives negentropic, shrinking entropic, unchanged neutral.
    /// Never forbidden.
    static HexPolarity polarity_from_evidence(std::uint32_t pos0, std::uint32_t neg0, std::uint32_t pos1,
                                              std::uint32_t neg1, bool stale)
    {
        return static_cast<HexPolarity>(detail::hex_out(
            [&](std::uint8_t* o) { return zxv_hex_polarity(pos0, neg0, pos1, neg1, stale ? 1 : 0, o); }));
    }

    /// The byte value, 0..63.
    [[nodiscard]] constexpr std::uint8_t tag() const noexcept { return tag_; }
    /// Bits 5-4.
    [[nodiscard]] constexpr HexBase base() const noexcept { return static_cast<HexBase>(tag_ >> 4); }
    /// Bits 3-2.
    [[nodiscard]] constexpr HexPolarity polarity() const noexcept
    {
        return static_cast<HexPolarity>((tag_ >> 2) & 3);
    }
    /// Bits 1-0.
    [[nodiscard]] constexpr HexPhase phase() const noexcept { return static_cast<HexPhase>(tag_ & 3); }
    /// True if the polarity is forbidden (absorbing).
    [[nodiscard]] constexpr bool is_forbidden() const noexcept { return polarity() == HexPolarity::forbidden; }

    /// What happens next: one lookup in the kernel's 64-entry table.
    [[nodiscard]] HexAction action() const
    {
        std::int32_t a = 0;
        detail::check(zxv_hex_action(tag_, &a));
        return static_cast<HexAction>(a);
    }

    /// "base/polarity/phase", e.g. "true/negentropic/manifest".
    [[nodiscard]] std::string_view name() const noexcept { return zxv_hex_name(tag_); }

    /// The base as a Truth: the explicit conversion (zxv_hex_to_lp).
    [[nodiscard]] Truth to_truth() const
    {
        std::int32_t t = 0;
        detail::check(zxv_hex_to_lp(static_cast<std::uint8_t>(tag_ >> 4), &t));
        return truth_from_c(t);
    }

    /// The same work in `phase`: the same phase or the next one only
    /// (RefusedError, ZXV_E_STATE, for a jump).
    [[nodiscard]] Hexagram step(HexPhase phase) const
    {
        return pure([&](std::uint8_t* o) { return zxv_hex_step(tag_, static_cast<std::uint8_t>(phase), o); });
    }

    /// One step along the cycle.
    [[nodiscard]] Hexagram advance() const { return step(next(phase())); }

    /// New base and polarity, same phase. RefusedError (ZXV_E_STATE) if
    /// this tag is forbidden and `polarity` is not (forbidden is absorbing).
    [[nodiscard]] Hexagram with_state(HexBase base, HexPolarity polarity) const
    {
        return pure([&](std::uint8_t* o) {
            return zxv_hex_set(tag_, static_cast<std::uint8_t>(base), static_cast<std::uint8_t>(polarity), o);
        });
    }

    /// The same tag with polarity forbidden.
    [[nodiscard]] Hexagram forbid() const
    {
        return pure([&](std::uint8_t* o) { return zxv_hex_forbid(tag_, o); });
    }

    /// Conjunction: four-valued AND on the base, worst polarity, least advanced phase.
    friend Hexagram operator&(Hexagram a, Hexagram b)
    {
        return pure([&](std::uint8_t* o) { return zxv_hex_and(a.tag_, b.tag_, o); });
    }
    /// Disjunction: four-valued OR on the base, worst polarity, least advanced phase.
    friend Hexagram operator|(Hexagram a, Hexagram b)
    {
        return pure([&](std::uint8_t* o) { return zxv_hex_or(a.tag_, b.tag_, o); });
    }
    /// Negation of the base; polarity and phase are kept.
    friend Hexagram operator~(Hexagram a)
    {
        return pure([&](std::uint8_t* o) { return zxv_hex_not(a.tag_, o); });
    }

    /// Same tag.
    friend constexpr bool operator==(Hexagram, Hexagram) noexcept = default;

private:
    constexpr Hexagram(std::uint8_t tag, int) noexcept : tag_(tag) {}
    template <class F>
    static Hexagram pure(F&& f)
    {
        return Hexagram(detail::hex_out(std::forward<F>(f)), 0);
    }
    std::uint8_t tag_ = 0;
};

/// Stream as its name.
inline std::ostream& operator<<(std::ostream& os, Hexagram h) { return os << h.name(); }

/// One record of a QuarantineLog.
struct QuarantineRecord {
    std::uint64_t seq = 0;            ///< its index
    Hexagram tag;                     ///< the forbidden tag
    std::uint8_t reason = 0;          ///< the caller's reason code
    std::uint32_t submitter = 0;      ///< who submitted the work
    std::array<std::uint8_t, 32> subject{}; ///< digest of the refused work
    std::array<std::uint8_t, 32> prev{};    ///< previous hash (zeros for the first)
    std::array<std::uint8_t, 32> hash{};    ///< this record's hash
};

/// The quarantine log of ABI 1.3 (zxv_hexlog): an append-only, SHA-256
/// hash-chained record of forbidden work, read for audit and never
/// replayed. Creating and appending need caps::attest. Thread-safe (the C
/// log has its own mutex).
///
/// Record k's hash is SHA-256("zxv/lpx-qlog/v1" || seq (u64 LE) || tag ||
/// reason || submitter (u32 LE) || subject || prev). A full log refuses the
/// append with ZXV_E_CAPACITY (RefusedError); the caller must then halt the
/// context (Context::halt), since forbidden work it cannot record must not
/// continue. Signing head() is the caller's job.
class QuarantineLog {
public:
    /// Largest capacity (ZXV_HEXLOG_MAX_RECORDS).
    static constexpr std::uint32_t max_records = ZXV_HEXLOG_MAX_RECORDS;

    /// A log of at most `capacity` (1..max_records) records (needs caps::attest).
    QuarantineLog(const Capability& cap, std::uint32_t capacity) : cap_(cap.core())
    {
        zxv_hexlog* l = nullptr;
        cap_->call([&] { return zxv_hexlog_create(cap_->cap, capacity, &l); });
        log_.reset(l);
    }

    /// Quarantine forbidden work (`tag` must be forbidden, else UsageError);
    /// returns the record's sequence number.
    std::uint64_t append(Hexagram tag, std::uint8_t reason, std::uint32_t submitter,
                         std::span<const std::uint8_t, 32> subject)
    {
        std::uint64_t seq = 0;
        cap_->call([&] {
            return zxv_hexlog_append(cap_->cap, log_.get(), tag.tag(), reason, submitter, subject.data(), &seq);
        });
        return seq;
    }

    /// Number of records.
    [[nodiscard]] std::uint32_t count() const
    {
        std::uint32_t n = 0;
        cap_->call([&] { return zxv_hexlog_count(log_.get(), &n); });
        return n;
    }

    /// The chain head: the last record's hash (zeros when empty).
    [[nodiscard]] std::array<std::uint8_t, 32> head() const
    {
        std::array<std::uint8_t, 32> h{};
        cap_->call([&] { return zxv_hexlog_head(log_.get(), h.data()); });
        return h;
    }

    /// Record `index`, oldest first.
    [[nodiscard]] QuarantineRecord record(std::uint32_t index) const
    {
        QuarantineRecord r;
        std::uint8_t tag = 0;
        cap_->call([&] {
            return zxv_hexlog_at(log_.get(), index, &r.seq, &tag, &r.reason, &r.submitter, r.subject.data(),
                                 r.prev.data(), r.hash.data());
        });
        r.tag = Hexagram::from_tag(tag);
        return r;
    }

    /// Every record, oldest first.
    [[nodiscard]] std::vector<QuarantineRecord> records() const
    {
        std::vector<QuarantineRecord> v;
        const std::uint32_t n = count();
        v.reserve(n);
        for (std::uint32_t i = 0; i < n; ++i) v.push_back(record(i));
        return v;
    }

    /// Recompute every link; a broken chain is ZXV_E_INVARIANT (FatalError:
    /// the context halts).
    void verify() const
    {
        cap_->call([&] { return zxv_hexlog_verify(log_.get()); });
    }

private:
    struct Del {
        void operator()(zxv_hexlog* l) const noexcept { zxv_hexlog_destroy(l); }
    };
    std::shared_ptr<detail::CapCore> cap_;
    std::unique_ptr<zxv_hexlog, Del> log_;
};

// ===========================================================================
// Budgets
// ===========================================================================

/// What a budget measures (rmag_resource_t).
enum class Resource : std::int32_t {
    compute_cycles = ZXV_RES_COMPUTE_CYCLES, memory = ZXV_RES_MEMORY, bandwidth = ZXV_RES_BANDWIDTH,
    energy = ZXV_RES_ENERGY, storage = ZXV_RES_STORAGE,
    tokens = ZXV_RES_TOKENS, ///< one model token = one event
};

/// Exact status of a budget.
struct BudgetStatus {
    Rational remaining;     ///< limit - used, clamped at 0
    Rational used;          ///< consumed so far
    std::uint64_t epoch = 0; ///< counts replenishments
};

namespace detail {
struct BudgetsCore {
    std::shared_ptr<CapCore> cap;
    zxv_rmag* rmag = nullptr;
    std::mutex m;
    std::condition_variable_any cv;
    std::uint64_t replenish_count = 0; // bumped by the listener
    bool closing = false;
    std::map<std::uint32_t, std::vector<std::function<void(std::uint32_t)>>> on_exhausted;

    static void listener(void* user, std::uint32_t budget, std::int32_t ev) noexcept
    {
        auto* self = static_cast<BudgetsCore*>(user);
        if (ev == ZXV_RMAG_EV_REPLENISHED) {
            {
                std::lock_guard lk(self->m);
                ++self->replenish_count;
            }
            self->cv.notify_all();
        } else if (ev == ZXV_RMAG_EV_DENIED) {
            std::vector<std::function<void(std::uint32_t)>> fns;
            {
                std::lock_guard lk(self->m);
                if (auto it = self->on_exhausted.find(budget); it != self->on_exhausted.end()) fns = it->second;
            }
            for (auto& f : fns) {
                try {
                    f(budget);
                } catch (...) { // never unwind through C
                }
            }
        }
    }
    BudgetsCore(std::shared_ptr<CapCore> c, zxv_rmag* r) : cap(std::move(c)), rmag(r)
    {
        zxv_rmag_listen(rmag, &listener, this);
    }
    BudgetsCore(const BudgetsCore&) = delete;
    BudgetsCore& operator=(const BudgetsCore&) = delete;
    ~BudgetsCore()
    {
        zxv_rmag_listen(rmag, nullptr, nullptr);
        zxv_rmag_destroy(rmag);
    }
    void close()
    {
        {
            std::lock_guard lk(m);
            closing = true;
        }
        cv.notify_all();
    }
};
} // namespace detail

class Budgets;

/// One budget with an exact rational limit. A cheap handle into its
/// Budgets registry (copyable); keeps the registry's memory alive.
class Budget {
public:
    /// Index of this budget in its registry (as the C ABI numbers it).
    [[nodiscard]] std::uint32_t id() const noexcept { return id_; }

    /// Consume `amount` (> 0). Throws BudgetExhausted if an enforced limit
    /// would be passed (nothing is consumed) and UsageError for a malformed
    /// amount (ZXV_E_RMAG_INVALID; never retry the same value).
    void consume(Rational amount) const
    {
        core_->cap->call([&] { return zxv_rmag_consume(core_->cap->cap, core_->rmag, id_, amount.num(), amount.den()); });
    }

    /// Consume, blocking until the budget is replenished if it is exhausted
    /// (a release or larger limit). Throws OperationCancelled when `stop` is
    /// requested or the Budgets owner is destroyed, BudgetExhausted when
    /// `timeout` elapses, and UsageError at once for a malformed amount.
    void consume_wait(Rational amount, zxv::stop_token stop = {},
                      std::optional<std::chrono::milliseconds> timeout = std::nullopt) const
    {
        const auto deadline = timeout ? std::optional(std::chrono::steady_clock::now() + *timeout) : std::nullopt;
        for (;;) {
            std::uint64_t seen;
            {
                std::lock_guard lk(core_->m);
                if (core_->closing) throw OperationCancelled("the Budgets owner was destroyed");
                seen = core_->replenish_count;
            }
            try {
                consume(amount);
                return;
            } catch (const BudgetExhausted&) {
                // fall through to wait for a replenishment
            }
            // A stop request notifies the cv under its mutex, so a waiter
            // cannot miss it between testing the predicate and blocking.
            // Registered before taking the lock (it may run at once) and
            // destroyed after releasing it (its destructor may wait for it).
            detail::BudgetsCore* c = core_.get();
            zxv::stop_callback wake_on_stop(stop, [c] {
                std::lock_guard g(c->m);
                c->cv.notify_all();
            });
            std::unique_lock lk(core_->m);
            auto pred = [&] { return stop.stop_requested() || core_->closing || core_->replenish_count != seen; };
            bool woke = true;
            if (deadline)
                woke = core_->cv.wait_until(lk, *deadline, pred);
            else
                core_->cv.wait(lk, pred);
            if (stop.stop_requested()) throw OperationCancelled("consume_async was cancelled");
            if (core_->closing) throw OperationCancelled("the Budgets owner was destroyed");
            if (!woke) throw BudgetExhausted(ZXV_E_BUDGET, "timed out waiting for the budget to be replenished");
        }
    }

    /// Asynchronous consume_wait on a worker thread. The future completes
    /// when the consume succeeds, or holds the exception consume_wait threw.
    [[nodiscard]] std::future<void> consume_async(Rational amount, zxv::stop_token stop = {},
                                                  std::optional<std::chrono::milliseconds> timeout = std::nullopt) const
    {
        std::promise<void> p;
        std::future<void> f = p.get_future();
        std::thread([self = *this, amount, stop, timeout, p = std::move(p)]() mutable {
            try {
                self.consume_wait(amount, stop, timeout);
                p.set_value();
            } catch (...) {
                p.set_exception(std::current_exception());
            }
        }).detach();
        return f;
    }

    /// Give back (clamped at zero used). Wakes waiters.
    void release(Rational amount) const
    {
        core_->cap->call([&] { return zxv_rmag_release(core_->cap->cap, core_->rmag, id_, amount.num(), amount.den()); });
    }

    /// Replace the limit (>= 0). Growing it wakes waiters.
    void set_limit(Rational limit) const
    {
        core_->cap->call([&] { return zxv_rmag_set_limit(core_->cap->cap, core_->rmag, id_, limit.num(), limit.den()); });
    }

    /// Remaining, used (exact) and the replenish epoch.
    [[nodiscard]] BudgetStatus status() const
    {
        std::int64_t rn = 0, rd = 1, un = 0, ud = 1;
        std::uint64_t ep = 0;
        core_->cap->call([&] { return zxv_rmag_status(core_->rmag, id_, &rn, &rd, &un, &ud, &ep); });
        return {Rational::of(rn, rd), Rational::of(un, ud), ep};
    }

    /// Register a callback run (on the denied caller's thread) each time a
    /// consume of this budget is refused, including by a bound Session.
    /// Exceptions it throws are discarded (they cannot cross the C ABI).
    void on_exhausted(std::function<void(std::uint32_t budget)> fn) const
    {
        std::lock_guard lk(core_->m);
        core_->on_exhausted[id_].push_back(std::move(fn));
    }

    /// Internal: the registry core.
    [[nodiscard]] const std::shared_ptr<detail::BudgetsCore>& core() const noexcept { return core_; }

private:
    friend class Budgets;
    Budget(std::shared_ptr<detail::BudgetsCore> c, std::uint32_t id) : core_(std::move(c)), id_(id) {}
    std::shared_ptr<detail::BudgetsCore> core_;
    std::uint32_t id_ = 0;
};

/// A registry of rational resource budgets (rmag, K2). Needs caps::budget.
/// Destroying it cancels pending consume_async waits (OperationCancelled).
class Budgets {
public:
    /// A registry in the capability's context (needs caps::budget).
    explicit Budgets(const Capability& cap)
    {
        zxv_rmag* r = nullptr;
        cap.core()->call([&] { return zxv_rmag_create_cap(cap.core()->cap, &r); });
        core_ = std::make_shared<detail::BudgetsCore>(cap.core(), r);
    }
    Budgets(Budgets&&) noexcept = default; ///< move
    Budgets& operator=(Budgets&& o) noexcept
    {
        if (this != &o) {
            if (core_) core_->close();
            core_ = std::move(o.core_);
        }
        return *this;
    }
    Budgets(const Budgets&) = delete;
    Budgets& operator=(const Budgets&) = delete;
    ~Budgets()
    {
        if (core_) core_->close();
    }

    /// Create a budget with an exact limit (>= 0). enforce=false only counts.
    [[nodiscard]] Budget create(const std::string& name, Resource resource, Rational limit, bool enforce = true)
    {
        std::uint32_t id = 0;
        core_->cap->call([&] {
            return zxv_rmag_budget(core_->cap->cap, core_->rmag, name.c_str(), static_cast<std::int32_t>(resource),
                                   limit.num(), limit.den(), enforce ? 1 : 0, &id);
        });
        return Budget(core_, id);
    }

private:
    std::shared_ptr<detail::BudgetsCore> core_;
};

// ===========================================================================
// PhaseClock
// ===========================================================================

/// Decision of the K6 coordinator.
enum class Decision : std::int32_t {
    admit = ZXV_ADMIT, ///< token issued
    veto = ZXV_VETO,   ///< rejected
    defer = ZXV_DEFER, ///< parked in S0
    retry = ZXV_RETRY, ///< try again
};

/// Step flags for PhaseClock::admit.
namespace step {
inline constexpr std::uint32_t requires_health = ZXV_STEP_REQUIRES_HEALTH; ///< phase must be healthy
inline constexpr std::uint32_t hardware = ZXV_STEP_HARDWARE;               ///< touches hardware
inline constexpr std::uint32_t s0_pending = ZXV_STEP_S0_PENDING;           ///< park in S0
} // namespace step

/// Health of a phase.
enum class Health : std::int32_t {
    unknown = ZXV_HEALTH_UNKNOWN, healthy = ZXV_HEALTH_HEALTHY,
    degraded = ZXV_HEALTH_DEGRADED, unhealthy = ZXV_HEALTH_UNHEALTHY,
};

/// State of a deferred S0 event.
enum class EventState : std::int32_t {
    pending = ZXV_EV_PENDING,     ///< waiting for its tick
    cleared = ZXV_EV_CLEARED,     ///< its tick was reached
    cancelled = ZXV_EV_CANCELLED, ///< cancelled
};

/// Result of an admission request.
struct Admission {
    Decision decision = Decision::veto; ///< what the coordinator decided
    std::uint64_t token = 0;            ///< non-zero only on admit
};

/// Result of advancing the clock.
struct Advance {
    std::uint64_t now = 0;      ///< tick after advancing
    std::uint32_t cleared = 0;  ///< events cleared by this advance
};

namespace detail {
struct PhaseCore {
    std::shared_ptr<CapCore> cap;
    zxv_phase* phase = nullptr;
    std::mutex m; // serializes C calls with waiter bookkeeping
    std::map<std::uint64_t, std::promise<void>> waiters;
    zxv::jthread driver;
    std::condition_variable_any driver_cv;

    PhaseCore(std::shared_ptr<CapCore> c, zxv_phase* p) : cap(std::move(c)), phase(p) {}
    PhaseCore(const PhaseCore&) = delete;
    PhaseCore& operator=(const PhaseCore&) = delete;
    ~PhaseCore()
    {
        stop_driver();
        zxv_phase_destroy(phase);
    }
    void stop_driver()
    {
        if (driver.joinable()) {
            driver.request_stop();
            driver_cv.notify_all();
            driver.join();
        }
    }
    // Complete waiters whose event is no longer pending. Caller holds m.
    void settle_locked()
    {
        for (auto it = waiters.begin(); it != waiters.end();) {
            std::int32_t st = ZXV_EV_PENDING;
            if (zxv_phase_event(phase, it->first, &st, nullptr) != ZXV_OK || st == ZXV_EV_PENDING) {
                ++it;
                continue;
            }
            if (st == ZXV_EV_CLEARED)
                it->second.set_value();
            else
                it->second.set_exception(std::make_exception_ptr(OperationCancelled("the event was cancelled")));
            it = waiters.erase(it);
        }
    }
    void fail_all_locked(std::exception_ptr e)
    {
        for (auto& [id, p] : waiters) p.set_exception(e);
        waiters.clear();
    }
    Advance advance(std::uint64_t delta)
    {
        std::lock_guard lk(m);
        Advance a;
        try {
            cap->call([&] { return zxv_phase_advance(cap->cap, phase, delta, &a.now, &a.cleared); });
        } catch (...) {
            if (cap->ctx->dead.load()) fail_all_locked(std::current_exception());
            throw;
        }
        settle_locked();
        return a;
    }
};
} // namespace detail

/// A deferred S0 event: completes when the clock passes its tick.
class DeferredEvent {
public:
    /// Event id (as the C ABI numbers it).
    [[nodiscard]] std::uint64_t id() const noexcept { return id_; }
    /// The absolute tick it is due at.
    [[nodiscard]] std::uint64_t due_tick() const noexcept { return due_; }
    /// Current state.
    [[nodiscard]] EventState state() const
    {
        std::int32_t st = 0;
        std::lock_guard lk(core_->m);
        core_->cap->call([&] { return zxv_phase_event(core_->phase, id_, &st, nullptr); });
        return static_cast<EventState>(st);
    }
    /// A future that completes when the event clears, or holds
    /// OperationCancelled if it is cancelled (or the clock is destroyed),
    /// or FatalError if the context halts.
    [[nodiscard]] std::shared_future<void> await_cleared() const { return fut_; }
    /// Cancel the event; its future fails with OperationCancelled.
    void cancel()
    {
        std::lock_guard lk(core_->m);
        core_->cap->call([&] { return zxv_phase_cancel(core_->cap->cap, core_->phase, id_); });
        core_->settle_locked();
    }

private:
    friend class PhaseClock;
    DeferredEvent(std::shared_ptr<detail::PhaseCore> c, std::uint64_t id, std::uint64_t due, std::shared_future<void> f)
        : core_(std::move(c)), id_(id), due_(due), fut_(std::move(f))
    {
    }
    std::shared_ptr<detail::PhaseCore> core_;
    std::uint64_t id_;
    std::uint64_t due_;
    std::shared_future<void> fut_;
};

/// The phase clock (K6). ZXV time is a logical tick count; one fundamental
/// is zxv::phase_cycle ticks. Drive it manually with advance() (tests,
/// simulations, game lockstep) or in real time with start_realtime().
/// Needs caps::phase. Destroying it stops the driver and cancels waiters.
class PhaseClock {
public:
    /// A clock in the capability's context, at tick 0 (needs caps::phase).
    explicit PhaseClock(const Capability& cap)
    {
        zxv_phase* p = nullptr;
        cap.core()->call([&] { return zxv_phase_create_cap(cap.core()->cap, &p); });
        core_ = std::make_shared<detail::PhaseCore>(cap.core(), p);
    }
    PhaseClock(PhaseClock&&) noexcept = default; ///< move
    PhaseClock& operator=(PhaseClock&& o) noexcept
    {
        if (this != &o) {
            shutdown();
            core_ = std::move(o.core_);
        }
        return *this;
    }
    PhaseClock(const PhaseClock&) = delete;
    PhaseClock& operator=(const PhaseClock&) = delete;
    ~PhaseClock() { shutdown(); }

    /// The current tick.
    [[nodiscard]] std::uint64_t now() const
    {
        std::uint64_t t = 0;
        std::lock_guard lk(core_->m);
        core_->cap->call([&] { return zxv_phase_now(core_->phase, &t); });
        return t;
    }

    /// Advance by `delta` ticks (manual driver). Due events clear and their
    /// futures complete before this returns.
    Advance advance(std::uint64_t delta) { return core_->advance(delta); }

    /// Ask the coordinator to admit a step of phase `phase_id` (1..13).
    [[nodiscard]] Admission admit(std::uint32_t phase_id, std::uint32_t step_id, std::uint32_t flags = 0)
    {
        Admission a;
        std::int32_t d = 0;
        std::lock_guard lk(core_->m);
        core_->cap->call([&] { return zxv_phase_admit(core_->cap->cap, core_->phase, phase_id, step_id, flags, &d, &a.token); });
        a.decision = static_cast<Decision>(d);
        return a;
    }

    /// Report the health of a phase.
    void set_health(std::uint32_t phase_id, Health h)
    {
        std::lock_guard lk(core_->m);
        core_->cap->call([&] { return zxv_phase_set_health(core_->cap->cap, core_->phase, phase_id, static_cast<std::int32_t>(h)); });
    }

    /// Park an S0 event until absolute tick `due_tick` (> now).
    [[nodiscard]] DeferredEvent defer(std::uint64_t due_tick)
    {
        std::uint64_t ev = 0;
        std::lock_guard lk(core_->m);
        core_->cap->call([&] { return zxv_phase_defer(core_->cap->cap, core_->phase, due_tick, &ev); });
        auto [it, ok] = core_->waiters.try_emplace(ev);
        (void) ok;
        return DeferredEvent(core_, ev, due_tick, it->second.get_future().share());
    }

    /// A future completing at absolute tick `tick` (defer + await_cleared).
    [[nodiscard]] std::shared_future<void> until_tick(std::uint64_t tick) { return defer(tick).await_cleared(); }

    /// Earliest due tick of a pending event, or nullopt.
    [[nodiscard]] std::optional<std::uint64_t> next_due() const
    {
        std::uint64_t t = 0;
        std::lock_guard lk(core_->m);
        const std::int32_t st = zxv_phase_next_due(core_->phase, &t);
        if (st == ZXV_E_NOT_FOUND) return std::nullopt;
        detail::check(st, core_->cap->ctx.get());
        return t;
    }

    /// Start the real-time driver: a zxv::jthread advancing the clock at
    /// `ticks_per_second` (it batches ticks, waking at most every 1 ms).
    /// Replaces a running driver. A FATAL status stops it and fails waiters.
    void start_realtime(std::uint64_t ticks_per_second)
    {
        if (ticks_per_second == 0) throw UsageError(ZXV_E_ARG, "ticks_per_second must be > 0");
        core_->stop_driver();
        detail::PhaseCore* c = core_.get(); // the core joins this thread before dying
        core_->driver = zxv::jthread([c, ticks_per_second](zxv::stop_token st) {
            using clock = std::chrono::steady_clock;
            const auto start = clock::now();
            std::uint64_t issued = 0;
            std::mutex sleep_m;
            // Wake the sleep below on a stop request (notified under sleep_m,
            // so it cannot be lost); destroyed before sleep_m.
            zxv::stop_callback wake_on_stop(st, [&sleep_m, c] {
                std::lock_guard g(sleep_m);
                c->driver_cv.notify_all();
            });
            while (!st.stop_requested()) {
                constexpr std::uint64_t ns_per_s = 1000000000u;
                const auto elapsed = static_cast<std::uint64_t>(
                    std::chrono::duration_cast<std::chrono::nanoseconds>(clock::now() - start).count());
                // ticks = elapsed * tps / 1e9, split so it cannot overflow
                const std::uint64_t target =
                    (elapsed / ns_per_s) * ticks_per_second + (elapsed % ns_per_s) * ticks_per_second / ns_per_s;
                if (target > issued) {
                    try {
                        c->advance(target - issued);
                    } catch (...) {
                        std::lock_guard lk(c->m);
                        c->fail_all_locked(std::current_exception());
                        return;
                    }
                    issued = target;
                }
                const std::uint64_t nt = issued + 1; // when is tick nt due, in ns?
                const auto next_ns = static_cast<std::int64_t>(
                    (nt / ticks_per_second) * ns_per_s +
                    ((nt % ticks_per_second) * ns_per_s + ticks_per_second - 1) / ticks_per_second);
                auto wake = start + std::chrono::nanoseconds(next_ns);
                wake = std::max(wake, clock::now() + std::chrono::milliseconds(1));
                std::unique_lock lk(sleep_m);
                c->driver_cv.wait_until(lk, wake, [&st] { return st.stop_requested(); });
            }
        });
    }

    /// Stop the real-time driver (no-op if none). Pending events stay pending.
    void stop_realtime() { core_->stop_driver(); }

private:
    void shutdown() noexcept
    {
        if (!core_) return;
        core_->stop_driver();
        std::lock_guard lk(core_->m);
        core_->fail_all_locked(std::make_exception_ptr(OperationCancelled("the PhaseClock was destroyed")));
    }
    std::shared_ptr<detail::PhaseCore> core_;
};

// ===========================================================================
// UBH-168 wire frames (pure host code, cross-checked against C)
// ===========================================================================

/// Resolved truth of a frame (the W5 correlation resolver).
enum class WireTruth : std::int32_t {
    unknown = ZXV_TRUTH_UNKNOWN, true_ = ZXV_TRUTH_TRUE, false_ = ZXV_TRUTH_FALSE,
    neutral = ZXV_TRUTH_NEUTRAL, glut = ZXV_TRUTH_GLUT, paradox = ZXV_TRUTH_PARADOX,
};

/// One 21-octet UBH-168 frame: a tag, then five 32-bit words in the fixed
/// L-B-L-B-L byte order (W0 LE S+, W1 BE S-, W2 LE S+, W3 BE S-, W4 LE S+).
/// Packing and unpacking are pure constexpr C++ (no library needed), and
/// identical on every host.
struct Ubh168Frame {
    /// Frame size in octets.
    static constexpr std::size_t size = ZXV_WIRE_FRAME_BYTES;
    /// The raw octets.
    std::array<std::uint8_t, size> bytes{};

    /// tag = 100*sync + 10*shell + trunk (trunk, shell 0..9; sync 0..1).
    static constexpr std::uint8_t make_tag(std::uint32_t trunk, std::uint32_t shell, bool sync)
    {
        if (trunk > 9 || shell > 9) throw UsageError(ZXV_E_ARG, "trunk and shell must be 0..9");
        return static_cast<std::uint8_t>(100u * (sync ? 1u : 0u) + 10u * shell + trunk);
    }

    /// Pack a tag, three S+ words and two S- words.
    static constexpr Ubh168Frame pack(std::uint8_t tag, std::array<std::uint32_t, 3> s_plus,
                                      std::array<std::uint32_t, 2> s_minus) noexcept
    {
        Ubh168Frame f;
        f.bytes[0] = tag;
        const std::array<std::uint32_t, 5> w{s_plus[0], s_minus[0], s_plus[1], s_minus[1], s_plus[2]};
        for (std::size_t k = 0; k < 5; ++k)
            for (std::size_t b = 0; b < 4; ++b) {
                const std::size_t shift = (k % 2 == 0) ? 8 * b : 8 * (3 - b); // LE : BE
                f.bytes[1 + 4 * k + b] = static_cast<std::uint8_t>(w[k] >> shift);
            }
        return f;
    }

    /// Frame from 21 raw octets.
    static Ubh168Frame from_bytes(std::span<const std::uint8_t> octets)
    {
        if (octets.size() != size) throw UsageError(ZXV_E_FORMAT, "a UBH-168 frame is 21 octets");
        Ubh168Frame f;
        std::copy(octets.begin(), octets.end(), f.bytes.begin());
        return f;
    }

    /// The tag octet.
    [[nodiscard]] constexpr std::uint8_t tag() const noexcept { return bytes[0]; }
    /// The three S+ words (W0, W2, W4).
    [[nodiscard]] constexpr std::array<std::uint32_t, 3> s_plus() const noexcept { return {word(0), word(2), word(4)}; }
    /// The two S- words (W1, W3).
    [[nodiscard]] constexpr std::array<std::uint32_t, 2> s_minus() const noexcept { return {word(1), word(3)}; }

    /// Resolve the frame's truth (calls the C resolver).
    [[nodiscard]] WireTruth resolve() const
    {
        std::int32_t t = 0;
        detail::check(zxv_wire_resolve(bytes.data(), &t));
        return static_cast<WireTruth>(t);
    }

    /// The idle pilot frame of a trunk (never all zeros), from C.
    static Ubh168Frame idle(std::uint32_t trunk)
    {
        Ubh168Frame f;
        detail::check(zxv_wire_idle(trunk, f.bytes.data()));
        return f;
    }

    /// The same pack done by the C library (for cross-checks).
    static Ubh168Frame pack_native(std::uint8_t tag, std::array<std::uint32_t, 3> s_plus,
                                   std::array<std::uint32_t, 2> s_minus)
    {
        Ubh168Frame f;
        detail::check(zxv_wire_pack(tag, s_plus.data(), s_minus.data(), f.bytes.data()));
        return f;
    }

    /// Octet-wise equality.
    friend constexpr bool operator==(const Ubh168Frame&, const Ubh168Frame&) noexcept = default;

private:
    [[nodiscard]] constexpr std::uint32_t word(std::size_t k) const noexcept
    {
        std::uint32_t v = 0;
        for (std::size_t b = 0; b < 4; ++b) {
            const std::size_t shift = (k % 2 == 0) ? 8 * b : 8 * (3 - b);
            v |= static_cast<std::uint32_t>(bytes[1 + 4 * k + b]) << shift;
        }
        return v;
    }
};

// ===========================================================================
// Swarm token economy
// ===========================================================================

/// One model's allotment in the current cycle.
struct Allotment {
    std::uint64_t allotted = 0;  ///< real + imaginary tokens
    std::uint64_t imaginary = 0; ///< the emotional share
    std::uint64_t used = 0;      ///< consumed this cycle
};

/// Result of Swarm::consume.
struct SwarmGrant {
    std::uint64_t granted = 0; ///< tokens granted
    bool exhausted = false;    ///< true if fewer than requested were left
};

/// Behaviour profile of an emotion of the palette.
struct EmotionProfile {
    std::int32_t valence = 0;            ///< negative .. positive
    std::uint32_t temperature_milli = 0; ///< sampling temperature x1000
    std::uint32_t explore_permille = 0;  ///< exploration rate
    std::uint32_t verify_passes = 0;     ///< verification passes
    std::uint32_t priority = 0;          ///< scheduling priority
};

/// The Fibonacci + emotional token economy. Each cycle `tokens_per_cycle`
/// tokens are split across Fibonacci levels; the mood's charge funds an
/// imaginary share split by each model's feeling. Needs caps::swarm.
class Swarm {
public:
    /// A swarm of `num_levels` levels issuing `tokens_per_cycle` per cycle (needs
    /// caps::swarm).
    Swarm(const Capability& cap, std::uint32_t num_levels, std::uint64_t tokens_per_cycle) : cap_(cap.core())
    {
        zxv_swarm* s = nullptr;
        cap_->call([&] { return zxv_swarm_create_cap(cap_->cap, num_levels, tokens_per_cycle, &s); });
        sw_.reset(s);
    }
    /// Register a model at a level (level d holds F(d+2) models).
    void register_model(std::uint32_t model_id, std::uint32_t level)
    {
        cap_->call([&] { return zxv_swarm_register(cap_->cap, sw_.get(), model_id, level); });
    }
    /// Activate or deactivate a model.
    void set_active(std::uint32_t model_id, bool active)
    {
        cap_->call([&] { return zxv_swarm_set_active(cap_->cap, sw_.get(), model_id, active ? 1 : 0); });
    }
    /// Change the tokens issued per cycle.
    void set_rate(std::uint64_t tokens_per_cycle)
    {
        cap_->call([&] { return zxv_swarm_set_rate(cap_->cap, sw_.get(), tokens_per_cycle); });
    }
    /// Set the swarm mood: an emoji code point of the palette, intensity 0..5.
    void set_mood(char32_t codepoint, std::uint32_t intensity)
    {
        cap_->call([&] { return zxv_swarm_set_mood(cap_->cap, sw_.get(), static_cast<std::uint32_t>(codepoint), intensity); });
    }
    /// Set one model's feeling.
    void set_feeling(std::uint32_t model_id, char32_t codepoint, std::uint32_t intensity)
    {
        cap_->call([&] {
            return zxv_swarm_set_feeling(cap_->cap, sw_.get(), model_id, static_cast<std::uint32_t>(codepoint), intensity);
        });
    }
    /// Start a cycle; returns its number.
    std::uint64_t begin_cycle()
    {
        std::uint64_t c = 0;
        cap_->call([&] { return zxv_swarm_begin_cycle(cap_->cap, sw_.get(), &c); });
        return c;
    }
    /// Grant up to `requested` tokens. A partial grant is not an exception:
    /// it returns what was left with exhausted = true.
    SwarmGrant consume(std::uint32_t model_id, std::uint64_t requested)
    {
        SwarmGrant g;
        detail::ensure_alive(cap_->ctx.get());
        const std::int32_t st = zxv_swarm_consume(cap_->cap, sw_.get(), model_id, requested, &g.granted);
        if (st == ZXV_E_BUDGET) {
            g.exhausted = true;
            return g;
        }
        detail::check(st, cap_->ctx.get());
        return g;
    }
    /// A model's allotment this cycle.
    [[nodiscard]] Allotment allotment(std::uint32_t model_id) const
    {
        Allotment a;
        cap_->call([&] { return zxv_swarm_allotment(sw_.get(), model_id, &a.allotted, &a.imaginary, &a.used); });
        return a;
    }
    /// Close the cycle; returns the unused tokens (they never carry over).
    std::uint64_t end_cycle()
    {
        std::uint64_t e = 0;
        cap_->call([&] { return zxv_swarm_end_cycle(cap_->cap, sw_.get(), &e); });
        return e;
    }
    /// Capacity of level d: F(d+2).
    static std::uint32_t level_capacity(std::uint32_t level)
    {
        std::uint32_t c = 0;
        detail::check(zxv_swarm_level_capacity(level, &c));
        return c;
    }
    /// The behaviour profile of an emotion of the palette.
    static EmotionProfile emotion_profile(char32_t codepoint)
    {
        EmotionProfile p;
        detail::check(zxv_swarm_emotion_profile(static_cast<std::uint32_t>(codepoint), &p.valence, &p.temperature_milli,
                                                &p.explore_permille, &p.verify_passes, &p.priority));
        return p;
    }

private:
    struct Del {
        void operator()(zxv_swarm* s) const noexcept { zxv_swarm_destroy(s); }
    };
    std::shared_ptr<detail::CapCore> cap_;
    std::unique_ptr<zxv_swarm, Del> sw_;
};

// ===========================================================================
// Hashes, randomness, secret keys
// ===========================================================================

/// A 32-byte digest.
using Digest32 = std::array<std::uint8_t, 32>;

/// SHA3-256, SHAKE256 and Ethereum Keccak-256 (pure functions).
struct Hashes {
    /// SHA3-256 of bytes.
    static Digest32 sha3_256(std::span<const std::uint8_t> data)
    {
        Digest32 d{};
        detail::check(zxv_sha3_256(data.data(), data.size(), d.data()));
        return d;
    }
    /// SHA3-256 of text bytes.
    static Digest32 sha3_256(std::string_view s) { return sha3_256({detail::bytes_of(s), s.size()}); }
    /// Ethereum's Keccak-256 (pre-FIPS padding).
    static Digest32 keccak256(std::span<const std::uint8_t> data)
    {
        Digest32 d{};
        detail::check(zxv_keccak256(data.data(), data.size(), d.data()));
        return d;
    }
    /// Keccak-256 of text bytes.
    static Digest32 keccak256(std::string_view s) { return keccak256({detail::bytes_of(s), s.size()}); }
    /// SHAKE256 with `out_len` output bytes.
    static std::vector<std::uint8_t> shake256(std::span<const std::uint8_t> data, std::size_t out_len)
    {
        std::vector<std::uint8_t> out(out_len);
        detail::check(zxv_shake256(data.data(), data.size(), out.data(), out.size()));
        return out;
    }
    /// SHAKE256 of text bytes.
    static std::vector<std::uint8_t> shake256(std::string_view s, std::size_t out_len)
    {
        return shake256({detail::bytes_of(s), s.size()}, out_len);
    }
};

/// Fill `out` from the OS CSPRNG.
inline void random_bytes(std::span<std::uint8_t> out) { detail::check(zxv_random(out.data(), out.size())); }

/// Lower-case hex of bytes (for logs and tests).
inline std::string to_hex(std::span<const std::uint8_t> b)
{
    static constexpr char d[] = "0123456789abcdef";
    std::string s;
    s.reserve(b.size() * 2);
    for (std::uint8_t x : b) {
        s.push_back(d[x >> 4]);
        s.push_back(d[x & 15]);
    }
    return s;
}

/// Bytes from hex text (two digits per byte). Malformed text is ZXV_E_FORMAT.
inline std::vector<std::uint8_t> from_hex(std::string_view h)
{
    auto nib = [](char c) -> int {
        if (c >= '0' && c <= '9') return c - '0';
        if (c >= 'a' && c <= 'f') return c - 'a' + 10;
        if (c >= 'A' && c <= 'F') return c - 'A' + 10;
        return -1;
    };
    if (h.size() % 2) throw UsageError(ZXV_E_FORMAT, "odd-length hex");
    std::vector<std::uint8_t> out(h.size() / 2);
    for (std::size_t i = 0; i < out.size(); ++i) {
        const int a = nib(h[2 * i]), b = nib(h[2 * i + 1]);
        if (a < 0 || b < 0) throw UsageError(ZXV_E_FORMAT, "not hex");
        out[i] = static_cast<std::uint8_t>(a * 16 + b);
    }
    return out;
}

/// Secret key material (ML-KEM decapsulation keys, ML-DSA and Ed25519
/// secret keys, shared secrets). Move-only; its buffer is wiped with
/// zxv_secure_zero on destruction and on move-assignment. bytes() is a
/// view, not a copy; export_bytes_unsafe() makes a copy that is NOT wiped.
class SecretKey {
public:
    /// An all-zero key of `size` bytes (to be filled by the library).
    explicit SecretKey(std::size_t size) : data_(std::make_unique<std::uint8_t[]>(size)), size_(size) {}
    /// A key holding a copy of `bytes` (the caller should wipe its source).
    static SecretKey from_bytes(std::span<const std::uint8_t> bytes)
    {
        SecretKey k(bytes.size());
        std::copy(bytes.begin(), bytes.end(), k.data_.get());
        return k;
    }
    SecretKey(SecretKey&& o) noexcept : data_(std::move(o.data_)), size_(std::exchange(o.size_, 0)) {} ///< move
    SecretKey& operator=(SecretKey&& o) noexcept ///< move-assign (wipes the old key)
    {
        if (this != &o) {
            wipe();
            data_ = std::move(o.data_);
            size_ = std::exchange(o.size_, 0);
        }
        return *this;
    }
    SecretKey(const SecretKey&) = delete;
    SecretKey& operator=(const SecretKey&) = delete;
    ~SecretKey() { wipe(); }

    /// A read-only view of the key (valid while this object lives).
    [[nodiscard]] std::span<const std::uint8_t> bytes() const noexcept { return {data_.get(), size_}; }
    /// A writable view (for filling from C).
    [[nodiscard]] std::uint8_t* data() noexcept { return data_.get(); }
    /// Size in bytes.
    [[nodiscard]] std::size_t size() const noexcept { return size_; }
    /// A heap copy of the key. DEFEATS WIPING: the copy is yours to erase.
    [[nodiscard]] std::vector<std::uint8_t> export_bytes_unsafe() const { return {data_.get(), data_.get() + size_}; }
    /// Wipe now (the key becomes all zeros).
    void wipe() noexcept
    {
        if (data_) zxv_secure_zero(data_.get(), size_);
    }

private:
    std::unique_ptr<std::uint8_t[]> data_;
    std::size_t size_;
};

// ===========================================================================
// Post-quantum and classical crypto
// ===========================================================================

namespace detail {
inline void need_size(std::span<const std::uint8_t> b, std::size_t n, const char* what)
{
    if (b.size() != n)
        throw UsageError(ZXV_E_ARG, std::string(what) + " must be " + std::to_string(n) + " bytes, got " +
                                        std::to_string(b.size()));
}
} // namespace detail

/// An ML-KEM-1024 key pair.
struct KemKeyPair {
    std::vector<std::uint8_t> encapsulation_key; ///< public, 1568 bytes
    SecretKey decapsulation_key;                 ///< secret, 3168 bytes
};

/// The result of encapsulation.
struct Encapsulation {
    std::vector<std::uint8_t> ciphertext; ///< 1568 bytes, send to the key owner
    SecretKey shared_secret;              ///< 32 bytes
};

/// FIPS 203 ML-KEM-1024 (needs caps::crypto_kem). Not FIPS 140-3 validated.
class MlKem1024 {
public:
    static constexpr std::size_t ek_bytes = ZXV_MLKEM1024_EK_BYTES; ///< encapsulation key size
    static constexpr std::size_t dk_bytes = ZXV_MLKEM1024_DK_BYTES; ///< decapsulation key size
    static constexpr std::size_t ct_bytes = ZXV_MLKEM1024_CT_BYTES; ///< ciphertext size
    static constexpr std::size_t ss_bytes = ZXV_MLKEM_SS_BYTES;     ///< shared secret size

    /// Acting with `cap`.
    explicit MlKem1024(const Capability& cap) : cap_(cap.core()) {}
    /// Fresh key pair.
    [[nodiscard]] KemKeyPair keygen() const
    {
        KemKeyPair kp{std::vector<std::uint8_t>(ek_bytes), SecretKey(dk_bytes)};
        cap_->call([&] { return zxv_mlkem1024_keygen(cap_->cap, kp.encapsulation_key.data(), kp.decapsulation_key.data()); });
        return kp;
    }
    /// Deterministic key pair from d and z (32 bytes each), for KATs.
    [[nodiscard]] KemKeyPair keygen_derand(std::span<const std::uint8_t> d, std::span<const std::uint8_t> z) const
    {
        detail::need_size(d, 32, "d");
        detail::need_size(z, 32, "z");
        KemKeyPair kp{std::vector<std::uint8_t>(ek_bytes), SecretKey(dk_bytes)};
        cap_->call([&] {
            return zxv_mlkem1024_keygen_derand(cap_->cap, d.data(), z.data(), kp.encapsulation_key.data(),
                                               kp.decapsulation_key.data());
        });
        return kp;
    }
    /// Encapsulate to `ek`. A malformed key throws UsageError (ZXV_E_FORMAT).
    [[nodiscard]] Encapsulation encapsulate(std::span<const std::uint8_t> ek) const
    {
        detail::need_size(ek, ek_bytes, "encapsulation key");
        Encapsulation e{std::vector<std::uint8_t>(ct_bytes), SecretKey(ss_bytes)};
        cap_->call([&] { return zxv_mlkem1024_encaps(cap_->cap, ek.data(), e.ciphertext.data(), e.shared_secret.data()); });
        return e;
    }
    /// Deterministic encapsulation with message m (32 bytes), for KATs.
    [[nodiscard]] Encapsulation encapsulate_derand(std::span<const std::uint8_t> ek, std::span<const std::uint8_t> m) const
    {
        detail::need_size(ek, ek_bytes, "encapsulation key");
        detail::need_size(m, 32, "m");
        Encapsulation e{std::vector<std::uint8_t>(ct_bytes), SecretKey(ss_bytes)};
        cap_->call([&] {
            return zxv_mlkem1024_encaps_derand(cap_->cap, ek.data(), m.data(), e.ciphertext.data(), e.shared_secret.data());
        });
        return e;
    }
    /// Recover the shared secret from a ciphertext.
    [[nodiscard]] SecretKey decapsulate(const SecretKey& dk, std::span<const std::uint8_t> ct) const
    {
        detail::need_size(dk.bytes(), dk_bytes, "decapsulation key");
        detail::need_size(ct, ct_bytes, "ciphertext");
        SecretKey ss(ss_bytes);
        cap_->call([&] { return zxv_mlkem1024_decaps(cap_->cap, dk.bytes().data(), ct.data(), ss.data()); });
        return ss;
    }

private:
    std::shared_ptr<detail::CapCore> cap_;
};

/// A signature key pair.
struct SigningKeyPair {
    std::vector<std::uint8_t> public_key; ///< share freely
    SecretKey secret_key;                 ///< wiped on destruction
};

/// FIPS 204 ML-DSA-87 (needs caps::crypto_sign). Not FIPS 140-3 validated.
class MlDsa87 {
public:
    static constexpr std::size_t pk_bytes = ZXV_MLDSA87_PK_BYTES;   ///< public key size
    static constexpr std::size_t sk_bytes = ZXV_MLDSA87_SK_BYTES;   ///< secret key size
    static constexpr std::size_t sig_bytes = ZXV_MLDSA87_SIG_BYTES; ///< signature size

    /// Acting with `cap`.
    explicit MlDsa87(const Capability& cap) : cap_(cap.core()) {}
    /// Fresh key pair.
    [[nodiscard]] SigningKeyPair keygen() const
    {
        SigningKeyPair kp{std::vector<std::uint8_t>(pk_bytes), SecretKey(sk_bytes)};
        cap_->call([&] { return zxv_mldsa87_keygen(cap_->cap, kp.public_key.data(), kp.secret_key.data()); });
        return kp;
    }
    /// Key pair from a 32-byte seed.
    [[nodiscard]] SigningKeyPair keygen_seed(std::span<const std::uint8_t> seed) const
    {
        detail::need_size(seed, 32, "seed");
        SigningKeyPair kp{std::vector<std::uint8_t>(pk_bytes), SecretKey(sk_bytes)};
        cap_->call([&] { return zxv_mldsa87_keygen_seed(cap_->cap, seed.data(), kp.public_key.data(), kp.secret_key.data()); });
        return kp;
    }
    /// Hedged signature (fresh randomness). `context` <= 255 bytes.
    [[nodiscard]] std::vector<std::uint8_t> sign(const SecretKey& sk, std::span<const std::uint8_t> msg,
                                                 std::span<const std::uint8_t> context = {}) const
    {
        return do_sign(&zxv_mldsa87_sign, sk, msg, context);
    }
    /// Deterministic signature (same input, same signature), for KATs.
    [[nodiscard]] std::vector<std::uint8_t> sign_deterministic(const SecretKey& sk, std::span<const std::uint8_t> msg,
                                                               std::span<const std::uint8_t> context = {}) const
    {
        return do_sign(&zxv_mldsa87_sign_deterministic, sk, msg, context);
    }
    /// True if `sig` is valid; false if it does not verify.
    [[nodiscard]] bool verify(std::span<const std::uint8_t> pk, std::span<const std::uint8_t> msg,
                              std::span<const std::uint8_t> sig, std::span<const std::uint8_t> context = {}) const
    {
        detail::need_size(pk, pk_bytes, "public key");
        detail::need_size(sig, sig_bytes, "signature");
        detail::ensure_alive(cap_->ctx.get());
        const std::int32_t st = zxv_mldsa87_verify(cap_->cap, pk.data(), msg.data(), msg.size(),
                                                   context.empty() ? nullptr : context.data(), context.size(), sig.data());
        if (st == ZXV_E_VERIFY) return false;
        detail::check(st, cap_->ctx.get());
        return true;
    }

private:
    using sign_fn = std::int32_t (*)(const zxv_cap*, const std::uint8_t*, const std::uint8_t*, std::size_t,
                                     const std::uint8_t*, std::size_t, std::uint8_t*);
    std::vector<std::uint8_t> do_sign(sign_fn f, const SecretKey& sk, std::span<const std::uint8_t> msg,
                                      std::span<const std::uint8_t> context) const
    {
        detail::need_size(sk.bytes(), sk_bytes, "secret key");
        std::vector<std::uint8_t> sig(sig_bytes);
        cap_->call([&] {
            return f(cap_->cap, sk.bytes().data(), msg.data(), msg.size(), context.empty() ? nullptr : context.data(),
                     context.size(), sig.data());
        });
        return sig;
    }
    std::shared_ptr<detail::CapCore> cap_;
};

/// RFC 8032 Ed25519 (needs caps::crypto_sign).
class Ed25519 {
public:
    static constexpr std::size_t pk_bytes = ZXV_ED25519_PK_BYTES;   ///< public key size
    static constexpr std::size_t sk_bytes = ZXV_ED25519_SK_BYTES;   ///< secret key (seed) size
    static constexpr std::size_t sig_bytes = ZXV_ED25519_SIG_BYTES; ///< signature size

    /// Acting with `cap`.
    explicit Ed25519(const Capability& cap) : cap_(cap.core()) {}
    /// Fresh key pair.
    [[nodiscard]] SigningKeyPair keygen() const
    {
        SigningKeyPair kp{std::vector<std::uint8_t>(pk_bytes), SecretKey(sk_bytes)};
        cap_->call([&] { return zxv_ed25519_keygen(cap_->cap, kp.public_key.data(), kp.secret_key.data()); });
        return kp;
    }
    /// The public key of a secret key.
    [[nodiscard]] std::vector<std::uint8_t> public_key(const SecretKey& sk) const
    {
        detail::need_size(sk.bytes(), sk_bytes, "secret key");
        std::vector<std::uint8_t> pk(pk_bytes);
        cap_->call([&] { return zxv_ed25519_public(cap_->cap, sk.bytes().data(), pk.data()); });
        return pk;
    }
    /// Sign a message.
    [[nodiscard]] std::vector<std::uint8_t> sign(const SecretKey& sk, std::span<const std::uint8_t> msg) const
    {
        detail::need_size(sk.bytes(), sk_bytes, "secret key");
        std::vector<std::uint8_t> sig(sig_bytes);
        cap_->call([&] { return zxv_ed25519_sign(cap_->cap, sk.bytes().data(), msg.data(), msg.size(), sig.data()); });
        return sig;
    }
    /// True if `sig` is valid; false if it does not verify.
    [[nodiscard]] bool verify(std::span<const std::uint8_t> pk, std::span<const std::uint8_t> msg,
                              std::span<const std::uint8_t> sig) const
    {
        detail::need_size(pk, pk_bytes, "public key");
        detail::need_size(sig, sig_bytes, "signature");
        detail::ensure_alive(cap_->ctx.get());
        const std::int32_t st = zxv_ed25519_verify(cap_->cap, pk.data(), msg.data(), msg.size(), sig.data());
        if (st == ZXV_E_VERIFY) return false;
        detail::check(st, cap_->ctx.get());
        return true;
    }

private:
    std::shared_ptr<detail::CapCore> cap_;
};

/// View text as bytes (for signing and hashing APIs).
inline std::span<const std::uint8_t> as_bytes(std::string_view s) noexcept { return {detail::bytes_of(s), s.size()}; }

// ===========================================================================
// Engine: Model, Session, generation
// ===========================================================================

/// Model metadata keys.
enum class ModelInfo : std::int32_t {
    arch = ZXV_MODEL_ARCH, ///< 1 qwen2, 2 qwen3, 3 llama
    n_vocab = ZXV_MODEL_N_VOCAB, n_ctx_train = ZXV_MODEL_N_CTX_TRAIN, n_layer = ZXV_MODEL_N_LAYER,
    n_embd = ZXV_MODEL_N_EMBD, n_head = ZXV_MODEL_N_HEAD, n_head_kv = ZXV_MODEL_N_HEAD_KV,
    eos = ZXV_MODEL_EOS, ///< end-of-sequence token id
};

/// KV cache representation.
enum class KvType : std::int32_t {
    q16 = ZXV_KV_Q16, ///< exact, 4 bytes per value
    q8 = ZXV_KV_Q8,   ///< 8-bit, ~1.16 bytes per value
};

/// Why generation stopped.
enum class StopReason : std::int32_t {
    none = 0,
    eos = ZXV_STOP_EOS,           ///< end-of-sequence token
    max_tokens = ZXV_STOP_MAX,    ///< max_tokens reached
    callback = ZXV_STOP_CALLBACK, ///< the callback (or the stream's consumer) stopped it
    context_full = ZXV_STOP_CTX,  ///< the context window is full
    budget = ZXV_STOP_BUDGET,     ///< the bound budget ran out
};

/// Sampling parameters. All exact: Rationals are converted to the engine's
/// Q16.16 with Rational::to_q16. temperature 0 is greedy. The same seed and
/// inputs give the same tokens on every machine.
struct GenerateOptions {
    std::uint32_t max_tokens = 64;        ///< upper bound on generated tokens
    Rational temperature{};               ///< 0 = greedy
    std::uint32_t top_k = 0;              ///< 0 = off
    Rational top_p{};                     ///< 0 = off
    Rational repeat_penalty{};            ///< 0 = off
    std::uint32_t repeat_last_n = 0;      ///< window for the penalty
    std::uint64_t seed = 0;               ///< sampling seed
};

/// One generated token.
struct Token {
    std::int32_t id = 0;   ///< token id
    std::string piece;     ///< its UTF-8 bytes (possibly a partial character)
};

/// The outcome of a generation.
struct GenerateResult {
    std::string text;                 ///< concatenated pieces
    std::vector<std::int32_t> tokens; ///< generated token ids
    std::uint32_t prompt_tokens = 0;  ///< prompt length in tokens
    StopReason stop = StopReason::none; ///< why it stopped
};

namespace detail {
struct ModelCore {
    std::shared_ptr<CapCore> cap;
    zxv_model* model = nullptr;
    ModelCore(std::shared_ptr<CapCore> c, zxv_model* m) : cap(std::move(c)), model(m) {}
    ModelCore(const ModelCore&) = delete;
    ModelCore& operator=(const ModelCore&) = delete;
    ~ModelCore() { zxv_model_destroy(model); }
};
struct SessionCore {
    std::shared_ptr<ModelCore> model;
    zxv_session* session = nullptr;
    std::shared_ptr<BudgetsCore> bound; // keeps a bound rmag alive
    std::atomic<bool> busy{false};
    SessionCore(std::shared_ptr<ModelCore> m, zxv_session* s) : model(std::move(m)), session(s) {}
    SessionCore(const SessionCore&) = delete;
    SessionCore& operator=(const SessionCore&) = delete;
    ~SessionCore() { zxv_session_destroy(session); }
};
struct BusyGuard {
    SessionCore* s;
    explicit BusyGuard(SessionCore* x) : s(x)
    {
        if (s->busy.exchange(true))
            throw UsageError(ZXV_E_STATE, "the session is in use by another call or an open TokenStream");
    }
    BusyGuard(const BusyGuard&) = delete;
    BusyGuard& operator=(const BusyGuard&) = delete;
    ~BusyGuard() { s->busy.store(false); }
};

struct GenCallback {
    std::function<bool(const Token&)>* fn;
    GenerateResult* result;
    std::exception_ptr error;
    static std::int32_t trampoline(void* user, std::int32_t id, const std::uint8_t* piece, std::size_t len) noexcept
    {
        auto* self = static_cast<GenCallback*>(user);
        try {
            Token t{id, std::string(reinterpret_cast<const char*>(piece), len)};
            self->result->tokens.push_back(id);
            self->result->text += t.piece;
            if (self->fn && *self->fn) return (*self->fn)(t) ? 1 : 0;
            return 1;
        } catch (...) {
            self->error = std::current_exception();
            return 0;
        }
    }
};

/// Run zxv_generate. The C token callback must not unwind: exceptions from
/// `fn` are caught, generation stops, and they are rethrown here.
inline GenerateResult run_generate(SessionCore& s, std::string_view prompt, const GenerateOptions& o,
                                   std::function<bool(const Token&)>* fn)
{
    const std::int32_t temp = o.temperature.to_q16(), top_p = o.top_p.to_q16(), rep = o.repeat_penalty.to_q16();
    GenerateResult r;
    GenCallback cb{fn, &r, nullptr};
    std::uint32_t np = 0, ng = 0;
    std::int32_t stop = 0;
    auto& cap = *s.model->cap;
    ensure_alive(cap.ctx.get());
    const std::int32_t st =
        zxv_generate(cap.cap, s.session, bytes_of(prompt), prompt.size(), o.max_tokens, temp, o.top_k, top_p, rep,
                     o.repeat_last_n, o.seed, &GenCallback::trampoline, &cb, &np, &ng, &stop);
    if (cb.error) std::rethrow_exception(cb.error);
    check(st, cap.ctx.get());
    r.prompt_tokens = np;
    r.stop = static_cast<StopReason>(stop);
    return r;
}
} // namespace detail

class Session;

/// An iterator-style stream of generated tokens. Generation runs on a
/// worker zxv::jthread one token ahead of the consumer. Destroying the
/// stream (or cancel()) stops generation at the next token. Move-only.
///
///     for (const zxv::Token& t : session.stream(prompt, opts)) std::cout << t.piece;
class TokenStream {
    struct State {
        std::mutex m;
        std::condition_variable cv;
        std::optional<Token> slot;
        bool done = false, cancelled = false;
        std::exception_ptr error;
        GenerateResult result;
    };

public:
    /// Input iterator over tokens; compares equal to end() when finished.
    class iterator {
    public:
        using iterator_concept = std::input_iterator_tag; ///< single pass
        using value_type = Token;                         ///< a token
        using difference_type = std::ptrdiff_t;           ///< required
        iterator() = default;                             ///< end-equivalent
        /// The current token.
        const Token& operator*() const { return *cur_; }
        /// Member access to the current token.
        const Token* operator->() const { return &*cur_; }
        /// Fetch the next token (blocks until generated).
        iterator& operator++()
        {
            cur_ = s_->next();
            return *this;
        }
        /// Post-increment (input iterator).
        void operator++(int) { ++*this; }
        /// True when the stream is exhausted.
        friend bool operator==(const iterator& it, std::default_sentinel_t) noexcept { return !it.cur_; }

    private:
        friend class TokenStream;
        explicit iterator(TokenStream* s) : s_(s), cur_(s->next()) {}
        TokenStream* s_ = nullptr;
        std::optional<Token> cur_;
    };

    TokenStream(TokenStream&&) noexcept = default; ///< move
    /// Move-assign: cancels and joins the stream being replaced first.
    TokenStream& operator=(TokenStream&& o) noexcept
    {
        if (this != &o) {
            if (st_) cancel();
            worker_ = std::move(o.worker_); // joins the old worker
            st_ = std::move(o.st_);
        }
        return *this;
    }
    TokenStream(const TokenStream&) = delete;
    TokenStream& operator=(const TokenStream&) = delete;
    ~TokenStream()
    {
        if (st_) cancel();
    } // the jthread joins after this

    /// Begin iterating (fetches the first token).
    iterator begin() { return iterator(this); }
    /// End sentinel.
    std::default_sentinel_t end() const noexcept { return {}; }

    /// The next token, or nullopt when generation has finished. Rethrows a
    /// generation error.
    std::optional<Token> next()
    {
        std::unique_lock lk(st_->m);
        st_->cv.wait(lk, [&] { return st_->slot.has_value() || st_->done; });
        if (st_->slot) {
            std::optional<Token> t = std::move(st_->slot);
            st_->slot.reset();
            st_->cv.notify_all();
            return t;
        }
        if (st_->error) std::rethrow_exception(st_->error);
        return std::nullopt;
    }

    /// Stop generation at the next token.
    void cancel()
    {
        {
            std::lock_guard lk(st_->m);
            st_->cancelled = true;
        }
        st_->cv.notify_all();
    }

    /// Drain remaining tokens and return the full result (stop reason etc.).
    GenerateResult finish()
    {
        while (next()) {
        }
        if (worker_.joinable()) worker_.join();
        return st_->result;
    }

private:
    friend class Session;
    TokenStream(std::shared_ptr<detail::SessionCore> s, std::string prompt, GenerateOptions o)
        : st_(std::make_shared<State>())
    {
        if (s->busy.exchange(true))
            throw UsageError(ZXV_E_STATE, "the session is in use by another call or an open TokenStream");
        worker_ = zxv::jthread([st = st_, s = std::move(s), prompt = std::move(prompt), o]() {
            std::function<bool(const Token&)> fn = [&](const Token& t) {
                std::unique_lock lk(st->m);
                st->cv.wait(lk, [&] { return !st->slot.has_value() || st->cancelled; });
                if (st->cancelled) return false;
                st->slot = t;
                st->cv.notify_all();
                return true;
            };
            GenerateResult r;
            std::exception_ptr err;
            try {
                r = detail::run_generate(*s, prompt, o, &fn);
            } catch (...) {
                err = std::current_exception();
            }
            s->busy.store(false);
            std::lock_guard lk(st->m);
            st->result = std::move(r);
            st->error = err;
            st->done = true;
            st->cv.notify_all();
        });
    }
    std::shared_ptr<State> st_;
    zxv::jthread worker_;
};

/// A conversation with a model: a KV cache. Generation appends, so a chat
/// continues turn after turn; reset() forgets. One thread at a time.
class Session {
public:
    Session(Session&&) noexcept = default; ///< move
    Session& operator=(Session&&) noexcept = default; ///< move-assign
    Session(const Session&) = delete;
    Session& operator=(const Session&) = delete;
    ~Session() = default;

    /// Generate from `prompt`. `on_token` (optional) sees each token and
    /// returns false to stop (StopReason::callback). Exceptions it throws
    /// stop generation and are rethrown from here. Needs caps::ai_inference.
    GenerateResult generate(std::string_view prompt, const GenerateOptions& opts = {},
                            std::function<bool(const Token&)> on_token = {})
    {
        detail::BusyGuard g(core_.get());
        return detail::run_generate(*core_, prompt, opts, on_token ? &on_token : nullptr);
    }

    /// Generate as an iterator-style stream (worker thread; see TokenStream).
    [[nodiscard]] TokenStream stream(std::string prompt, const GenerateOptions& opts = {})
    {
        return TokenStream(core_, std::move(prompt), opts);
    }

    /// Forget the conversation (empty KV cache).
    void reset()
    {
        detail::BusyGuard g(core_.get());
        detail::check(zxv_session_reset(core_->session), core_->model->cap->ctx.get());
    }

    /// Current position (tokens in the KV cache).
    [[nodiscard]] std::uint32_t position() const
    {
        std::uint32_t p = 0;
        detail::check(zxv_session_position(core_->session, &p), core_->model->cap->ctx.get());
        return p;
    }

    /// Charge `budget` one unit per prompt and generated token. Prompt
    /// tokens are charged up front: if they do not fit, generate throws
    /// BudgetExhausted and runs nothing; mid-generation exhaustion stops
    /// with StopReason::budget.
    void bind_budget(const Budget& budget)
    {
        detail::BusyGuard g(core_.get());
        detail::check(zxv_session_bind_budget(core_->session, budget.core()->rmag, budget.id()), core_->model->cap->ctx.get());
        core_->bound = budget.core();
    }
    /// Remove the budget binding.
    void unbind_budget()
    {
        detail::BusyGuard g(core_.get());
        detail::check(zxv_session_bind_budget(core_->session, nullptr, 0), core_->model->cap->ctx.get());
        core_->bound.reset();
    }

private:
    friend class Model;
    explicit Session(std::shared_ptr<detail::SessionCore> c) : core_(std::move(c)) {}
    std::shared_ptr<detail::SessionCore> core_;
};

/// A GGUF model in the integer inference engine (qwen2, qwen3, llama),
/// bit-identical on every machine. Read-only after loading; sessions on
/// any threads may share it. Move-only. Needs caps::ai_inference.
class Model {
public:
    /// Open a GGUF file; `tokenizer_path` empty = the model's own tokenizer.
    static Model open(const Capability& cap, const std::string& model_path, const std::string& tokenizer_path = {})
    {
        zxv_model* m = nullptr;
        cap.core()->call([&] {
            return zxv_model_open(cap.native_handle(), model_path.c_str(),
                                  tokenizer_path.empty() ? nullptr : tokenizer_path.c_str(), &m);
        });
        return Model(std::make_shared<detail::ModelCore>(cap.core(), m));
    }
    /// Open from memory (the bytes are copied).
    static Model open_memory(const Capability& cap, std::span<const std::uint8_t> model,
                             std::span<const std::uint8_t> tokenizer = {})
    {
        zxv_model* m = nullptr;
        cap.core()->call([&] {
            return zxv_model_open_memory(cap.native_handle(), model.data(), model.size(),
                                         tokenizer.empty() ? nullptr : tokenizer.data(), tokenizer.size(), &m);
        });
        return Model(std::make_shared<detail::ModelCore>(cap.core(), m));
    }
    Model(Model&&) noexcept = default; ///< move
    Model& operator=(Model&&) noexcept = default; ///< move-assign
    Model(const Model&) = delete;
    Model& operator=(const Model&) = delete;
    ~Model() = default;

    /// A metadata value.
    [[nodiscard]] std::int64_t info(ModelInfo key) const
    {
        std::int64_t v = 0;
        detail::check(zxv_model_info(core_->model, static_cast<std::int32_t>(key), &v));
        return v;
    }
    /// Token ids of UTF-8 text.
    [[nodiscard]] std::vector<std::int32_t> tokenize(std::string_view text, bool parse_special = true) const
    {
        std::size_t n = 0;
        const std::int32_t st = zxv_tokenize(core_->model, detail::bytes_of(text), text.size(), parse_special ? 1 : 0,
                                             nullptr, 0, &n);
        if (st != ZXV_E_BUFFER) detail::check(st);
        std::vector<std::int32_t> ids(n);
        if (st == ZXV_E_BUFFER)
            detail::check(zxv_tokenize(core_->model, detail::bytes_of(text), text.size(), parse_special ? 1 : 0,
                                       ids.data(), ids.size(), &n));
        ids.resize(n);
        return ids;
    }
    /// UTF-8 text of token ids.
    [[nodiscard]] std::string detokenize(std::span<const std::int32_t> ids) const
    {
        return detail::read_text([&](char* b, std::size_t c, std::size_t* l) {
            return zxv_detokenize(core_->model, ids.data(), ids.size(), b, c, l);
        });
    }
    /// A new session. n_ctx 0 = min(n_ctx_train, 4096).
    [[nodiscard]] Session new_session(std::uint32_t n_ctx = 0, KvType kv = KvType::q16) const
    {
        zxv_session* s = nullptr;
        detail::check(zxv_session_create(core_->model, n_ctx, static_cast<std::int32_t>(kv), &s));
        return Session(std::make_shared<detail::SessionCore>(core_, s));
    }
    /// LoRA adapters: declared, not built yet. Throws NotImplemented while
    /// zxv::feature::lora is off.
    void load_lora(const std::string& path, Rational scale)
    {
        zxv_lora* l = nullptr;
        core_->cap->call([&] { return zxv_lora_load(core_->cap->cap, core_->model, path.c_str(), scale.to_q16(), &l); });
        zxv_lora_destroy(l); // unreachable today; keeps the handle from leaking later
        throw NotImplemented(ZXV_E_NOTIMPL, "LoRA handles are not exposed by this SDK version");
    }

private:
    explicit Model(std::shared_ptr<detail::ModelCore> c) : core_(std::move(c)) {}
    std::shared_ptr<detail::ModelCore> core_;
};

// ===========================================================================
// EVM state channels
// ===========================================================================

/// A 20-byte Ethereum address.
using Address = std::array<std::uint8_t, 20>;

/// abi.encode(bytes32 channelId, uint256 nonce, address[] parties,
/// int256[] balances) and its keccak256 digest.
struct EvmState {
    std::vector<std::uint8_t> encoding; ///< the ABI encoding
    Digest32 digest{};                  ///< keccak256(encoding): what parties sign (EIP-191)
};

/// Encodes ledger balances for the Solidity state-channel contract
/// (sdk/solidity/ZxvStateChannel.sol). Needs caps::evm (and ledger_read for
/// ledger_state). ZXV has no secp256k1: parties sign the digest in their
/// own wallet.
class StateChannel {
public:
    /// Acting with `cap`.
    explicit StateChannel(const Capability& cap) : cap_(cap.core()) {}

    /// Encode explicit balances (one per address).
    [[nodiscard]] EvmState encode_state(const Digest32& channel_id, std::uint64_t nonce,
                                        std::span<const Address> parties, std::span<const std::int64_t> balances) const
    {
        if (parties.size() != balances.size()) throw UsageError(ZXV_E_ARG, "one balance per party");
        const auto addrs = flat(parties);
        return run([&](std::uint8_t* buf, std::size_t cap, std::size_t* len, std::uint8_t* dg) {
            return zxv_evm_encode_state(cap_->cap, channel_id.data(), nonce, addrs.data(), balances.data(),
                                        static_cast<std::uint32_t>(parties.size()), buf, cap, len, dg);
        });
    }

    /// Encode each account's EQUITY (888) balance, all in `ccy`.
    [[nodiscard]] EvmState ledger_state(const Digest32& channel_id, std::uint64_t nonce, const std::string& ccy,
                                        std::span<const std::uint32_t> accounts, std::span<const Address> parties) const
    {
        if (parties.size() != accounts.size()) throw UsageError(ZXV_E_ARG, "one account per party");
        const auto addrs = flat(parties);
        return run([&](std::uint8_t* buf, std::size_t cap, std::size_t* len, std::uint8_t* dg) {
            return zxv_evm_ledger_state(cap_->cap, channel_id.data(), nonce, ccy.c_str(), accounts.data(), addrs.data(),
                                        static_cast<std::uint32_t>(parties.size()), buf, cap, len, dg);
        });
    }

private:
    static std::vector<std::uint8_t> flat(std::span<const Address> a)
    {
        std::vector<std::uint8_t> v;
        v.reserve(a.size() * 20);
        for (const auto& x : a) v.insert(v.end(), x.begin(), x.end());
        return v;
    }
    template <class F>
    EvmState run(F&& f) const
    {
        EvmState s;
        std::size_t need = 0;
        detail::ensure_alive(cap_->ctx.get());
        std::int32_t st = f(nullptr, 0, &need, s.digest.data());
        if (st != ZXV_E_BUFFER) detail::check(st, cap_->ctx.get());
        s.encoding.resize(need);
        cap_->call([&] { return f(s.encoding.data(), s.encoding.size(), &need, s.digest.data()); });
        s.encoding.resize(need);
        return s;
    }
    std::shared_ptr<detail::CapCore> cap_;
};

// Out-of-line definitions that need complete types.
inline std::shared_ptr<detail::CtxCore> Message::ctx_core(const Context& c) { return c.core_; }

} // namespace zxv

#endif // ZXV_HPP
