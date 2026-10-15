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
// Swarm, Model and the ABI 1.4 hw::Matrix and hw::Region are safe to share
// across threads. A Session is used by
// one thread at a time (the SDK refuses concurrent use). The ABI 1.5 groups
// (treaty, cid/aipi, coin/token, fee, vhub, freight, web4, paradox) and the
// ABI 1.6 group (vino) state their own threading rules on each type.
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
/// Minimum ABI minor version this SDK needs (ABI 1.5: treaties, CIDs and
/// AIPI, Fibonacci coins and tokens, the assurance fee, the V-Bill hub,
/// freight, Web 4 agents and the paradox engine; 1.4 added hardware
/// emulation, 1.3 the hexagram tag, 1.2 the *_cap forms).
inline constexpr std::uint32_t header_abi_minor = 5;

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
inline constexpr std::uint32_t hw = ZXV_FEAT_HW;               ///< emulation matrix, ZHP-1, hardware updates (1.4)
} // namespace feature

/// Feature bits of the second mask, zxv_features2 (ZXV_FEAT2_*, ABI 1.5).
/// The first mask is nearly full, so every ABI 1.5 group reports here. A
/// group whose bit is clear is not built into the loaded library: every
/// call of it throws NotImplemented (ZXV_E_NOTIMPL).
namespace feature2 {
inline constexpr std::uint32_t treaty = ZXV_FEAT2_TREATY;   ///< treaties: copybook records, Sutra text
inline constexpr std::uint32_t aipi = ZXV_FEAT2_AIPI;       ///< content-addressed functions, CIDs
inline constexpr std::uint32_t coin = ZXV_FEAT2_COIN;       ///< Fibonacci coins and token octane
inline constexpr std::uint32_t fee = ZXV_FEAT2_FEE;         ///< assurance fee and its split
inline constexpr std::uint32_t vhub = ZXV_FEAT2_VHUB;       ///< V-Bill hub pegs, quotes and swaps
inline constexpr std::uint32_t freight = ZXV_FEAT2_FREIGHT; ///< erasure-coded freight packets
inline constexpr std::uint32_t web4 = ZXV_FEAT2_WEB4;       ///< agent manifests, directory, consent
inline constexpr std::uint32_t paradox = ZXV_FEAT2_PARADOX; ///< paradox engine
inline constexpr std::uint32_t vino = ZXV_FEAT2_VINO;       ///< Vino Embed: play lane, payment requests, receipts (1.6)
} // namespace feature2

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
inline constexpr std::uint64_t all = ZXV_CAP_ALL;                   ///< every ABI 1.1 bit (this value forever)
inline constexpr std::uint64_t hw_matrix = ZXV_CAP_HW_MATRIX;       ///< build and call the emulation matrix (1.4)
inline constexpr std::uint64_t hw_reconf = ZXV_CAP_HW_RECONF;       ///< load hardware images and contracts (1.4)
inline constexpr std::uint64_t all_1_4 = ZXV_CAP_ALL_1_4;           ///< every bit as of ABI 1.4
inline constexpr std::uint64_t aipi = ZXV_CAP_AIPI;                 ///< publish and store AIPI functions and blocks (1.5)
inline constexpr std::uint64_t mint = ZXV_CAP_MINT;                 ///< mint tokens, change token octane (1.5)
inline constexpr std::uint64_t paradox = ZXV_CAP_PARADOX;           ///< paradox operators, logs and policies (1.5)
inline constexpr std::uint64_t all_1_5 = ZXV_CAP_ALL_1_5;           ///< every bit as of ABI 1.5
inline constexpr std::uint64_t vino = ZXV_CAP_VINO;                 ///< play-lane vouchers and the spend meter (1.6)
inline constexpr std::uint64_t all_1_6 = ZXV_CAP_ALL_1_6;           ///< every bit as of ABI 1.6
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

/// No proven hardware implementation may serve the call (ZXV_E_NOPROVIDER).
class NoProvider : public RefusedError {
public:
    using RefusedError::RefusedError;
};

/// A shadow check found two implementations disagree (ZXV_E_DISPUTE).
class Disputed : public RefusedError {
public:
    using RefusedError::RefusedError;
};

/// A hardware image failed in hardware: rolled back and logged
/// (ZXV_E_QUARANTINED).
class Quarantined : public RefusedError {
public:
    using RefusedError::RefusedError;
};

/// An image or contract is not newer, or is below the floor (ZXV_E_ROLLBACK).
class RollbackRefused : public RefusedError {
public:
    using RefusedError::RefusedError;
};

/// The image was quarantined before; it is never reloaded (ZXV_E_BLOCKED).
class ImageBlocked : public RefusedError {
public:
    using RefusedError::RefusedError;
};

/// Refused by the function's placement policy (ZXV_E_POLICY).
class PolicyDenied : public RefusedError {
public:
    using RefusedError::RefusedError;
};

/// The image is for other hardware (ZXV_E_TARGET, USAGE class).
class TargetMismatch : public UsageError {
public:
    using UsageError::UsageError;
};

/// Not a face of the Fibonacci ladder: a coin or token denomination that
/// does not exist (ZXV_E_DENOM, USAGE class, ABI 1.5). To pay an arbitrary
/// amount, split it (coin::split, token::canonical).
class DenominationError : public UsageError {
public:
    using UsageError::UsageError;
};

/// A treaty failed validation (ZXV_E_TREATY, USAGE class, ABI 1.5). When the
/// SDK raised it from a parse or a validation it carries the reason
/// (treaty::Fault, as its integer value) and, for a parse, the byte offset
/// of the first fault; otherwise both are 0.
class TreatyError : public UsageError {
public:
    /// From a status and detail only (fault and offset 0).
    TreatyError(std::int32_t status, std::string detail) : UsageError(status, std::move(detail)) {}
    /// With the ZXV_TREATY_FAULT_* reason and the byte offset of the fault.
    TreatyError(std::int32_t status, std::string detail, std::int32_t fault, std::uint32_t offset)
        : UsageError(status, std::move(detail)), fault_(fault), offset_(offset)
    {
    }
    /// The ZXV_TREATY_FAULT_* value (convert with treaty::Fault).
    [[nodiscard]] std::int32_t fault_code() const noexcept { return fault_; }
    /// The byte offset of the first fault in the parsed text or record.
    [[nodiscard]] std::uint32_t offset() const noexcept { return offset_; }

private:
    std::int32_t fault_ = 0;
    std::uint32_t offset_ = 0;
};

/// Bytes do not match their CID, or the CID itself is malformed (ZXV_E_CID,
/// REFUSED class, ABI 1.5). Drop bytes that fail; never use them.
class CidMismatch : public RefusedError {
public:
    using RefusedError::RefusedError;
};

/// No valid consent for this agent action (ZXV_E_CONSENT, ABI 1.5): no
/// token, a mismatch, expired, a bad signature, or a token already used or
/// revoked. detail() says which; never branch on it.
class ConsentDenied : public RefusedError {
public:
    using RefusedError::RefusedError;
};

/// Too few distinct freight rows arrived to rebuild the payload
/// (ZXV_E_ERASURE, ABI 1.5). Decode again when more rows arrive.
class ErasureError : public RefusedError {
public:
    using RefusedError::RefusedError;
};

/// No valid signed peg for this currency pair, or a stale or replayed peg
/// epoch (ZXV_E_PEG, ABI 1.5).
class PegError : public RefusedError {
public:
    using RefusedError::RefusedError;
};

/// A V-Bill hub limit rule refused the swap (ZXV_E_LIMIT, ABI 1.5). Not a
/// fault: the rule did its job; a later rule epoch may allow it.
class LimitRefused : public RefusedError {
public:
    using RefusedError::RefusedError;
};

/// A paradox graph reached its step limit unresolved and was escalated to
/// the owner (ZXV_E_PARADOX_STEP, ABI 1.5). The pass was logged and the
/// graph stays readable.
class ParadoxStepLimit : public RefusedError {
public:
    using RefusedError::RefusedError;
};

/// The action needs the owner's approval: it touches money or devices
/// (ZXV_E_PARADOX_OWNER, ABI 1.5).
class OwnerApprovalRequired : public RefusedError {
public:
    using RefusedError::RefusedError;
};

/// The play voucher is not on the rail the call named (ZXV_E_RAIL,
/// REFUSED class, ABI 1.6). A voucher lives on exactly one rail at a time,
/// BEARER or GHOST, and must be spent on that one; switching it to the rail
/// it is already on is refused too. Read its rail with
/// vino::Play::voucher() and call again.
class RailMismatch : public RefusedError {
public:
    using RefusedError::RefusedError;
};

/// The play-lane issuer would fall below 1.8x coverage (ZXV_E_COVERAGE,
/// REFUSED class, ABI 1.6): an issue or a withdrawal of backing that would
/// break 10 * backing >= 18 * outstanding. Nothing changed; post more play
/// backing or wait for vouchers to be redeemed.
class CoverageRefused : public RefusedError {
public:
    using RefusedError::RefusedError;
};

/// Wrong lane for this call (ZXV_E_LANE, USAGE class, ABI 1.6): a payment
/// request whose lane is neither vino::Lane::play nor vino::Lane::live.
class LaneError : public UsageError {
public:
    using UsageError::UsageError;
};

/// The payment request expired, or the wallet approved it outside its time
/// window (ZXV_E_EXPIRED, REFUSED class, ABI 1.6). Build a fresh request.
class RequestExpired : public RefusedError {
public:
    using RefusedError::RefusedError;
};

/// A LIVE payment request marked a paid chance outcome (a loot box, a paid
/// draw) and was refused (ZXV_E_CHANCE, REFUSED class, ABI 1.6). Vino
/// payment requests never carry paid chance outcomes on the live lane.
class PaidChanceRefused : public RefusedError {
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
        if (status == ZXV_E_TARGET) throw TargetMismatch(status, std::move(detail));
        if (status == ZXV_E_DENOM) throw DenominationError(status, std::move(detail));
        if (status == ZXV_E_TREATY) throw TreatyError(status, std::move(detail));
        if (status == ZXV_E_LANE) throw LaneError(status, std::move(detail));
        throw UsageError(status, std::move(detail));
    case StatusClass::refused:
        switch (status) {
        case ZXV_E_CAP: throw CapabilityDenied(status, std::move(detail));
        case ZXV_E_BUDGET: throw BudgetExhausted(status, std::move(detail));
        case ZXV_E_FUNDS: throw InsufficientFunds(status, std::move(detail));
        case ZXV_E_VERIFY: throw VerificationFailed(status, std::move(detail));
        case ZXV_E_NOTIMPL: throw NotImplemented(status, std::move(detail));
        case ZXV_E_NOPROVIDER: throw NoProvider(status, std::move(detail));
        case ZXV_E_DISPUTE: throw Disputed(status, std::move(detail));
        case ZXV_E_QUARANTINED: throw Quarantined(status, std::move(detail));
        case ZXV_E_ROLLBACK: throw RollbackRefused(status, std::move(detail));
        case ZXV_E_BLOCKED: throw ImageBlocked(status, std::move(detail));
        case ZXV_E_POLICY: throw PolicyDenied(status, std::move(detail));
        case ZXV_E_CID: throw CidMismatch(status, std::move(detail));
        case ZXV_E_CONSENT: throw ConsentDenied(status, std::move(detail));
        case ZXV_E_ERASURE: throw ErasureError(status, std::move(detail));
        case ZXV_E_PEG: throw PegError(status, std::move(detail));
        case ZXV_E_LIMIT: throw LimitRefused(status, std::move(detail));
        case ZXV_E_PARADOX_STEP: throw ParadoxStepLimit(status, std::move(detail));
        case ZXV_E_PARADOX_OWNER: throw OwnerApprovalRequired(status, std::move(detail));
        case ZXV_E_RAIL: throw RailMismatch(status, std::move(detail));
        case ZXV_E_COVERAGE: throw CoverageRefused(status, std::move(detail));
        case ZXV_E_EXPIRED: throw RequestExpired(status, std::move(detail));
        case ZXV_E_CHANCE: throw PaidChanceRefused(status, std::move(detail));
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

/// Bitmask of the ABI 1.5 groups live in the linked libzxv
/// (zxv::feature2::*, zxv_features2).
inline std::uint32_t features2()
{
    std::uint32_t m = 0;
    detail::check(zxv_features2(&m));
    return m;
}

/// True if every bit of `mask` (zxv::feature2::*) is live in this build.
inline bool has_feature2(std::uint32_t mask) { return (features2() & mask) == mask; }

/// Refuse a library whose major version differs or whose minor is below 5
/// (SDK_DESIGN.md section 1). Called once, automatically, by Context.
inline void ensure_compatible()
{
    static const Version v = version();
    if (v.major != header_abi_major || v.minor < header_abi_minor)
        throw SystemError(ZXV_E_INTERNAL, "libzxv ABI " + std::to_string(v.major) + "." +
                                              std::to_string(v.minor) + " is not compatible with " +
                                              "this SDK (needs 1.5 or later 1.x)");
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
    std::int64_t debit = 0;  ///< 555 NRE: sum received
    std::int64_t credit = 0; ///< 777 NCR: sum sent
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

// ===========================================================================
// Hardware emulation and hardware updates (ABI 1.4)
// ===========================================================================

/// ABI 1.4: hardware emulation and hardware updates.
///
///   * hw::Profile: a ZHP-1 machine profile, the "hardware ROM" (plain
///     data, no capability): probe this host, take the public reference
///     machine R1, present R1 on a host, encode, digest and match.
///   * hw::Matrix: functions (engine contracts) served by interchangeable,
///     proven implementations. Every matrix starts with the standard
///     engines (hw::fn_sha256 ... hw::fn_tensor), each served by a proven
///     SOFT implementation. Needs caps::hw_matrix; contract packages need
///     caps::hw_reconf.
///   * hw::Release: release keys and signed packages (ML-DSA-87 AND
///     SLH-DSA-SHAKE-256s). Needs caps::crypto_sign. SLH-DSA signing takes
///     seconds: building is a release step.
///   * hw::Region: a reconfigurable region updated with signed images:
///     stage, trial, commit or roll back, every step an event and a record
///     in a hash-chained audit log. Needs caps::hw_reconf.
///
/// Callbacks (providers, device operations, event handlers) are
/// std::functions called through noexcept trampolines: no exception ever
/// crosses C. A zxv::Exception (or FatalError) thrown by a provider or a
/// device operation is reported to the library as its status; any other
/// exception as ZXV_E_INTERNAL. Exceptions from event handlers are
/// swallowed. Everything is integers.
namespace hw {

// ----- constants -----------------------------------------------------------

inline constexpr std::uint32_t fn_sha256 = ZXV_HW_FN_SHA256;   ///< op 0: SHA-256
inline constexpr std::uint32_t fn_keccak = ZXV_HW_FN_KECCAK;   ///< op 0/1 SHA3-256/512, op 2/3 SHAKE
inline constexpr std::uint32_t fn_mac = ZXV_HW_FN_MAC;         ///< op 0: HMAC-SHA256 (key-bearing)
inline constexpr std::uint32_t fn_gcd = ZXV_HW_FN_GCD;         ///< op 0: n pairs of u64 -> n gcds
inline constexpr std::uint32_t fn_logic64 = ZXV_HW_FN_LOGIC64; ///< 64-lane four-valued AND / OR / NOT
inline constexpr std::uint32_t fn_tensor = ZXV_HW_FN_TENSOR;   ///< op 0: one Q8_0 row dot

inline constexpr std::uint32_t res_host = ZXV_HW_RES_HOST; ///< the resource every matrix starts with: this CPU
inline constexpr std::uint32_t root = ZXV_HW_ROOT;         ///< parent id of a root profile node
inline constexpr std::size_t ext_max = ZXV_HW_EXT_MAX;     ///< private extension bytes per node
inline constexpr std::size_t pk_bytes = ZXV_HW_PK_BYTES;   ///< ML-DSA-87 key || SLH-DSA key
inline constexpr std::size_t mldsa_sk_bytes = ZXV_HW_MLDSA_SK_BYTES;
inline constexpr std::size_t slhdsa_sk_bytes = ZXV_HW_SLHDSA_SK_BYTES;
inline constexpr std::size_t sig_bytes = ZXV_HW_SIG_BYTES;
inline constexpr std::size_t header_bytes = ZXV_HW_HEADER_BYTES;
/// Output capacity of Matrix::call (the largest golden-vector output).
inline constexpr std::size_t call_out_default = 512;

/// Component classes (ZXV_HW_CLASS_*). Classes from cls::private_ up are an
/// implementer's own.
namespace cls {
inline constexpr std::uint32_t machine = ZXV_HW_CLASS_MACHINE;
inline constexpr std::uint32_t cpu_cluster = ZXV_HW_CLASS_CPU_CLUSTER;
inline constexpr std::uint32_t cpu_core = ZXV_HW_CLASS_CPU_CORE;
inline constexpr std::uint32_t cache = ZXV_HW_CLASS_CACHE;
inline constexpr std::uint32_t memory = ZXV_HW_CLASS_MEMORY;
inline constexpr std::uint32_t interconnect = ZXV_HW_CLASS_INTERCONNECT;
inline constexpr std::uint32_t matrix_unit = ZXV_HW_CLASS_MATRIX_UNIT;
inline constexpr std::uint32_t vector_unit = ZXV_HW_CLASS_VECTOR_UNIT;
inline constexpr std::uint32_t ternary_unit = ZXV_HW_CLASS_TERNARY_UNIT;
inline constexpr std::uint32_t hash_unit = ZXV_HW_CLASS_HASH_UNIT;
inline constexpr std::uint32_t crypto_unit = ZXV_HW_CLASS_CRYPTO_UNIT;
inline constexpr std::uint32_t codec_unit = ZXV_HW_CLASS_CODEC_UNIT;
inline constexpr std::uint32_t reconf_fabric = ZXV_HW_CLASS_RECONF_FABRIC;
inline constexpr std::uint32_t storage = ZXV_HW_CLASS_STORAGE;
inline constexpr std::uint32_t network = ZXV_HW_CLASS_NETWORK;
inline constexpr std::uint32_t radio = ZXV_HW_CLASS_RADIO;
inline constexpr std::uint32_t sensor = ZXV_HW_CLASS_SENSOR;
inline constexpr std::uint32_t actuator = ZXV_HW_CLASS_ACTUATOR;
inline constexpr std::uint32_t clock = ZXV_HW_CLASS_CLOCK;
inline constexpr std::uint32_t power = ZXV_HW_CLASS_POWER;
inline constexpr std::uint32_t secure_element = ZXV_HW_CLASS_SECURE_ELEMENT;
inline constexpr std::uint32_t display = ZXV_HW_CLASS_DISPLAY;
inline constexpr std::uint32_t audio = ZXV_HW_CLASS_AUDIO;
inline constexpr std::uint32_t wire = ZXV_HW_CLASS_WIRE;
inline constexpr std::uint32_t gpu = ZXV_HW_CLASS_GPU;
inline constexpr std::uint32_t package = ZXV_HW_CLASS_PACKAGE;
inline constexpr std::uint32_t lane = ZXV_HW_CLASS_LANE;
inline constexpr std::uint32_t logic_unit = ZXV_HW_CLASS_LOGIC_UNIT;
inline constexpr std::uint32_t ntt_unit = ZXV_HW_CLASS_NTT_UNIT;
inline constexpr std::uint32_t frame_unit = ZXV_HW_CLASS_FRAME_UNIT;
inline constexpr std::uint32_t gcd_unit = ZXV_HW_CLASS_GCD_UNIT;
inline constexpr std::uint32_t mem_channel = ZXV_HW_CLASS_MEM_CHANNEL;
inline constexpr std::uint32_t secure_island = ZXV_HW_CLASS_SECURE_ISLAND;
inline constexpr std::uint32_t io = ZXV_HW_CLASS_IO;
inline constexpr std::uint32_t private_ = ZXV_HW_CLASS_PRIVATE;
} // namespace cls

/// Capability keys of a profile node (ZXV_HW_CAP_*): bit 15 critical, bits
/// 14-13 comparison (MIN, EXACT, MASK, INFO), bits 12-0 id.
namespace cap {
inline constexpr std::uint32_t word_bits = ZXV_HW_CAP_WORD_BITS;
inline constexpr std::uint32_t count = ZXV_HW_CAP_COUNT;
inline constexpr std::uint32_t freq_khz = ZXV_HW_CAP_FREQ_KHZ;
inline constexpr std::uint32_t bytes = ZXV_HW_CAP_BYTES;
inline constexpr std::uint32_t line_bytes = ZXV_HW_CAP_LINE_BYTES;
inline constexpr std::uint32_t isa = ZXV_HW_CAP_ISA;
inline constexpr std::uint32_t int_widths = ZXV_HW_CAP_INT_WIDTHS;
inline constexpr std::uint32_t radix = ZXV_HW_CAP_RADIX;
inline constexpr std::uint32_t macs_per_cycle = ZXV_HW_CAP_MACS_PER_CYCLE;
inline constexpr std::uint32_t simd = ZXV_HW_CAP_SIMD;
inline constexpr std::uint32_t tick_ns = ZXV_HW_CAP_TICK_NS;
inline constexpr std::uint32_t regions = ZXV_HW_CAP_REGIONS;
inline constexpr std::uint32_t bandwidth_mbps = ZXV_HW_CAP_BANDWIDTH_MBPS;
inline constexpr std::uint32_t latency_ns = ZXV_HW_CAP_LATENCY_NS;
inline constexpr std::uint32_t pq_algs = ZXV_HW_CAP_PQ_ALGS;
inline constexpr std::uint32_t hash_algs = ZXV_HW_CAP_HASH_ALGS;
inline constexpr std::uint32_t wire_bits = ZXV_HW_CAP_WIRE_BITS;
inline constexpr std::uint32_t peak_ops_mhz = ZXV_HW_CAP_PEAK_OPS_MHZ;
inline constexpr std::uint32_t contract_id = ZXV_HW_CAP_CONTRACT_ID;   ///< the engine contract (fn_*)
inline constexpr std::uint32_t contract_ver = ZXV_HW_CAP_CONTRACT_VER; ///< its golden-vector version
inline constexpr std::uint32_t kind = ZXV_HW_CAP_KIND;                 ///< the hw::Kind serving the node
inline constexpr std::uint32_t capacity_class = ZXV_HW_CAP_CAPACITY_CLASS;
inline constexpr std::uint32_t perf_bucket = ZXV_HW_CAP_PERF_BUCKET;
inline constexpr std::uint32_t power_bucket = ZXV_HW_CAP_POWER_BUCKET; ///< hw::Power scale
inline constexpr std::uint64_t isa_x86_64 = ZXV_HW_ISA_X86_64;
inline constexpr std::uint64_t isa_arm64 = ZXV_HW_ISA_ARM64;
inline constexpr std::uint64_t isa_riscv64 = ZXV_HW_ISA_RISCV64;
inline constexpr std::uint64_t isa_wasm32 = ZXV_HW_ISA_WASM32;
} // namespace cap

/// How an implementation is built, in the order the matrix prefers them at
/// equal cost.
enum class Kind : std::int32_t {
    hard = ZXV_HW_KIND_HARD,     ///< silicon built for the function
    fabric = ZXV_HW_KIND_FABRIC, ///< a reconfigurable fabric region
    micro = ZXV_HW_KIND_MICRO,   ///< a microcoded unit or soft core
    peer = ZXV_HW_KIND_PEER,     ///< another local component standing in
    soft = ZXV_HW_KIND_SOFT,     ///< a software emulator
    remote = ZXV_HW_KIND_REMOTE, ///< another machine (never key-bearing work)
};

/// The state of a matrix resource.
enum class ResourceState : std::int32_t {
    ok = ZXV_HW_RES_OK,
    faulted = ZXV_HW_RES_FAULTED,
    reconfiguring = ZXV_HW_RES_RECONFIGURING,
    offline = ZXV_HW_RES_OFFLINE,
};

/// Placement policy flags (combine with |).
namespace policy {
inline constexpr std::uint32_t key_bearing = ZXV_HW_POLICY_KEY_BEARING; ///< always local, never memoized; sticky
inline constexpr std::uint32_t local_only = ZXV_HW_POLICY_LOCAL_ONLY;   ///< no REMOTE implementation
} // namespace policy

/// Implementation status flags.
namespace pf {
inline constexpr std::uint32_t fault = ZXV_HW_PF_FAULT;       ///< its resource is faulted or offline
inline constexpr std::uint32_t disputed = ZXV_HW_PF_DISPUTED; ///< a shadow check found it disagreeing
inline constexpr std::uint32_t failed = ZXV_HW_PF_FAILED;     ///< it failed the golden vectors
inline constexpr std::uint32_t unproven = ZXV_HW_PF_UNPROVEN; ///< not proven against the current contract
inline constexpr std::uint32_t disabled = ZXV_HW_PF_DISABLED;
inline constexpr std::uint32_t policy = ZXV_HW_PF_POLICY;   ///< the function's policy excludes it
inline constexpr std::uint32_t serving = ZXV_HW_PF_SERVING; ///< serving the function now
} // namespace pf

/// Coarse power classes.
enum class Power : std::uint32_t {
    unknown = ZXV_HW_POWER_UNKNOWN,
    mw10 = ZXV_HW_POWER_10MW,
    mw100 = ZXV_HW_POWER_100MW,
    w1 = ZXV_HW_POWER_1W,
    w10 = ZXV_HW_POWER_10W,
    w100 = ZXV_HW_POWER_100W,
    more = ZXV_HW_POWER_MORE,
};

/// Package kinds.
enum class ImageKind : std::uint32_t {
    bitstream = ZXV_HW_IMG_BITSTREAM,
    partial = ZXV_HW_IMG_PARTIAL,
    microcode = ZXV_HW_IMG_MICROCODE,
    firmware = ZXV_HW_IMG_FIRMWARE,
    vectors = ZXV_HW_IMG_VECTORS, ///< a new golden-vector contract for a function
};

/// Region states.
enum class RegionState : std::int32_t {
    empty = ZXV_HW_REGION_EMPTY,
    active = ZXV_HW_REGION_ACTIVE,
    staged = ZXV_HW_REGION_STAGED,
    probation = ZXV_HW_REGION_PROBATION,
    halted = ZXV_HW_REGION_HALTED,
};

/// Region events and audit record kinds.
enum class EventKind : std::int32_t {
    staged = ZXV_HW_EV_STAGED,           ///< verified and loaded into the inactive slot
    tested = ZXV_HW_EV_TESTED,           ///< switched to and proven: probation begins
    activated = ZXV_HW_EV_ACTIVATED,     ///< committed: permanent, floor raised
    rolled_back = ZXV_HW_EV_ROLLED_BACK, ///< back to the previous image (or S0)
    rejected = ZXV_HW_EV_REJECTED,       ///< refused before loading
    quarantined = ZXV_HW_EV_QUARANTINED, ///< failed in hardware; payload blocked
    contract = ZXV_HW_EV_CONTRACT,       ///< a function's golden vectors replaced
};

/// How one target component is served on a host.
enum class Pairing : std::int32_t {
    native = ZXV_HW_NATIVE,   ///< a host component satisfies it (host_id)
    emulate = ZXV_HW_EMULATE, ///< nothing does: emulated (unmet_key, 0 if no such class)
    group = ZXV_HW_GROUP,     ///< a device or package node: nothing to run
};

using Digest = std::array<std::uint8_t, 32>; ///< a SHA-256 digest

namespace detail {
/// The status a callback hands back to C for the exception in flight.
inline std::int32_t current_status() noexcept
{
    try {
        throw;
    } catch (const zxv::Exception& e) {
        return e.status() < 0 ? e.status() : ZXV_E_INTERNAL;
    } catch (const zxv::FatalError& e) {
        return e.status() < 0 ? e.status() : ZXV_E_INTERNAL;
    } catch (...) {
        return ZXV_E_INTERNAL;
    }
}
} // namespace detail

// ----- profiles ------------------------------------------------------------

/// One node of a profile.
struct Node {
    std::uint32_t id = 0, parent = 0, cls = 0;
    std::uint32_t caps = 0; ///< how many capabilities it carries
};

/// How many target components a host runs natively and how many it emulates.
struct Match {
    std::uint32_t native = 0, emulated = 0;
};

/// The pairing of one target node (Profile::pair).
struct Pair {
    std::uint32_t need_id = 0;
    Pairing kind = Pairing::group;
    std::uint32_t host_id = 0;   ///< for Pairing::native
    std::uint32_t unmet_key = 0; ///< for Pairing::emulate
};

/// A ZHP-1 machine profile: a canonical tree of abstract components with
/// integer capability descriptors and an opaque private extension per node.
/// Plain data: no capability is needed. Move-only; clone() copies.
class Profile {
public:
    /// An empty profile.
    Profile()
    {
        zxv_hw_profile* r = nullptr;
        zxv::detail::check(zxv_hw_profile_create(&r));
        p_.reset(r);
    }
    Profile(Profile&&) noexcept = default;            ///< move
    Profile& operator=(Profile&&) noexcept = default; ///< move-assign
    Profile(const Profile&) = delete;
    Profile& operator=(const Profile&) = delete;
    ~Profile() = default;

    /// Describes this machine: CPU cluster (ISA, cores, SIMD), memory, cache
    /// line. Only what the OS reports reliably.
    static Profile probe_host() { return make([](zxv_hw_profile** o) { return zxv_hw_profile_probe_host(o); }); }
    /// A copy of the public VULPINE-class reference machine R1.
    static Profile reference() { return make([](zxv_hw_profile** o) { return zxv_hw_profile_reference(o); }); }
    /// `target` as served on `host`: every node that runs something carries
    /// cap::kind (HARD where the host has it, SOFT where it is emulated).
    static Profile present(const Profile& host, const Profile& target)
    {
        return make([&](zxv_hw_profile** o) { return zxv_hw_profile_present(host.get(), target.get(), o); });
    }
    /// Parse a canonical ZHP-1 encoding (anything else: UsageError, ZXV_E_FORMAT).
    static Profile decode(std::span<const std::uint8_t> b)
    {
        return make([&](zxv_hw_profile** o) { return zxv_hw_profile_decode(b.data(), b.size(), o); });
    }
    /// A copy (through the canonical encoding).
    [[nodiscard]] Profile clone() const { return decode(encode()); }

    /// The canonical encoding.
    [[nodiscard]] std::vector<std::uint8_t> encode() const
    {
        std::size_t n = 0;
        const std::int32_t st = zxv_hw_profile_encode(get(), nullptr, 0, &n);
        if (st != ZXV_E_BUFFER) {
            zxv::detail::check(st);
            return {};
        }
        std::vector<std::uint8_t> v(n);
        zxv::detail::check(zxv_hw_profile_encode(get(), v.data(), v.size(), &n));
        v.resize(n);
        return v;
    }
    /// SHA-256 of the canonical encoding: the profile's identity.
    [[nodiscard]] Digest digest() const
    {
        Digest d{};
        zxv::detail::check(zxv_hw_profile_digest(get(), d.data()));
        return d;
    }
    /// Add a node: ids strictly increase, parent first (or hw::root).
    void add_node(std::uint32_t id, std::uint32_t parent, std::uint32_t cls)
    {
        zxv::detail::check(zxv_hw_profile_node_add(p_.get(), id, parent, cls));
    }
    /// Set a capability of a node.
    void set_cap(std::uint32_t node, std::uint32_t key, std::uint64_t value)
    {
        zxv::detail::check(zxv_hw_profile_cap_set(p_.get(), node, key, value));
    }
    /// A capability of a node (UsageError ZXV_E_NOT_FOUND if it lacks it).
    [[nodiscard]] std::uint64_t cap(std::uint32_t node, std::uint32_t key) const
    {
        std::uint64_t v = 0;
        zxv::detail::check(zxv_hw_profile_cap_get(get(), node, key, &v));
        return v;
    }
    /// A capability of a node, or nullopt if it lacks it.
    [[nodiscard]] std::optional<std::uint64_t> find_cap(std::uint32_t node, std::uint32_t key) const
    {
        std::uint64_t v = 0;
        const std::int32_t st = zxv_hw_profile_cap_get(get(), node, key, &v);
        if (st == ZXV_E_NOT_FOUND) return std::nullopt;
        zxv::detail::check(st);
        return v;
    }
    /// Set a node's private extension: at most ext_max opaque bytes that
    /// ZXV carries, encodes and digests but never interprets.
    void set_ext(std::uint32_t node, std::span<const std::uint8_t> ext)
    {
        zxv::detail::check(zxv_hw_profile_ext_set(p_.get(), node, ext.data(), ext.size()));
    }
    /// A node's private extension.
    [[nodiscard]] std::vector<std::uint8_t> ext(std::uint32_t node) const
    {
        std::vector<std::uint8_t> v(ext_max);
        std::size_t n = 0;
        zxv::detail::check(zxv_hw_profile_ext_get(get(), node, v.data(), v.size(), &n));
        v.resize(n);
        return v;
    }
    /// Number of nodes.
    [[nodiscard]] std::uint32_t node_count() const
    {
        std::uint32_t n = 0;
        zxv::detail::check(zxv_hw_profile_count(get(), &n));
        return n;
    }
    /// Node `index` (0 .. node_count()-1).
    [[nodiscard]] Node node(std::uint32_t index) const
    {
        Node n;
        zxv::detail::check(zxv_hw_profile_node(get(), index, &n.id, &n.parent, &n.cls, &n.caps));
        return n;
    }
    /// Every node, in order.
    [[nodiscard]] std::vector<Node> nodes() const
    {
        std::vector<Node> v;
        const std::uint32_t n = node_count();
        v.reserve(n);
        for (std::uint32_t i = 0; i < n; ++i) v.push_back(node(i));
        return v;
    }
    /// Match a target against a host.
    static Match match(const Profile& host, const Profile& target)
    {
        Match m;
        zxv::detail::check(zxv_hw_profile_match(host.get(), target.get(), &m.native, &m.emulated));
        return m;
    }
    /// The pairing of target node `index` (in target order) on `host`.
    static Pair pair(const Profile& host, const Profile& target, std::uint32_t index)
    {
        Pair p;
        std::int32_t k = 0;
        std::uint32_t have = 0, unmet = 0;
        zxv::detail::check(zxv_hw_profile_pair(host.get(), target.get(), index, &p.need_id, &k, &have, &unmet));
        p.kind = static_cast<Pairing>(k);
        if (p.kind == Pairing::native) p.host_id = have;
        if (p.kind == Pairing::emulate) p.unmet_key = unmet;
        return p;
    }

    /// The raw C handle (owned by this object; never destroy it).
    [[nodiscard]] const zxv_hw_profile* native_handle() const noexcept { return p_.get(); }

private:
    struct Del {
        void operator()(zxv_hw_profile* p) const noexcept { zxv_hw_profile_destroy(p); }
    };
    struct Raw {};
    explicit Profile(Raw) noexcept {}
    template <class F>
    static Profile make(F&& f)
    {
        zxv_hw_profile* r = nullptr;
        zxv::detail::check(f(&r));
        Profile p{Raw{}};
        p.p_.reset(r);
        return p;
    }
    [[nodiscard]] const zxv_hw_profile* get() const
    {
        if (!p_) throw UsageError(ZXV_E_ARG, "moved-from hw::Profile");
        return p_.get();
    }
    std::unique_ptr<zxv_hw_profile, Del> p_;
};

// ----- the matrix ----------------------------------------------------------

/// An implementation: write at most out.size() bytes of output for (op, in)
/// and return how many. Throw zxv::Exception to report a status. `in` and
/// `out` are valid only during the call. It runs on the calling thread with
/// the matrix locked; it may call the matrix again for the functions it
/// declared with Matrix::add_dependency.
using ProviderFn =
    std::function<std::size_t(std::uint32_t op, std::span<const std::uint8_t> in, std::span<std::uint8_t> out)>;

/// What Matrix::function_info reports.
struct FunctionInfo {
    std::uint32_t contract = 0; ///< current contract version
    std::uint32_t policy = 0;   ///< hw::policy flags
    std::uint32_t vectors = 0;  ///< golden vectors of the current contract
};

/// What Matrix::provider_info reports.
struct ProviderInfo {
    std::uint32_t fn_id = 0;
    Kind kind = Kind::soft;
    Truth proof = Truth::neither; ///< TRUE serves, FALSE a vector failed, NEITHER no vectors
    std::uint32_t contract = 0;   ///< the contract version it last proved
    std::uint32_t flags = 0;      ///< hw::pf flags
    std::uint32_t throughput_class = 0;
    Power power = Power::unknown;
    std::uint64_t calls = 0;
    std::uint64_t cost_spent = 0;
};

/// The head of an audit log.
struct Log {
    Digest head{};
    std::uint32_t count = 0;
};

/// One audit record (regions and matrices).
struct LogEntry {
    EventKind event = EventKind::staged;
    std::uint32_t reason = 0;  ///< failure code, 0 for none
    std::uint32_t id = 0;      ///< region id (function id for EventKind::contract)
    std::uint32_t version = 0;
    Hexagram tag;              ///< forbidden for rejections and quarantines
    Digest subject{};          ///< SHA-256 of the package header
    Digest hash{};             ///< the record's chain hash
};

namespace detail {
struct ProviderSlot {
    ProviderFn fn;
    static std::int32_t trampoline(void* user, std::uint32_t op, const std::uint8_t* in, std::size_t in_len,
                                   std::uint8_t* out, std::size_t out_cap, std::size_t* out_len) noexcept
    {
        try {
            auto* self = static_cast<ProviderSlot*>(user);
            const std::size_t n = self->fn(op, std::span<const std::uint8_t>(in, in ? in_len : 0),
                                           std::span<std::uint8_t>(out, out ? out_cap : 0));
            if (n > out_cap) return ZXV_E_BUFFER;
            if (out_len) *out_len = n;
            return ZXV_OK;
        } catch (...) {
            return current_status();
        }
    }
};

struct MatrixCore {
    std::shared_ptr<zxv::detail::CapCore> cap;
    zxv_hw_matrix* mx = nullptr;
    std::mutex m;
    std::vector<std::unique_ptr<ProviderSlot>> providers; // freed after the matrix
    MatrixCore(std::shared_ptr<zxv::detail::CapCore> c, zxv_hw_matrix* x) noexcept : cap(std::move(c)), mx(x) {}
    MatrixCore(const MatrixCore&) = delete;
    MatrixCore& operator=(const MatrixCore&) = delete;
    ~MatrixCore() { zxv_hw_matrix_destroy(mx); }
    template <class F>
    void call(F&& f) const
    {
        cap->call([&] { return f(cap->cap, mx); });
    }
};

template <class F>
LogEntry read_entry(F&& f)
{
    LogEntry e;
    std::int32_t ev = 0;
    std::uint32_t tag = 0;
    f(&ev, &e.reason, &e.id, &e.version, &tag, e.subject.data(), e.hash.data());
    if (tag > 63) throw SystemError(ZXV_E_INTERNAL, "audit tag out of range");
    e.event = static_cast<EventKind>(ev);
    e.tag = Hexagram::from_tag(static_cast<std::uint8_t>(tag));
    return e;
}
} // namespace detail

/// The emulation matrix: functions served by interchangeable, proven
/// implementations (needs caps::hw_matrix). A new matrix holds the res_host
/// resource and the standard engines. Thread-safe (the C matrix serializes
/// calls). Move-only; a Region made on it keeps it alive.
class Matrix {
public:
    /// A matrix with the standard engines (needs caps::hw_matrix).
    explicit Matrix(const Capability& cap)
    {
        const auto& c = cap.core();
        zxv_hw_matrix* m = nullptr;
        c->call([&] { return zxv_hw_matrix_create_cap(c->cap, &m); });
        core_ = std::make_shared<detail::MatrixCore>(c, m);
    }
    Matrix(Matrix&&) noexcept = default;            ///< move
    Matrix& operator=(Matrix&&) noexcept = default; ///< move-assign
    Matrix(const Matrix&) = delete;
    Matrix& operator=(const Matrix&) = delete;
    ~Matrix() = default;

    /// Add a function (name: UTF-8, at most 23 bytes; empty for none).
    void add_function(std::uint32_t fn_id, const std::string& name = {})
    {
        core_->call([&](const zxv_cap* c, zxv_hw_matrix* m) {
            return zxv_hw_function_add(c, m, fn_id, name.empty() ? nullptr : name.c_str());
        });
    }
    /// Add a golden vector of the function's current contract (output at most
    /// 512 bytes). Every implementation of it becomes unproven.
    void add_vector(std::uint32_t fn_id, std::uint32_t op, std::span<const std::uint8_t> in,
                    std::span<const std::uint8_t> expect)
    {
        core_->call([&](const zxv_cap* c, zxv_hw_matrix* m) {
            return zxv_hw_vector_add(c, m, fn_id, op, in.data(), in.size(), expect.data(), expect.size());
        });
    }
    /// Start contract `version` (strictly newer, else RollbackRefused): the
    /// vectors are cleared and every implementation must prove itself again.
    void set_contract(std::uint32_t fn_id, std::uint32_t version)
    {
        core_->call([&](const zxv_cap* c, zxv_hw_matrix* m) { return zxv_hw_function_contract(c, m, fn_id, version); });
    }
    /// Placement: hw::policy flags, a latency ceiling and a power budget (0:
    /// none). Clearing policy::key_bearing once set: PolicyDenied.
    void set_policy(std::uint32_t fn_id, std::uint32_t flags, std::uint32_t max_latency_ns = 0,
                    std::uint32_t power_budget_mw = 0)
    {
        core_->call([&](const zxv_cap* c, zxv_hw_matrix* m) {
            return zxv_hw_policy_set(c, m, fn_id, flags, max_latency_ns, power_budget_mw);
        });
    }
    /// Contract version, policy and vector count of a function.
    [[nodiscard]] FunctionInfo function_info(std::uint32_t fn_id) const
    {
        FunctionInfo i;
        core_->call([&](const zxv_cap* c, zxv_hw_matrix* m) {
            return zxv_hw_function_info(c, m, fn_id, &i.contract, &i.policy, &i.vectors);
        });
        return i;
    }
    /// Add a resource (a component implementations run on).
    void add_resource(std::uint32_t res_id)
    {
        core_->call([&](const zxv_cap* c, zxv_hw_matrix* m) { return zxv_hw_resource_add(c, m, res_id); });
    }
    /// Set a resource's state.
    void set_resource(std::uint32_t res_id, ResourceState state)
    {
        core_->call([&](const zxv_cap* c, zxv_hw_matrix* m) {
            return zxv_hw_resource_set(c, m, res_id, static_cast<std::int32_t>(state));
        });
    }
    /// Add an implementation of `fn_id` on resource `res_id`; returns its
    /// index. It serves only after attest() proves it.
    std::uint32_t add_provider(std::uint32_t fn_id, std::uint32_t res_id, Kind kind, std::uint32_t cost, ProviderFn fn)
    {
        if (!fn) throw UsageError(ZXV_E_ARG, "provider function is empty");
        auto slot = std::make_unique<detail::ProviderSlot>(detail::ProviderSlot{std::move(fn)});
        std::uint32_t idx = 0;
        std::lock_guard lk(core_->m);
        core_->call([&](const zxv_cap* c, zxv_hw_matrix* m) {
            return zxv_hw_provider_add(c, m, fn_id, res_id, static_cast<std::int32_t>(kind), cost,
                                       &detail::ProviderSlot::trampoline, slot.get(), &idx);
        });
        core_->providers.push_back(std::move(slot));
        return idx;
    }
    /// Declare an implementation's traits: typical latency per call, power
    /// while serving, throughput class 0 (slowest) .. 15.
    void set_traits(std::uint32_t provider, std::uint32_t latency_ns, std::uint32_t power_mw,
                    std::uint32_t throughput_class)
    {
        core_->call([&](const zxv_cap* c, zxv_hw_matrix* m) {
            return zxv_hw_provider_traits(c, m, provider, latency_ns, power_mw, throughput_class);
        });
    }
    /// Declare that an implementation calls `dep_fn_id`.
    void add_dependency(std::uint32_t provider, std::uint32_t dep_fn_id)
    {
        core_->call([&](const zxv_cap* c, zxv_hw_matrix* m) { return zxv_hw_provider_dep(c, m, provider, dep_fn_id); });
    }
    /// Enable or disable an implementation.
    void enable(std::uint32_t provider, bool on)
    {
        core_->call([&](const zxv_cap* c, zxv_hw_matrix* m) { return zxv_hw_provider_enable(c, m, provider, on ? 1 : 0); });
    }
    /// Run the golden vectors: TRUE (serves), FALSE (a vector failed) or
    /// NEITHER (no vectors).
    Truth attest(std::uint32_t provider)
    {
        std::int32_t lp = 0;
        core_->call([&](const zxv_cap* c, zxv_hw_matrix* m) { return zxv_hw_attest(c, m, provider, &lp); });
        return truth_from_c(lp);
    }
    /// Number of implementations (indexed 0 .. n-1).
    [[nodiscard]] std::uint32_t provider_count() const
    {
        std::uint32_t n = 0;
        core_->call([&](const zxv_cap* c, zxv_hw_matrix* m) { return zxv_hw_provider_count(c, m, &n); });
        return n;
    }
    /// Everything about implementation `provider`.
    [[nodiscard]] ProviderInfo provider_info(std::uint32_t provider) const
    {
        ProviderInfo i;
        std::int32_t kind = 0, lp = 0;
        std::uint32_t pwr = 0;
        core_->call([&](const zxv_cap* c, zxv_hw_matrix* m) {
            return zxv_hw_provider_info(c, m, provider, &i.fn_id, &kind, &lp, &i.contract, &i.flags,
                                        &i.throughput_class, &pwr, &i.calls, &i.cost_spent);
        });
        i.kind = static_cast<Kind>(kind);
        i.proof = truth_from_c(lp);
        i.power = static_cast<Power>(pwr);
        return i;
    }
    /// Every implementation, in index order.
    [[nodiscard]] std::vector<ProviderInfo> providers() const
    {
        std::vector<ProviderInfo> v;
        const std::uint32_t n = provider_count();
        v.reserve(n);
        for (std::uint32_t i = 0; i < n; ++i) v.push_back(provider_info(i));
        return v;
    }
    /// The index of the first implementation of `fn_id`, if any.
    [[nodiscard]] std::optional<std::uint32_t> find_provider(std::uint32_t fn_id) const
    {
        const std::uint32_t n = provider_count();
        for (std::uint32_t i = 0; i < n; ++i)
            if (provider_info(i).fn_id == fn_id) return i;
        return std::nullopt;
    }
    /// The implementation serving `fn_id` now (NoProvider if none).
    [[nodiscard]] std::uint32_t bound(std::uint32_t fn_id) const
    {
        std::uint32_t p = 0;
        core_->call([&](const zxv_cap* c, zxv_hw_matrix* m) { return zxv_hw_bound(c, m, fn_id, &p); });
        return p;
    }
    /// Call `fn_id` with an output of at most call_out_default bytes.
    std::vector<std::uint8_t> call(std::uint32_t fn_id, std::uint32_t op, std::span<const std::uint8_t> in)
    {
        std::vector<std::uint8_t> out(call_out_default);
        out.resize(call_into(fn_id, op, in, out));
        return out;
    }
    /// Call `fn_id`, writing into `out`; returns the output length.
    std::size_t call_into(std::uint32_t fn_id, std::uint32_t op, std::span<const std::uint8_t> in,
                          std::span<std::uint8_t> out)
    {
        std::size_t n = 0;
        core_->call([&](const zxv_cap* c, zxv_hw_matrix* m) {
            return zxv_hw_call(c, m, fn_id, op, in.data(), in.size(), out.data(), out.size(), &n);
        });
        return n;
    }
    /// call() that also runs the next-best implementation and compares;
    /// `checked` reports whether a second one ran. On a mismatch both stop
    /// serving: Disputed.
    std::vector<std::uint8_t> shadow(std::uint32_t fn_id, std::uint32_t op, std::span<const std::uint8_t> in,
                                     bool* checked = nullptr)
    {
        std::vector<std::uint8_t> out(call_out_default);
        std::size_t n = 0;
        std::int32_t ck = 0;
        core_->call([&](const zxv_cap* c, zxv_hw_matrix* m) {
            return zxv_hw_shadow(c, m, fn_id, op, in.data(), in.size(), out.data(), out.size(), &n, &ck);
        });
        out.resize(n);
        if (checked) *checked = ck != 0;
        return out;
    }
    /// Declare `fn_id` pure with a memo of `entries` (a power of two, at most
    /// 65536; 0 removes it); returns {hits, misses}. Key-bearing functions:
    /// PolicyDenied.
    std::pair<std::uint64_t, std::uint64_t> memo(std::uint32_t fn_id, std::uint32_t entries)
    {
        std::uint64_t h = 0, mi = 0;
        core_->call([&](const zxv_cap* c, zxv_hw_matrix* m) { return zxv_hw_memo(c, m, fn_id, entries, &h, &mi); });
        return {h, mi};
    }
    /// Trust a release key (pk_bytes; at most 4) for contract packages
    /// (needs caps::hw_reconf).
    void trust_key(std::span<const std::uint8_t> pk)
    {
        zxv::detail::need_size(pk, pk_bytes, "release public key");
        core_->call([&](const zxv_cap* c, zxv_hw_matrix* m) { return zxv_hw_matrix_trust_key(c, m, pk.data()); });
    }
    /// Apply a signed ImageKind::vectors package: verifies both signatures,
    /// requires a newer version and svn >= the floor (RollbackRefused),
    /// installs the vectors, proves every implementation again and records
    /// it. Returns how many implementations prove the new contract (needs
    /// caps::hw_reconf).
    std::uint32_t apply_contract(std::span<const std::uint8_t> pkg)
    {
        std::uint32_t proven = 0;
        core_->call([&](const zxv_cap* c, zxv_hw_matrix* m) {
            return zxv_hw_contract_apply(c, m, pkg.data(), pkg.size(), &proven);
        });
        return proven;
    }
    /// The audit log's head and length (FatalError if the chain does not verify).
    [[nodiscard]] Log log() const
    {
        Log l;
        core_->call([&](const zxv_cap* c, zxv_hw_matrix* m) { return zxv_hw_matrix_log(c, m, l.head.data(), &l.count); });
        return l;
    }
    /// Audit record `index` (0 = oldest).
    [[nodiscard]] LogEntry log_entry(std::uint32_t index) const
    {
        return detail::read_entry([&](auto... out) {
            core_->call([&](const zxv_cap* c, zxv_hw_matrix* m) { return zxv_hw_matrix_log_entry(c, m, index, out...); });
        });
    }
    /// Every audit record, oldest first.
    [[nodiscard]] std::vector<LogEntry> log_entries() const
    {
        std::vector<LogEntry> v;
        const std::uint32_t n = log().count;
        for (std::uint32_t i = 0; i < n; ++i) v.push_back(log_entry(i));
        return v;
    }

    /// The raw C handle (owned by this object; never destroy it).
    [[nodiscard]] zxv_hw_matrix* native_handle() const noexcept { return core_->mx; }
    /// Internal: the shared core (kept alive by regions).
    [[nodiscard]] const std::shared_ptr<detail::MatrixCore>& core() const noexcept { return core_; }

private:
    std::shared_ptr<detail::MatrixCore> core_;
};

// ----- release keys and packages -------------------------------------------

/// A release key pair: the public key (pk_bytes) and both secret halves,
/// each a zxv::SecretKey (wiped with zxv_secure_zero when destroyed).
struct ReleaseKey {
    std::vector<std::uint8_t> public_key; ///< ML-DSA-87 public key || SLH-DSA public key
    SecretKey mldsa{mldsa_sk_bytes};      ///< ML-DSA-87 secret half
    SecretKey slhdsa{slhdsa_sk_bytes};    ///< SLH-DSA-SHAKE-256s secret half
};

/// The header fields of a package.
struct ImageSpec {
    ImageKind kind = ImageKind::partial;
    std::uint32_t target_class = 0;      ///< hardware class the image is for (0 for a contract package)
    std::optional<Digest> target_digest; ///< target profile digest (nullopt: zeros)
    std::uint32_t fn_id = 0;   ///< function the region serves, or whose contract a vectors package replaces
    std::uint32_t version = 0;
    std::uint32_t svn = 0;     ///< anti-rollback floor
};

/// Release keys and signed packages (needs caps::crypto_sign). SLH-DSA
/// signing takes seconds: building is a release step.
class Release {
public:
    /// Packaging through `cap`.
    explicit Release(const Capability& cap) : cap_(cap.core()) {}

    /// The length of a package with a `payload_len`-byte payload.
    static constexpr std::size_t package_len(std::size_t payload_len) noexcept
    {
        return header_bytes + payload_len + sig_bytes;
    }
    /// A fresh release key pair.
    [[nodiscard]] ReleaseKey keygen() const
    {
        ReleaseKey k;
        k.public_key.resize(pk_bytes);
        cap_->call([&] { return zxv_hw_keygen(cap_->cap, k.public_key.data(), k.mldsa.data(), k.slhdsa.data()); });
        return k;
    }
    /// Build and sign a package: header || payload || signatures.
    [[nodiscard]] std::vector<std::uint8_t> build(const ImageSpec& spec, std::span<const std::uint8_t> payload,
                                                  const ReleaseKey& key) const
    {
        zxv::detail::need_size(key.mldsa.bytes(), mldsa_sk_bytes, "ML-DSA secret key");
        zxv::detail::need_size(key.slhdsa.bytes(), slhdsa_sk_bytes, "SLH-DSA secret key");
        std::vector<std::uint8_t> buf(package_len(payload.size()));
        std::size_t n = 0;
        cap_->call([&] {
            return zxv_hw_image_build(cap_->cap, static_cast<std::uint32_t>(spec.kind), spec.target_class,
                                      spec.target_digest ? spec.target_digest->data() : nullptr, spec.fn_id,
                                      spec.version, spec.svn, payload.data(), payload.size(),
                                      key.mldsa.bytes().data(), key.slhdsa.bytes().data(), buf.data(), buf.size(),
                                      &n);
        });
        buf.resize(n);
        return buf;
    }

private:
    std::shared_ptr<zxv::detail::CapCore> cap_;
};

// ----- regions -------------------------------------------------------------

/// The device side of a region: a two-slot reconfigurable unit. Each
/// operation runs on the thread of the region call that needs it, with the
/// region (and its matrix) locked. Throw zxv::Exception to report a status.
struct Device {
    /// Load `payload` (valid only during the call) into `slot`.
    std::function<void(std::uint32_t slot, std::span<const std::uint8_t> payload)> load;
    /// The SHA-256 of what `slot` really holds.
    std::function<Digest(std::uint32_t slot)> readback;
    /// Switch the unit to `slot`.
    std::function<void(std::uint32_t slot)> activate;
};

/// One region event.
struct Event {
    std::uint32_t region_id = 0;
    EventKind kind = EventKind::staged;
    std::uint32_t reason = 0; ///< failure code, 0 for none
    std::uint32_t version = 0;
};

/// What Region::info reports.
struct RegionInfo {
    RegionState state = RegionState::empty;
    std::uint32_t version = 0; ///< active image version
    std::uint32_t floor = 0;   ///< anti-rollback floor
    std::uint32_t quarantined = 0;
};

namespace detail {
struct RegionCore {
    std::shared_ptr<zxv::detail::CapCore> cap;
    std::shared_ptr<MatrixCore> mx; // the C region references the matrix
    zxv_hw_region* rg = nullptr;
    Device dev;
    std::mutex m;
    std::vector<std::shared_ptr<std::function<void(const Event&)>>> handlers;

    RegionCore(std::shared_ptr<zxv::detail::CapCore> c, std::shared_ptr<MatrixCore> x, Device d)
        : cap(std::move(c)), mx(std::move(x)), dev(std::move(d))
    {
    }
    RegionCore(const RegionCore&) = delete;
    RegionCore& operator=(const RegionCore&) = delete;
    ~RegionCore()
    {
        if (rg) zxv_hw_region_destroy(rg);
    }
    template <class F>
    void call(F&& f) const
    {
        cap->call([&] { return f(cap->cap, rg); });
    }

    static std::int32_t load(void* user, std::uint32_t slot, const std::uint8_t* p, std::size_t len) noexcept
    {
        try {
            static_cast<RegionCore*>(user)->dev.load(slot, std::span<const std::uint8_t>(p, p ? len : 0));
            return ZXV_OK;
        } catch (...) {
            return current_status();
        }
    }
    static std::int32_t readback(void* user, std::uint32_t slot, std::uint8_t digest[32]) noexcept
    {
        try {
            const Digest d = static_cast<RegionCore*>(user)->dev.readback(slot);
            std::copy(d.begin(), d.end(), digest);
            return ZXV_OK;
        } catch (...) {
            return current_status();
        }
    }
    static std::int32_t activate(void* user, std::uint32_t slot) noexcept
    {
        try {
            static_cast<RegionCore*>(user)->dev.activate(slot);
            return ZXV_OK;
        } catch (...) {
            return current_status();
        }
    }
    static void event(void* user, std::uint32_t region_id, std::int32_t ev, std::uint32_t reason,
                      std::uint32_t version) noexcept
    {
        try {
            auto* self = static_cast<RegionCore*>(user);
            std::vector<std::shared_ptr<std::function<void(const Event&)>>> hs;
            {
                std::lock_guard lk(self->m);
                hs = self->handlers;
            }
            const Event e{region_id, static_cast<EventKind>(ev), reason, version};
            for (auto& h : hs) {
                try {
                    (*h)(e);
                } catch (...) { // never unwind through C
                }
            }
        } catch (...) {
        }
    }
};
} // namespace detail

/// A reconfigurable region updated with signed images (needs
/// caps::hw_reconf). stage() verifies an image (ML-DSA-87 and SLH-DSA, both
/// required) and loads it into the inactive slot, trial() switches to it and
/// proves it against the golden vectors of the matrix implementation it
/// serves, commit() makes it permanent. Every step is an Event and a record
/// in a hash-chained audit log; a failing image is rolled back and
/// quarantined forever. When the log fills, the region halts and the
/// context is poisoned (fail closed). Thread-safe; move-only. It keeps its
/// matrix alive.
class Region {
public:
    /// Region `region_id` for hardware class `target_class`. `matrix` and
    /// `provider` name the FABRIC or MICRO implementation whose golden
    /// vectors test each new image (matrix nullptr: no test). `log_cap` is
    /// the audit log size (1 .. 2^20).
    Region(const Capability& cap, const Matrix* matrix, std::uint32_t provider, std::uint32_t region_id,
           std::uint32_t target_class, std::optional<Digest> target_digest, Device device, std::uint32_t log_cap)
    {
        if (!device.load || !device.readback || !device.activate)
            throw UsageError(ZXV_E_ARG, "hw::Device needs load, readback and activate");
        auto core = std::make_shared<detail::RegionCore>(cap.core(), matrix ? matrix->core() : nullptr,
                                                         std::move(device));
        zxv_hw_region* r = nullptr;
        core->cap->call([&] {
            return zxv_hw_region_create_cap(core->cap->cap, core->mx ? core->mx->mx : nullptr, provider, region_id,
                                            target_class, target_digest ? target_digest->data() : nullptr,
                                            &detail::RegionCore::load, &detail::RegionCore::readback,
                                            &detail::RegionCore::activate, core.get(), log_cap, &r);
        });
        core->rg = r;
        core->call([&](const zxv_cap* c, zxv_hw_region* g) {
            return zxv_hw_region_events(c, g, &detail::RegionCore::event, core.get());
        });
        core_ = std::move(core);
    }
    Region(Region&&) noexcept = default;            ///< move
    Region& operator=(Region&&) noexcept = default; ///< move-assign
    Region(const Region&) = delete;
    Region& operator=(const Region&) = delete;
    ~Region() = default;

    /// Register a handler for every event, delivered after each recorded
    /// step once the region call has released its locks, in order, on the
    /// thread of that call. Exceptions from it are swallowed.
    void on_event(std::function<void(const Event&)> f)
    {
        std::lock_guard lk(core_->m);
        core_->handlers.push_back(std::make_shared<std::function<void(const Event&)>>(std::move(f)));
    }
    /// Trust a release key (pk_bytes; at most 4 per region).
    void trust_key(std::span<const std::uint8_t> pk)
    {
        zxv::detail::need_size(pk, pk_bytes, "release public key");
        core_->call([&](const zxv_cap* c, zxv_hw_region* g) { return zxv_hw_region_trust_key(c, g, pk.data()); });
    }
    /// Verify an image and load it into the inactive slot. Refusals
    /// (VerificationFailed, RollbackRefused) are recorded; a read-back
    /// mismatch is Quarantined and blocks the image.
    void stage(std::span<const std::uint8_t> image)
    {
        core_->call([&](const zxv_cap* c, zxv_hw_region* g) {
            return zxv_hw_region_stage(c, g, image.data(), image.size());
        });
    }
    /// Switch to the staged image and prove it; passing starts a probation
    /// of `ticks`. A failure is Quarantined with the previous image (or S0)
    /// back in place.
    void trial(std::uint32_t ticks)
    {
        core_->call([&](const zxv_cap* c, zxv_hw_region* g) { return zxv_hw_region_trial(c, g, ticks); });
    }
    /// Make the tested image permanent and raise the floor.
    void commit()
    {
        core_->call([&](const zxv_cap* c, zxv_hw_region* g) { return zxv_hw_region_commit(c, g); });
    }
    /// Return to the previous image (or S0).
    void rollback()
    {
        core_->call([&](const zxv_cap* c, zxv_hw_region* g) { return zxv_hw_region_rollback(c, g); });
    }
    /// One health tick; an expiring probation rolls back (Quarantined).
    void tick()
    {
        core_->call([&](const zxv_cap* c, zxv_hw_region* g) { return zxv_hw_region_tick(c, g); });
    }
    /// State, active version, floor and quarantine count.
    [[nodiscard]] RegionInfo info() const
    {
        RegionInfo i;
        std::int32_t st = 0;
        core_->call([&](const zxv_cap* c, zxv_hw_region* g) {
            return zxv_hw_region_info(c, g, &st, &i.version, &i.floor, &i.quarantined);
        });
        i.state = static_cast<RegionState>(st);
        return i;
    }
    /// The audit log's head and length (FatalError if the chain does not verify).
    [[nodiscard]] Log log() const
    {
        Log l;
        core_->call([&](const zxv_cap* c, zxv_hw_region* g) { return zxv_hw_region_log(c, g, l.head.data(), &l.count); });
        return l;
    }
    /// Audit record `index` (0 = oldest).
    [[nodiscard]] LogEntry log_entry(std::uint32_t index) const
    {
        return detail::read_entry([&](auto... out) {
            core_->call([&](const zxv_cap* c, zxv_hw_region* g) { return zxv_hw_region_log_entry(c, g, index, out...); });
        });
    }
    /// Every audit record, oldest first.
    [[nodiscard]] std::vector<LogEntry> log_entries() const
    {
        std::vector<LogEntry> v;
        const std::uint32_t n = log().count;
        for (std::uint32_t i = 0; i < n; ++i) v.push_back(log_entry(i));
        return v;
    }

    /// The raw C handle (owned by this object; never destroy it).
    [[nodiscard]] zxv_hw_region* native_handle() const noexcept { return core_->rg; }

private:
    std::shared_ptr<detail::RegionCore> core_;
};

} // namespace hw

// ===========================================================================
// ABI 1.5: shared helpers
// ===========================================================================
//
// ABI 1.5 adds eight groups, each in its own namespace:
//
//   zxv::treaty   COBOL copybook treaties and their Sutra text (pure)
//   zxv::cid      IPFS content identifiers, byte-identical to Kubo (pure)
//   zxv::aipi     AIPI function, call and claim records; an offline node
//   zxv::coin     the VFV Fibonacci coin ladder (pure)
//   zxv::token    swarm token tiers, octane, Fibonacci codes; a wallet
//   zxv::fee      the 0.08889% assurance fee and its four buckets (pure)
//   zxv::vhub     a demo V-Bill hub: test currencies, signed pegs, swaps
//   zxv::freight  erasure-coded 168-row freight packets and op streams
//   zxv::web4     Web 4 agents, signed manifests, consent, envelopes
//   zxv::paradox  the paradox engine: kinds, operators, logs, graphs
//
// Everything is integers (64-bit values stay 64-bit; no floating point).
// Variable-length results use the C two-call buffer pattern, hidden here:
// every function returns a std::vector or std::string. Groups that belong
// to a context take a Capability at construction, keep it, and fail closed
// with FatalError once the context halts. Every handle type is move-only;
// using a moved-from handle throws UsageError (ZXV_E_ARG) instead of
// reaching C.

namespace detail {

/// Deleter for a C handle with a `void destroy(T*)` function.
template <class T, void (*Destroy)(T*)>
struct HandleDel {
    void operator()(T* p) const noexcept { Destroy(p); }
};
/// A uniquely owned C handle.
template <class T, void (*Destroy)(T*)>
using Owned = std::unique_ptr<T, HandleDel<T, Destroy>>;

/// The handle, or UsageError when the owning object was moved from.
template <class T>
T* live(T* p, const char* what)
{
    if (!p) throw UsageError(ZXV_E_ARG, std::string("use of a moved-from ") + what);
    return p;
}

/// "R4 for bytes": try a buffer of `guess` bytes, grow it once to the
/// length the library reports, and return exactly the result's bytes.
template <class F>
std::vector<std::uint8_t> read_bytes(F&& call, CtxCore* core = nullptr, std::size_t guess = 256)
{
    std::vector<std::uint8_t> v(guess);
    std::size_t need = 0;
    std::int32_t st = call(v.empty() ? nullptr : v.data(), v.size(), &need);
    if (st == ZXV_E_BUFFER) {
        v.assign(need, 0);
        st = call(v.empty() ? nullptr : v.data(), v.size(), &need);
    }
    check(st, core);
    v.resize(need);
    return v;
}

/// A NUL-terminated view of `s`, refusing embedded NULs (which C would
/// silently treat as the end of the text).
inline const char* cstr(const std::string& s, const char* what)
{
    if (s.find('\0') != std::string::npos) throw UsageError(ZXV_E_ARG, std::string(what) + " contains a NUL byte");
    return s.c_str();
}

/// A copy of `b` as a fixed array, after checking its size.
template <std::size_t N>
std::array<std::uint8_t, N> fixed(std::span<const std::uint8_t> b, const char* what)
{
    need_size(b, N, what);
    std::array<std::uint8_t, N> a{};
    std::copy(b.begin(), b.end(), a.begin());
    return a;
}

/// Little-endian readers for the record layouts the C header documents.
inline std::uint32_t le32(const std::uint8_t* p) noexcept
{
    return static_cast<std::uint32_t>(p[0]) | static_cast<std::uint32_t>(p[1]) << 8 |
           static_cast<std::uint32_t>(p[2]) << 16 | static_cast<std::uint32_t>(p[3]) << 24;
}
inline std::uint64_t le64(const std::uint8_t* p) noexcept
{
    return static_cast<std::uint64_t>(le32(p)) | static_cast<std::uint64_t>(le32(p + 4)) << 32;
}
inline Digest32 digest_at(const std::uint8_t* p) noexcept
{
    Digest32 d{};
    std::copy(p, p + 32, d.begin());
    return d;
}

/// The capability core a 1.5 handle is created from, kept alive by it.
inline const std::shared_ptr<CapCore>& cap_of(const Capability& cap)
{
    if (!cap.core()) throw UsageError(ZXV_E_ARG, "use of a moved-from zxv::Capability");
    return cap.core();
}

} // namespace detail

// ===========================================================================
// Treaties (ABI 1.5)
// ===========================================================================

/// ABI 1.5: treaties. A treaty is one batch of settlement work: who takes
/// part, what each party deposits and may claim back, how the batch's
/// dividend is shared (each party's weight num/den), and who owes whom. It
/// has two exact spellings of the same treaty:
///
///   * the RECORD, a fixed treaty::record_bytes-byte COBOL copybook record
///     (ZXVTRTY.cpy, all DISPLAY text, so plain ASCII everywhere), and
///   * the SUTRA, compact text such as
///
///         TREATY CORRIDOR-7 USD SETL
///         PARTY ACME 5000 4000 1/2
///         PARTY BETA 3000 3000 1/2
///         OWE ACME BETA 1200
///         END
///
/// Both are canonical: parse then emit gives the input back byte for byte,
/// and the record and Sutra forms of one treaty compare equal. Every parse
/// and emit checks R1 (weights in lowest terms adding up to exactly 1), R2
/// (claim <= deposit), R3 (amounts fit PIC S9(18)) and R4 (1..16 distinct
/// parties, 0..64 obligations between two different known parties). A
/// refusal is TreatyError, carrying the treaty::Fault and, for a parse, the
/// byte offset of the first fault.
///
/// Everything here is pure: no context, no capability. Running a treaty
/// works in a private scratch engine freed before the call returns; its
/// numbers are a deterministic report, not a payment.
///
/// Honest limits: a treaty is a batch description, not a contract in law
/// (signatures, consent and authority live elsewhere); one asset per
/// treaty; the dividend shares are reported only, nothing is paid out.
namespace treaty {

inline constexpr std::size_t record_bytes = ZXV_TREATY_REC_LEN;        ///< ZXVTRTY.cpy record length
inline constexpr std::size_t result_record_bytes = ZXV_TREATY_RESULT_LEN; ///< ZXVRSLT.cpy record length
inline constexpr std::size_t sutra_max = ZXV_TREATY_SUTRA_MAX;         ///< longest canonical Sutra text
inline constexpr std::uint32_t max_parties = ZXV_TREATY_MAX_PARTIES;   ///< parties per treaty
inline constexpr std::uint32_t max_obligations = ZXV_TREATY_MAX_OWES;  ///< obligations per treaty
inline constexpr std::size_t name_max = ZXV_TREATY_NAME_MAX;           ///< treaty name: A-Z 0-9 '-', starts with a letter
inline constexpr std::size_t id_max = ZXV_TREATY_ID_MAX;               ///< party id length, same alphabet
inline constexpr std::int64_t pic18_max = ZXV_TREATY_PIC18_MAX;        ///< largest PIC S9(18) magnitude
inline constexpr std::uint32_t weight_max = ZXV_TREATY_WEIGHT_MAX;     ///< largest weight term, PIC 9(9)

/// What a treaty asks the engine to do.
enum class Op : std::int32_t {
    none = 0,                   ///< an empty treaty
    net = ZXV_TREATY_OP_NET,    ///< net the obligations only
    settle = ZXV_TREATY_OP_SETL ///< net, post the legs with the fee, pay the claims
};

/// Why a treaty was refused (TreatyError::fault_code()).
enum class Fault : std::int32_t {
    none = ZXV_TREATY_FAULT_NONE,     ///< valid
    magic = ZXV_TREATY_FAULT_MAGIC,   ///< record does not start with "ZXT1"
    syntax = ZXV_TREATY_FAULT_SYNTAX, ///< Sutra text outside the grammar (also non-ASCII text)
    name = ZXV_TREATY_FAULT_NAME,     ///< treaty name not 1..30 of A-Z 0-9 '-'
    asset = ZXV_TREATY_FAULT_ASSET,   ///< asset not three capital letters
    op = ZXV_TREATY_FAULT_OP,         ///< operation not NET or SETL
    count = ZXV_TREATY_FAULT_COUNT,   ///< party or obligation count out of range
    number = ZXV_TREATY_FAULT_NUMBER, ///< malformed number or outside PIC S9(18)
    party = ZXV_TREATY_FAULT_PARTY,   ///< bad, duplicate or unknown party id
    weight = ZXV_TREATY_FAULT_WEIGHT, ///< R1: weights not in lowest terms or not summing to 1
    claim = ZXV_TREATY_FAULT_CLAIM,   ///< R2: a claim above its deposit
    owe = ZXV_TREATY_FAULT_OWE,       ///< an obligation to self, or not above zero
    pad = ZXV_TREATY_FAULT_PAD,       ///< an unused record slot or field not blank
};

/// A short lower-case name of a fault ("weight"); "unknown" otherwise.
constexpr std::string_view to_string(Fault f) noexcept
{
    switch (f) {
    case Fault::none: return "none";
    case Fault::magic: return "magic";
    case Fault::syntax: return "syntax";
    case Fault::name: return "name";
    case Fault::asset: return "asset";
    case Fault::op: return "op";
    case Fault::count: return "count";
    case Fault::number: return "number";
    case Fault::party: return "party";
    case Fault::weight: return "weight";
    case Fault::claim: return "claim";
    case Fault::owe: return "owe";
    case Fault::pad: return "pad";
    }
    return "unknown";
}

/// The fault a TreatyError carries.
inline Fault fault_of(const TreatyError& e) noexcept { return static_cast<Fault>(e.fault_code()); }

/// One party of a treaty.
struct Party {
    std::string id;                ///< 1..8 of A-Z 0-9 '-', starts with a letter
    std::int64_t deposit = 0;      ///< minor units of the asset deposited (>= 0)
    std::int64_t claim = 0;        ///< minor units it may claim back (<= deposit)
    std::uint32_t weight_num = 0;  ///< dividend weight numerator
    std::uint32_t weight_den = 1;  ///< dividend weight denominator (lowest terms)
};

/// One obligation: party `from` owes party `to` `amount` (0-based indices).
struct Obligation {
    std::uint32_t from = 0;   ///< index of the payer
    std::uint32_t to = 0;     ///< index of the payee
    std::int64_t amount = 0;  ///< minor units (> 0)
};

/// One party's line of a run result.
struct PartyResult {
    std::int64_t net = 0;      ///< what it is owed minus what it owes (gross)
    std::int64_t balance = 0;  ///< NET: its deposit; SETL: what it holds after legs, fee and claim
    std::int64_t dividend = 0; ///< its reported dividend share (0 for NET; reported, not paid)
};

/// One netted leg: party `from` pays party `to` `amount` (0-based indices).
struct Leg {
    std::uint32_t from = 0;  ///< index of the payer
    std::uint32_t to = 0;    ///< index of the payee
    std::int64_t amount = 0; ///< minor units
};

/// What running a treaty returned. Immutable, so any number of threads may
/// read it; independent of the treaty it came from. Move-only.
class Result {
public:
    Result(Result&&) noexcept = default;            ///< move
    Result& operator=(Result&&) noexcept = default; ///< move-assign
    Result(const Result&) = delete;
    Result& operator=(const Result&) = delete;
    ~Result() = default;

    /// Number of parties (treaty order).
    [[nodiscard]] std::uint32_t party_count() const { return summary().parties; }
    /// Number of netted legs (at most parties - 1).
    [[nodiscard]] std::uint32_t leg_count() const { return summary().legs; }
    /// The total assurance fee charged (0 for NET).
    [[nodiscard]] std::int64_t fees() const { return summary().fees; }
    /// Party `index` (treaty order). UsageError (ZXV_E_NOT_FOUND) past the end.
    [[nodiscard]] PartyResult party(std::uint32_t index) const
    {
        PartyResult p;
        detail::check(zxv_treaty_result_party(get(), index, &p.net, &p.balance, &p.dividend));
        return p;
    }
    /// Every party, in treaty order.
    [[nodiscard]] std::vector<PartyResult> parties() const
    {
        std::vector<PartyResult> v;
        for (std::uint32_t i = 0, n = party_count(); i < n; ++i) v.push_back(party(i));
        return v;
    }
    /// Netted leg `index`. UsageError (ZXV_E_NOT_FOUND) past the end.
    [[nodiscard]] Leg leg(std::uint32_t index) const
    {
        Leg l;
        detail::check(zxv_treaty_result_leg(get(), index, &l.from, &l.to, &l.amount));
        return l;
    }
    /// Every netted leg, in order.
    [[nodiscard]] std::vector<Leg> legs() const
    {
        std::vector<Leg> v;
        for (std::uint32_t i = 0, n = leg_count(); i < n; ++i) v.push_back(leg(i));
        return v;
    }
    /// The ZXVRSLT.cpy result record (result_record_bytes bytes; party
    /// numbers in it are 1-based, as the copybook says).
    [[nodiscard]] std::vector<std::uint8_t> record() const
    {
        return detail::read_bytes([&](std::uint8_t* b, std::size_t c, std::size_t* n) {
            return zxv_treaty_result_record(get(), b, c, n);
        }, nullptr, result_record_bytes);
    }
    /// The raw C handle (owned by this object; never destroy it).
    [[nodiscard]] const zxv_treaty_result* native_handle() const noexcept { return r_.get(); }

private:
    friend class Treaty;
    struct Summary {
        std::uint32_t parties = 0, legs = 0;
        std::int64_t fees = 0;
    };
    explicit Result(zxv_treaty_result* r) noexcept : r_(r) {}
    [[nodiscard]] const zxv_treaty_result* get() const { return detail::live(r_.get(), "treaty::Result"); }
    [[nodiscard]] Summary summary() const
    {
        Summary s;
        detail::check(zxv_treaty_result_summary(get(), &s.parties, &s.legs, &s.fees));
        return s;
    }
    detail::Owned<zxv_treaty_result, zxv_treaty_result_destroy> r_;
};

/// One treaty. A default-constructed Treaty is empty (no parties) and fails
/// validation until a parse fills it. NOT thread-safe: use it from one
/// thread at a time (concurrent const calls are fine). Move-only.
///
///     auto t = zxv::treaty::Treaty::from_sutra(text);   // TreatyError on a bad treaty
///     auto rec = t.record();                             // the same treaty as a copybook record
///     auto r = t.run();                                  // a deterministic report
class Treaty {
public:
    /// An empty treaty.
    Treaty()
    {
        zxv_treaty* t = nullptr;
        detail::check(zxv_treaty_create(&t));
        t_.reset(t);
    }
    Treaty(Treaty&&) noexcept = default;            ///< move
    Treaty& operator=(Treaty&&) noexcept = default; ///< move-assign
    Treaty(const Treaty&) = delete;
    Treaty& operator=(const Treaty&) = delete;
    ~Treaty() = default;

    /// Parse Sutra text (at most sutra_max bytes). TreatyError with the
    /// fault and its byte offset when it is refused.
    static Treaty from_sutra(const std::string& text)
    {
        Treaty t;
        t.parse_sutra(text);
        return t;
    }
    /// Parse a copybook record of exactly record_bytes bytes (UsageError
    /// ZXV_E_ARG for another length; TreatyError when it is refused).
    static Treaty from_record(std::span<const std::uint8_t> record)
    {
        Treaty t;
        t.parse_record(record);
        return t;
    }

    /// Replace the contents by parsed Sutra text. On any failure the treaty
    /// is unchanged. TreatyError (with fault and offset), UsageError
    /// (ZXV_E_UTF8, ZXV_E_TOO_LONG).
    void parse_sutra(const std::string& text)
    {
        const char* s = detail::cstr(text, "Sutra text");
        parse([&](std::uint32_t* off, std::int32_t* f) { return zxv_treaty_parse_sutra(get(), s, off, f); });
    }
    /// Replace the contents by a parsed copybook record. On any failure the
    /// treaty is unchanged.
    void parse_record(std::span<const std::uint8_t> record)
    {
        parse([&](std::uint32_t* off, std::int32_t* f) {
            return zxv_treaty_parse_record(get(), record.data(), record.size(), off, f);
        });
    }
    /// Empty it again.
    void clear() { detail::check(zxv_treaty_clear(get())); }

    /// The first rule this treaty breaks, or Fault::none. Never throws for
    /// an invalid treaty (an empty one reads Fault::name).
    [[nodiscard]] Fault fault() const
    {
        std::int32_t f = 0;
        const std::int32_t st = zxv_treaty_validate(get(), &f);
        if (st != ZXV_E_TREATY) detail::check(st);
        return static_cast<Fault>(f);
    }
    /// True if the treaty validates.
    [[nodiscard]] bool is_valid() const { return fault() == Fault::none; }
    /// Throw TreatyError (with its fault) unless the treaty validates.
    void validate() const
    {
        call([&] { return zxv_treaty_validate(get(), nullptr); });
    }

    /// The canonical Sutra text. TreatyError if it does not validate.
    [[nodiscard]] std::string sutra() const
    {
        std::string s;
        call([&] {
            std::size_t need = 0;
            std::int32_t st = zxv_treaty_emit_sutra(get(), nullptr, 0, &need);
            if (st != ZXV_E_BUFFER) return st;
            s.assign(need, '\0');
            st = zxv_treaty_emit_sutra(get(), s.data(), need + 1, &need);
            if (st == ZXV_OK) s.resize(need);
            return st;
        });
        return s;
    }
    /// The canonical copybook record (record_bytes bytes). TreatyError if it
    /// does not validate.
    [[nodiscard]] std::vector<std::uint8_t> record() const
    {
        std::vector<std::uint8_t> v(record_bytes);
        std::size_t n = 0;
        call([&] { return zxv_treaty_emit_record(get(), v.data(), v.size(), &n); });
        v.resize(n);
        return v;
    }

    /// The treaty name ("" when empty).
    [[nodiscard]] std::string name() const
    {
        return detail::read_text([&](char* b, std::size_t c, std::size_t* n) { return zxv_treaty_name(get(), b, c, n); });
    }
    /// The asset, three capital letters ("" when empty).
    [[nodiscard]] std::string asset() const
    {
        return detail::read_text([&](char* b, std::size_t c, std::size_t* n) { return zxv_treaty_asset(get(), b, c, n); });
    }
    /// The operation (Op::none when empty).
    [[nodiscard]] Op op() const { return static_cast<Op>(header().op); }
    /// Number of parties.
    [[nodiscard]] std::uint32_t party_count() const { return header().parties; }
    /// Number of obligations.
    [[nodiscard]] std::uint32_t obligation_count() const { return header().owes; }
    /// Party `index` (0-based, treaty order). UsageError (ZXV_E_NOT_FOUND)
    /// past the last party.
    [[nodiscard]] Party party(std::uint32_t index) const
    {
        Party p;
        p.id = detail::read_text(
            [&](char* b, std::size_t c, std::size_t* n) { return zxv_treaty_party_id(get(), index, b, c, n); });
        detail::check(zxv_treaty_party(get(), index, &p.deposit, &p.claim, &p.weight_num, &p.weight_den));
        return p;
    }
    /// Every party, in treaty order.
    [[nodiscard]] std::vector<Party> parties() const
    {
        std::vector<Party> v;
        for (std::uint32_t i = 0, n = party_count(); i < n; ++i) v.push_back(party(i));
        return v;
    }
    /// Obligation `index` (0-based). UsageError (ZXV_E_NOT_FOUND) past the end.
    [[nodiscard]] Obligation obligation(std::uint32_t index) const
    {
        Obligation o;
        detail::check(zxv_treaty_owe(get(), index, &o.from, &o.to, &o.amount));
        return o;
    }
    /// Every obligation, in order.
    [[nodiscard]] std::vector<Obligation> obligations() const
    {
        std::vector<Obligation> v;
        for (std::uint32_t i = 0, n = obligation_count(); i < n; ++i) v.push_back(obligation(i));
        return v;
    }

    /// Run the treaty in a fresh private engine: fund each deposit, net the
    /// obligations, and for SETL post the legs with the 0.08889% assurance
    /// fee (all or nothing), pay each claim back and report each party's
    /// dividend share. Nothing persists and no capability is needed; the
    /// same treaty always gives the same result. Takes about 500 KB of heap
    /// for the call. TreatyError (invalid), InsufficientFunds (a payer
    /// cannot cover its legs plus the fee, or a claim exceeds what is left),
    /// UsageError ZXV_E_RANGE (a position leaves PIC S9(18) or an amount
    /// exceeds 2^59), SystemError ZXV_E_INTERNAL (a kernel bug).
    [[nodiscard]] Result run() const
    {
        zxv_treaty_result* r = nullptr;
        call([&] { return zxv_treaty_run(get(), &r); });
        return Result(r);
    }

    /// True if both hold the same treaty (every field, every slot), whatever
    /// spelling each was parsed from. Two empty treaties are equal.
    friend bool operator==(const Treaty& a, const Treaty& b)
    {
        std::int32_t eq = 0;
        detail::check(zxv_treaty_equal(a.get(), b.get(), &eq));
        return eq != 0;
    }

    /// The raw C handle (owned by this object; never destroy it).
    [[nodiscard]] zxv_treaty* native_handle() const noexcept { return t_.get(); }

private:
    struct Header {
        std::int32_t op = 0;
        std::uint32_t parties = 0, owes = 0;
    };
    [[nodiscard]] zxv_treaty* get() const { return detail::live(t_.get(), "treaty::Treaty"); }
    [[nodiscard]] Header header() const
    {
        Header h;
        detail::check(zxv_treaty_header(get(), &h.op, &h.parties, &h.owes));
        return h;
    }
    template <class F>
    static void parse(F&& f)
    {
        std::uint32_t off = 0;
        std::int32_t fault = 0;
        const std::int32_t st = f(&off, &fault);
        if (st == ZXV_E_TREATY) throw TreatyError(st, last_error(), fault, off);
        detail::check(st);
    }
    /// Map a status; a ZXV_E_TREATY gains the treaty's fault.
    template <class F>
    void call(F&& f) const
    {
        const std::int32_t st = f();
        if (st != ZXV_E_TREATY) {
            detail::check(st);
            return;
        }
        std::string why = last_error();
        std::int32_t fault = 0;
        (void) zxv_treaty_validate(get(), &fault);
        throw TreatyError(st, std::move(why), fault, 0);
    }
    detail::Owned<zxv_treaty, zxv_treaty_destroy> t_;
};

/// Quotient and remainder of pic_div.
struct Division {
    std::int64_t quotient = 0;  ///< truncated toward zero
    std::int64_t remainder = 0; ///< has the dividend's sign
};

/// a + b as COBOL computes PIC S9(18) with ON SIZE ERROR: exact, or
/// UsageError (ZXV_E_RANGE) when an operand or the result leaves
/// [-pic18_max, pic18_max].
inline std::int64_t pic_add(std::int64_t a, std::int64_t b)
{
    std::int64_t r = 0;
    detail::check(zxv_treaty_pic_add(a, b, &r));
    return r;
}
/// a - b in PIC S9(18) (see pic_add).
inline std::int64_t pic_sub(std::int64_t a, std::int64_t b)
{
    std::int64_t r = 0;
    detail::check(zxv_treaty_pic_sub(a, b, &r));
    return r;
}
/// a * b in PIC S9(18) (see pic_add).
inline std::int64_t pic_mul(std::int64_t a, std::int64_t b)
{
    std::int64_t r = 0;
    detail::check(zxv_treaty_pic_mul(a, b, &r));
    return r;
}
/// DIVIDE a BY b GIVING q REMAINDER r: -7 / 2 = -3 rem -1, 7 / -2 = -3
/// rem 1. UsageError (ZXV_E_RANGE) for b = 0 or an operand out of range.
inline Division pic_div(std::int64_t a, std::int64_t b)
{
    Division d;
    detail::check(zxv_treaty_pic_div(a, b, &d.quotient, &d.remainder));
    return d;
}

} // namespace treaty

// ===========================================================================
// CIDs (ABI 1.5)
// ===========================================================================

/// ABI 1.5: IPFS content identifiers. A CID names bytes by what they are (a
/// hash), not where they live. The CIDs here are byte-for-byte the ones
/// Kubo makes (tested against Kubo 0.32.1): Cid::compute is `ipfs block
/// put`, Cid::unixfs is `ipfs add --cid-version=1`, and to_text() with
/// Base::kubo prints what Kubo prints. Everything here is pure.
///
/// Honest limits: a CID proves which bytes are meant; it does not make them
/// available (a CID resolves only while someone holds the bytes) and it
/// does not prove a function's claimed output is right.
namespace cid {

inline constexpr std::size_t max_bytes = ZXV_CID_MAX;       ///< longest binary CID
inline constexpr std::size_t text_max = ZXV_CID_TEXT_MAX;   ///< longest CID text
inline constexpr std::size_t digest_max = ZXV_CID_DIGEST_MAX; ///< longest multihash digest
inline constexpr std::uint32_t codec_raw = ZXV_CID_CODEC_RAW;       ///< multicodec raw (0x55): plain bytes
inline constexpr std::uint32_t codec_dag_pb = ZXV_CID_CODEC_DAG_PB; ///< multicodec dag-pb (0x70): UnixFS
inline constexpr std::uint32_t hash_identity = ZXV_CID_HASH_IDENTITY; ///< identity multihash
inline constexpr std::uint32_t hash_sha2_256 = ZXV_CID_HASH_SHA2_256; ///< sha2-256 multihash

/// How a CID is written as text.
enum class Base : std::uint32_t {
    kubo = ZXV_CID_BASE_DEFAULT,     ///< Kubo's form: CIDv1 "b..." base32, CIDv0 "Qm..."
    base58btc = ZXV_CID_BASE_58BTC,  ///< "z..." base58btc (the AIPI form); CIDv1 only
};

/// The parts of a CID.
struct Info {
    std::uint32_t version = 0;         ///< 0 or 1
    std::uint32_t codec = 0;           ///< multicodec (codec_raw, codec_dag_pb, an AIPI codec)
    std::uint32_t hash = 0;            ///< multihash code (hash_sha2_256, hash_identity)
    std::vector<std::uint8_t> digest;  ///< the multihash digest
};

/// A CID in its binary form (version, codec, multihash): an immutable value
/// of at most max_bytes bytes. Every Cid made by this SDK is exactly one
/// canonical CIDv0 or CIDv1, checked by libzxv; a default-constructed Cid
/// is empty() and every libzxv call refuses it.
class Cid {
public:
    /// An empty, invalid CID (a placeholder).
    constexpr Cid() noexcept = default;

    /// Check and copy binary CID bytes. CidMismatch (ZXV_E_CID) if they are
    /// not exactly one canonical CIDv0 or CIDv1.
    static Cid from_bytes(std::span<const std::uint8_t> bytes)
    {
        if (bytes.size() > max_bytes) throw CidMismatch(ZXV_E_CID, "CID longer than ZXV_CID_MAX bytes");
        Cid c;
        std::copy(bytes.begin(), bytes.end(), c.b_.begin());
        c.n_ = bytes.size();
        detail::check(zxv_cid_info(c.b_.data(), c.n_, nullptr, nullptr, nullptr, nullptr, nullptr));
        return c;
    }
    /// Parse CID text: "b..." (CIDv1 base32), "Qm..." (CIDv0) or "z..."
    /// (CIDv1 base58btc). Only canonical text is accepted (no upper case,
    /// padding or other bases): CidMismatch otherwise; UsageError for ""
    /// (ZXV_E_ARG) or text over text_max (ZXV_E_TOO_LONG).
    static Cid parse(const std::string& text)
    {
        const char* t = detail::cstr(text, "CID text");
        return make([&](std::uint8_t* o, std::size_t* n) { return zxv_cid_from_text(t, o, n); });
    }
    /// The CIDv1 of `data` as one block: sha2-256 under multicodec `codec`
    /// (Kubo `ipfs block put`).
    static Cid compute(std::span<const std::uint8_t> data, std::uint32_t codec = codec_raw)
    {
        return make([&](std::uint8_t* o, std::size_t* n) { return zxv_cid_compute(codec, data.data(), data.size(), o, n); });
    }
    /// The root CID Kubo gives `data` as a file (`ipfs add --cid-version=1`:
    /// 256 KiB chunks, raw leaves): a raw CID up to 262144 bytes, a dag-pb
    /// root above. The blocks are not kept.
    static Cid unixfs(std::span<const std::uint8_t> data)
    {
        return make([&](std::uint8_t* o, std::size_t* n) { return zxv_cid_unixfs(data.data(), data.size(), o, n); });
    }

    /// The binary form.
    [[nodiscard]] std::span<const std::uint8_t> bytes() const noexcept { return {b_.data(), n_}; }
    /// Length of the binary form.
    [[nodiscard]] std::size_t size() const noexcept { return n_; }
    /// True for a default-constructed placeholder.
    [[nodiscard]] bool empty() const noexcept { return n_ == 0; }

    /// The text form. CidMismatch for a CIDv0 in Base::base58btc (it has no
    /// multibase form).
    [[nodiscard]] std::string to_text(Base base = Base::kubo) const
    {
        return detail::read_text([&](char* b, std::size_t c, std::size_t* n) {
            return zxv_cid_to_text(b_.data(), n_, static_cast<std::uint32_t>(base), b, c, n);
        });
    }
    /// Version, codec, hash code and digest.
    [[nodiscard]] Info info() const
    {
        Info i;
        std::array<std::uint8_t, digest_max> d{};
        std::size_t dl = 0;
        detail::check(zxv_cid_info(b_.data(), n_, &i.version, &i.codec, &i.hash, d.data(), &dl));
        i.digest.assign(d.begin(), d.begin() + static_cast<std::ptrdiff_t>(dl));
        return i;
    }
    /// The CIDv1 of this CID (a CIDv0 becomes dag-pb CIDv1 with the same
    /// hash, Kubo `ipfs cid base32`; a CIDv1 is returned unchanged).
    [[nodiscard]] Cid to_v1() const
    {
        return make([&](std::uint8_t* o, std::size_t* n) { return zxv_cid_to_v1(b_.data(), n_, o, n); });
    }
    /// True if `data` is exactly the block this CID names (re-hashed; an
    /// identity CID compares the bytes). A UnixFS root names its root BLOCK,
    /// not the file: check a file with Cid::unixfs(file) == root instead.
    [[nodiscard]] bool matches(std::span<const std::uint8_t> data) const
    {
        const std::int32_t st = zxv_cid_verify(b_.data(), n_, data.data(), data.size());
        if (st == ZXV_E_CID && !empty()) return false;
        detail::check(st);
        return true;
    }
    /// Like matches(), but throws CidMismatch when the bytes do not match.
    void verify(std::span<const std::uint8_t> data) const
    {
        detail::check(zxv_cid_verify(b_.data(), n_, data.data(), data.size()));
    }

    /// Same bytes.
    friend bool operator==(const Cid& a, const Cid& b) noexcept
    {
        return std::equal(a.b_.begin(), a.b_.begin() + static_cast<std::ptrdiff_t>(a.n_), b.b_.begin(),
                          b.b_.begin() + static_cast<std::ptrdiff_t>(b.n_));
    }
    /// Byte-wise order (for ordered containers).
    friend bool operator<(const Cid& a, const Cid& b) noexcept
    {
        return std::lexicographical_compare(a.b_.begin(), a.b_.begin() + static_cast<std::ptrdiff_t>(a.n_),
                                            b.b_.begin(), b.b_.begin() + static_cast<std::ptrdiff_t>(b.n_));
    }

private:
    template <class F>
    static Cid make(F&& f)
    {
        Cid c;
        std::size_t n = 0;
        detail::check(f(c.b_.data(), &n));
        c.n_ = n;
        return c;
    }
    std::array<std::uint8_t, ZXV_CID_MAX> b_{};
    std::size_t n_ = 0;
};

/// Stream as Kubo text.
inline std::ostream& operator<<(std::ostream& os, const Cid& c) { return os << c.to_text(); }

} // namespace cid

// ===========================================================================
// AIPI (ABI 1.5)
// ===========================================================================

/// ABI 1.5: AIPI, content-addressed functions (docs/AIPI.md). AIPI names
/// data, functions and calls by CIDs:
///
///   * data: raw (or dag-pb) CIDs of blobs, Z-Wasm modules and contracts;
///   * a function: the CID of its canonical function record (codec
///     aipi::mc_fn + intent), so one function has exactly one CID;
///   * a call: the CID of a call record (function CID + input);
///   * a claim: an executor's answer, the bytes the executor signs.
///
/// The record functions are pure. aipi::Node is an OFFLINE, in-process IPFS
/// block store with the AIPI context over it (needs caps::aipi to store).
///
/// Honest limits: no network transport (no mesh, IPFS peers, gateway or
/// libp2p: a get finds only what was put into this node); no EXECUTE
/// (running a function needs the Z-Wasm sandbox in sdk/kits/aipi, which is
/// not part of libzxv: plan() only reports the route); no money moves here;
/// signing and checking claims is the caller's job. The 0x3A10xx codecs are
/// multicodec private-use values, not registered.
namespace aipi {

inline constexpr std::uint32_t mc_fn = ZXV_AIPI_MC_FN;                       ///< zxv-fn (intent 0)
inline constexpr std::uint32_t mc_tensor_infer = ZXV_AIPI_MC_TENSOR_INFER;   ///< zxv-tensor-infer (intent 1)
inline constexpr std::uint32_t mc_settle_ledger = ZXV_AIPI_MC_SETTLE_LEDGER; ///< zxv-settle-ledger (intent 2)
inline constexpr std::uint32_t mc_swarm_route = ZXV_AIPI_MC_SWARM_ROUTE;     ///< zxv-swarm-route (intent 3)
inline constexpr std::uint32_t mc_call = ZXV_AIPI_MC_CALL;                   ///< zxv-aipi-call
inline constexpr std::size_t name_max = ZXV_AIPI_NAME_MAX;       ///< export names: [A-Za-z0-9_]{1,63}
inline constexpr std::size_t io_max = ZXV_AIPI_IO_MAX;           ///< call input, claim output
inline constexpr std::size_t cert_max = ZXV_AIPI_CERT_MAX;       ///< claim certificate
inline constexpr std::uint32_t quorum_max = ZXV_AIPI_QUORUM_MAX; ///< QUORUM n
inline constexpr std::size_t fn_max = ZXV_AIPI_FN_MAX;           ///< longest function record
inline constexpr std::size_t call_max = ZXV_AIPI_CALL_MAX;       ///< longest call record
inline constexpr std::size_t claim_max = ZXV_AIPI_CLAIM_MAX;     ///< longest claim record
inline constexpr std::size_t block_max = ZXV_AIPI_BLOCK_MAX;     ///< largest blob a node stores (2 MiB)
inline constexpr std::size_t store_min = ZXV_AIPI_STORE_MIN;     ///< smallest node store
inline constexpr std::size_t store_max = ZXV_AIPI_STORE_MAX;     ///< largest node store (256 MiB)
inline constexpr std::uint32_t blocks_max = ZXV_AIPI_BLOCKS_MAX; ///< most blocks per node

/// What a function computes; it picks the function CID's codec.
enum class Intent : std::uint32_t {
    fn = ZXV_AIPI_INTENT_FN,                       ///< a general function
    tensor_infer = ZXV_AIPI_INTENT_TENSOR_INFER,   ///< tensor inference
    settle_ledger = ZXV_AIPI_INTENT_SETTLE_LEDGER, ///< ledger settlement
    swarm_route = ZXV_AIPI_INTENT_SWARM_ROUTE,     ///< swarm routing
};
/// How a result is accepted.
enum class Verify : std::uint32_t {
    checker = ZXV_AIPI_VERIFY_CHECKER, ///< a checker export judges (input, output)
    cert = ZXV_AIPI_VERIFY_CERT,       ///< as checker, with a certificate
    quorum = ZXV_AIPI_VERIFY_QUORUM,   ///< k of n executors agree byte for byte
    local = ZXV_AIPI_VERIFY_LOCAL,     ///< never accept a remote result
};
/// Whether the function may leave the node.
enum class Privacy : std::uint32_t {
    public_ = ZXV_AIPI_PRIV_PUBLIC, ///< may be announced and fetched
    local = ZXV_AIPI_PRIV_LOCAL,    ///< private or key-bearing: never leaves the node (stored sealed)
};
/// Whether two runs on the same input give the same bytes.
enum class Determinism : std::uint32_t {
    nondeterministic = ZXV_AIPI_DET_NONDETERMINISTIC, ///< outputs may differ
    deterministic = ZXV_AIPI_DET_DETERMINISTIC,       ///< same input, same output (needed by QUORUM)
};
/// The route a call would take (Node::plan). This offline library reports
/// only local or unavailable; the others are the kernel's routes for a
/// networked host.
enum class Route : std::int32_t {
    cache = ZXV_AIPI_ROUTE_CACHE,             ///< a cached result (networked hosts)
    local = ZXV_AIPI_ROUTE_LOCAL,             ///< the code is held here
    fetch_local = ZXV_AIPI_ROUTE_FETCH_LOCAL, ///< fetch, then run here (networked hosts)
    mesh = ZXV_AIPI_ROUTE_MESH,               ///< run on the mesh (networked hosts)
    unavailable = ZXV_AIPI_ROUTE_UNAVAILABLE, ///< the module (or contract) is not held
};

/// Every field of a function record. encode_function checks them all and
/// masks none: quorum_n/k are for Verify::quorum only (2 <= n <= 8,
/// n/2 < k <= n, deterministic) and 0 otherwise; checker is for checker and
/// cert only (and differs from entry); certifier is for cert only.
struct FunctionSpec {
    Intent intent = Intent::fn;                                 ///< picks the function CID's codec
    Verify verify = Verify::local;                              ///< how a result is accepted
    Privacy privacy = Privacy::public_;                         ///< public or node-local
    Determinism determinism = Determinism::deterministic;       ///< determinism class
    std::uint32_t quorum_n = 0;                                 ///< QUORUM n, else 0
    std::uint32_t quorum_k = 0;                                 ///< QUORUM k, else 0
    std::uint64_t fuel_max = 0;                                 ///< > 0: the most fuel one run may use
    cid::Cid module;                                            ///< CIDv1 raw sha2-256 of the Z-Wasm module
    std::optional<cid::Cid> contract;                           ///< CIDv1 raw sha2-256 of the z-contract text
    std::string entry;                                          ///< the export computing the result
    std::string checker;                                        ///< checker export, or "" for none
    std::string certifier;                                      ///< certifier export, or "" for none
};

/// The canonical function record of `f`. UsageError (ZXV_E_ARG for a field
/// out of range or breaking a rule, ZXV_E_TOO_LONG for a name over 63
/// bytes, ZXV_E_UTF8), CidMismatch (a module or contract CID that is not
/// CIDv1 raw sha2-256).
inline std::vector<std::uint8_t> encode_function(const FunctionSpec& f)
{
    const char* entry = detail::cstr(f.entry, "entry");
    const char* checker = f.checker.empty() ? nullptr : detail::cstr(f.checker, "checker");
    const char* certifier = f.certifier.empty() ? nullptr : detail::cstr(f.certifier, "certifier");
    return detail::read_bytes([&](std::uint8_t* b, std::size_t c, std::size_t* n) {
        return zxv_aipi_fn_encode(static_cast<std::uint32_t>(f.intent), static_cast<std::uint32_t>(f.verify),
                                  static_cast<std::uint32_t>(f.privacy), static_cast<std::uint32_t>(f.determinism),
                                  f.quorum_n, f.quorum_k, f.fuel_max, f.module.bytes().data(), f.module.size(),
                                  f.contract ? f.contract->bytes().data() : nullptr, f.contract ? f.contract->size() : 0,
                                  entry, checker, certifier, b, c, n);
    }, nullptr, fn_max);
}

/// Check a function record strictly (it must be the canonical encoding) and
/// read every field. UsageError (ZXV_E_FORMAT) otherwise.
inline FunctionSpec decode_function(std::span<const std::uint8_t> record)
{
    FunctionSpec f;
    std::uint32_t it = 0, ve = 0, pr = 0, de = 0;
    detail::check(zxv_aipi_fn_decode(record.data(), record.size(), &it, &ve, &pr, &de, &f.quorum_n, &f.quorum_k,
                                     &f.fuel_max));
    f.intent = static_cast<Intent>(it);
    f.verify = static_cast<Verify>(ve);
    f.privacy = static_cast<Privacy>(pr);
    f.determinism = static_cast<Determinism>(de);
    auto get_cid = [&](std::uint32_t field) -> std::optional<cid::Cid> {
        std::array<std::uint8_t, ZXV_CID_MAX> c{};
        std::size_t n = 0;
        const std::int32_t st = zxv_aipi_fn_get_cid(record.data(), record.size(), field, c.data(), &n);
        if (st == ZXV_E_NOT_FOUND) return std::nullopt;
        detail::check(st);
        return cid::Cid::from_bytes({c.data(), n});
    };
    auto get_name = [&](std::uint32_t field) {
        return detail::read_text([&](char* b, std::size_t c, std::size_t* n) {
            return zxv_aipi_fn_get_name(record.data(), record.size(), field, b, c, n);
        });
    };
    f.module = *get_cid(ZXV_AIPI_FIELD_MODULE);
    f.contract = get_cid(ZXV_AIPI_FIELD_CONTRACT);
    f.entry = get_name(ZXV_AIPI_FIELD_ENTRY);
    f.checker = get_name(ZXV_AIPI_FIELD_CHECKER);
    f.certifier = get_name(ZXV_AIPI_FIELD_CERTIFIER);
    return f;
}

/// The function CID of a record: codec mc_fn + intent, sha2-256 of the
/// record bytes. UsageError (ZXV_E_FORMAT) for a record that is not one.
inline cid::Cid function_cid(std::span<const std::uint8_t> record)
{
    std::array<std::uint8_t, ZXV_CID_MAX> c{};
    std::size_t n = 0;
    detail::check(zxv_aipi_fn_cid(record.data(), record.size(), c.data(), &n));
    return cid::Cid::from_bytes({c.data(), n});
}

/// The call record naming one invocation of `function` on `input` (at most
/// io_max bytes). CidMismatch (not a function CID), UsageError
/// (ZXV_E_TOO_LONG).
inline std::vector<std::uint8_t> encode_call(const cid::Cid& function, std::span<const std::uint8_t> input)
{
    return detail::read_bytes([&](std::uint8_t* b, std::size_t c, std::size_t* n) {
        return zxv_aipi_call_encode(function.bytes().data(), function.size(), input.data(), input.size(), b, c, n);
    }, nullptr, call_max);
}

/// The parts of a call record.
struct Call {
    cid::Cid function;               ///< the function CID
    std::vector<std::uint8_t> input; ///< the input bytes
};

/// Check a call record strictly and copy out its parts. UsageError
/// (ZXV_E_FORMAT) otherwise.
inline Call decode_call(std::span<const std::uint8_t> record)
{
    std::size_t fo = 0, fl = 0, io = 0, il = 0;
    detail::check(zxv_aipi_call_decode(record.data(), record.size(), &fo, &fl, &io, &il));
    Call c;
    c.function = cid::Cid::from_bytes(record.subspan(fo, fl));
    const auto in = record.subspan(io, il);
    c.input.assign(in.begin(), in.end());
    return c;
}

/// The call CID (codec mc_call, sha2-256 of the call record) of
/// (`function`, `input`). Errors as encode_call.
inline cid::Cid call_cid(const cid::Cid& function, std::span<const std::uint8_t> input)
{
    std::array<std::uint8_t, ZXV_CID_MAX> c{};
    std::size_t n = 0;
    detail::check(zxv_aipi_call_cid(function.bytes().data(), function.size(), input.data(), input.size(), c.data(), &n));
    return cid::Cid::from_bytes({c.data(), n});
}

/// A 32-byte executor (node) id.
using ExecutorId = std::array<std::uint8_t, 32>;

/// The claim record an executor signs: its `output` (and, for cert
/// functions, `cert`) for the call `call`. Signing it is the caller's job.
/// CidMismatch (not a call CID), UsageError (ZXV_E_TOO_LONG: output or cert
/// over 1024 bytes).
inline std::vector<std::uint8_t> encode_claim(const cid::Cid& call, const ExecutorId& executor,
                                              std::span<const std::uint8_t> output,
                                              std::span<const std::uint8_t> cert = {})
{
    return detail::read_bytes([&](std::uint8_t* b, std::size_t c, std::size_t* n) {
        return zxv_aipi_claim_encode(call.bytes().data(), call.size(), executor.data(), output.data(), output.size(),
                                     cert.data(), cert.size(), b, c, n);
    }, nullptr, claim_max);
}

/// The parts of a claim record.
struct Claim {
    cid::Cid call;                    ///< the call CID it answers
    ExecutorId executor{};            ///< the executor's node id
    std::vector<std::uint8_t> output; ///< its answer
    std::vector<std::uint8_t> cert;   ///< its certificate (cert functions), else empty
};

/// Check a claim record strictly and copy out its parts. UsageError
/// (ZXV_E_FORMAT) otherwise. The signature over it is not checked here.
inline Claim decode_claim(std::span<const std::uint8_t> record)
{
    std::size_t co = 0, cl = 0, eo = 0, oo = 0, ol = 0, xo = 0, xl = 0;
    detail::check(zxv_aipi_claim_decode(record.data(), record.size(), &co, &cl, &eo, &oo, &ol, &xo, &xl));
    Claim c;
    c.call = cid::Cid::from_bytes(record.subspan(co, cl));
    const auto ex = record.subspan(eo, 32);
    std::copy(ex.begin(), ex.end(), c.executor.begin());
    const auto out = record.subspan(oo, ol);
    c.output.assign(out.begin(), out.end());
    const auto ce = record.subspan(xo, xl);
    c.cert.assign(ce.begin(), ce.end());
    return c;
}

/// The Kademlia key under which providers of `cid` are announced on Vinea:
/// SHA-256("zxv/aipi/v1/provider" || binary CID). Pure; announcing is not
/// in this library.
inline Digest32 provider_key(const cid::Cid& c)
{
    Digest32 k{};
    detail::check(zxv_aipi_provider_key(c.bytes().data(), c.size(), k.data()));
    return k;
}

/// Audit counters of a Node.
struct NodeInfo {
    std::uint32_t blocks = 0;          ///< blocks indexed
    std::uint64_t store_used = 0;      ///< store bytes used
    std::uint32_t gets_verified = 0;   ///< gets whose bytes re-hashed to their CID
    std::uint32_t forbidden = 0;       ///< gets whose stored bytes failed their hash (dropped)
};

/// An offline, in-memory IPFS node (block store and pins) with the AIPI
/// context over it. It belongs to the context of the capability that
/// created it and fails closed with it (FatalError). Storing needs
/// caps::aipi; reads need only the node. Internally serialized: any thread
/// may call it. Move-only; destroying it wipes the node key.
class Node {
public:
    /// A node holding at most `max_blocks` blocks (1..blocks_max) in
    /// `store_bytes` (store_min..store_max; each block costs its length plus
    /// about 100 bytes). `node_key` (32 bytes, copied; empty for none) seals
    /// private blocks with ChaCha20-Poly1305; without it private puts throw
    /// RefusedError (ZXV_E_STATE). Needs caps::aipi.
    Node(const Capability& cap, std::size_t store_bytes, std::uint32_t max_blocks,
         std::span<const std::uint8_t> node_key = {})
        : cap_(detail::cap_of(cap))
    {
        if (!node_key.empty()) detail::need_size(node_key, 32, "AIPI node key");
        zxv_aipi* a = nullptr;
        cap_->call([&] {
            return zxv_aipi_create(cap_->cap, store_bytes, max_blocks, node_key.empty() ? nullptr : node_key.data(), &a);
        });
        a_.reset(a);
    }
    Node(Node&&) noexcept = default;            ///< move
    Node& operator=(Node&&) noexcept = default; ///< move-assign
    Node(const Node&) = delete;
    Node& operator=(const Node&) = delete;
    ~Node() = default;

    /// Store and pin `data` (at most block_max bytes) as one raw block and
    /// return its CID. `is_private` stores it sealed and never announceable.
    /// The same bytes again is a no-op; the same bytes public after private
    /// (or the reverse) is RefusedError (ZXV_E_STATE): making something
    /// public is a deliberate act. RefusedError (ZXV_E_CAPACITY) when full.
    cid::Cid put(std::span<const std::uint8_t> data, bool is_private = false)
    {
        return out_cid([&](std::uint8_t* o, std::size_t* n) {
            return zxv_aipi_put(cap_->cap, node(), data.data(), data.size(), is_private ? 1 : 0, o, n);
        });
    }
    /// The bytes `c` names, re-hashed before they are returned. UsageError
    /// (ZXV_E_NOT_FOUND: not held here, and there is no network),
    /// CidMismatch (an identity CID, or stored bytes that no longer match:
    /// those are dropped), RefusedError (ZXV_E_STATE: private and no key).
    [[nodiscard]] std::vector<std::uint8_t> get(const cid::Cid& c) const
    {
        return detail::read_bytes([&](std::uint8_t* b, std::size_t cp, std::size_t* n) {
            return zxv_aipi_get(node(), c.bytes().data(), c.size(), b, cp, n);
        }, cap_->ctx.get());
    }
    /// Store a function record (checked strictly; sealed when its privacy is
    /// local) and return its function CID. The module and contract need not
    /// be held yet. Needs caps::aipi.
    cid::Cid publish(std::span<const std::uint8_t> function_record)
    {
        return out_cid([&](std::uint8_t* o, std::size_t* n) {
            return zxv_aipi_publish_fn(cap_->cap, node(), function_record.data(), function_record.size(), o, n);
        });
    }
    /// The function record a function CID names, verified by hash, decoded
    /// strictly and its codec checked against its intent.
    [[nodiscard]] std::vector<std::uint8_t> load(const cid::Cid& function) const
    {
        return detail::read_bytes([&](std::uint8_t* b, std::size_t cp, std::size_t* n) {
            return zxv_aipi_load_fn(node(), function.bytes().data(), function.size(), b, cp, n);
        }, cap_->ctx.get(), fn_max);
    }
    /// The route a call of `function` on `input` would take now. It runs and
    /// changes nothing: Route::local when the module (and contract) are
    /// held, else Route::unavailable.
    [[nodiscard]] Route plan(const cid::Cid& function, std::span<const std::uint8_t> input = {},
                             bool input_private = false) const
    {
        std::int32_t r = 0;
        run([&] {
            return zxv_aipi_plan(node(), function.bytes().data(), function.size(), input.data(), input.size(),
                                 input_private ? 1 : 0, &r);
        });
        return static_cast<Route>(r);
    }
    /// The audit counters.
    [[nodiscard]] NodeInfo info() const
    {
        NodeInfo i;
        run([&] { return zxv_aipi_info(node(), &i.blocks, &i.store_used, &i.gets_verified, &i.forbidden); });
        return i;
    }
    /// The raw C handle (owned by this object; never destroy it).
    [[nodiscard]] zxv_aipi* native_handle() const noexcept { return a_.get(); }

private:
    [[nodiscard]] zxv_aipi* node() const { return detail::live(a_.get(), "aipi::Node"); }
    template <class F>
    void run(F&& f) const
    {
        (void) node();
        cap_->call(std::forward<F>(f));
    }
    template <class F>
    cid::Cid out_cid(F&& f)
    {
        std::array<std::uint8_t, ZXV_CID_MAX> c{};
        std::size_t n = 0;
        run([&] { return f(c.data(), &n); });
        return cid::Cid::from_bytes({c.data(), n});
    }
    std::shared_ptr<detail::CapCore> cap_; // declared first: outlives the node
    detail::Owned<zxv_aipi, zxv_aipi_destroy> a_;
};

} // namespace aipi


// ===========================================================================
// Fibonacci coins and swarm tokens (ABI 1.5)
// ===========================================================================

/// ABI 1.5: the VFV coin ladder (Vino). A coin's face is a rung of the
/// Fibonacci ladder: rung r (1..rungs) has face F(r+1), so the faces are 1,
/// 2, 3, 5, 8, 13, ... up to top_face (rung 45). Rung 0 is the ceremonial
/// V0 note, face 0. Any other face (4, 6, 100, ...) is not a coin
/// (DenominationError); pay an arbitrary amount with split(). Pure and
/// integer-only.
///
/// Honest limit: minting coins is not in this ABI yet (the kernel's mint
/// needs a coin-store handle bound to a ledger and an integer mint path);
/// caps::mint is used only by token::Wallet.
namespace coin {

inline constexpr std::uint32_t rungs = ZXV_COIN_RUNGS;      ///< rungs 1..45 carry value
inline constexpr std::uint64_t top_face = ZXV_COIN_TOP_FACE; ///< face of rung 45, F(46) = 1,836,311,903

/// The face of ladder rung `rung` (0..rungs; rung 0 is the V0 note, face
/// 0). UsageError (ZXV_E_RANGE) above 45.
inline std::uint64_t face(std::uint32_t rung)
{
    std::uint64_t f = 0;
    detail::check(zxv_coin_face(rung, &f));
    return f;
}

/// The rung whose face is exactly `face` (face 1 is rung 1). Throws
/// DenominationError for 0 and any face not on the ladder, including
/// Fibonacci numbers above top_face.
inline std::uint32_t rung(std::uint64_t face)
{
    std::uint32_t r = 0;
    detail::check(zxv_coin_rung(face, &r));
    return r;
}

/// How many coins split(amount) returns, without allocating them.
/// UsageError (ZXV_E_RANGE) when the split would need more than
/// 4,294,967,294 coins (amounts of about 7.9e18 and up).
inline std::size_t split_count(std::uint64_t amount)
{
    std::size_t n = 0;
    const std::int32_t st = zxv_coin_split(amount, nullptr, 0, &n);
    if (st != ZXV_E_BUFFER) detail::check(st);
    return n;
}

/// The coins for a whole `amount` as rungs, largest first: its Zeckendorf
/// split (floor(amount / top_face) top coins first, then each lower rung at
/// most once and never two neighbours), the unique fewest-coins form. 0 is
/// an empty split. Check split_count() first for very large amounts: each
/// coin takes 4 bytes here.
inline std::vector<std::uint32_t> split(std::uint64_t amount)
{
    std::vector<std::uint32_t> v(split_count(amount));
    std::size_t n = 0;
    detail::check(zxv_coin_split(amount, v.empty() ? nullptr : v.data(), v.size(), &n));
    v.resize(n);
    return v;
}

} // namespace coin

/// ABI 1.5: swarm AI token tiers, octane and Fibonacci codes. Tier k
/// (0..tiers-1) has face F(k+2): 1, 2, 3, 5, 8, ... F(65); tiers super_tier
/// (face 144) and up are super tokens. A holding is Counts (tokens held per
/// tier); its value is the sum of count * face. OCTANE is the densest tier a
/// holding may use (octane::plain .. octane::max, named like a fuel pump's
/// grades; any 0..63 is allowed). The canonical form of a value at octane o
/// puts floor(value / face(o)) tokens on tier o and the rest in Zeckendorf
/// form below it: one canonical form per value per octane.
///
/// NO PREMIUM: a dense token is worth exactly its face in plain tokens.
/// Density earns no interest, costs no fee and buys no allotment or
/// priority; changing octane never changes the value. It buys only fewer
/// pieces (1,000,000 tokens are 17 pieces at octane::racing), which helps
/// wherever tokens are handled one piece at a time. It does not make a model
/// faster or an amount shorter on the wire.
///
/// The free functions are pure. A value that would pass 64 bits throws
/// UsageError (ZXV_E_RANGE) and changes nothing.
namespace token {

inline constexpr std::uint32_t tiers = ZXV_TOKEN_TIERS;           ///< 64 tiers
inline constexpr std::uint32_t super_tier = ZXV_TOKEN_SUPER_TIER; ///< first super-token tier (face 144)
inline constexpr std::uint32_t code_max_bits = ZXV_TOKEN_CODE_MAX_BITS;   ///< longest Fibonacci code (93 bits)
inline constexpr std::size_t code_max_bytes = ZXV_TOKEN_CODE_MAX_BYTES;   ///< largest code buffer

/// The named octane grades.
namespace octane {
inline constexpr std::uint32_t plain = ZXV_OCTANE_PLAIN;       ///< face 1 only
inline constexpr std::uint32_t regular = ZXV_OCTANE_REGULAR;   ///< up to face 8
inline constexpr std::uint32_t midgrade = ZXV_OCTANE_MIDGRADE; ///< up to face 89
inline constexpr std::uint32_t premium = ZXV_OCTANE_PREMIUM;   ///< up to face 987
inline constexpr std::uint32_t racing = ZXV_OCTANE_RACING;     ///< up to face 75025
inline constexpr std::uint32_t max = ZXV_OCTANE_MAX;           ///< up to face F(65)
} // namespace octane

/// A holding: tokens held per tier (index = tier).
using Counts = std::array<std::uint64_t, ZXV_TOKEN_TIERS>;

/// The face of a tier and whether it is a super token.
struct TierFace {
    std::uint64_t face = 0;  ///< F(tier + 2)
    bool is_super = false;   ///< tier >= super_tier
};

/// The value of a holding and its number of pieces.
struct Value {
    std::uint64_t value = 0;  ///< sum of count * face
    std::uint64_t pieces = 0; ///< sum of counts
};

/// The face of `tier`. UsageError (ZXV_E_RANGE) for tier >= 64.
inline TierFace face(std::uint32_t tier)
{
    TierFace t;
    std::int32_t s = 0;
    detail::check(zxv_token_face(tier, &t.face, &s));
    t.is_super = s != 0;
    return t;
}

/// The tier whose face is exactly `face`. DenominationError if it is not
/// one of the 64 faces.
inline std::uint32_t tier(std::uint64_t face)
{
    std::uint32_t t = 0;
    detail::check(zxv_token_tier(face, &t));
    return t;
}

/// The value and piece count of a holding. UsageError (ZXV_E_RANGE) when
/// the value passes 64 bits.
inline Value value(const Counts& counts)
{
    Value v;
    detail::check(zxv_token_value(counts.data(), &v.value, &v.pieces));
    return v;
}

/// The canonical form of `value` at `octane`. UsageError (ZXV_E_RANGE) for
/// octane >= 64.
inline Counts canonical(std::uint64_t value, std::uint32_t octane)
{
    Counts c{};
    detail::check(zxv_token_canonical(value, octane, c.data()));
    return c;
}

/// True if `counts` is the canonical form of its value at `octane` (false
/// also when a tier above the octane is used).
inline bool is_canonical(const Counts& counts, std::uint32_t octane)
{
    std::int32_t r = 0;
    detail::check(zxv_token_is_canonical(counts.data(), octane, &r));
    return r != 0;
}

/// Rewrite `counts` in place into the canonical form of its value at
/// `octane`; the value never changes. Unchanged on any failure.
inline void compress(Counts& counts, std::uint32_t octane)
{
    detail::check(zxv_token_compress(counts.data(), octane));
}

/// Fuse in place: one tier-k and one tier-(k+1) token become one
/// tier-(k+2) token (F(k+2) + F(k+3) = F(k+4)). InsufficientFunds (no tier-k
/// or tier-(k+1) token), RefusedError ZXV_E_STATE (tier k+2 is denser than
/// `octane` allows), UsageError ZXV_E_RANGE (tier > 61, octane >= 64, or a
/// full count). Unchanged on any failure.
inline void fuse(Counts& counts, std::uint32_t octane, std::uint32_t tier)
{
    detail::check(zxv_token_fuse(counts.data(), octane, tier));
}

/// Split in place: one tier-k token becomes one tier-(k-1) and one
/// tier-(k-2) token (tier 1 becomes two plain tokens; tier 0 is atomic:
/// UsageError ZXV_E_RANGE). InsufficientFunds when there is no tier-k token.
/// Splitting needs no octane: it only makes a holding plainer.
inline void split(Counts& counts, std::uint32_t tier)
{
    detail::check(zxv_token_split(counts.data(), tier));
}

/// Number of bits in the Fibonacci code of `value` (2..93). UsageError
/// (ZXV_E_RANGE) for UINT64_MAX, which has no code.
inline std::uint32_t code_bits(std::uint64_t value)
{
    std::uint32_t b = 0;
    detail::check(zxv_token_code_bits(value, &b));
    return b;
}

/// Append the Fibonacci code of `value` (Zeckendorf bits of value+1, lowest
/// first, then a closing 1, so every code ends in "11" and needs no length
/// field) at bit `bit` of `buf` (bit i is bit i%8 of byte i/8); `bit`
/// advances past it. Only the code's bits are written. UsageError
/// (ZXV_E_BUFFER) when it does not fit: nothing is written.
inline void code_put(std::span<std::uint8_t> buf, std::uint32_t& bit, std::uint64_t value)
{
    std::uint32_t b = bit;
    detail::check(zxv_token_code_put(buf.data(), buf.size(), &b, value));
    bit = b;
}

/// Read one Fibonacci code from bit `bit` of `buf`; `bit` advances past it.
/// UsageError ZXV_E_FORMAT (no closing "11", or a value past 64 bits),
/// ZXV_E_RANGE (`bit` past the end). Nothing changes on failure.
inline std::uint64_t code_get(std::span<const std::uint8_t> buf, std::uint32_t& bit)
{
    std::uint32_t b = bit;
    std::uint64_t v = 0;
    detail::check(zxv_token_code_get(buf.data(), buf.size(), &b, &v));
    bit = b;
    return v;
}

/// What Wallet::info reports.
struct WalletInfo {
    std::uint32_t model_id = 0; ///< the wallet's label
    std::uint32_t octane = 0;   ///< its octane
    std::uint64_t value = 0;    ///< what it holds
    std::uint64_t pieces = 0;   ///< in how many tokens
};

/// A token wallet: a holding kept canonical at its own octane. Every change
/// needs caps::mint (tokens enter a wallet only by mint(); it is not tied to
/// a Swarm budget, whose authority would not cover creating tokens); reads
/// need no capability. It belongs to the capability's context and fails
/// closed with it (FatalError). Internally serialized: any thread may call
/// it. Move-only.
class Wallet {
public:
    /// An empty wallet at `octane`, labelled `model_id` (a free label, e.g.
    /// the swarm model that holds it). Needs caps::mint.
    Wallet(const Capability& cap, std::uint32_t model_id, std::uint32_t octane) : cap_(detail::cap_of(cap))
    {
        zxv_token_wallet* w = nullptr;
        cap_->call([&] { return zxv_token_wallet_create(cap_->cap, model_id, octane, &w); });
        w_.reset(w);
    }
    Wallet(Wallet&&) noexcept = default;            ///< move
    Wallet& operator=(Wallet&&) noexcept = default; ///< move-assign
    Wallet(const Wallet&) = delete;
    Wallet& operator=(const Wallet&) = delete;
    ~Wallet() = default;

    /// Create `amount` new tokens in the wallet. UsageError (ZXV_E_RANGE)
    /// when the value would pass 64 bits; nothing changes.
    void mint(std::uint64_t amount)
    {
        run([&](zxv_token_wallet* w) { return zxv_token_wallet_mint(cap_->cap, w, amount); });
    }
    /// Spend (burn) `amount`; the change stays canonical. InsufficientFunds
    /// when the wallet holds less (nothing changes).
    void spend(std::uint64_t amount)
    {
        run([&](zxv_token_wallet* w) { return zxv_token_wallet_spend(cap_->cap, w, amount); });
    }
    /// Move `amount` to `to`: this wallet keeps its change at its octane,
    /// `to` receives at ITS octane, and the total is conserved exactly. Both
    /// wallets must belong to this wallet's context (else
    /// CapabilityDenied). Transferring to itself only checks the funds.
    /// Neither wallet changes on failure.
    void transfer_to(Wallet& to, std::uint64_t amount)
    {
        zxv_token_wallet* dst = to.h();
        run([&](zxv_token_wallet* w) { return zxv_token_wallet_transfer(cap_->cap, w, dst, amount); });
    }
    /// Change the octane (up = denser, down = plainer); the same value is
    /// re-expressed canonically.
    void set_octane(std::uint32_t octane)
    {
        run([&](zxv_token_wallet* w) { return zxv_token_wallet_set_octane(cap_->cap, w, octane); });
    }
    /// Label, octane, value and piece count.
    [[nodiscard]] WalletInfo info() const
    {
        WalletInfo i;
        run([&](zxv_token_wallet* w) {
            return zxv_token_wallet_info(w, &i.model_id, &i.octane, &i.value, &i.pieces);
        });
        return i;
    }
    /// The counts per tier.
    [[nodiscard]] Counts counts() const
    {
        Counts c{};
        run([&](zxv_token_wallet* w) { return zxv_token_wallet_counts(w, c.data()); });
        return c;
    }
    /// The raw C handle (owned by this object; never destroy it).
    [[nodiscard]] zxv_token_wallet* native_handle() const noexcept { return w_.get(); }

private:
    [[nodiscard]] zxv_token_wallet* h() const { return detail::live(w_.get(), "token::Wallet"); }
    template <class F>
    void run(F&& f) const
    {
        zxv_token_wallet* w = h();
        cap_->call([&] { return f(w); });
    }
    std::shared_ptr<detail::CapCore> cap_; // declared first: outlives the wallet
    detail::Owned<zxv_token_wallet, zxv_token_wallet_destroy> w_;
};

} // namespace token

// ===========================================================================
// The assurance fee (ABI 1.5)
// ===========================================================================

/// ABI 1.5: the 0.08889% assurance fee, exact and integer-only:
///
///     fee(g) = floor(g * 8889 / 10,000,000)
///
/// g is in the unit's own minor measure (money minor units, compute
/// token-cycles, bandwidth bytes, storage byte-hours). It is formed in 128
/// bits, so every g up to UINT64_MAX is exact; fee(1124) = 0, fee(1125) = 1.
/// The fee is charged ONCE per transaction, on its gross: never on a fee,
/// never compounded. A Carry keeps an account's sub-unit remainder so that
/// many small charges pay exactly what one charge of their sum pays.
///
/// A fee is split 50% reserve floor (backs Vino), 25% V-Bill dividend pool,
/// 15% infrastructure / node bounties, 10% regenerative capital; buckets
/// 1-3 get floor(fee * pct / 100) and the reserve floor gets every
/// remainder unit, so the four parts always sum to the fee exactly.
///
/// Honest limits: these are the arithmetic rules only. How a fee, a reserve
/// or a dividend pool is treated for tax, securities or consumer law is a
/// question for counsel; nothing here is a licence or a certification.
namespace fee {

inline constexpr std::uint64_t num = ZXV_FEE_NUM; ///< 8889
inline constexpr std::uint64_t den = ZXV_FEE_DEN; ///< 10^7; a carry is always below it

/// The four buckets a fee is split into (index into Split).
enum class Bucket : std::int32_t {
    reserve_floor = ZXV_FEE_RESERVE_FLOOR,   ///< 50% + every remainder unit; backs Vino
    vbill_dividend = ZXV_FEE_VBILL_DIVIDEND, ///< 25%
    infra_bounty = ZXV_FEE_INFRA_BOUNTY,     ///< 15%: infrastructure and node bounties
    regen_capital = ZXV_FEE_REGEN_CAPITAL,   ///< 10%: regenerative capital
};
/// "reserve_floor", "vbill_dividend", "infra_bounty" or "regen_capital";
/// "unknown" otherwise.
constexpr std::string_view to_string(Bucket b) noexcept
{
    switch (b) {
    case Bucket::reserve_floor: return "reserve_floor";
    case Bucket::vbill_dividend: return "vbill_dividend";
    case Bucket::infra_bounty: return "infra_bounty";
    case Bucket::regen_capital: return "regen_capital";
    }
    return "unknown";
}
/// The kinds of unit a fee may be charged in (index into Policy::rates).
enum class Unit : std::int32_t {
    money = ZXV_FEE_UNIT_MONEY,         ///< minor units of a ledger asset
    compute = ZXV_FEE_UNIT_COMPUTE,     ///< token-cycles
    bandwidth = ZXV_FEE_UNIT_BANDWIDTH, ///< bytes
    storage = ZXV_FEE_UNIT_STORAGE,     ///< byte-hours
};

/// The four parts of a fee, indexed by Bucket.
using Split = std::array<std::uint64_t, ZXV_FEE_BUCKETS>;

/// An exact non-negative ratio num/den.
struct Ratio {
    std::uint64_t num = 0; ///< numerator
    std::uint64_t den = 1; ///< denominator
    /// Same terms (not cross-multiplied).
    friend constexpr bool operator==(const Ratio&, const Ratio&) = default;
};

/// A fee and the remainder to carry to the account's next charge.
struct Charge {
    std::uint64_t fee = 0;       ///< what this charge pays
    std::uint64_t remainder = 0; ///< store it for the next charge (< den)
};

/// fee(gross) with no carry.
inline std::uint64_t assure(std::uint64_t gross)
{
    std::uint64_t f = 0;
    detail::check(zxv_fee_assure(gross, &f));
    return f;
}

/// The fee on `gross` with the account's incoming remainder `remainder_in`:
/// floor((gross * 8889 + r) / 10^7), and the new remainder. UsageError
/// (ZXV_E_RANGE) for remainder_in >= den (a corrupt carry).
inline Charge assure_carry(std::uint64_t remainder_in, std::uint64_t gross)
{
    Charge c;
    detail::check(zxv_fee_assure_carry(remainder_in, gross, &c.fee, &c.remainder));
    return c;
}

/// One account's fee carry: charge() applies assure_carry and keeps the
/// remainder. Starting from 0, many small charges pay exactly what one
/// charge of their sum pays. A plain value; not thread-safe.
class Carry {
public:
    /// Start from a stored remainder (0 for a new account). UsageError
    /// (ZXV_E_RANGE) on the first charge if it is not below den.
    explicit Carry(std::uint64_t remainder = 0) noexcept : rem_(remainder) {}
    /// The fee on `gross`; the remainder is kept for the next charge.
    std::uint64_t charge(std::uint64_t gross)
    {
        const Charge c = assure_carry(rem_, gross);
        rem_ = c.remainder;
        return c.fee;
    }
    /// The remainder to store for this account.
    [[nodiscard]] std::uint64_t remainder() const noexcept { return rem_; }

private:
    std::uint64_t rem_;
};

/// Split `fee` into its four buckets (summing to `fee` exactly).
inline Split split(std::uint64_t fee)
{
    Split s{};
    detail::check(zxv_fee_split(fee, s.data()));
    return s;
}

/// The default fee policy.
struct Policy {
    std::array<std::uint32_t, ZXV_FEE_BUCKETS> percent{}; ///< bucket shares: 50, 25, 15, 10
    bool allow_below = false;                             ///< false: a contribution below the fee is raised to it
    /// The posted VFV store-credit rate per Unit for a voluntary
    /// contribution above the fee: money 1/1, the others 0/1 (unset: the
    /// excess earns no credit until a rate is posted).
    std::array<Ratio, ZXV_FEE_UNITS> rates{};
    Ratio multiplier;  ///< the operator's credit multiplier, 1/1
    Ratio commons_cap; ///< the no-monopoly cap on one recipient's share of the non-money commons pool, 8/21
};

/// The default policy.
inline Policy default_policy()
{
    Policy p;
    std::array<std::uint64_t, ZXV_FEE_UNITS> rn{}, rd{};
    std::int32_t below = 0;
    detail::check(zxv_fee_policy_default(p.percent.data(), &below, rn.data(), rd.data(), &p.multiplier.num,
                                         &p.multiplier.den, &p.commons_cap.num, &p.commons_cap.den));
    p.allow_below = below != 0;
    for (std::size_t i = 0; i < p.rates.size(); ++i) p.rates[i] = {rn[i], rd[i]};
    return p;
}

} // namespace fee

// ===========================================================================
// The V-Bill hub (ABI 1.5)
// ===========================================================================

/// ABI 1.5: a demonstration V-Bill hub for cross-currency clearing. Each
/// test currency has ONE exact peg against the V-Bill, set only by its
/// issuer with an ML-DSA-87 signature, and a swap from currency A to B goes
/// A -> V-Bill -> B as ONE balanced, all-or-nothing six-line posting on the
/// hub's own in-process ledger:
///
///     v = a * denA / numA,   b = v * numB / denB
///
/// Both divisions must be exact or the swap is refused (UsageError
/// ZXV_E_RANGE): nothing is rounded and there is no float rate anywhere.
/// Hub::quote gives the largest exact amount not above a requested one.
///
/// RULES: every rule of the sending AND receiving currency must allow a
/// swap, and a currency with no rule refuses every swap (fail closed,
/// RefusedError ZXV_E_STATE): add a limit to each currency first. Only the
/// built-in limit rule is exposed (LimitRefused when it refuses); the
/// kernel's C rule plug-ins are not.
///
/// CAPABILITIES: creating a hub, adding currencies and rules, setting pegs,
/// opening and funding accounts and swapping need caps::ledger_post;
/// quotes, balances, currency info and the check need caps::ledger_read.
/// The hub keeps the capability it was created with, so give it both.
///
/// Honest limits: test currencies only (ISO 4217 codes are refused) and no
/// real money: the ledger is in process memory, separate from the
/// context's, and is lost on destroy. No network: it is connected to no
/// settlement rail, central bank or issuer. Peg signatures use ML-DSA-87
/// (not FIPS 140-3 validated). V-Bills are likely securities in many
/// jurisdictions; real use needs legal review and a participating issuer.
/// A rule reports what it decided; it is not itself a legal check. An exact
/// peg removes rounding, not exchange-rate risk.
namespace vhub {

inline constexpr std::uint32_t max_currencies = ZXV_VHUB_MAX_CCY;     ///< currency slots 0..7
inline constexpr std::uint32_t max_rules = ZXV_VHUB_MAX_RULES;        ///< limit rules per currency
inline constexpr std::uint32_t limit_senders = ZXV_VHUB_LIMIT_SENDERS; ///< senders one limit tracks per epoch
inline constexpr std::size_t peg_message_bytes = ZXV_VHUB_PEG_MSG_BYTES; ///< what an issuer signs
inline constexpr std::size_t code_max = ZXV_VHUB_CODE_MAX;            ///< test currency code length
inline constexpr std::uint64_t amount_max = ZXV_VHUB_AMOUNT_MAX;      ///< any one amount (2^59)
inline constexpr std::uint32_t vbill_issuer = ZXV_VHUB_VBILL_ISSUER;  ///< the V-Bill issuer's account

/// The exact 40 bytes an issuer signs to set a peg.
using PegMessage = std::array<std::uint8_t, ZXV_VHUB_PEG_MSG_BYTES>;

/// The peg message: "zxv/vhub-peg/v1" || slot || num || den || epoch (each
/// 8 bytes little-endian). Sign it with MlDsa87::sign (empty context). Pure.
inline PegMessage peg_message(std::uint8_t slot, std::uint64_t num, std::uint64_t den, std::uint64_t epoch)
{
    PegMessage m{};
    detail::check(zxv_vhub_peg_msg(slot, num, den, epoch, m.data()));
    return m;
}

/// A currency's accounts and its current peg.
struct CurrencyInfo {
    std::uint32_t issuer_account = 0; ///< the currency issuer's account
    std::uint32_t pool_account = 0;   ///< the hub's pool of the currency
    std::uint32_t vfloat_account = 0; ///< the currency's V-Bill float
    std::uint64_t peg_num = 0;        ///< num minor units of the currency ...
    std::uint64_t peg_den = 0;        ///< ... per den V-Bills (0/0: no peg yet)
    std::uint64_t peg_epoch = 0;      ///< the peg's epoch (0: no peg yet)
    /// True once a peg was set.
    [[nodiscard]] bool has_peg() const noexcept { return peg_den != 0; }
};

/// One account's rails: what it holds and owes. equity = debit - credit.
struct Balance {
    std::uint64_t debit = 0;  ///< held (A)
    std::uint64_t credit = 0; ///< owed (L; issuers only)
    std::int64_t equity = 0;  ///< debit - credit (E)
};

/// A quote: the largest exact amount not above the request, and what it
/// would carry.
struct Quote {
    std::uint64_t amount = 0;   ///< exact amount of the sending currency (0 if none)
    std::uint64_t vbills = 0;   ///< V-Bills it would move
    std::uint64_t received = 0; ///< units of the receiving currency
};

/// What a swap moved.
struct Swap {
    std::uint64_t vbills = 0;   ///< V-Bills moved
    std::uint64_t received = 0; ///< units the receiver got
};

/// One demo hub and its ledger. It belongs to the capability's context and
/// fails closed with it (FatalError). Internally serialized: any thread may
/// call it. Move-only.
class Hub {
public:
    /// A hub whose ledger starts with the V-Bill asset ("VBL") and the
    /// V-Bill issuer's account (vbill_issuer). `seed` (32 bytes; empty for a
    /// fresh random one) derives every posting's idempotency key and UETR.
    /// Needs caps::ledger_post.
    explicit Hub(const Capability& cap, std::span<const std::uint8_t> seed = {}) : cap_(detail::cap_of(cap))
    {
        if (!seed.empty()) detail::need_size(seed, 32, "hub seed");
        zxv_vhub* h = nullptr;
        cap_->call([&] { return zxv_vhub_create(cap_->cap, seed.empty() ? nullptr : seed.data(), &h); });
        h_.reset(h);
    }
    Hub(Hub&&) noexcept = default;            ///< move
    Hub& operator=(Hub&&) noexcept = default; ///< move-assign
    Hub(const Hub&) = delete;
    Hub& operator=(const Hub&) = delete;
    ~Hub() = default;

    /// Register test currency `slot` (0..7): `code` (3..12 of A-Z 0-9; not
    /// an ISO 4217 code, "VBL" or "VFV"), its minor units (0..4) and its
    /// issuer's ML-DSA-87 public key (2592 bytes; it alone may set the peg).
    /// Opens the issuer, pool and V-Bill float accounts and funds the pool
    /// with `pool_funding` and the float with `vbill_backing` V-Bills. The
    /// peg starts unset. UsageError ZXV_E_CURRENCY (an ISO code),
    /// ZXV_E_DUPLICATE (slot or code taken), ZXV_E_FORMAT / ZXV_E_RANGE.
    void add_currency(std::uint8_t slot, const std::string& code, std::uint8_t minor_units,
                      std::span<const std::uint8_t> issuer_public_key, std::uint64_t pool_funding = 0,
                      std::uint64_t vbill_backing = 0)
    {
        detail::need_size(issuer_public_key, MlDsa87::pk_bytes, "issuer public key");
        const char* c = detail::cstr(code, "currency code");
        run([&](zxv_vhub* h) {
            return zxv_vhub_add_ccy(cap_->cap, h, slot, c, minor_units, issuer_public_key.data(), pool_funding,
                                    vbill_backing);
        });
    }
    /// A currency's accounts and peg (caps::ledger_read). UsageError
    /// (ZXV_E_NOT_FOUND) for an unregistered slot.
    [[nodiscard]] CurrencyInfo currency(std::uint8_t slot) const
    {
        CurrencyInfo i;
        run([&](zxv_vhub* h) {
            return zxv_vhub_ccy_info(cap_->cap, h, slot, &i.issuer_account, &i.pool_account, &i.vfloat_account,
                                     &i.peg_num, &i.peg_den, &i.peg_epoch);
        });
        return i;
    }
    /// Set a currency's peg to num/den at `epoch` (above the current one)
    /// given the issuer's ML-DSA-87 signature (4627 bytes, empty context)
    /// over peg_message(slot, num, den, epoch). PegError (a stale or
    /// replayed epoch), VerificationFailed (not the issuer's signature over
    /// these numbers), UsageError ZXV_E_RANGE (num or den 0). A refused peg
    /// changes nothing.
    void set_peg(std::uint8_t slot, std::uint64_t num, std::uint64_t den, std::uint64_t epoch,
                 std::span<const std::uint8_t> signature)
    {
        detail::need_size(signature, MlDsa87::sig_bytes, "peg signature");
        run([&](zxv_vhub* h) { return zxv_vhub_set_peg(cap_->cap, h, slot, num, den, epoch, signature.data()); });
    }
    /// Open a holder account in currency `slot` for `owner` (an id of the
    /// caller's choosing); returns the account number. RefusedError
    /// (ZXV_E_CAPACITY) when the ledger holds 512 accounts.
    std::uint32_t open_account(std::uint8_t slot, std::uint32_t owner)
    {
        std::uint32_t a = 0;
        run([&](zxv_vhub* h) { return zxv_vhub_open_account(cap_->cap, h, slot, owner, &a); });
        return a;
    }
    /// Test faucet: issue `amount` to a holder account or a pool (by the
    /// currency's issuer), or V-Bills to a V-Bill float (by the V-Bill
    /// issuer). UsageError ZXV_E_RANGE (0, above amount_max, or a balance
    /// reaching 2^62).
    void issue(std::uint32_t account, std::uint64_t amount)
    {
        run([&](zxv_vhub* h) { return zxv_vhub_issue(cap_->cap, h, account, amount); });
    }
    /// One account's rails (caps::ledger_read).
    [[nodiscard]] Balance balance(std::uint32_t account) const
    {
        Balance b;
        run([&](zxv_vhub* h) { return zxv_vhub_balance(cap_->cap, h, account, &b.debit, &b.credit, &b.equity); });
        return b;
    }
    /// Add the built-in limit rule to currency `slot`: at most `limit` minor
    /// units out per sender per rule epoch (16 distinct senders per epoch; a
    /// 17th is refused). RefusedError (ZXV_E_CAPACITY) after 4 rules.
    void add_limit(std::uint8_t slot, std::uint64_t limit)
    {
        run([&](zxv_vhub* h) { return zxv_vhub_add_limit(cap_->cap, h, slot, limit); });
    }
    /// Start the next rule epoch (every limit restarts every sender at
    /// zero); returns the new epoch.
    std::uint64_t next_epoch()
    {
        std::uint64_t e = 0;
        run([&](zxv_vhub* h) { return zxv_vhub_next_epoch(cap_->cap, h, &e); });
        return e;
    }
    /// The largest exact amount of `from` not above `want`, and what it
    /// would carry (caps::ledger_read). Runs no rules and checks no
    /// balances, so a quoted swap may still be refused. PegError (either peg
    /// unset), UsageError (from == to).
    [[nodiscard]] Quote quote(std::uint8_t from, std::uint8_t to, std::uint64_t want) const
    {
        Quote q;
        run([&](zxv_vhub* h) { return zxv_vhub_quote(cap_->cap, h, from, to, want, &q.amount, &q.vbills, &q.received); });
        return q;
    }
    /// Swap `amount` minor units of `from` from holder account `sender` to
    /// holder account `receiver` in `to`, as one posting at `tick` (a
    /// caller-supplied time, never the wall clock). UsageError ZXV_E_RANGE
    /// (not exact under the pegs) / ZXV_E_CURRENCY (an account of another
    /// currency), PegError, RefusedError ZXV_E_STATE (a currency has no
    /// rule), LimitRefused, InsufficientFunds (sender, float or pool would
    /// go below zero). A refused swap changes nothing.
    Swap swap(std::uint8_t from, std::uint8_t to, std::uint32_t sender, std::uint32_t receiver,
              std::uint64_t amount, std::uint64_t tick)
    {
        Swap s;
        run([&](zxv_vhub* h) {
            return zxv_vhub_swap(cap_->cap, h, from, to, sender, receiver, amount, tick, &s.vbills, &s.received);
        });
        return s;
    }
    /// Verify the whole hub ledger (A = L + E per account, no negative rail,
    /// DEBIT == CREDIT per asset, the posting hash chain) and return the
    /// number of postings so far. A failure is FatalError (ZXV_E_INVARIANT)
    /// and poisons the context.
    std::uint64_t check() const
    {
        std::uint64_t n = 0;
        run([&](zxv_vhub* h) { return zxv_vhub_check(cap_->cap, h, &n); });
        return n;
    }
    /// The raw C handle (owned by this object; never destroy it).
    [[nodiscard]] zxv_vhub* native_handle() const noexcept { return h_.get(); }

private:
    template <class F>
    void run(F&& f) const
    {
        zxv_vhub* h = detail::live(h_.get(), "vhub::Hub");
        cap_->call([&] { return f(h); });
    }
    std::shared_ptr<detail::CapCore> cap_; // declared first: outlives the hub
    detail::Owned<zxv_vhub, zxv_vhub_destroy> h_;
};

} // namespace vhub


// ===========================================================================
// Freight (ABI 1.5)
// ===========================================================================

/// ABI 1.5: freight packets. A FREIGHT carries up to max_payload bytes as
/// 168 packets of 21 bytes (one UBH-168 frame each): byte 0 is the row index
/// 0..167, bytes 1..20 are row data. Rows 0..k-1 hold the payload verbatim;
/// rows k..167 are parity of a systematic Cauchy Reed-Solomon code over
/// GF(2^8). ANY k distinct rows rebuild the payload, so up to 168 - k rows
/// may be lost: k trades payload (k * 20 bytes) against loss tolerance.
///
/// The 64-byte Header travels separately (in the outer message). A payload
/// larger than one freight is split into a SET of up to max_set freights
/// sharing a freight id; a Join reassembles it in any order.
///
/// A payload may be an OP STREAM (LITERAL, COPY back references, CIDREF
/// bytes the receiver resolves by content id, SEED generators) that the
/// receiver expands. Expansion checks the declared total against the
/// caller's limit before writing a byte.
///
/// Honest limits: erasure coding only. A lost row is recovered; a CORRUPTED
/// row is detected (VerificationFailed: the payload SHA-256 fails) but not
/// located or repaired: drop rows you distrust and decode again. The header
/// is neither protected by the code nor authenticated: sign it (or the
/// outer message) if the sender matters. A freight is larger than its
/// payload (3528 wire bytes for at most 3360). Op streams make no size
/// promise: random data encodes slightly LARGER, COPY helps only data that
/// repeats, CIDREF only names bytes the receiver must already hold, SEED
/// only reproduces the built-in generators, and no information is created.
///
/// Everything here is pure (no capability, no context). Join and OpsWriter
/// hold only their own memory and are not thread-safe.
namespace freight {

inline constexpr std::size_t packet_bytes = ZXV_FREIGHT_PACKET_BYTES;  ///< 21: one row on the wire
inline constexpr std::size_t row_payload = ZXV_FREIGHT_ROW_PAYLOAD;    ///< 20 payload bytes per row
inline constexpr std::uint32_t rows = ZXV_FREIGHT_ROWS;                ///< 168 rows per freight
inline constexpr std::size_t freight_bytes = ZXV_FREIGHT_BYTES;        ///< 3528: one encoded freight
inline constexpr std::size_t max_payload = ZXV_FREIGHT_MAX_PAYLOAD;    ///< 3360, at k = 168
inline constexpr std::size_t header_bytes = ZXV_FREIGHT_HEADER_BYTES;  ///< 64
inline constexpr std::uint8_t version = ZXV_FREIGHT_VERSION;           ///< header version 1
inline constexpr std::uint32_t max_set = ZXV_FREIGHT_MAX_SET;          ///< freights in one set
inline constexpr std::uint64_t set_max_bytes = ZXV_FREIGHT_SET_MAX_BYTES; ///< largest set payload
inline constexpr std::uint8_t f_opstream = ZXV_FREIGHT_F_OPSTREAM;     ///< header flag: payload is an op stream
inline constexpr std::size_t cid_max = ZXV_FREIGHT_CID_MAX;            ///< longest CIDREF content id
inline constexpr std::size_t gen_param_max = ZXV_FREIGHT_GEN_PARAM_MAX; ///< longest SEED params
inline constexpr std::uint64_t ops_max = ZXV_FREIGHT_OPS_MAX;          ///< largest op stream or expansion (64 MiB)

/// The built-in SEED generators.
enum class Generator : std::uint8_t {
    xorshift = ZXV_FREIGHT_GEN_XORSHIFT,     ///< 8 bytes: u64 LE seed, nonzero; xorshift64* bytes
    repeat = ZXV_FREIGHT_GEN_REPEAT,         ///< 1..32 bytes: the pattern, repeated
    fibword = ZXV_FREIGHT_GEN_FIBWORD,       ///< 2 distinct bytes a, b: the Fibonacci word abaab...
    sierpinski = ZXV_FREIGHT_GEN_SIERPINSKI, ///< 3 bytes: log2 width (1..15), on, off
};

/// The fields of a freight header.
struct HeaderFields {
    std::uint8_t version = ZXV_FREIGHT_VERSION; ///< always 1 (ignored by Header::from_fields)
    std::uint8_t k = 0;                         ///< data rows, 1..168
    std::uint8_t flags = 0;                     ///< 0 or f_opstream
    std::uint16_t payload_len = 0;              ///< this freight's payload bytes (<= k * 20)
    std::uint16_t seq = 0;                      ///< member index in its set
    std::uint16_t seq_count = 1;                ///< members in the set, 1..16384
    std::uint64_t freight_id = 0;               ///< shared by a set's members
    std::uint64_t expand_limit = 0;             ///< op streams only, else 0
    Digest32 payload_sha256{};                  ///< SHA-256 of this freight's payload
};

/// A serialized 64-byte freight header: "ZXFH", version, k, flags, payload
/// length, seq, seq count, freight id, expand limit, payload SHA-256. A
/// value type; every Header made here passed libzxv's check.
class Header {
public:
    /// The header of a single freight (seq 0 of 1) carrying `payload` with
    /// `k` data rows (1..168); it hashes the payload. `expand_limit` must be
    /// 0 unless flags has f_opstream. UsageError ZXV_E_ARG / ZXV_E_TOO_LONG
    /// (payload over k * 20 bytes).
    static Header for_payload(std::uint64_t freight_id, std::uint8_t k, std::span<const std::uint8_t> payload,
                              std::uint8_t flags = 0, std::uint64_t expand_limit = 0)
    {
        Header h;
        detail::check(zxv_freight_header_init(freight_id, k, flags, expand_limit, payload.data(), payload.size(),
                                              h.b_.data()));
        return h;
    }
    /// Serialize a header from its fields (a set member, or one received
    /// field by field). UsageError (ZXV_E_ARG) for any field breaking the
    /// header rules; the version is always 1.
    static Header from_fields(const HeaderFields& f)
    {
        Header h;
        detail::check(zxv_freight_header_serialize(f.k, f.flags, f.payload_len, f.seq, f.seq_count, f.freight_id,
                                                   f.expand_limit, f.payload_sha256.data(), h.b_.data()));
        return h;
    }
    /// Check 64 received bytes. UsageError (ZXV_E_FORMAT: bad magic,
    /// reserved bytes or a field; ZXV_E_ARG for another length).
    static Header parse(std::span<const std::uint8_t> bytes)
    {
        Header h;
        h.b_ = detail::fixed<ZXV_FREIGHT_HEADER_BYTES>(bytes, "freight header");
        detail::check(zxv_freight_header_check(h.b_.data()));
        return h;
    }
    /// The fields.
    [[nodiscard]] HeaderFields fields() const
    {
        HeaderFields f;
        detail::check(zxv_freight_header_parse(b_.data(), &f.version, &f.k, &f.flags, &f.payload_len, &f.seq,
                                               &f.seq_count, &f.freight_id, &f.expand_limit, f.payload_sha256.data()));
        return f;
    }
    /// The 64 bytes.
    [[nodiscard]] std::span<const std::uint8_t, ZXV_FREIGHT_HEADER_BYTES> bytes() const noexcept { return b_; }
    /// Same bytes.
    friend bool operator==(const Header&, const Header&) = default;

private:
    Header() = default;
    std::array<std::uint8_t, ZXV_FREIGHT_HEADER_BYTES> b_{};
};

/// Encode the 168 packets (freight_bytes bytes, rows in order) of the
/// freight `header` describes. `payload` must be exactly what the header
/// was built for: another length is UsageError (ZXV_E_ARG), another hash
/// VerificationFailed.
inline std::vector<std::uint8_t> encode(const Header& header, std::span<const std::uint8_t> payload)
{
    std::vector<std::uint8_t> p(freight_bytes);
    detail::check(zxv_freight_encode(header.bytes().data(), payload.data(), payload.size(), p.data()));
    return p;
}

/// Rebuild the payload from whichever packets arrived: `packets` holds
/// packets_len / 21 packets in any order; duplicates and row indices above
/// 167 are ignored and the first k distinct rows are used. ErasureError
/// (fewer than k distinct rows), VerificationFailed (a corrupted row or a
/// header from another freight), UsageError ZXV_E_ARG (a length not a
/// multiple of 21).
inline std::vector<std::uint8_t> decode(const Header& header, std::span<const std::uint8_t> packets)
{
    return detail::read_bytes([&](std::uint8_t* b, std::size_t c, std::size_t* n) {
        return zxv_freight_decode(header.bytes().data(), packets.data(), packets.size(), b, c, n);
    }, nullptr, max_payload);
}

/// Freights needed to carry `len` bytes at `k` (0 bytes still needs one).
/// UsageError (ZXV_E_TOO_LONG) above max_set.
inline std::uint32_t set_count(std::uint64_t len, std::uint8_t k)
{
    std::uint32_t c = 0;
    detail::check(zxv_freight_set_count(len, k, &c));
    return c;
}

/// One member of a freight set: its header and its 168 packets.
struct Member {
    Header header;                     ///< travels in the outer message
    std::vector<std::uint8_t> packets; ///< freight_bytes bytes
};

/// Member `seq` of the set carrying `data` (it carries bytes
/// [seq * k * 20, ...)). UsageError ZXV_E_RANGE (seq not below
/// set_count(data.size(), k)), ZXV_E_TOO_LONG (more than max_set freights).
inline Member split(std::uint64_t freight_id, std::uint8_t k, std::span<const std::uint8_t> data, std::uint32_t seq,
                    std::uint8_t flags = 0, std::uint64_t expand_limit = 0)
{
    std::array<std::uint8_t, ZXV_FREIGHT_HEADER_BYTES> h{};
    std::vector<std::uint8_t> p(freight_bytes);
    detail::check(zxv_freight_split(freight_id, k, flags, expand_limit, data.data(), data.size(), seq, h.data(), p.data()));
    return {Header::parse(h), std::move(p)};
}

/// Progress of a Join.
struct JoinStatus {
    std::uint32_t received = 0; ///< members accepted
    std::uint32_t count = 0;    ///< members in the set (0 until the first is accepted)
    bool complete = false;      ///< every member arrived
    std::uint64_t total = 0;    ///< the joined length once complete, else 0
};

/// Reassembles one freight set, member by member, in any order, into a
/// buffer of `capacity` bytes. The first member fixes the set. Not
/// thread-safe; move-only.
class Join {
public:
    /// A join for a set of at most `capacity` bytes (0..set_max_bytes,
    /// else UsageError ZXV_E_RANGE).
    explicit Join(std::uint64_t capacity)
    {
        zxv_freight_join* j = nullptr;
        detail::check(zxv_freight_join_create(capacity, &j));
        j_.reset(j);
    }
    Join(Join&&) noexcept = default;            ///< move
    Join& operator=(Join&&) noexcept = default; ///< move-assign
    Join(const Join&) = delete;
    Join& operator=(const Join&) = delete;
    ~Join() = default;

    /// Decode one member into its place; a member already received changes
    /// nothing. RefusedError ZXV_E_STATE (another set, or a non-last member
    /// not full), RefusedError ZXV_E_CAPACITY (the set cannot fit), and the
    /// errors of decode(). A failed member is not recorded: add it again
    /// when more rows arrive.
    void add(const Header& header, std::span<const std::uint8_t> packets)
    {
        detail::check(zxv_freight_join_add(h(), header.bytes().data(), packets.data(), packets.size()));
    }
    /// Add a Member made by split().
    void add(const Member& m) { add(m.header, m.packets); }
    /// Progress.
    [[nodiscard]] JoinStatus status() const
    {
        JoinStatus s;
        std::int32_t c = 0;
        detail::check(zxv_freight_join_status(h(), &s.received, &s.count, &c, &s.total));
        s.complete = c != 0;
        return s;
    }
    /// The joined bytes. RefusedError (ZXV_E_STATE) until every member arrived.
    [[nodiscard]] std::vector<std::uint8_t> result() const
    {
        return detail::read_bytes([&](std::uint8_t* b, std::size_t c, std::size_t* n) {
            return zxv_freight_join_result(h(), b, c, n);
        });
    }
    /// The raw C handle (owned by this object; never destroy it).
    [[nodiscard]] zxv_freight_join* native_handle() const noexcept { return j_.get(); }

private:
    [[nodiscard]] zxv_freight_join* h() const { return detail::live(j_.get(), "freight::Join"); }
    detail::Owned<zxv_freight_join, zxv_freight_join_destroy> j_;
};

/// What an op stream declares.
struct OpsInfo {
    std::uint64_t total = 0; ///< bytes its expansion produces
    bool has_sha = false;    ///< it carries the SHA-256 of its output
};

/// Read an op stream's declared total without expanding it. UsageError
/// (ZXV_E_FORMAT) for a bad stream header.
inline OpsInfo peek(std::span<const std::uint8_t> ops)
{
    OpsInfo i;
    std::int32_t s = 0;
    detail::check(zxv_freight_ops_peek(ops.data(), ops.size(), &i.total, &s));
    i.has_sha = s != 0;
    return i;
}

/// A CIDREF resolver: fill `out` (exactly out.size() bytes) with the content
/// `cid` names and return true, or return false if it cannot. It must not
/// expand op streams itself. An exception it throws is caught before C,
/// ends the expansion and is rethrown by expand().
using Resolver = std::function<bool(std::span<const std::uint8_t> cid, std::span<std::uint8_t> out)>;

namespace detail {
struct ResolverSlot {
    const Resolver* fn = nullptr;
    std::exception_ptr error;
    static std::int32_t trampoline(void* user, const std::uint8_t* cid, std::size_t cid_len, std::uint8_t* out,
                                   std::uint64_t len) noexcept
    {
        auto* self = static_cast<ResolverSlot*>(user);
        try {
            if (len > static_cast<std::uint64_t>(SIZE_MAX)) return ZXV_E_RANGE;
            return (*self->fn)(std::span<const std::uint8_t>(cid, cid ? cid_len : 0),
                               std::span<std::uint8_t>(out, out ? static_cast<std::size_t>(len) : 0))
                       ? ZXV_OK
                       : ZXV_E_NOT_FOUND;
        } catch (...) { // never unwind through C
            self->error = std::current_exception();
            return ZXV_E_INTERNAL;
        }
    }
};
} // namespace detail

/// Expand an op stream, accepting at most `limit` bytes (<= ops_max, else
/// UsageError ZXV_E_RANGE); a declared total above it is UsageError
/// ZXV_E_TOO_LONG before anything is written. `resolve` supplies CIDREF
/// content (none: a CIDREF fails with UsageError ZXV_E_NOT_FOUND).
/// UsageError ZXV_E_FORMAT (a malformed stream), VerificationFailed (the
/// output fails the stream's SHA-256).
inline std::vector<std::uint8_t> expand(std::span<const std::uint8_t> ops, std::uint64_t limit,
                                        const Resolver& resolve = {})
{
    const OpsInfo info = peek(ops);
    const bool fits = limit <= ops_max && info.total <= limit;
    std::vector<std::uint8_t> out(fits ? static_cast<std::size_t>(info.total) : 0);
    detail::ResolverSlot slot{&resolve, nullptr};
    std::size_t n = 0;
    const std::int32_t st = zxv_freight_ops_expand(ops.data(), ops.size(), limit,
                                                   resolve ? &detail::ResolverSlot::trampoline : nullptr,
                                                   resolve ? &slot : nullptr, out.empty() ? nullptr : out.data(),
                                                   out.size(), &n);
    if (slot.error) std::rethrow_exception(slot.error);
    zxv::detail::check(st);
    out.resize(n);
    return out;
}

/// Run one SEED generator directly: `len` bytes. UsageError (ZXV_E_ARG) for
/// an unknown generator or params it refuses.
inline std::vector<std::uint8_t> generate(Generator gen, std::span<const std::uint8_t> params, std::size_t len)
{
    std::vector<std::uint8_t> out(len);
    zxv::detail::check(zxv_freight_gen_run(static_cast<std::uint8_t>(gen), params.data(), params.size(),
                                           out.empty() ? nullptr : out.data(), len));
    return out;
}

/// Encode bytes as a LITERAL/COPY op stream (greedy LZ77, window 65536,
/// deterministic), with the output SHA-256 when `with_sha`. The result may
/// be LARGER than the input. UsageError (ZXV_E_TOO_LONG) above ops_max.
inline std::vector<std::uint8_t> encode_ops(std::span<const std::uint8_t> in, bool with_sha = false)
{
    return zxv::detail::read_bytes([&](std::uint8_t* b, std::size_t c, std::size_t* n) {
        return zxv_freight_ops_encode(in.data(), in.size(), with_sha ? 1 : 0, b, c, n);
    });
}

/// The worst-case encode_ops size of `n` input bytes (all literals).
inline std::uint64_t ops_bound(std::uint64_t n, bool with_sha = false)
{
    std::uint64_t b = 0;
    zxv::detail::check(zxv_freight_ops_bound(n, with_sha ? 1 : 0, &b));
    return b;
}

/// Builds an op stream op by op. `declared_total` is what the ops must
/// produce in all; `sha256` (optional) is the SHA-256 of that output, which
/// expansion will verify. Every put throws on its own error, and the first
/// error is sticky: later puts and finish() throw it again. Not
/// thread-safe; move-only. The put functions return *this for chaining.
class OpsWriter {
public:
    /// A writer whose buffer holds at most `max_bytes` (14, or 46 with a
    /// SHA-256, up to ops_max).
    OpsWriter(std::uint64_t declared_total, std::uint64_t max_bytes, std::optional<Digest32> sha256 = std::nullopt)
    {
        zxv_freight_ops_writer* w = nullptr;
        zxv::detail::check(zxv_freight_ops_writer_create(declared_total, sha256 ? sha256->data() : nullptr, max_bytes, &w));
        w_.reset(w);
    }
    OpsWriter(OpsWriter&&) noexcept = default;            ///< move
    OpsWriter& operator=(OpsWriter&&) noexcept = default; ///< move-assign
    OpsWriter(const OpsWriter&) = delete;
    OpsWriter& operator=(const OpsWriter&) = delete;
    ~OpsWriter() = default;

    /// LITERAL: these bytes verbatim.
    OpsWriter& literal(std::span<const std::uint8_t> bytes)
    {
        zxv::detail::check(zxv_freight_ops_put_literal(h(), bytes.data(), bytes.size()));
        return *this;
    }
    /// COPY: `len` bytes from `distance` back (overlap allowed).
    OpsWriter& copy(std::uint64_t distance, std::uint64_t len)
    {
        zxv::detail::check(zxv_freight_ops_put_copy(h(), distance, len));
        return *this;
    }
    /// CIDREF: `len` bytes the receiver resolves by content id `cid`
    /// (1..cid_max bytes).
    OpsWriter& cidref(std::span<const std::uint8_t> cid, std::uint64_t len)
    {
        zxv::detail::check(zxv_freight_ops_put_cidref(h(), cid.data(), cid.size(), len));
        return *this;
    }
    /// CIDREF by a binary CID.
    OpsWriter& cidref(const cid::Cid& c, std::uint64_t len) { return cidref(c.bytes(), len); }
    /// SEED: `len` bytes of a built-in generator.
    OpsWriter& seed(Generator gen, std::span<const std::uint8_t> params, std::uint64_t len)
    {
        zxv::detail::check(zxv_freight_ops_put_seed(h(), static_cast<std::uint8_t>(gen), params.data(), params.size(), len));
        return *this;
    }
    /// The finished stream. RefusedError (ZXV_E_STATE) while the ops produce
    /// fewer bytes than declared. The writer stays usable.
    [[nodiscard]] std::vector<std::uint8_t> finish() const
    {
        return zxv::detail::read_bytes([&](std::uint8_t* b, std::size_t c, std::size_t* n) {
            return zxv_freight_ops_writer_finish(h(), b, c, n);
        });
    }
    /// The raw C handle (owned by this object; never destroy it).
    [[nodiscard]] zxv_freight_ops_writer* native_handle() const noexcept { return w_.get(); }

private:
    [[nodiscard]] zxv_freight_ops_writer* h() const { return zxv::detail::live(w_.get(), "freight::OpsWriter"); }
    zxv::detail::Owned<zxv_freight_ops_writer, zxv_freight_ops_writer_destroy> w_;
};

} // namespace freight

// ===========================================================================
// Web 4 agents (ABI 1.5)
// ===========================================================================

/// ABI 1.5: the Web 4 agent layer. Agents hold ML-DSA-65 keys (FIPS 204)
/// and publish signed tool manifests; peers keep a Directory of verified
/// agent cards; a human's consent token is required before an agent moves
/// money or shares personal data; requests and replies travel as signed
/// envelopes. The kernel's rules, kept here:
///
///   * IDENTITY: agent_id = SHA3-256(agent public key). An agent is bound
///     to the node hosting it by a record signed by the node's key
///     (peer_id = SHA3-256(node public key)); a self-bound agent has
///     peer_id == agent_id.
///   * CONTEXTS: every signature carries a purpose string, so a signature
///     for one purpose cannot be replayed as another.
///   * CONSENT: a token signed by the HUMAN's key binds the exact tool,
///     parties, request id and payload; tokens are single-use.
///
/// SECRETS: a private key never crosses the ABI. It is generated inside an
/// Agent (also used for a human's key: human_id is its agent_id), used there
/// for signing, and wiped when the Agent is destroyed. There is no call that
/// returns it.
///
/// TIME: there is no clock here; every call that needs the time takes
/// now_ms (Unix milliseconds) from the caller.
///
/// Honest limits: envelopes are signed, not encrypted. A consent token
/// proves that a key holder signed; that the holder is the right human and
/// understood what they signed depends on the device and UI that showed the
/// action. Replay windows, rate limits, freight carriage and the Web 2 /
/// Web 3 bridges are not in this ABI; interoperability is between ZXV
/// instances only. Built without PQC, every call throws NotImplemented.
namespace web4 {

inline constexpr std::size_t pk_bytes = ZXV_W4_PK_BYTES;           ///< ML-DSA-65 public key
inline constexpr std::size_t sig_bytes = ZXV_W4_SIG_BYTES;         ///< ML-DSA-65 signature
inline constexpr std::size_t id_bytes = ZXV_W4_ID_BYTES;           ///< agent_id, peer_id, human_id
inline constexpr std::size_t name_max = ZXV_W4_NAME_MAX;           ///< agent and tool names: 1..31 of [a-z0-9._-]
inline constexpr std::uint32_t max_tools = ZXV_W4_MAX_TOOLS;       ///< tools per manifest
inline constexpr std::size_t max_payload = ZXV_W4_MSG_MAX_PAYLOAD; ///< envelope payload
inline constexpr std::size_t consent_bytes = ZXV_W4_CONSENT_BYTES; ///< an encoded consent token
inline constexpr std::size_t card_max = ZXV_W4_CARD_MAX;           ///< a node-bound card
inline constexpr std::size_t manifest_max = ZXV_W4_MANIFEST_MAX;   ///< a signed manifest
inline constexpr std::size_t envelope_max = ZXV_W4_ENV_MAX;        ///< a signed envelope
inline constexpr std::uint32_t directory_max = ZXV_W4_DIR_MAX;     ///< directory entries
inline constexpr std::uint32_t consent_log_max = ZXV_W4_CONSENT_LOG_MAX; ///< consent log entries

/// Tool flags (Tool::flags).
namespace tool {
inline constexpr std::uint32_t money = ZXV_W4_TOOL_MONEY;   ///< the tool moves money
inline constexpr std::uint32_t pii = ZXV_W4_TOOL_PII;       ///< the tool reveals personal data
inline constexpr std::uint32_t stream = ZXV_W4_TOOL_STREAM; ///< answers with STREAM chunks
} // namespace tool
/// Consent scopes (what a consent token allows).
namespace scope {
inline constexpr std::uint32_t money = ZXV_W4_CONSENT_MONEY; ///< move money up to max_vfv
inline constexpr std::uint32_t pii = ZXV_W4_CONSENT_PII;     ///< share personal data
} // namespace scope
/// Envelope flags.
namespace msg_flag {
inline constexpr std::uint32_t final_ = ZXV_W4_MF_FINAL;     ///< last STREAM chunk / final response
inline constexpr std::uint32_t consent = ZXV_W4_MF_CONSENT;  ///< a token is attached (set by sealing, never by the caller)
} // namespace msg_flag

/// The kind of an envelope.
enum class MsgKind : std::int32_t {
    request = ZXV_W4_MSG_REQUEST,   ///< ask an agent to run a tool
    response = ZXV_W4_MSG_RESPONSE, ///< the answer
    stream = ZXV_W4_MSG_STREAM,     ///< one chunk of a streamed answer
    cancel = ZXV_W4_MSG_CANCEL,     ///< cancel a request
    error = ZXV_W4_MSG_ERROR,       ///< the request failed
};

/// A 32-byte agent, peer or human id (a SHA3-256).
using Id = std::array<std::uint8_t, ZXV_W4_ID_BYTES>;

/// What a verified card says.
struct CardInfo {
    Id agent_id{};             ///< SHA3-256 of the agent's key
    Id peer_id{};              ///< SHA3-256 of the hosting node's key (== agent_id when self-bound)
    std::string name;          ///< the agent's name
    std::uint64_t bound_ms = 0; ///< when the binding was signed
};

/// Verify a card (well formed, binding signature valid under the node key,
/// or the agent's own key when self-bound) and read it. UsageError
/// (ZXV_E_FORMAT), VerificationFailed.
inline CardInfo verify_card(std::span<const std::uint8_t> card)
{
    CardInfo c;
    std::array<char, 32> nm{};
    detail::check(zxv_w4_card_verify(card.data(), card.size(), c.agent_id.data(), c.peer_id.data(), nm.data(), &c.bound_ms));
    c.name = nm.data();
    return c;
}

/// The SHA3-256 digest a consent token for one request must carry: over
/// "web4/v1/action", tool, from, to, req_id and the payload (at most
/// max_payload bytes, else UsageError ZXV_E_TOO_LONG). Pure.
inline Id action_digest(const std::string& tool, const Id& from, const Id& to, std::uint64_t req_id,
                        std::span<const std::uint8_t> payload)
{
    Id d{};
    const char* t = detail::cstr(tool, "tool name");
    detail::check(zxv_w4_action_digest(t, from.data(), to.data(), req_id, payload.data(), payload.size(), d.data()));
    return d;
}

/// The fields of a consent token (read without checking its signature).
struct ConsentInfo {
    Id human_id{};                       ///< who consented
    Id agent_id{};                       ///< which agent may act
    Id action{};                         ///< the action digest it covers
    std::uint32_t scope = 0;             ///< scope::* bits
    std::uint64_t max_vfv = 0;           ///< the most VFV minor units it allows
    std::uint64_t issued_ms = 0;         ///< valid from
    std::uint64_t expires_ms = 0;        ///< valid before
    std::array<std::uint8_t, 16> nonce{}; ///< makes it single-use
};

/// An encoded consent token (consent_bytes bytes, "W4CS"): a value type.
class ConsentToken {
public:
    /// Check the format of received bytes (UsageError ZXV_E_FORMAT or
    /// ZXV_E_ARG). The signature is checked by ConsentLog::check.
    static ConsentToken from_bytes(std::span<const std::uint8_t> bytes)
    {
        ConsentToken t;
        t.b_ = detail::fixed<ZXV_W4_CONSENT_BYTES>(bytes, "consent token");
        (void) t.info();
        return t;
    }
    /// The fields (no signature check).
    [[nodiscard]] ConsentInfo info() const
    {
        ConsentInfo i;
        detail::check(zxv_w4_consent_info(b_.data(), i.human_id.data(), i.agent_id.data(), i.action.data(), &i.scope,
                                          &i.max_vfv, &i.issued_ms, &i.expires_ms, i.nonce.data()));
        return i;
    }
    /// The encoded bytes.
    [[nodiscard]] std::span<const std::uint8_t> bytes() const noexcept { return b_; }
    /// Same bytes.
    friend bool operator==(const ConsentToken&, const ConsentToken&) = default;

private:
    friend class Agent;
    ConsentToken() = default;
    std::array<std::uint8_t, ZXV_W4_CONSENT_BYTES> b_{};
};

class Manifest;

/// A key handle: an agent's (or a human's) ML-DSA-65 key pair and its
/// signed card. The private key never leaves it and is wiped when it is
/// destroyed. It belongs to the capability's context and fails closed with
/// it. The capability it keeps signs for it: caps::crypto_sign for the
/// card, manifests and envelopes; caps::attest as well to issue consent.
/// Not thread-safe (sealing advances its envelope sequence). Move-only.
class Agent {
public:
    /// Create a key handle named `name` (1..31 of [a-z0-9._-]) bound at
    /// `now_ms`. `node`: the agent hosting it (it signs the binding; same
    /// context), or nullptr for a self-bound agent. `seed`: 32 bytes for a
    /// reproducible key, or empty for a fresh one. Needs caps::crypto_sign.
    Agent(const Capability& cap, const std::string& name, std::uint64_t now_ms, const Agent* node = nullptr,
          std::span<const std::uint8_t> seed = {})
        : cap_(detail::cap_of(cap))
    {
        if (!seed.empty()) detail::need_size(seed, 32, "agent seed");
        const char* n = detail::cstr(name, "agent name");
        const zxv_w4_agent* nd = node ? node->h() : nullptr;
        zxv_w4_agent* a = nullptr;
        cap_->call([&] { return zxv_w4_agent_create(cap_->cap, seed.empty() ? nullptr : seed.data(), n, nd, now_ms, &a); });
        a_.reset(a);
    }
    Agent(Agent&&) noexcept = default;            ///< move
    Agent& operator=(Agent&&) noexcept = default; ///< move-assign
    Agent(const Agent&) = delete;
    Agent& operator=(const Agent&) = delete;
    ~Agent() = default;

    /// SHA3-256 of the public key.
    [[nodiscard]] Id agent_id() const
    {
        Id i{};
        run([&](zxv_w4_agent* a) { return zxv_w4_agent_info(a, i.data(), nullptr, nullptr); });
        return i;
    }
    /// SHA3-256 of the hosting node's public key (== agent_id when self-bound).
    [[nodiscard]] Id peer_id() const
    {
        Id i{};
        run([&](zxv_w4_agent* a) { return zxv_w4_agent_info(a, nullptr, i.data(), nullptr); });
        return i;
    }
    /// The ML-DSA-65 public key (pk_bytes bytes).
    [[nodiscard]] std::vector<std::uint8_t> public_key() const
    {
        std::vector<std::uint8_t> pk(pk_bytes);
        run([&](zxv_w4_agent* a) { return zxv_w4_agent_info(a, nullptr, nullptr, pk.data()); });
        return pk;
    }
    /// The agent's card: what a peer needs to verify it (verify_card).
    [[nodiscard]] std::vector<std::uint8_t> card() const
    {
        zxv_w4_agent* a = h();
        return detail::read_bytes([&](std::uint8_t* b, std::size_t c, std::size_t* n) {
            detail::ensure_alive(cap_->ctx.get());
            return zxv_w4_agent_card(a, b, c, n);
        }, cap_->ctx.get(), card_max);
    }

    /// Issue a consent token on the HUMAN's side (this handle is the
    /// human's key), after showing them the action: it lets `agent` do
    /// `action` within `scope` (scope::* bits, nonzero), money up to
    /// `max_vfv` minor units, valid in [issued_ms, expires_ms). `nonce`: 16
    /// bytes, or nullopt for a random one. Needs caps::attest.
    [[nodiscard]] ConsentToken issue_consent(const Id& agent, const Id& action, std::uint32_t scope,
                                             std::uint64_t max_vfv, std::uint64_t issued_ms, std::uint64_t expires_ms,
                                             std::optional<std::array<std::uint8_t, 16>> nonce = std::nullopt) const
    {
        ConsentToken t;
        run([&](zxv_w4_agent* a) {
            return zxv_w4_consent_issue(cap_->cap, a, agent.data(), action.data(), scope, max_vfv, issued_ms, expires_ms,
                                        nonce ? nonce->data() : nullptr, t.b_.data());
        });
        return t;
    }

    /// Build, sign and encode an envelope from this agent to `to`, using the
    /// next sequence number. `kind`: any MsgKind; `flags`: 0 or
    /// msg_flag::final_; `tool`: a valid tool name; `payload` at most
    /// max_payload bytes; `consent`: a token to attach, or nullptr. Needs
    /// caps::crypto_sign.
    std::vector<std::uint8_t> seal(MsgKind kind, const Id& to, std::uint64_t req_id, std::uint32_t chunk,
                                   const std::string& tool, std::uint32_t flags, std::span<const std::uint8_t> payload,
                                   std::uint64_t now_ms, const ConsentToken* consent = nullptr)
    {
        const char* t = detail::cstr(tool, "tool name");
        zxv_w4_agent* a = h();
        return detail::read_bytes([&](std::uint8_t* b, std::size_t c, std::size_t* n) {
            detail::ensure_alive(cap_->ctx.get());
            return zxv_w4_env_seal(cap_->cap, a, static_cast<std::int32_t>(kind), to.data(), req_id, chunk, t, flags,
                                   payload.data(), payload.size(), consent ? consent->bytes().data() : nullptr, now_ms,
                                   b, c, n);
        }, cap_->ctx.get(), envelope_max);
    }
    /// Seal a reply to a REQUEST envelope addressed to this agent (to/from
    /// swapped, same request id and tool). `kind`: response, stream or
    /// error. The request's signature is not checked here (verify_envelope).
    std::vector<std::uint8_t> reply(std::span<const std::uint8_t> request, MsgKind kind, std::uint32_t chunk,
                                    std::uint32_t flags, std::span<const std::uint8_t> payload, std::uint64_t now_ms)
    {
        zxv_w4_agent* a = h();
        return detail::read_bytes([&](std::uint8_t* b, std::size_t c, std::size_t* n) {
            detail::ensure_alive(cap_->ctx.get());
            return zxv_w4_env_reply(cap_->cap, a, request.data(), request.size(), static_cast<std::int32_t>(kind), chunk,
                                    flags, payload.data(), payload.size(), now_ms, b, c, n);
        }, cap_->ctx.get(), envelope_max);
    }

    /// The raw C handle (owned by this object; never destroy it).
    [[nodiscard]] zxv_w4_agent* native_handle() const noexcept { return a_.get(); }

private:
    friend class Manifest;
    [[nodiscard]] zxv_w4_agent* h() const { return detail::live(a_.get(), "web4::Agent"); }
    template <class F>
    void run(F&& f) const
    {
        zxv_w4_agent* a = h();
        cap_->call([&] { return f(a); });
    }
    std::shared_ptr<detail::CapCore> cap_; // declared first: outlives the key handle
    detail::Owned<zxv_w4_agent, zxv_w4_agent_destroy> a_;
};

/// A directory of verified agent cards, keyed by agent_id. Not
/// thread-safe; move-only.
class Directory {
public:
    /// A directory of at most `capacity` cards (1..directory_max).
    explicit Directory(std::uint32_t capacity)
    {
        zxv_w4_dir* d = nullptr;
        detail::check(zxv_w4_dir_create(capacity, &d));
        d_.reset(d);
    }
    Directory(Directory&&) noexcept = default;            ///< move
    Directory& operator=(Directory&&) noexcept = default; ///< move-assign
    Directory(const Directory&) = delete;
    Directory& operator=(const Directory&) = delete;
    ~Directory() = default;

    /// Verify a card, then add it or replace the entry for the same agent.
    /// VerificationFailed, UsageError (ZXV_E_FORMAT), RefusedError
    /// ZXV_E_STATE (its binding is older than the one held: no rollback),
    /// RefusedError ZXV_E_CAPACITY (full).
    void add(std::span<const std::uint8_t> card) { detail::check(zxv_w4_dir_add(h(), card.data(), card.size())); }
    /// The card held for `agent_id`, or nullopt.
    [[nodiscard]] std::optional<std::vector<std::uint8_t>> find(const Id& agent_id) const
    {
        std::vector<std::uint8_t> v(card_max);
        std::size_t n = 0;
        const std::int32_t st = zxv_w4_dir_find(h(), agent_id.data(), v.data(), v.size(), &n);
        if (st == ZXV_E_NOT_FOUND) return std::nullopt;
        detail::check(st);
        v.resize(n);
        return v;
    }
    /// Number of cards held.
    [[nodiscard]] std::uint32_t count() const
    {
        std::uint32_t n = 0;
        detail::check(zxv_w4_dir_count(h(), &n));
        return n;
    }
    /// The raw C handle (owned by this object; never destroy it).
    [[nodiscard]] zxv_w4_dir* native_handle() const noexcept { return d_.get(); }

private:
    [[nodiscard]] zxv_w4_dir* h() const { return detail::live(d_.get(), "web4::Directory"); }
    detail::Owned<zxv_w4_dir, zxv_w4_dir_destroy> d_;
};

/// One tool of a manifest.
struct Tool {
    std::string name;             ///< 1..31 of [a-z0-9._-]
    std::uint64_t price_vfv = 0;  ///< price in VFV minor units (2 digits)
    std::uint32_t rate_per_min = 1; ///< calls per minute (>= 1)
    std::uint32_t burst = 1;      ///< burst (>= 1)
    std::uint32_t flags = 0;      ///< tool::* bits
    std::uint32_t max_input = 0;  ///< largest input (<= max_payload)
};

/// A manifest's header fields.
struct ManifestInfo {
    Id agent_id{};               ///< the signer (zeros until signed)
    Id peer_id{};                ///< the signer's node (zeros until signed)
    std::uint32_t version = 0;   ///< the caller's manifest version
    std::uint64_t issued_ms = 0; ///< valid from
    std::uint64_t expires_ms = 0; ///< valid before
    std::uint32_t tool_count = 0; ///< tools listed
    bool is_signed = false;      ///< signed and unchanged since
};

/// A signed tool manifest: up to max_tools tools with prices, rate limits
/// and flags. Signing fixes the agent and peer ids; adding a tool afterwards
/// drops the signature. Not thread-safe; move-only.
class Manifest {
public:
    /// An empty, unsigned manifest valid in [issued_ms, expires_ms).
    Manifest(std::uint32_t version, std::uint64_t issued_ms, std::uint64_t expires_ms)
    {
        zxv_w4_manifest* m = nullptr;
        detail::check(zxv_w4_manifest_create(version, issued_ms, expires_ms, &m));
        m_.reset(m);
    }
    Manifest(Manifest&&) noexcept = default;            ///< move
    Manifest& operator=(Manifest&&) noexcept = default; ///< move-assign
    Manifest(const Manifest&) = delete;
    Manifest& operator=(const Manifest&) = delete;
    ~Manifest() = default;

    /// Parse a signed manifest's wire form. The signature is kept but NOT
    /// checked: call verify(). UsageError (ZXV_E_FORMAT).
    static Manifest decode(std::span<const std::uint8_t> bytes)
    {
        zxv_w4_manifest* m = nullptr;
        detail::check(zxv_w4_manifest_decode(bytes.data(), bytes.size(), &m));
        return Manifest(m);
    }

    /// Add a tool. UsageError ZXV_E_DUPLICATE (name listed), ZXV_E_ARG /
    /// ZXV_E_TOO_LONG; RefusedError ZXV_E_CAPACITY (16 tools).
    void add_tool(const Tool& t)
    {
        const char* n = detail::cstr(t.name, "tool name");
        detail::check(zxv_w4_manifest_add_tool(h(), n, t.price_vfv, t.rate_per_min, t.burst, t.flags, t.max_input));
    }
    /// Sign with `agent`'s key (through the capability it keeps, which
    /// needs caps::crypto_sign).
    void sign(const Agent& agent)
    {
        zxv_w4_manifest* m = h();
        agent.run([&](zxv_w4_agent* a) { return zxv_w4_manifest_sign(agent.cap_->cap, a, m); });
    }
    /// Check that `public_key` signed it, that key is the manifest's agent,
    /// and issued_ms <= now_ms < expires_ms. VerificationFailed (wrong key,
    /// bad or no signature), RefusedError ZXV_E_STATE (valid signature, not
    /// valid at now_ms).
    void verify(std::span<const std::uint8_t> public_key, std::uint64_t now_ms) const
    {
        detail::need_size(public_key, pk_bytes, "agent public key");
        detail::check(zxv_w4_manifest_verify(h(), public_key.data(), now_ms));
    }
    /// The signed wire form. RefusedError (ZXV_E_STATE) if not signed.
    [[nodiscard]] std::vector<std::uint8_t> encode() const
    {
        return detail::read_bytes([&](std::uint8_t* b, std::size_t c, std::size_t* n) {
            return zxv_w4_manifest_encode(h(), b, c, n);
        }, nullptr, manifest_max);
    }
    /// The Web 2 discovery face (e.g. /.well-known/web4-agent.json) as JSON
    /// text; prices stay integers in minor units.
    [[nodiscard]] std::string to_json() const
    {
        return detail::read_text([&](char* b, std::size_t c, std::size_t* n) { return zxv_w4_manifest_to_json(h(), b, c, n); });
    }
    /// The header fields.
    [[nodiscard]] ManifestInfo info() const
    {
        ManifestInfo i;
        std::int32_t s = 0;
        detail::check(zxv_w4_manifest_info(h(), i.agent_id.data(), i.peer_id.data(), &i.version, &i.issued_ms,
                                           &i.expires_ms, &i.tool_count, &s));
        i.is_signed = s != 0;
        return i;
    }
    /// Tool `index` (0-based). UsageError (ZXV_E_NOT_FOUND) past the last.
    [[nodiscard]] Tool tool(std::uint32_t index) const
    {
        Tool t;
        std::array<char, 32> nm{};
        detail::check(zxv_w4_manifest_tool(h(), index, nm.data(), &t.price_vfv, &t.rate_per_min, &t.burst, &t.flags,
                                           &t.max_input));
        t.name = nm.data();
        return t;
    }
    /// Every tool, in order.
    [[nodiscard]] std::vector<Tool> tools() const
    {
        std::vector<Tool> v;
        for (std::uint32_t i = 0, n = info().tool_count; i < n; ++i) v.push_back(tool(i));
        return v;
    }
    /// The index of the tool named `name`, or nullopt.
    [[nodiscard]] std::optional<std::uint32_t> find(const std::string& name) const
    {
        std::uint32_t i = 0;
        const std::int32_t st = zxv_w4_manifest_find(h(), detail::cstr(name, "tool name"), &i);
        if (st == ZXV_E_NOT_FOUND) return std::nullopt;
        detail::check(st);
        return i;
    }
    /// The raw C handle (owned by this object; never destroy it).
    [[nodiscard]] zxv_w4_manifest* native_handle() const noexcept { return m_.get(); }

private:
    explicit Manifest(zxv_w4_manifest* m) noexcept : m_(m) {}
    [[nodiscard]] zxv_w4_manifest* h() const { return detail::live(m_.get(), "web4::Manifest"); }
    detail::Owned<zxv_w4_manifest, zxv_w4_manifest_destroy> m_;
};

/// A single-use log of consumed and revoked consent tokens. Not
/// thread-safe; move-only.
class ConsentLog {
public:
    /// A log of at most `capacity` entries (1..consent_log_max).
    explicit ConsentLog(std::uint32_t capacity)
    {
        zxv_w4_consent_log* l = nullptr;
        detail::check(zxv_w4_consent_log_create(capacity, &l));
        l_.reset(l);
    }
    ConsentLog(ConsentLog&&) noexcept = default;            ///< move
    ConsentLog& operator=(ConsentLog&&) noexcept = default; ///< move-assign
    ConsentLog(const ConsentLog&) = delete;
    ConsentLog& operator=(const ConsentLog&) = delete;
    ~ConsentLog() = default;

    /// Check that `token` lets `agent` do `action` with scope `need`
    /// (nonzero scope::* bits) for `amount` VFV minor units (checked against
    /// max_vfv when need has scope::money), signed by the human whose key is
    /// `human_public_key`, valid at now_ms; then record it as used. Returning
    /// normally means consent was given and is now spent. ConsentDenied (no
    /// token, a mismatch, expired or not yet valid, a bad signature, used or
    /// revoked), RefusedError ZXV_E_CAPACITY (the log is full even after
    /// pruning: fail closed).
    void check(const ConsentToken* token, std::span<const std::uint8_t> human_public_key, const Id& agent,
               const Id& action, std::uint32_t need, std::uint64_t amount, std::uint64_t now_ms)
    {
        detail::need_size(human_public_key, pk_bytes, "human public key");
        detail::check(zxv_w4_consent_check(h(), token ? token->bytes().data() : nullptr, human_public_key.data(),
                                           agent.data(), action.data(), need, amount, now_ms));
    }
    /// Revoke a token before use; later checks of it fail with
    /// ConsentDenied. Needs no capability (it only takes authority away).
    void revoke(const ConsentToken& token, std::uint64_t now_ms)
    {
        detail::check(zxv_w4_consent_revoke(h(), token.bytes().data(), now_ms));
    }
    /// Drop entries whose token expired at or before now_ms; returns how
    /// many remain.
    std::uint32_t prune(std::uint64_t now_ms)
    {
        std::uint32_t n = 0;
        detail::check(zxv_w4_consent_log_prune(h(), now_ms, &n));
        return n;
    }
    /// The raw C handle (owned by this object; never destroy it).
    [[nodiscard]] zxv_w4_consent_log* native_handle() const noexcept { return l_.get(); }

private:
    [[nodiscard]] zxv_w4_consent_log* h() const { return detail::live(l_.get(), "web4::ConsentLog"); }
    detail::Owned<zxv_w4_consent_log, zxv_w4_consent_log_destroy> l_;
};

/// The fields of an envelope (read without checking its signature).
struct Envelope {
    MsgKind kind = MsgKind::request; ///< what it is
    std::uint32_t flags = 0;         ///< msg_flag::* bits
    Id from{};                       ///< sender's agent_id
    Id to{};                         ///< recipient's agent_id
    std::uint64_t seq = 0;           ///< the sender's sequence number
    std::uint64_t ts_ms = 0;         ///< when it was sealed
    std::uint64_t req_id = 0;        ///< request id
    std::uint32_t chunk = 0;         ///< chunk index
    std::string tool;                ///< tool name
    std::uint32_t payload_len = 0;   ///< payload bytes
};

/// Parse an envelope (no signature check). UsageError (ZXV_E_FORMAT).
inline Envelope decode_envelope(std::span<const std::uint8_t> env)
{
    Envelope e;
    std::int32_t k = 0;
    std::array<char, 32> tool{};
    detail::check(zxv_w4_env_decode(env.data(), env.size(), &k, &e.flags, e.from.data(), e.to.data(), &e.seq, &e.ts_ms,
                                    &e.req_id, &e.chunk, tool.data(), &e.payload_len));
    e.kind = static_cast<MsgKind>(k);
    e.tool = tool.data();
    return e;
}

/// An envelope's payload.
inline std::vector<std::uint8_t> envelope_payload(std::span<const std::uint8_t> env)
{
    return detail::read_bytes([&](std::uint8_t* b, std::size_t c, std::size_t* n) {
        return zxv_w4_env_payload(env.data(), env.size(), b, c, n);
    }, nullptr, max_payload);
}

/// The consent token attached to an envelope, or nullopt.
inline std::optional<ConsentToken> envelope_consent(std::span<const std::uint8_t> env)
{
    std::array<std::uint8_t, ZXV_W4_CONSENT_BYTES> t{};
    const std::int32_t st = zxv_w4_env_consent(env.data(), env.size(), t.data());
    if (st == ZXV_E_NOT_FOUND) return std::nullopt;
    detail::check(st);
    return ConsentToken::from_bytes(t);
}

/// True if `sender_public_key` is the envelope's sender (from == its
/// SHA3-256) and the signature verifies; false otherwise. Replay, timestamp
/// and rate checks are the receiver's job.
inline bool verify_envelope(std::span<const std::uint8_t> env, std::span<const std::uint8_t> sender_public_key)
{
    detail::need_size(sender_public_key, pk_bytes, "sender public key");
    const std::int32_t st = zxv_w4_env_verify(env.data(), env.size(), sender_public_key.data());
    if (st == ZXV_E_VERIFY) return false;
    detail::check(st);
    return true;
}

} // namespace web4


// ===========================================================================
// Paradox engine (ABI 1.5)
// ===========================================================================

/// ABI 1.5: the paradox engine. A paradox here is work whose four-valued
/// state is Both (evidence points both ways) or Neither (no evidence). The
/// engine does not decide which side is "really" true: it reads the work's
/// Hexagram tag, gives the paradox one of four kinds, and runs a fixed,
/// logged operator for that kind.
///
///   Kind           Operator            What it does
///   constructive   Book::constructive  a bounded policy update, applied only
///                                      inside published bounds
///   destructive    Book::destructive   contain: quarantine, roll back from a
///                                      checkpoint, return a RepairProof
///   neutral        Book::neutral       hold every candidate branch (Hold)
///   special        Book::special       a permanent frontier record plus a
///                                      draft classification for review
///
/// Every operator takes the pair (kind, tag) that classify() returned. A
/// pair that breaks the class rules is UsageError (ZXV_E_ARG). A forbidden
/// tag (a real check found a broken invariant) is accepted with any kind;
/// every operator refuses it and rolls back.
///
/// The Book holds three append-only, SHA-256 hash-chained logs (operator,
/// frontier, quarantine) and the signature registry. Operators that ran
/// return an Outcome (Outcome::refused and Outcome::proposal_for_owner are
/// decisions, not errors). An exception means nothing ran and nothing was
/// logged, with two exceptions documented on Book::destructive and
/// Book::pass. A full log is RefusedError (ZXV_E_CAPACITY): halt the context.
///
/// Capabilities: creating a Book, Policy, Hold or Graph and every operator
/// need caps::paradox (CapabilityDenied otherwise). Reading, classifying,
/// adding branches, nodes and updates to a handle you hold, and the free
/// functions need none. Nothing here signs: sign the bytes yourself
/// (MlDsa87 or Ed25519 with caps::crypto_sign).
///
/// Threading: a Book is internally serialized. Policy, Hold and Graph are
/// not thread-safe: use each from one thread at a time.
///
/// Honest limits:
///   * Automatic changes happen only inside published bounds. An update with
///     a value outside [lo, hi] changes nothing and becomes a proposal for
///     the owner. The operator has no balance input: it cannot move money.
///   * Policies, registry entries and drafts flagged flag::money or
///     flag::device need the owner. `owner_ok` is the caller's assertion;
///     inside one process this library cannot check who the owner is.
///   * Detection is only by the checks the caller runs (step limit, budget,
///     repeated state fingerprints, the caller's invariant result, registry
///     lookups), each when the caller calls. Nothing is instant.
///   * Destructive work is contained, never answered: there is no way to
///     reply to or act against a sender.
///   * This is contradiction-tolerant bookkeeping; it never makes both sides
///     true. Peers agree by applying the same fixed order to the same signed
///     inputs, not through any shared clock.
///   * Integers only; no floating point anywhere.
namespace paradox {

inline constexpr std::size_t hash_bytes = ZXV_PDX_HASH_BYTES;         ///< a signature, id or hash
inline constexpr std::size_t record_bytes = ZXV_PDX_REC_BYTES;        ///< an operator or frontier record
inline constexpr std::size_t quarantine_record_bytes = ZXV_PDX_QREC_BYTES; ///< a quarantine record
inline constexpr std::size_t repair_bytes = ZXV_PDX_REPAIR_BYTES;     ///< a RepairProof
inline constexpr std::size_t draft_bytes = ZXV_PDX_DRAFT_BYTES;       ///< a draft classification
inline constexpr std::uint32_t log_max = ZXV_PDX_LOG_MAX;             ///< records per log
inline constexpr std::uint32_t registry_max = ZXV_PDX_REGISTRY_MAX;   ///< registry entries
inline constexpr std::uint32_t policy_max = ZXV_PDX_POLICY_MAX;       ///< parameters per policy
inline constexpr std::uint32_t hold_max = ZXV_PDX_HOLD_MAX;           ///< branches per hold
inline constexpr std::uint32_t graph_max = ZXV_PDX_GRAPH_MAX;         ///< nodes per graph
inline constexpr std::uint32_t max_deps = ZXV_PDX_MAX_DEPS;           ///< dependencies per node
inline constexpr std::uint32_t updates_max = ZXV_PDX_UPDATES_MAX;     ///< queued updates per pass
inline constexpr std::uint32_t trace_max = ZXV_PDX_TRACE_MAX;         ///< fingerprints per detection
inline constexpr std::uint64_t src_seed = ZXV_PDX_SRC_SEED;           ///< registry entry not learned from the frontier

/// The size of a policy proposal of `n` parameters.
constexpr std::size_t proposal_bytes(std::uint32_t n) noexcept { return 12u + 32u * static_cast<std::size_t>(n); }

/// The four kinds, plus none (True or False: not a paradox).
enum class Kind : std::int32_t {
    constructive = ZXV_PDX_KIND_CONSTRUCTIVE, ///< Both, margin growing (catalyst)
    destructive = ZXV_PDX_KIND_DESTRUCTIVE,   ///< Both, margin shrinking or stale (immune)
    neutral = ZXV_PDX_KIND_NEUTRAL,           ///< Both and steady, or Neither
    special = ZXV_PDX_KIND_SPECIAL,           ///< unclassified: the frontier
    none = ZXV_PDX_KIND_NONE,                 ///< True or False: not a paradox
};

/// What to do with a classified pair (action()).
enum class Action : std::int32_t {
    none = ZXV_PDX_DO_NONE,             ///< not a paradox: use Hexagram::action()
    synthesize = ZXV_PDX_DO_SYNTHESIZE, ///< constructive: Book::constructive
    contain = ZXV_PDX_DO_CONTAIN,       ///< destructive: Book::destructive
    hold = ZXV_PDX_DO_HOLD,             ///< neutral: Book::neutral
    record = ZXV_PDX_DO_RECORD,         ///< special: Book::special (sandbox)
    rollback = ZXV_PDX_DO_ROLLBACK,     ///< forbidden: refuse, roll back, quarantine
};

/// An operator's decision.
enum class Outcome : std::int32_t {
    applied = ZXV_PDX_OUT_APPLIED,                       ///< constructive update applied
    kept = ZXV_PDX_OUT_KEPT,                             ///< an applied update survived evaluation
    proposal_for_owner = ZXV_PDX_OUT_PROPOSAL_FOR_OWNER, ///< out of bounds or unapproved: no change
    rolled_back = ZXV_PDX_OUT_ROLLED_BACK,               ///< tension grew, or forbidden: restored
    contained = ZXV_PDX_OUT_CONTAINED,                   ///< destructive: quarantined, rolled back
    deferred = ZXV_PDX_OUT_DEFERRED,                     ///< neutral: still holding every branch
    settled = ZXV_PDX_OUT_SETTLED,                       ///< neutral: one branch chosen
    recorded = ZXV_PDX_OUT_RECORDED,                     ///< special: frontier record written
    accepted = ZXV_PDX_OUT_ACCEPTED,                     ///< reviewed draft joined the registry
    pending = ZXV_PDX_OUT_PENDING,                       ///< graph: more passes needed
    resolved = ZXV_PDX_OUT_RESOLVED,                     ///< graph: every node is final
    escalate_to_owner = ZXV_PDX_OUT_ESCALATE_TO_OWNER,   ///< graph: step limit reached
    refused = ZXV_PDX_OUT_REFUSED,                       ///< wrong kind: nothing changed
    log_full = ZXV_PDX_OUT_LOG_FULL,                     ///< never reported (ZXV_E_CAPACITY instead)
};

/// A graph node's state.
enum class NodeState : std::int32_t {
    open = ZXV_PDX_NS_OPEN,               ///< not resolved yet
    clear = ZXV_PDX_NS_CLEAR,             ///< kind none: not a paradox
    contained = ZXV_PDX_NS_CONTAINED,     ///< destructive: contained (sticky)
    rolled_back = ZXV_PDX_NS_ROLLED_BACK, ///< forbidden: refused and rolled back (sticky)
    tainted = ZXV_PDX_NS_TAINTED,         ///< depends on contained work (sticky)
    sandboxed = ZXV_PDX_NS_SANDBOXED,     ///< special: recorded, awaiting review
    waiting = ZXV_PDX_NS_WAITING,         ///< depends on a sandboxed node
    held = ZXV_PDX_NS_HELD,               ///< neutral: branches kept
    blocked = ZXV_PDX_NS_BLOCKED,         ///< constructive reached by held/waiting work
    applied = ZXV_PDX_NS_APPLIED,         ///< constructive: applied
};

/// Which log of a Book.
enum class Log : std::int32_t {
    ops = ZXV_PDX_LOG_OPS,               ///< operator log
    frontier = ZXV_PDX_LOG_FRONTIER,     ///< special records, never edited
    quarantine = ZXV_PDX_LOG_QUARANTINE, ///< contained work (lpres_hex X6)
};

/// Flags on policies, registry entries and drafts.
namespace flag {
inline constexpr std::uint8_t money = ZXV_PDX_FLAG_MONEY;   ///< touches money: the owner decides
inline constexpr std::uint8_t device = ZXV_PDX_FLAG_DEVICE; ///< touches devices: the owner decides
} // namespace flag

/// The checks that fired in Book::destructive (RepairProof::trips).
namespace trip {
inline constexpr std::uint32_t steps = ZXV_PDX_TRIP_STEPS;         ///< steps >= step limit
inline constexpr std::uint32_t budget = ZXV_PDX_TRIP_BUDGET;       ///< spent >= budget
inline constexpr std::uint32_t cycle = ZXV_PDX_TRIP_CYCLE;         ///< a state fingerprint repeated
inline constexpr std::uint32_t invariant = ZXV_PDX_TRIP_INVARIANT; ///< the caller's invariant failed
inline constexpr std::uint32_t forbidden = ZXV_PDX_TRIP_FORBIDDEN; ///< the tag was already forbidden
} // namespace trip

/// "constructive", ..., "none"; "INVALID" otherwise.
inline std::string_view to_string(Kind k) noexcept { return zxv_paradox_kind_name(static_cast<std::int32_t>(k)); }
/// "applied", "proposal-for-owner", ...; "INVALID" otherwise.
inline std::string_view to_string(Outcome o) noexcept
{
    return zxv_paradox_outcome_name(static_cast<std::int32_t>(o));
}
/// "open", "sandboxed", ...; "INVALID" otherwise.
inline std::string_view to_string(NodeState s) noexcept
{
    return zxv_paradox_node_state_name(static_cast<std::int32_t>(s));
}

/// The signature of an anomaly: SHA-256("zxv/pdx-sig/v1" || data), from a
/// canonical description the caller builds.
inline Digest32 signature(std::span<const std::uint8_t> data)
{
    Digest32 d{};
    detail::check(zxv_paradox_signature(data.data(), data.size(), d.data()));
    return d;
}
/// signature() of the bytes of `text`.
inline Digest32 signature(std::string_view text) { return signature(as_bytes(text)); }

/// The kind a tag reads as on its own (a forbidden tag reads as none).
inline Kind kind_of_tag(Hexagram tag)
{
    std::int32_t k = 0;
    detail::check(zxv_paradox_kind_of_tag(tag.tag(), &k));
    return static_cast<Kind>(k);
}
/// What to do with a classified pair. UsageError (ZXV_E_ARG) for a pair
/// that breaks the class rules; a forbidden tag gives Action::rollback.
inline Action action(Kind kind, Hexagram tag)
{
    std::int32_t a = 0;
    detail::check(zxv_paradox_action(static_cast<std::int32_t>(kind), tag.tag(), &a));
    return static_cast<Action>(a);
}
/// The HexAction an outcome amounts to (applied -> proceed, deferred ->
/// wait, proposal_for_owner -> escalate, contained -> rollback, ...).
inline HexAction outcome_action(Outcome o)
{
    std::int32_t a = 0;
    detail::check(zxv_paradox_outcome_action(static_cast<std::int32_t>(o), &a));
    return static_cast<HexAction>(a);
}

/// Evidence counts before and after: the trend that classify() reads.
struct Evidence {
    std::uint32_t pos0 = 0; ///< evidence for, before
    std::uint32_t neg0 = 0; ///< evidence against, before
    std::uint32_t pos1 = 0; ///< evidence for, after
    std::uint32_t neg1 = 0; ///< evidence against, after
};

/// What classify() decided.
struct Classification {
    Kind kind = Kind::none;     ///< the kind
    Hexagram tag;               ///< the tag with the kind's polarity (forbidden stays forbidden)
    std::int32_t reg_index = -1; ///< the registry entry that decided it, or -1
};

namespace detail {
inline Classification classify(const zxv_paradox* book, Hexagram tag, const std::optional<Evidence>& ev, bool stale,
                               const std::optional<Digest32>& sig, zxv::detail::CtxCore* core)
{
    std::array<std::uint32_t, 4> e{};
    if (ev) e = {ev->pos0, ev->neg0, ev->pos1, ev->neg1};
    std::int32_t k = 0, idx = -1;
    std::uint8_t t = 0;
    zxv::detail::check(zxv_paradox_classify(book, tag.tag(), ev ? e.data() : nullptr, stale ? 1 : 0,
                                            sig ? sig->data() : nullptr, &k, &t, &idx),
                       core);
    return {static_cast<Kind>(k), Hexagram::from_tag(t), idx};
}
} // namespace detail

/// Classify a tag without a registry (no Book). `evidence` empty means no
/// trend (polarity neutral); `stale` says the newest evidence is past your
/// freshness limit (ignored without evidence); a `sig` is ignored without
/// a registry. Pure.
inline Classification classify(Hexagram tag, const std::optional<Evidence>& evidence = {}, bool stale = false)
{
    return detail::classify(nullptr, tag, evidence, stale, {}, nullptr);
}

/// A RepairProof (repair_bytes, little-endian), returned by
/// Book::destructive. Share signature() with peers: they add it to their
/// registries as Kind::destructive to recognise the trap. Signing the proof
/// is the caller's job.
class RepairProof {
public:
    /// A proof from its bytes (size checked; content not verified).
    static RepairProof from_bytes(std::span<const std::uint8_t> b)
    {
        RepairProof p;
        p.b_ = zxv::detail::fixed<ZXV_PDX_REPAIR_BYTES>(b, "RepairProof");
        return p;
    }
    /// The checks that fired (paradox::trip bits).
    [[nodiscard]] std::uint32_t trips() const noexcept { return zxv::detail::le32(b_.data()); }
    /// Steps taken when it was detected.
    [[nodiscard]] std::uint32_t steps() const noexcept { return zxv::detail::le32(b_.data() + 4); }
    /// Budget spent when it was detected.
    [[nodiscard]] std::uint32_t spent() const noexcept { return zxv::detail::le32(b_.data() + 8); }
    /// Number of state fingerprints checked.
    [[nodiscard]] std::uint32_t trace_count() const noexcept { return zxv::detail::le32(b_.data() + 12); }
    /// Who submitted the work.
    [[nodiscard]] std::uint32_t submitter() const noexcept { return zxv::detail::le32(b_.data() + 16); }
    /// The state length.
    [[nodiscard]] std::uint32_t state_length() const noexcept { return zxv::detail::le32(b_.data() + 20); }
    /// SHA-256 of the state as found.
    [[nodiscard]] Digest32 state_found() const noexcept { return zxv::detail::digest_at(b_.data() + 24); }
    /// SHA-256 of the state after rollback.
    [[nodiscard]] Digest32 state_restored() const noexcept { return zxv::detail::digest_at(b_.data() + 56); }
    /// The signature to share with peers (signature() of the pattern).
    [[nodiscard]] Digest32 signature() const noexcept { return zxv::detail::digest_at(b_.data() + 88); }
    /// The proof hash, SHA-256("zxv/pdx-repair/v1" || bytes[0,120)).
    [[nodiscard]] Digest32 proof_hash() const noexcept { return zxv::detail::digest_at(b_.data() + 120); }
    /// The quarantine record's index.
    [[nodiscard]] std::uint64_t quarantine_index() const noexcept { return zxv::detail::le64(b_.data() + 152); }
    /// The proof's bytes.
    [[nodiscard]] const std::array<std::uint8_t, ZXV_PDX_REPAIR_BYTES>& bytes() const noexcept { return b_; }
    /// True when the proof hash matches its bytes (integrity only; a
    /// signature over the proof is the caller's to verify).
    [[nodiscard]] bool verify() const
    {
        std::int32_t st = zxv_paradox_repair_verify(b_.data());
        if (st == ZXV_E_VERIFY) return false;
        zxv::detail::check(st);
        return true;
    }
    /// Byte equality.
    friend bool operator==(const RepairProof&, const RepairProof&) = default;

private:
    RepairProof() = default;
    std::array<std::uint8_t, ZXV_PDX_REPAIR_BYTES> b_{};
};

/// A draft classification of a frontier record (draft_bytes), from
/// Book::draft or sent by a peer, for Book::review.
class Draft {
public:
    /// A draft from its bytes (size checked; Book::review checks content).
    static Draft from_bytes(std::span<const std::uint8_t> b)
    {
        Draft d;
        d.b_ = zxv::detail::fixed<ZXV_PDX_DRAFT_BYTES>(b, "paradox draft");
        return d;
    }
    /// The frontier record index.
    [[nodiscard]] std::uint32_t record() const noexcept { return zxv::detail::le32(b_.data()); }
    /// The frontier record's hash.
    [[nodiscard]] Digest32 record_hash() const noexcept { return zxv::detail::digest_at(b_.data() + 4); }
    /// The record's signature.
    [[nodiscard]] Digest32 signature() const noexcept { return zxv::detail::digest_at(b_.data() + 36); }
    /// The proposed kind.
    [[nodiscard]] Kind kind() const noexcept { return static_cast<Kind>(b_[68]); }
    /// The caller's reason code.
    [[nodiscard]] std::uint8_t reason() const noexcept { return b_[69]; }
    /// paradox::flag bits.
    [[nodiscard]] std::uint8_t flags() const noexcept { return b_[70]; }
    /// The draft's bytes.
    [[nodiscard]] const std::array<std::uint8_t, ZXV_PDX_DRAFT_BYTES>& bytes() const noexcept { return b_; }
    /// Byte equality.
    friend bool operator==(const Draft&, const Draft&) = default;

private:
    Draft() = default;
    std::array<std::uint8_t, ZXV_PDX_DRAFT_BYTES> b_{};
};

/// One registry entry (append-only: never edited).
struct RegistryEntry {
    Digest32 signature{};          ///< the anomaly's signature
    Kind kind = Kind::none;        ///< constructive, destructive or neutral
    std::uint8_t reason = 0;       ///< the caller's reason code
    std::uint8_t flags = 0;        ///< paradox::flag bits
    std::uint64_t source = src_seed; ///< the frontier record it was learned from, or src_seed
};

/// An operator or frontier log record (record_bytes), parsed.
struct OpRecord {
    std::uint64_t seq = 0;    ///< its index
    std::uint8_t op = 0;      ///< 1 constructive, 2 destructive, 3 neutral, 4 special, 5 draft, 6 review/registry, 7 graph pass
    std::uint8_t kind = 0;    ///< the kind it ran for
    std::uint8_t tag = 0;     ///< the tag it ran for
    std::uint8_t outcome = 0; ///< the Outcome
    std::uint32_t aux = 0;    ///< per-op detail (see the C header)
    Digest32 a{};             ///< per-op hash a
    Digest32 b{};             ///< per-op hash b
    Digest32 prev{};          ///< previous record's hash
    Digest32 hash{};          ///< SHA-256("zxv/pdx-log/v1" || bytes[0,112))

    /// Parse record bytes (size checked).
    static OpRecord parse(std::span<const std::uint8_t> r)
    {
        zxv::detail::need_size(r, ZXV_PDX_REC_BYTES, "paradox log record");
        const std::uint8_t* p = r.data();
        OpRecord o;
        o.seq = zxv::detail::le64(p);
        o.op = p[8];
        o.kind = p[9];
        o.tag = p[10];
        o.outcome = p[11];
        o.aux = zxv::detail::le32(p + 12);
        o.a = zxv::detail::digest_at(p + 16);
        o.b = zxv::detail::digest_at(p + 48);
        o.prev = zxv::detail::digest_at(p + 80);
        o.hash = zxv::detail::digest_at(p + 112);
        return o;
    }
};

/// Parse a quarantine log record (quarantine_record_bytes) into the ABI 1.3
/// QuarantineRecord (reason holds the paradox::trip bits).
inline QuarantineRecord parse_quarantine_record(std::span<const std::uint8_t> r)
{
    zxv::detail::need_size(r, ZXV_PDX_QREC_BYTES, "paradox quarantine record");
    const std::uint8_t* p = r.data();
    QuarantineRecord q;
    q.seq = zxv::detail::le64(p);
    q.tag = Hexagram::from_tag(p[8]);
    q.reason = p[9];
    q.submitter = zxv::detail::le32(p + 10);
    q.subject = zxv::detail::digest_at(p + 14);
    q.prev = zxv::detail::digest_at(p + 46);
    q.hash = zxv::detail::digest_at(p + 78);
    return q;
}

class Book;

/// The live state of a Policy.
struct PolicyState {
    std::vector<std::int64_t> values;   ///< the live values
    std::vector<std::int64_t> previous; ///< the values before the last applied update
    bool armed = false;                 ///< an applied update awaits evaluation
    std::uint32_t tension_at = 0;       ///< the tension it was applied at
};

/// A constructive policy: n parameters (1..policy_max) with published
/// bounds that never change. The values are swarm parameters (budget
/// shares, routing weights, escrow timing), never balances. A policy
/// flagged flag::money or flag::device is never changed automatically:
/// every update becomes a proposal for the owner. Needs caps::paradox.
/// Not thread-safe.
class Policy {
public:
    /// A policy with bounds lo[i] <= hi[i] and initial values inside them.
    /// UsageError ZXV_E_ARG (sizes differ, n out of range, unknown flags),
    /// ZXV_E_RANGE (lo > hi, or an initial value outside its bounds).
    Policy(const Capability& cap, std::span<const std::int64_t> lo, std::span<const std::int64_t> hi,
           std::span<const std::int64_t> initial, std::uint32_t flags = 0)
        : cap_(zxv::detail::cap_of(cap))
    {
        if (hi.size() != lo.size() || initial.size() != lo.size())
            throw UsageError(ZXV_E_ARG, "paradox policy: lo, hi and initial differ in size");
        n_ = static_cast<std::uint32_t>(std::min<std::size_t>(lo.size(), 0xFFFFFFFFu));
        zxv_paradox_policy* p = nullptr;
        cap_->call([&] {
            return zxv_paradox_policy_create(cap_->cap, n_, lo.data(), hi.data(), initial.data(), flags, &p);
        });
        p_.reset(p);
    }
    Policy(Policy&&) noexcept = default;            ///< move
    Policy& operator=(Policy&&) noexcept = default; ///< move-assign
    Policy(const Policy&) = delete;
    Policy& operator=(const Policy&) = delete;
    ~Policy() = default;

    /// The number of parameters.
    [[nodiscard]] std::uint32_t size() const noexcept { return n_; }
    /// The live values, previous values, armed flag and tension at apply.
    [[nodiscard]] PolicyState state() const
    {
        PolicyState s;
        s.values.resize(n_);
        s.previous.resize(n_);
        std::int32_t armed = 0;
        run([&](zxv_paradox_policy* p) {
            return zxv_paradox_policy_values(p, s.values.data(), s.previous.data(), n_, &armed, &s.tension_at);
        });
        s.armed = armed != 0;
        return s;
    }
    /// The live values.
    [[nodiscard]] std::vector<std::int64_t> values() const { return state().values; }
    /// The lower bounds.
    [[nodiscard]] std::vector<std::int64_t> lower() const { return bounds(true); }
    /// The upper bounds.
    [[nodiscard]] std::vector<std::int64_t> upper() const { return bounds(false); }
    /// The flags (paradox::flag bits).
    [[nodiscard]] std::uint32_t flags() const
    {
        std::uint32_t f = 0;
        run([&](zxv_paradox_policy* p) { return zxv_paradox_policy_bounds(p, nullptr, nullptr, n_, &f); });
        return f;
    }
    /// A proposed update as bytes for the owner or for signing
    /// (proposal_bytes(n), little-endian: n, how many values are out of
    /// bounds, flags, then lo, hi, current, proposed per entry). Reads only.
    [[nodiscard]] std::vector<std::uint8_t> proposal(std::span<const std::int64_t> synth) const
    {
        zxv_paradox_policy* p = handle();
        return zxv::detail::read_bytes([&](std::uint8_t* b, std::size_t c, std::size_t* n) {
            return zxv_paradox_policy_proposal(p, synth.data(), static_cast<std::uint32_t>(synth.size()), b, c, n);
        }, cap_->ctx.get(), proposal_bytes(n_));
    }
    /// The raw C handle (owned by this object; never destroy it).
    [[nodiscard]] zxv_paradox_policy* native_handle() const noexcept { return p_.get(); }

private:
    friend class Book;
    [[nodiscard]] zxv_paradox_policy* handle() const { return zxv::detail::live(p_.get(), "paradox::Policy"); }
    template <class F>
    void run(F&& f) const
    {
        zxv_paradox_policy* p = handle();
        zxv::detail::check(f(p), cap_->ctx.get());
    }
    std::vector<std::int64_t> bounds(bool low) const
    {
        std::vector<std::int64_t> v(n_);
        run([&](zxv_paradox_policy* p) {
            return zxv_paradox_policy_bounds(p, low ? v.data() : nullptr, low ? nullptr : v.data(), n_, nullptr);
        });
        return v;
    }
    std::shared_ptr<zxv::detail::CapCore> cap_; // declared first: outlives the policy
    std::uint32_t n_ = 0;
    zxv::detail::Owned<zxv_paradox_policy, zxv_paradox_policy_destroy> p_;
};

/// One branch of a Hold.
struct Branch {
    Digest32 id{};          ///< the candidate's 32-byte digest
    std::uint32_t pos = 0;  ///< evidence for
    std::uint32_t neg = 0;  ///< evidence against
};

/// A Hold's summary.
struct HoldInfo {
    std::uint32_t count = 0;    ///< branches
    std::int32_t settled = -1;  ///< the settled branch, -1 while deferred
    std::int32_t decisive = -1; ///< the branch with decisive evidence now, -1 if none
};

/// A neutral hold of up to `capacity` (1..hold_max) candidate branches. A
/// branch settles (Book::neutral) when its net evidence leads every other
/// by at least `decisive` (0 means 1), or alone reaches it, or the owner
/// picks it. Creating needs caps::paradox; adding branches and evidence
/// needs none. Not thread-safe.
class Hold {
public:
    /// A new, empty hold.
    Hold(const Capability& cap, std::uint32_t capacity, std::uint32_t decisive = 0) : cap_(zxv::detail::cap_of(cap))
    {
        zxv_paradox_hold* h = nullptr;
        cap_->call([&] { return zxv_paradox_hold_create(cap_->cap, capacity, decisive, &h); });
        h_.reset(h);
    }
    Hold(Hold&&) noexcept = default;            ///< move
    Hold& operator=(Hold&&) noexcept = default; ///< move-assign
    Hold(const Hold&) = delete;
    Hold& operator=(const Hold&) = delete;
    ~Hold() = default;

    /// Add a candidate (the same id again is the same candidate).
    /// RefusedError ZXV_E_STATE (settled), ZXV_E_CAPACITY (full).
    void add(const Digest32& id)
    {
        run([&](zxv_paradox_hold* h) { return zxv_paradox_hold_add(h, id.data()); });
    }
    /// One piece of evidence for (`positive`) or against a candidate.
    /// UsageError ZXV_E_NOT_FOUND (unknown id), RefusedError ZXV_E_STATE.
    void evidence(const Digest32& id, bool positive)
    {
        run([&](zxv_paradox_hold* h) { return zxv_paradox_hold_evidence(h, id.data(), positive ? 1 : 0); });
    }
    /// The branch count, settled branch and decisive branch.
    [[nodiscard]] HoldInfo info() const
    {
        HoldInfo i;
        run([&](zxv_paradox_hold* h) { return zxv_paradox_hold_info(h, &i.count, &i.settled, &i.decisive); });
        return i;
    }
    /// Branch `index`. UsageError ZXV_E_NOT_FOUND.
    [[nodiscard]] Branch branch(std::uint32_t index) const
    {
        Branch b;
        run([&](zxv_paradox_hold* h) { return zxv_paradox_hold_branch(h, index, b.id.data(), &b.pos, &b.neg); });
        return b;
    }
    /// The raw C handle (owned by this object; never destroy it).
    [[nodiscard]] zxv_paradox_hold* native_handle() const noexcept { return h_.get(); }

private:
    friend class Book;
    [[nodiscard]] zxv_paradox_hold* handle() const { return zxv::detail::live(h_.get(), "paradox::Hold"); }
    template <class F>
    void run(F&& f) const
    {
        zxv_paradox_hold* h = handle();
        zxv::detail::check(f(h), cap_->ctx.get());
    }
    std::shared_ptr<zxv::detail::CapCore> cap_; // declared first: outlives the hold
    zxv::detail::Owned<zxv_paradox_hold, zxv_paradox_hold_destroy> h_;
};

/// One graph node, in id order.
struct GraphNode {
    Digest32 id{};                       ///< the hash of its signed input
    Kind kind = Kind::none;              ///< its current kind
    NodeState state = NodeState::open;   ///< its state
};

/// A Graph's summary.
struct GraphInfo {
    std::uint32_t nodes = 0;       ///< node count
    std::uint32_t passes = 0;      ///< passes run
    std::uint32_t total_mag = 0;   ///< net tension of the open nodes: magnitude
    std::int32_t total_dir = 0;    ///< net tension of the open nodes: direction -1/0/+1
    bool escalated = false;        ///< reached the step limit: the owner decides
    Digest32 chain{};              ///< SHA-256 chain over every pass (zeros before the first)
};

/// A complex paradox: a dependency graph of up to graph_max nodes, resolved
/// by Book::pass in a fixed order (destructive and forbidden contained, and
/// their dependants tainted; special sandboxed, dependants wait; neutral
/// held; constructive applied where nothing reaches it). Nodes are taken in
/// id order, so every peer with the same signed inputs reaches the same
/// states and chain. It escalates to the owner after `step_limit` passes
/// (>= 1) without resolving. Creating needs caps::paradox; adding nodes,
/// edges and updates needs none. Not thread-safe.
class Graph {
public:
    /// A new, empty graph.
    Graph(const Capability& cap, std::uint32_t step_limit) : cap_(zxv::detail::cap_of(cap))
    {
        zxv_paradox_graph* g = nullptr;
        cap_->call([&] { return zxv_paradox_graph_create(cap_->cap, step_limit, &g); });
        g_.reset(g);
    }
    Graph(Graph&&) noexcept = default;            ///< move
    Graph& operator=(Graph&&) noexcept = default; ///< move-assign
    Graph(const Graph&) = delete;
    Graph& operator=(const Graph&) = delete;
    ~Graph() = default;

    /// Add node `id` (a classified pair, tension magnitude and direction
    /// -1/0/+1) before the first pass. UsageError ZXV_E_DUPLICATE,
    /// RefusedError ZXV_E_CAPACITY / ZXV_E_STATE (after the first pass).
    void add_node(const Digest32& id, Kind kind, Hexagram tag, std::uint32_t mag, std::int32_t dir)
    {
        run([&](zxv_paradox_graph* g) {
            return zxv_paradox_graph_add_node(g, id.data(), static_cast<std::int32_t>(kind), tag.tag(), mag, dir);
        });
    }
    /// Node `id` depends on node `dep` (which may be added later).
    void add_dep(const Digest32& id, const Digest32& dep)
    {
        run([&](zxv_paradox_graph* g) { return zxv_paradox_graph_add_dep(g, id.data(), dep.data()); });
    }
    /// Queue new evidence for a node, applied at the next pass ("worst
    /// wins" when several are queued for one node).
    void update(const Digest32& id, Kind kind, Hexagram tag, std::uint32_t mag, std::int32_t dir)
    {
        run([&](zxv_paradox_graph* g) {
            return zxv_paradox_graph_update(g, id.data(), static_cast<std::int32_t>(kind), tag.tag(), mag, dir);
        });
    }
    /// A node's state (NodeState::open before the first pass).
    [[nodiscard]] NodeState state(const Digest32& id) const
    {
        std::int32_t s = 0;
        run([&](zxv_paradox_graph* g) { return zxv_paradox_graph_state(g, id.data(), &s); });
        return static_cast<NodeState>(s);
    }
    /// Node `index` in id order (after the first pass, else RefusedError
    /// ZXV_E_STATE).
    [[nodiscard]] GraphNode node_at(std::uint32_t index) const
    {
        GraphNode n;
        std::int32_t k = 0, s = 0;
        run([&](zxv_paradox_graph* g) { return zxv_paradox_graph_node_at(g, index, n.id.data(), &k, &s); });
        n.kind = static_cast<Kind>(k);
        n.state = static_cast<NodeState>(s);
        return n;
    }
    /// The graph's summary.
    [[nodiscard]] GraphInfo info() const
    {
        GraphInfo i;
        std::int32_t esc = 0;
        run([&](zxv_paradox_graph* g) {
            return zxv_paradox_graph_info(g, &i.nodes, &i.passes, &i.total_mag, &i.total_dir, &esc, i.chain.data());
        });
        i.escalated = esc != 0;
        return i;
    }
    /// The raw C handle (owned by this object; never destroy it).
    [[nodiscard]] zxv_paradox_graph* native_handle() const noexcept { return g_.get(); }

private:
    friend class Book;
    [[nodiscard]] zxv_paradox_graph* handle() const { return zxv::detail::live(g_.get(), "paradox::Graph"); }
    template <class F>
    void run(F&& f) const
    {
        zxv_paradox_graph* g = handle();
        zxv::detail::check(f(g), cap_->ctx.get());
    }
    std::shared_ptr<zxv::detail::CapCore> cap_; // declared first: outlives the graph
    zxv::detail::Owned<zxv_paradox_graph, zxv_paradox_graph_destroy> g_;
};

/// The inputs of Book::destructive's checks.
struct Containment {
    std::uint32_t steps = 0;          ///< steps taken
    std::uint32_t step_limit = 0;     ///< trips when steps >= it (0: no limit)
    std::uint32_t spent = 0;          ///< budget spent
    std::uint32_t budget = 0;         ///< trips when spent >= it (0: no budget)
    std::vector<std::uint64_t> trace; ///< state fingerprints in visit order (trips on a repeat)
    bool invariant_ok = true;         ///< the caller's invariant check result
    std::vector<std::uint8_t> pattern; ///< canonical description of the trap (its signature is shared)
    std::uint32_t submitter = 0;      ///< who submitted the work
};

/// Book::destructive's result.
struct Contained {
    Outcome outcome = Outcome::refused; ///< contained, or refused (nothing changed)
    std::optional<RepairProof> proof;   ///< the proof when contained
};

/// Book::neutral's result.
struct Settlement {
    Outcome outcome = Outcome::deferred; ///< settled, deferred, rolled_back or refused
    std::int32_t settled = -1;           ///< the settled branch, or -1
};

/// Book::special's result.
struct FrontierEntry {
    Outcome outcome = Outcome::refused;  ///< recorded, refused or rolled_back
    std::optional<std::uint32_t> record; ///< the frontier index when recorded
};

/// The paradox book: the operator, frontier and quarantine logs and the
/// signature registry, and the operators that write them. Internally
/// serialized (thread-safe). It keeps its capability and context alive,
/// so destruction order cannot be wrong.
class Book {
public:
    /// A book with three empty logs of `log_capacity` records (1..log_max)
    /// and a registry of `registry_capacity` entries (1..registry_max).
    /// Needs caps::paradox.
    Book(const Capability& cap, std::uint32_t log_capacity, std::uint32_t registry_capacity)
        : cap_(zxv::detail::cap_of(cap))
    {
        zxv_paradox* b = nullptr;
        cap_->call([&] { return zxv_paradox_create(cap_->cap, log_capacity, registry_capacity, &b); });
        b_.reset(b);
    }
    Book(Book&&) noexcept = default;            ///< move
    Book& operator=(Book&&) noexcept = default; ///< move-assign
    Book(const Book&) = delete;
    Book& operator=(const Book&) = delete;
    ~Book() = default;

    /// Classify a tag with this book's registry: a known `sig` overrides
    /// the trend for Both and Neither. Reads only.
    [[nodiscard]] Classification classify(Hexagram tag, const std::optional<Evidence>& evidence = {},
                                          bool stale = false, const std::optional<Digest32>& sig = {}) const
    {
        return detail::classify(book(), tag, evidence, stale, sig, cap_->ctx.get());
    }

    /// Add a reviewed signature as kind constructive, destructive or
    /// neutral; returns its index. An entry flagged money or device needs
    /// `owner_ok`, else OwnerApprovalRequired. UsageError ZXV_E_DUPLICATE
    /// (already registered), RefusedError ZXV_E_CAPACITY. Needs caps::paradox.
    std::uint32_t registry_add(const Digest32& sig, Kind kind, std::uint8_t reason, std::uint8_t flags = 0,
                               bool owner_ok = false)
    {
        std::uint32_t i = 0;
        gated([&](zxv_paradox* b) {
            return zxv_paradox_registry_add(cap_->cap, b, sig.data(), static_cast<std::int32_t>(kind), reason, flags,
                                            owner_ok ? 1 : 0, &i);
        });
        return i;
    }
    /// The index of a signature, or nothing.
    [[nodiscard]] std::optional<std::uint32_t> registry_find(const Digest32& sig) const
    {
        std::uint32_t i = 0;
        std::int32_t st = zxv_paradox_registry_find(book(), sig.data(), &i);
        if (st == ZXV_E_NOT_FOUND) return std::nullopt;
        zxv::detail::check(st, cap_->ctx.get());
        return i;
    }
    /// The number of registry entries.
    [[nodiscard]] std::uint32_t registry_count() const
    {
        std::uint32_t n = 0;
        read([&](const zxv_paradox* b) { return zxv_paradox_registry_count(b, &n); });
        return n;
    }
    /// Registry entry `index`. UsageError ZXV_E_NOT_FOUND.
    [[nodiscard]] RegistryEntry registry_at(std::uint32_t index) const
    {
        RegistryEntry e;
        std::int32_t k = 0;
        read([&](const zxv_paradox* b) {
            return zxv_paradox_registry_at(b, index, e.signature.data(), &k, &e.reason, &e.flags, &e.source);
        });
        e.kind = static_cast<Kind>(k);
        return e;
    }

    /// The record count of a log.
    [[nodiscard]] std::uint32_t log_count(Log which) const
    {
        std::uint32_t n = 0;
        read([&](const zxv_paradox* b) { return zxv_paradox_log_count(b, static_cast<std::int32_t>(which), &n); });
        return n;
    }
    /// A log's chain head (32 zeros when empty).
    [[nodiscard]] Digest32 log_head(Log which) const
    {
        Digest32 h{};
        read([&](const zxv_paradox* b) { return zxv_paradox_log_head(b, static_cast<std::int32_t>(which), h.data()); });
        return h;
    }
    /// Record `index` of a log as bytes (record_bytes, or
    /// quarantine_record_bytes for Log::quarantine). UsageError
    /// ZXV_E_NOT_FOUND past the end.
    [[nodiscard]] std::vector<std::uint8_t> log_record(Log which, std::uint32_t index) const
    {
        const zxv_paradox* bk = book();
        return zxv::detail::read_bytes([&](std::uint8_t* b, std::size_t c, std::size_t* n) {
            return zxv_paradox_log_record(bk, static_cast<std::int32_t>(which), index, b, c, n);
        }, cap_->ctx.get(), ZXV_PDX_REC_BYTES);
    }
    /// Operator or frontier record `index`, parsed.
    [[nodiscard]] OpRecord op_record(Log which, std::uint32_t index) const
    {
        if (which == Log::quarantine) throw UsageError(ZXV_E_ARG, "op_record: use quarantine_record");
        return OpRecord::parse(log_record(which, index));
    }
    /// Quarantine record `index`, parsed.
    [[nodiscard]] QuarantineRecord quarantine_record(std::uint32_t index) const
    {
        return parse_quarantine_record(log_record(Log::quarantine, index));
    }
    /// Recompute every link of a log. A broken chain is FatalError
    /// (ZXV_E_INVARIANT) and poisons the context (fail closed).
    void log_verify(Log which)
    {
        zxv_paradox* b = book();
        zxv::detail::check(zxv_paradox_log_verify(b, static_cast<std::int32_t>(which)), cap_->ctx.get());
    }

    /// One constructive evaluation and proposed update `synth` (one value
    /// per policy parameter). Outcomes: applied (in bounds, unflagged),
    /// proposal_for_owner (out of bounds or a money/device policy: nothing
    /// changes), rolled_back (tension above the tension at apply, or a
    /// forbidden tag), refused (kind not constructive). UsageError ZXV_E_ARG
    /// (a bad pair, size mismatch). Needs caps::paradox.
    Outcome constructive(Policy& policy, Kind kind, Hexagram tag, std::span<const std::int64_t> synth,
                         std::uint32_t tension)
    {
        if (synth.size() != policy.size()) throw UsageError(ZXV_E_ARG, "paradox constructive: synth size mismatch");
        return run_constructive(policy, kind, tag, synth.data(), tension);
    }
    /// An evaluation only (no new update): kept (an armed update survives
    /// and is disarmed), rolled_back or refused.
    Outcome constructive(Policy& policy, Kind kind, Hexagram tag, std::uint32_t tension)
    {
        return run_constructive(policy, kind, tag, nullptr, tension);
    }

    /// Detect and contain. When the kind is destructive or any check in `c`
    /// fires, the work is quarantined, `state` is restored in place from
    /// `checkpoint` (same size) and the result carries the RepairProof;
    /// otherwise the outcome is refused and nothing changes. It contains:
    /// there is no way to reply to or act against the submitter. Rollback
    /// is always done, even when RefusedError ZXV_E_CAPACITY (a log full:
    /// halt) is thrown. UsageError ZXV_E_RANGE (trace over trace_max).
    /// Needs caps::paradox.
    Contained destructive(Kind kind, Hexagram tag, std::span<std::uint8_t> state,
                          std::span<const std::uint8_t> checkpoint, const Containment& c)
    {
        if (checkpoint.size() != state.size())
            throw UsageError(ZXV_E_ARG, "paradox destructive: state and checkpoint differ in size");
        auto u32 = [](std::size_t n, const char* what) {
            if (n > 0xFFFFFFFFu) throw UsageError(ZXV_E_RANGE, std::string(what) + " too long");
            return static_cast<std::uint32_t>(n);
        };
        const std::uint32_t len = u32(state.size(), "state");
        const std::uint32_t tn = u32(c.trace.size(), "trace");
        const std::uint32_t pn = u32(c.pattern.size(), "pattern");
        std::array<std::uint8_t, ZXV_PDX_REPAIR_BYTES> proof{};
        std::int32_t out = 0;
        gated([&](zxv_paradox* b) {
            return zxv_paradox_destructive(cap_->cap, b, static_cast<std::int32_t>(kind), tag.tag(), c.steps,
                                           c.step_limit, c.spent, c.budget, c.trace.data(), tn,
                                           c.invariant_ok ? 1 : 0, state.data(), checkpoint.data(), len,
                                           c.pattern.data(), pn, c.submitter, &out, proof.data());
        });
        Contained r;
        r.outcome = static_cast<Outcome>(out);
        if (r.outcome == Outcome::contained) r.proof = RepairProof::from_bytes(proof);
        return r;
    }

    /// Settle a hold on decisive evidence or on `owner_pick` (a branch
    /// index); otherwise keep every branch. Outcomes: settled, deferred,
    /// rolled_back (forbidden), refused (kind not neutral). UsageError
    /// ZXV_E_NOT_FOUND (owner_pick past the branches). Needs caps::paradox.
    Settlement neutral(Hold& hold, Kind kind, Hexagram tag, std::optional<std::uint32_t> owner_pick = {})
    {
        zxv_paradox_hold* h = hold.handle();
        std::int32_t pick = -1;
        if (owner_pick) {
            if (*owner_pick > 0x7FFFFFFFu) throw UsageError(ZXV_E_ARG, "paradox neutral: owner_pick out of range");
            pick = static_cast<std::int32_t>(*owner_pick);
        }
        Settlement s;
        std::int32_t out = 0;
        gated([&](zxv_paradox* b) {
            return zxv_paradox_neutral(cap_->cap, b, h, static_cast<std::int32_t>(kind), tag.tag(), pick, &out,
                                       &s.settled);
        });
        s.outcome = static_cast<Outcome>(out);
        return s;
    }

    /// Record an unclassified paradox: a permanent frontier record (`sig`,
    /// `subject` = the work's digest), never edited or erased. Outcomes:
    /// recorded (with the frontier index), refused (kind not special),
    /// rolled_back (forbidden). The work stays sandboxed (not run) until a
    /// reviewed classification exists. Needs caps::paradox.
    FrontierEntry special(Kind kind, Hexagram tag, const Digest32& sig, const Digest32& subject)
    {
        std::int32_t out = 0;
        std::uint32_t rec = 0;
        gated([&](zxv_paradox* b) {
            return zxv_paradox_special(cap_->cap, b, static_cast<std::int32_t>(kind), tag.tag(), sig.data(),
                                       subject.data(), &out, &rec);
        });
        FrontierEntry f;
        f.outcome = static_cast<Outcome>(out);
        if (f.outcome == Outcome::recorded) f.record = rec;
        return f;
    }
    /// A draft classification of frontier record `rec` for review (kind
    /// constructive, destructive or neutral). UsageError ZXV_E_NOT_FOUND
    /// (no such record). Needs caps::paradox.
    Draft draft(std::uint32_t rec, Kind kind, std::uint8_t reason, std::uint8_t flags = 0)
    {
        std::array<std::uint8_t, ZXV_PDX_DRAFT_BYTES> d{};
        gated([&](zxv_paradox* b) {
            return zxv_paradox_draft(cap_->cap, b, rec, static_cast<std::int32_t>(kind), reason, flags, d.data());
        });
        return Draft::from_bytes(d);
    }
    /// Review a draft (ours or a peer's; it must match this book's frontier
    /// record). A money or device draft needs `owner_ok`; others `owner_ok`
    /// or `consensus_ok` (your quorum result). Outcomes: accepted (a new
    /// registry entry), proposal_for_owner (not approved), refused (already
    /// registered, or registry full). VerificationFailed (the hash or
    /// signature does not match the record). Needs caps::paradox.
    Outcome review(const Draft& d, bool owner_ok, bool consensus_ok)
    {
        std::int32_t out = 0;
        gated([&](zxv_paradox* b) {
            return zxv_paradox_review(cap_->cap, b, d.bytes().data(), owner_ok ? 1 : 0, consensus_ok ? 1 : 0, &out);
        });
        return static_cast<Outcome>(out);
    }

    /// One pass over a graph (logged): Outcome::resolved or
    /// Outcome::pending. When the pass count reaches the step limit
    /// unresolved, the pass is logged and ParadoxStepLimit
    /// (ZXV_E_PARADOX_STEP) is thrown: the graph stays readable and goes to
    /// the owner. UsageError ZXV_E_NOT_FOUND (a dependency names no node;
    /// first pass only). Needs caps::paradox.
    Outcome pass(Graph& graph)
    {
        zxv_paradox_graph* g = graph.handle();
        std::int32_t out = 0;
        gated([&](zxv_paradox* b) { return zxv_paradox_graph_pass(cap_->cap, b, g, &out); });
        return static_cast<Outcome>(out);
    }

    /// The raw C handle (owned by this object; never destroy it).
    [[nodiscard]] zxv_paradox* native_handle() const noexcept { return b_.get(); }

private:
    [[nodiscard]] zxv_paradox* book() const { return zxv::detail::live(b_.get(), "paradox::Book"); }
    template <class F>
    void read(F&& f) const
    {
        const zxv_paradox* b = book();
        zxv::detail::check(f(b), cap_->ctx.get());
    }
    template <class F>
    void gated(F&& f) const
    {
        zxv_paradox* b = book();
        cap_->call([&] { return f(b); });
    }
    Outcome run_constructive(Policy& policy, Kind kind, Hexagram tag, const std::int64_t* synth,
                             std::uint32_t tension)
    {
        zxv_paradox_policy* p = policy.handle();
        std::int32_t out = 0;
        gated([&](zxv_paradox* b) {
            return zxv_paradox_constructive(cap_->cap, b, p, static_cast<std::int32_t>(kind), tag.tag(), synth,
                                            policy.size(), tension, &out);
        });
        return static_cast<Outcome>(out);
    }
    std::shared_ptr<zxv::detail::CapCore> cap_; // declared first: outlives the book
    zxv::detail::Owned<zxv_paradox, zxv_paradox_destroy> b_;
};

} // namespace paradox

// ===========================================================================
// Vino Embed (ABI 1.6)
// ===========================================================================

/// ABI 1.6: Vino Embed. What an app or game needs to take part in Vino
/// without ever holding anyone's value or keys. There are two lanes, and
/// they never meet:
///
///   * PLAY (vino::Play): a sandbox inside the app with its own play units,
///     issuers, holders and Fibonacci-face vouchers, in process memory only.
///     No call takes a play sandbox and anything live, so a play unit can
///     never be converted into, redeemed for, or moved into live value.
///     Play units are not money and are lost when the sandbox is destroyed.
///   * LIVE: real Vino stays in the user's own ZXV wallet, never in the app.
///     The app builds a payment request; the user approves it in their
///     wallet, which signs a receipt (vino::sign_receipt, wallet side); the
///     app checks the receipt offline (vino::verify_receipt). The app never
///     sees a balance, a key or a custody account.
///
/// VOUCHERS (play lane). A voucher is one coin whose face is a Fibonacci
/// ladder face (coin::face), held by one holder on exactly ONE active rail:
/// Rail::bearer (a note in the holder's hand) or Rail::ghost (its twin on
/// the play ledger). A transfer must name the active rail (else
/// RailMismatch), so one voucher can never be spent twice. Switching rails
/// moves nothing.
///
/// COVERAGE. An issuer posts play backing and may only issue while
///     10 * backing >= 18 * outstanding    (exact integers, no rounding)
/// i.e. backing stays at least 1.8x the faces it has outstanding. An issue
/// or a withdrawal that would break this is refused (CoverageRefused).
/// Redeeming a voucher hands it back to its issuer and retires it: the
/// issuer's outstanding falls and nothing is paid out to anyone. No call
/// turns a voucher into backing, play units or anything else, and there is
/// no interest, yield or return of any kind.
///
/// DAILY CAP. vino::SpendMeter counts what one device approved per day and
/// refuses an amount that would pass a deployment's daily cap. It is a
/// counter, not a legal check, and makes no exemption claim.
///
/// CAPABILITIES. Play and SpendMeter need caps::vino (else
/// CapabilityDenied; a halted context gives FatalError). Building a receipt
/// message and verifying a receipt are pure and need no capability. Signing
/// a receipt needs caps::crypto_sign: it is the wallet's call, not the app's.
///
/// NOT HERE, by design: minting live Vino, setting pegs, any way into or out
/// of other currencies, V-Bill auctions or bids, V-Bonds and derivatives,
/// and every operator or steward action. None of them is app-facing.
///
/// Honest limits: the LIVE flow here is the app's half only; approving a
/// request happens in the user's wallet (zxvd), which posts the payment on
/// its own ledger. ML-DSA-87 is the reference implementation, not FIPS
/// 140-3 validated. Whether a given deployment of Vino is store credit, a
/// prepaid access product or something else is a legal question pending
/// counsel; nothing in this SDK decides it. The raffle is a separate
/// module, not a payment request.
namespace vino {

/// The most issuers plus holders one play sandbox can hold (256).
inline constexpr std::uint32_t max_accounts = ZXV_VINO_MAX_ACCOUNTS;
/// The most active (unredeemed) vouchers one play sandbox can hold (4096).
inline constexpr std::uint32_t max_vouchers = ZXV_VINO_MAX_VOUCHERS;
/// The largest single amount: a backing post, a request amount, a daily cap
/// or a meter admission (2^59).
inline constexpr std::uint64_t amount_max = ZXV_VINO_AMOUNT_MAX;
/// Size of an encoded payment request (160 bytes).
inline constexpr std::size_t request_bytes = ZXV_VINO_REQ_BYTES;
/// Size of a request id: SHA-256 of the request bytes (32 bytes).
inline constexpr std::size_t request_id_bytes = 32;
/// Longest memo on a payment request, in bytes of printable ASCII (64).
inline constexpr std::size_t memo_max = ZXV_VINO_MEMO_MAX;
/// Size of the message a wallet signs to approve a request (88 bytes).
inline constexpr std::size_t receipt_message_bytes = ZXV_VINO_RCPT_MSG_BYTES;
/// Size of a wallet's ML-DSA-87 public key (2592 bytes).
inline constexpr std::size_t wallet_public_key_bytes = ZXV_MLDSA87_PK_BYTES;
/// Size of a wallet's ML-DSA-87 secret key (4896 bytes).
inline constexpr std::size_t wallet_secret_key_bytes = ZXV_MLDSA87_SK_BYTES;
/// Size of a receipt signature (ML-DSA-87, 4627 bytes).
inline constexpr std::size_t receipt_signature_bytes = ZXV_MLDSA87_SIG_BYTES;

/// Payment request flags (Request::flags).
namespace flag {
/// The purchase has a paid chance outcome (a loot box, a paid draw). Only
/// allowed on the PLAY lane; a LIVE request with it is PaidChanceRefused.
inline constexpr std::uint32_t chance = ZXV_VINO_REQ_CHANCE;
/// Every flag this ABI knows; any other bit is UsageError ZXV_E_FORMAT.
inline constexpr std::uint32_t known = ZXV_VINO_REQ_FLAGS_KNOWN;
} // namespace flag

/// The rail a play voucher is on. Exactly one is active at a time.
enum class Rail : std::int32_t {
    bearer = ZXV_VINO_RAIL_BEARER, ///< a note in the holder's hand
    ghost = ZXV_VINO_RAIL_GHOST,   ///< its twin on the play ledger
};

/// The lane of a payment request. The two lanes never meet.
enum class Lane : std::int32_t {
    play = ZXV_VINO_LANE_PLAY, ///< play units inside the app; never live value
    live = ZXV_VINO_LANE_LIVE, ///< real Vino, approved in the user's own wallet
};

/// Whether a play account issues vouchers or holds them.
enum class AccountKind : std::int32_t {
    holder = 0, ///< receives, spends and redeems vouchers
    issuer = 1, ///< posts play backing and issues vouchers against it
};

/// A play voucher's life: active until its holder redeems it.
enum class VoucherState : std::int32_t {
    active = ZXV_VINO_V_ACTIVE,     ///< can be transferred, switched or redeemed
    redeemed = ZXV_VINO_V_REDEEMED, ///< retired; still listed by id, does nothing
};

/// The encoded bytes of a payment request (160 bytes).
using RequestBytes = std::array<std::uint8_t, ZXV_VINO_REQ_BYTES>;
/// A request id: SHA-256 of the 160 request bytes.
using RequestId = Digest32;
/// The 88 bytes a wallet signs to approve a request:
/// "zxv/vino-rcpt/v1" || request id || SHA-256(wallet public key) ||
/// approved_at (8 bytes little-endian).
using ReceiptMessage = std::array<std::uint8_t, ZXV_VINO_RCPT_MSG_BYTES>;

/// A snapshot of one play voucher (Play::voucher).
struct VoucherInfo {
    std::uint64_t face = 0;               ///< its Fibonacci ladder face, in play units
    std::uint32_t issuer = 0;             ///< the issuer account that issued it
    std::uint32_t holder = 0;             ///< the holder account that holds (or redeemed) it
    Rail rail = Rail::bearer;             ///< its one active rail
    VoucherState state = VoucherState::active; ///< active or redeemed
    /// True while it can still be transferred, switched or redeemed.
    [[nodiscard]] bool is_active() const noexcept { return state == VoucherState::active; }
};

/// A snapshot of one play account (Play::account).
struct AccountInfo {
    std::uint32_t owner = 0;                  ///< the owner id given to Play::open
    AccountKind kind = AccountKind::holder;   ///< issuer or holder
    std::uint64_t backing = 0;                ///< an issuer's play backing; 0 for a holder
    std::uint64_t faces = 0;                  ///< an issuer's outstanding faces, or the faces a holder holds
    std::uint32_t count = 0;                  ///< active vouchers it issued (issuer) or holds (holder)
    /// True for an issuer account.
    [[nodiscard]] bool is_issuer() const noexcept { return kind == AccountKind::issuer; }
};

/// One play-lane sandbox: issuers, holders and Fibonacci-face vouchers in
/// play units, in process memory only. Play units come from nowhere (back()
/// is a faucet), are worth nothing, are not money, can never become live
/// value, and are lost when the sandbox is destroyed.
///
/// Every call needs caps::vino from the capability the sandbox was created
/// with (it keeps that capability). Refusals change nothing. Account ids
/// count up from 0; voucher ids start at 1 and are never reused.
///
/// Thread safety: internally serialized, so any thread may call it.
/// Move-only; a moved-from Play throws UsageError (ZXV_E_ARG) instead of
/// reaching C. It keeps its context alive, so it can never outlive it.
class Play {
public:
    /// An empty sandbox on the capability's context. CapabilityDenied
    /// without caps::vino; FatalError on a halted context.
    explicit Play(const Capability& cap) : cap_(detail::cap_of(cap))
    {
        zxv_vino_play* p = nullptr;
        cap_->call([&] { return zxv_vino_play_create(cap_->cap, &p); });
        p_.reset(p);
    }
    Play(Play&&) noexcept = default;            ///< move
    Play& operator=(Play&&) noexcept = default; ///< move-assign
    Play(const Play&) = delete;
    Play& operator=(const Play&) = delete;
    ~Play() = default;

    /// Open an account of `kind` for `owner` (an id of your choosing; owners
    /// need not be distinct) and return its account id. RefusedError
    /// ZXV_E_CAPACITY once max_accounts are open; UsageError ZXV_E_ARG for a
    /// kind that is neither holder nor issuer.
    std::uint32_t open(std::uint32_t owner, AccountKind kind)
    {
        std::uint32_t a = 0;
        run([&](zxv_vino_play* p) {
            return zxv_vino_play_open(cap_->cap, p, owner, static_cast<std::int32_t>(kind), &a);
        });
        return a;
    }
    /// Open an issuer account for `owner`; returns its account id.
    std::uint32_t open_issuer(std::uint32_t owner) { return open(owner, AccountKind::issuer); }
    /// Open a holder account for `owner`; returns its account id.
    std::uint32_t open_holder(std::uint32_t owner) { return open(owner, AccountKind::holder); }

    /// Post `amount` play units of backing to `issuer` (the play faucet).
    /// UsageError ZXV_E_ARG (not an issuer), ZXV_E_NOT_FOUND (no such
    /// account), ZXV_E_RANGE (amount 0, above amount_max, or backing would
    /// pass 2^62).
    void back(std::uint32_t issuer, std::uint64_t amount)
    {
        run([&](zxv_vino_play* p) { return zxv_vino_play_back(cap_->cap, p, issuer, amount); });
    }
    /// Withdraw `amount` of an issuer's play backing, only while it stays at
    /// 1.8x coverage. CoverageRefused (it would not), InsufficientFunds
    /// (more than its backing), plus back()'s errors.
    void unback(std::uint32_t issuer, std::uint64_t amount)
    {
        run([&](zxv_vino_play* p) { return zxv_vino_play_unback(cap_->cap, p, issuer, amount); });
    }

    /// Issue one voucher of Fibonacci face `face` from `issuer` to `holder`
    /// on `rail`; returns its id (> 0, never reused here).
    /// DenominationError (not a ladder face: 4, 6, ...), CoverageRefused
    /// (the issuer would fall below 1.8x), UsageError ZXV_E_ARG (issuer not
    /// an issuer, holder not a holder, bad rail) / ZXV_E_NOT_FOUND,
    /// RefusedError ZXV_E_CAPACITY (max_vouchers active).
    std::uint64_t issue(std::uint32_t issuer, std::uint32_t holder, std::uint64_t face, Rail rail = Rail::bearer)
    {
        std::uint64_t id = 0;
        run([&](zxv_vino_play* p) {
            return zxv_vino_issue(cap_->cap, p, issuer, holder, face, static_cast<std::int32_t>(rail), &id);
        });
        return id;
    }
    /// Spend active voucher `voucher` from its holder `from` to holder `to`
    /// on `rail`, which must be its active rail; the rail does not change.
    /// RailMismatch (not the active rail), RefusedError ZXV_E_STATE
    /// (redeemed), UsageError ZXV_E_ARG (`from` is not its holder, `to` not
    /// a holder, from == to) / ZXV_E_NOT_FOUND.
    void transfer(std::uint64_t voucher, std::uint32_t from, std::uint32_t to, Rail rail)
    {
        run([&](zxv_vino_play* p) {
            return zxv_vino_transfer(cap_->cap, p, voucher, from, to, static_cast<std::int32_t>(rail));
        });
    }
    /// Switch active voucher `voucher`, by its holder, to `rail` (bearer <->
    /// ghost). Nothing moves. RailMismatch (already on that rail),
    /// RefusedError ZXV_E_STATE (redeemed), UsageError ZXV_E_ARG (not its
    /// holder) / ZXV_E_NOT_FOUND.
    void set_rail(std::uint64_t voucher, std::uint32_t holder, Rail rail)
    {
        run([&](zxv_vino_play* p) {
            return zxv_vino_set_rail(cap_->cap, p, voucher, holder, static_cast<std::int32_t>(rail));
        });
    }
    /// Redeem active voucher `voucher`, by its holder, to its issuer: it is
    /// retired and the issuer's outstanding falls by its face. Nothing is
    /// paid out to anyone. RefusedError ZXV_E_STATE (already redeemed),
    /// UsageError ZXV_E_ARG (not its holder) / ZXV_E_NOT_FOUND.
    void redeem(std::uint64_t voucher, std::uint32_t holder)
    {
        run([&](zxv_vino_play* p) { return zxv_vino_redeem(cap_->cap, p, voucher, holder); });
    }

    /// A voucher's face, issuer, holder, rail and state (active or
    /// redeemed). UsageError ZXV_E_NOT_FOUND for an id never issued here.
    [[nodiscard]] VoucherInfo voucher(std::uint64_t id) const
    {
        VoucherInfo v;
        std::int32_t rail = 0, state = 0;
        run([&](zxv_vino_play* p) {
            return zxv_vino_voucher_info(cap_->cap, p, id, &v.face, &v.issuer, &v.holder, &rail, &state);
        });
        v.rail = static_cast<Rail>(rail);
        v.state = static_cast<VoucherState>(state);
        return v;
    }
    /// An account's owner, kind and totals. UsageError ZXV_E_NOT_FOUND for
    /// an account never opened here.
    [[nodiscard]] AccountInfo account(std::uint32_t account) const
    {
        AccountInfo a;
        std::int32_t is_issuer = 0;
        run([&](zxv_vino_play* p) {
            return zxv_vino_account_info(cap_->cap, p, account, &a.owner, &is_issuer, &a.backing, &a.faces, &a.count);
        });
        a.kind = is_issuer ? AccountKind::issuer : AccountKind::holder;
        return a;
    }
    /// The ids of the active vouchers `account` issued (issuer) or holds
    /// (holder), ascending. Redeemed vouchers are not listed. UsageError
    /// ZXV_E_NOT_FOUND for an unknown account.
    [[nodiscard]] std::vector<std::uint64_t> vouchers(std::uint32_t account) const
    {
        std::vector<std::uint64_t> ids;
        run([&](zxv_vino_play* p) {
            // Two-call pattern: the list can change only under this call's
            // own lock, so retry while another thread grows it in between.
            for (;;) {
                std::size_t n = 0;
                const std::int32_t st =
                    zxv_vino_vouchers(cap_->cap, p, account, ids.empty() ? nullptr : ids.data(), ids.size(), &n);
                if (st != ZXV_E_BUFFER) {
                    if (st == ZXV_OK) ids.resize(n);
                    return st;
                }
                ids.assign(n, 0);
            }
        });
        return ids;
    }
    /// Re-check every invariant of the sandbox: each active voucher has a
    /// ladder face, one rail, an issuer and a holder; each issuer's
    /// outstanding is the sum of its active faces and keeps 1.8x coverage;
    /// each holder's total is what it holds. A failure is FatalError
    /// (ZXV_E_INVARIANT) and halts the context.
    void check() const
    {
        run([&](zxv_vino_play* p) { return zxv_vino_play_check(cap_->cap, p); });
    }

    /// The raw C handle (owned by this object; never destroy it).
    [[nodiscard]] zxv_vino_play* native_handle() const noexcept { return p_.get(); }

private:
    template <class F>
    void run(F&& f) const
    {
        zxv_vino_play* p = detail::live(p_.get(), "vino::Play");
        cap_->call([&] { return f(p); });
    }
    std::shared_ptr<detail::CapCore> cap_; // declared first: outlives the sandbox
    detail::Owned<zxv_vino_play, zxv_vino_play_destroy> p_;
};

/// A payment request and its id, as Request::encode returns them.
struct EncodedRequest {
    RequestBytes bytes{}; ///< the canonical 160 bytes, what the wallet approves
    RequestId id{};       ///< SHA-256 of `bytes`
};

/// A payment request: a plain value the app builds and the user's wallet
/// approves. It travels as exactly 160 canonical bytes (encode); its id is
/// SHA-256 of those bytes. Encoding and decoding are pure: no capability,
/// no context, no network. Building a request moves nothing; only the
/// wallet can approve it.
///
/// Layout (all integers little-endian): 0 "ZXVR", 4 version 1, 5 lane,
/// 6 flags (2), 8 merchant (32), 40 nonce (16), 56 amount (8),
/// 64 created_at (8), 72 expires_at (8), 80 memo length (1), 81 memo
/// (NUL padded to 64), 145 fifteen zero bytes.
struct Request {
    Lane lane = Lane::live;                  ///< play units (play) or real Vino (live)
    std::uint32_t flags = 0;                 ///< vino::flag bits; flag::chance is play-only
    std::array<std::uint8_t, 32> merchant{}; ///< the payee's id (e.g. SHA-256 of its key)
    std::array<std::uint8_t, 16> nonce{};    ///< unique per request (e.g. from random_bytes)
    std::uint64_t amount = 0;     ///< 1..amount_max: VFV minor units (live) or play units (play)
    std::uint64_t created_at = 0; ///< seconds of the caller's clock (Unix time by convention)
    std::uint64_t expires_at = 0; ///< after created_at; the wallet cannot approve after it
    std::string memo;             ///< 0..64 bytes of printable ASCII (0x20..0x7e)

    /// The canonical 160 bytes and the id. LaneError (lane neither play nor
    /// live), PaidChanceRefused (a live request with flag::chance),
    /// UsageError ZXV_E_FORMAT (an unknown flag, a memo that is not
    /// printable ASCII) / ZXV_E_RANGE (amount 0 or above amount_max,
    /// expires_at not after created_at) / ZXV_E_TOO_LONG (memo above 64
    /// bytes) / ZXV_E_ARG (memo with a NUL byte).
    [[nodiscard]] EncodedRequest encode() const
    {
        const char* m = detail::cstr(memo, "request memo");
        EncodedRequest e;
        detail::check(zxv_vino_request_encode(static_cast<std::int32_t>(lane), flags, merchant.data(), nonce.data(),
                                              amount, created_at, expires_at, m, e.bytes.data(), e.id.data()));
        return e;
    }
    /// Just the canonical 160 bytes (see encode()).
    [[nodiscard]] RequestBytes bytes() const { return encode().bytes; }
    /// Just the id, SHA-256 of the canonical bytes (see encode()).
    [[nodiscard]] RequestId id() const { return encode().id; }

    /// Parse and validate 160 request bytes under the same rules as
    /// encode(), plus the magic, the version and the zero padding
    /// (UsageError ZXV_E_FORMAT). UsageError ZXV_E_ARG when `bytes` is not
    /// 160 bytes long.
    [[nodiscard]] static Request decode(std::span<const std::uint8_t> bytes) { return decode_with_id(bytes).first; }
    /// decode() plus the request's id.
    [[nodiscard]] static std::pair<Request, RequestId> decode_with_id(std::span<const std::uint8_t> bytes)
    {
        detail::need_size(bytes, request_bytes, "request");
        Request r;
        RequestId id{};
        std::int32_t lane = 0;
        char memo_buf[ZXV_VINO_MEMO_MAX + 1] = {};
        detail::check(zxv_vino_request_decode(bytes.data(), &lane, &r.flags, r.merchant.data(), r.nonce.data(),
                                              &r.amount, &r.created_at, &r.expires_at, memo_buf, id.data()));
        r.lane = static_cast<Lane>(lane);
        r.memo = memo_buf;
        return {std::move(r), id};
    }

    /// Field-by-field equality.
    friend bool operator==(const Request&, const Request&) = default;
};

/// The exact 88 bytes a wallet signs to approve request `request_id` with
/// its public key `wallet_public_key` (2592 bytes) at `approved_at`. Pure.
/// UsageError ZXV_E_ARG for a key of the wrong size.
inline ReceiptMessage receipt_message(const RequestId& request_id, std::span<const std::uint8_t> wallet_public_key,
                                      std::uint64_t approved_at)
{
    detail::need_size(wallet_public_key, wallet_public_key_bytes, "wallet public key");
    ReceiptMessage m{};
    detail::check(zxv_vino_receipt_msg(request_id.data(), wallet_public_key.data(), approved_at, m.data()));
    return m;
}

/// WALLET SIDE: approve the encoded request `request` at `approved_at`
/// (which must lie within the request's [created_at, expires_at]) by
/// signing its receipt message with the wallet's ML-DSA-87 key. Returns the
/// 4627-byte signature (hedged: two calls give different, equally valid
/// signatures). Needs caps::crypto_sign.
///
/// The secret key stays the caller's: it is read for this call only and
/// never copied into any object. RequestExpired (outside the window), the
/// request decoding errors (UsageError ZXV_E_FORMAT / ZXV_E_RANGE /
/// ZXV_E_TOO_LONG, LaneError, PaidChanceRefused), NotImplemented (built
/// without post-quantum crypto), CapabilityDenied.
inline std::vector<std::uint8_t> sign_receipt(const Capability& cap, std::span<const std::uint8_t> request,
                                              const SecretKey& wallet_secret_key,
                                              std::span<const std::uint8_t> wallet_public_key,
                                              std::uint64_t approved_at)
{
    const auto& c = detail::cap_of(cap);
    detail::need_size(request, request_bytes, "request");
    detail::need_size(wallet_secret_key.bytes(), wallet_secret_key_bytes, "wallet secret key");
    detail::need_size(wallet_public_key, wallet_public_key_bytes, "wallet public key");
    std::vector<std::uint8_t> sig(receipt_signature_bytes);
    c->call([&] {
        return zxv_vino_receipt_sign(c->cap, request.data(), wallet_secret_key.bytes().data(),
                                     wallet_public_key.data(), approved_at, sig.data());
    });
    return sig;
}

/// APP SIDE, offline: check that `signature` is the wallet's approval of the
/// encoded `request` at `approved_at`, that approved_at lies within the
/// request's window and that `now` is not past its expiry. Returns normally
/// when it is. Pure: no capability, no network.
///
/// VerificationFailed (not that wallet's signature over this request and
/// time), RequestExpired (approved outside the window, or `now` past the
/// expiry), the request decoding errors, NotImplemented (built without
/// post-quantum crypto), UsageError ZXV_E_ARG (a buffer of the wrong size).
inline void verify_receipt(std::span<const std::uint8_t> request, std::span<const std::uint8_t> wallet_public_key,
                           std::uint64_t approved_at, std::span<const std::uint8_t> signature, std::uint64_t now)
{
    detail::need_size(request, request_bytes, "request");
    detail::need_size(wallet_public_key, wallet_public_key_bytes, "wallet public key");
    detail::need_size(signature, receipt_signature_bytes, "receipt signature");
    detail::check(zxv_vino_receipt_verify(request.data(), wallet_public_key.data(), approved_at, signature.data(), now));
}

/// verify_receipt as a yes/no answer: false for VerificationFailed or
/// RequestExpired; every other error (a malformed request, a wrong-size
/// buffer) still throws, since it is a bug, not a bad receipt.
[[nodiscard]] inline bool receipt_is_valid(std::span<const std::uint8_t> request,
                                           std::span<const std::uint8_t> wallet_public_key, std::uint64_t approved_at,
                                           std::span<const std::uint8_t> signature, std::uint64_t now)
{
    try {
        verify_receipt(request, wallet_public_key, approved_at, signature, now);
        return true;
    } catch (const VerificationFailed&) {
        return false;
    } catch (const RequestExpired&) {
        return false;
    }
}

/// What SpendMeter::try_admit decided.
struct MeterDecision {
    bool admitted = false;  ///< true when the amount was counted
    std::uint64_t left = 0; ///< what is left of today's cap after the call (either way)
};

/// One device's daily spend counter. It counts what the device approved per
/// day index and refuses an amount that would pass the daily cap. It is a
/// counter, not a legal check, and makes no exemption claim. Needs
/// caps::vino. Internally serialized; move-only; a moved-from meter throws
/// UsageError (ZXV_E_ARG).
class SpendMeter {
public:
    /// A meter with cap `daily_cap` (1..amount_max) per day. UsageError
    /// ZXV_E_RANGE outside that, CapabilityDenied without caps::vino.
    SpendMeter(const Capability& cap, std::uint64_t daily_cap) : cap_(detail::cap_of(cap))
    {
        zxv_vino_meter* m = nullptr;
        cap_->call([&] { return zxv_vino_meter_create(cap_->cap, daily_cap, &m); });
        m_.reset(m);
    }
    SpendMeter(SpendMeter&&) noexcept = default;            ///< move
    SpendMeter& operator=(SpendMeter&&) noexcept = default; ///< move-assign
    SpendMeter(const SpendMeter&) = delete;
    SpendMeter& operator=(const SpendMeter&) = delete;
    ~SpendMeter() = default;

    /// Count `amount` against day `day` (for example Unix time / 86400) and
    /// return what is left today. A day after the last one starts a fresh
    /// count. LimitRefused when today's total would pass the cap (nothing is
    /// counted; use try_admit to learn what is left), UsageError
    /// ZXV_E_RANGE for an amount of 0 or above amount_max, or a day before
    /// the last one (the clock went back).
    std::uint64_t admit(std::uint64_t day, std::uint64_t amount)
    {
        std::uint64_t left = 0;
        run([&](zxv_vino_meter* m) { return zxv_vino_meter_admit(cap_->cap, m, day, amount, &left); });
        return left;
    }
    /// Like admit(), but a cap refusal is an answer, not an exception:
    /// {false, left} when the amount would pass today's cap. Other errors
    /// still throw.
    MeterDecision try_admit(std::uint64_t day, std::uint64_t amount)
    {
        zxv_vino_meter* m = detail::live(m_.get(), "vino::SpendMeter");
        detail::ensure_alive(cap_->ctx.get());
        MeterDecision d;
        const std::int32_t st = zxv_vino_meter_admit(cap_->cap, m, day, amount, &d.left);
        if (st == ZXV_E_LIMIT) return d;
        detail::check(st, cap_->ctx.get());
        d.admitted = true;
        return d;
    }
    /// The raw C handle (owned by this object; never destroy it).
    [[nodiscard]] zxv_vino_meter* native_handle() const noexcept { return m_.get(); }

private:
    template <class F>
    void run(F&& f) const
    {
        zxv_vino_meter* m = detail::live(m_.get(), "vino::SpendMeter");
        cap_->call([&] { return f(m); });
    }
    std::shared_ptr<detail::CapCore> cap_; // declared first: outlives the meter
    detail::Owned<zxv_vino_meter, zxv_vino_meter_destroy> m_;
};

} // namespace vino

// Out-of-line definitions that need complete types.
inline std::shared_ptr<detail::CtxCore> Message::ctx_core(const Context& c) { return c.core_; }

} // namespace zxv

#endif // ZXV_HPP
