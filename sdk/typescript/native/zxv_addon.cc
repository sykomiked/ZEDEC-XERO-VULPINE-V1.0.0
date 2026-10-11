// Copyright (c) 2024-2026 Michael Laurence Curzi and 36N9 Genetics, LLC
// SPDX-License-Identifier: Apache-2.0
//
// zxv_addon.cc — the Node-API backend of @zedec/zxv.
//
// The addon does not link libzxv at build time. `load(path)` opens the
// library the TypeScript layer located (explicit path, ZXV_LIBRARY_PATH,
// the bundled copy, then the system loader; sdk/docs/SDK_DESIGN.md §1)
// and exposes every zxv_* function through ONE generic wrapper that is
// instantiated from the function's C type in zxv_api.h:
//
//   integer parameters  <- JS number (must be an integer in range) or bigint
//   pointer parameters  <- null/undefined (NULL), a TypedArray (its bytes,
//                          for the duration of the call), or a bigint
//                          address of addon-owned memory or a handle
//   int32_t result      -> number; const char * result -> string
//
// So the C types, not hand-written glue, decide every conversion, and a
// wrong JS type is a TypeError before C runs. This object is internal: the
// typed public API in src/ is the only supported way to call it.
//
// Generation runs on a libuv worker thread (Napi::AsyncProgressQueueWorker)
// and streams each token back to the event loop; the event loop is never
// blocked by the engine. Secret keys live in memory allocated here (outside
// the V8 heap) and are wiped with zxv_secure_zero before being freed.

#include <napi.h>

#include <atomic>
#include <cmath>
#include <condition_variable>
#include <cstdint>
#include <cstdlib>
#include <cstring>
#include <limits>
#include <memory>
#include <mutex>
#include <string>
#include <type_traits>
#include <vector>

#include "zxv_api.h"

#if defined(_WIN32)
#    include <windows.h>
static void *open_library(const std::string &path, std::string &err)
{
    HMODULE h = LoadLibraryA(path.c_str());
    if (!h) err = "LoadLibrary failed with error " + std::to_string(GetLastError());
    return reinterpret_cast<void *>(h);
}
static void *find_symbol(void *lib, const char *name)
{
    return reinterpret_cast<void *>(GetProcAddress(reinterpret_cast<HMODULE>(lib), name));
}
#else
#    include <dlfcn.h>
static void *open_library(const std::string &path, std::string &err)
{
    void *h = dlopen(path.c_str(), RTLD_NOW | RTLD_LOCAL);
    if (!h) {
        const char *e = dlerror();
        err = e ? e : "dlopen failed";
    }
    return h;
}
static void *find_symbol(void *lib, const char *name)
{
    return dlsym(lib, name);
}
#endif

namespace {

void *g_lib = nullptr;
std::string g_lib_path;

decltype(&zxv_generate) g_generate = nullptr;
decltype(&zxv_last_error) g_last_error = nullptr;
decltype(&zxv_secure_zero) g_secure_zero = nullptr;

// ---- JS -> C argument conversion, chosen by the C parameter type ----

template <typename T>
typename std::enable_if<std::is_integral<T>::value, T>::type from_js(const Napi::Value &v, size_t i)
{
    Napi::Env env = v.Env();
    if (v.IsBigInt()) {
        bool lossless = false;
        if (std::is_signed<T>::value) {
            int64_t x = v.As<Napi::BigInt>().Int64Value(&lossless);
            if (!lossless || x < static_cast<int64_t>(std::numeric_limits<T>::min()) ||
                x > static_cast<int64_t>(std::numeric_limits<T>::max()))
                throw Napi::RangeError::New(env, "argument " + std::to_string(i) + " out of range");
            return static_cast<T>(x);
        }
        uint64_t x = v.As<Napi::BigInt>().Uint64Value(&lossless);
        if (!lossless || x > static_cast<uint64_t>(std::numeric_limits<T>::max()))
            throw Napi::RangeError::New(env, "argument " + std::to_string(i) + " out of range");
        return static_cast<T>(x);
    }
    if (v.IsNumber()) {
        double d = v.As<Napi::Number>().DoubleValue();
        if (!std::isfinite(d) || std::trunc(d) != d)
            throw Napi::TypeError::New(env, "argument " + std::to_string(i) + " is not an integer");
        if (d < static_cast<double>(std::numeric_limits<T>::min()) ||
            d > static_cast<double>(std::numeric_limits<T>::max()) || std::fabs(d) > 9007199254740991.0)
            throw Napi::RangeError::New(env, "argument " + std::to_string(i) + " out of range");
        return static_cast<T>(static_cast<int64_t>(d));
    }
    if (v.IsBoolean()) return static_cast<T>(v.As<Napi::Boolean>().Value() ? 1 : 0);
    throw Napi::TypeError::New(env, "argument " + std::to_string(i) + " must be a number or bigint");
}

template <typename T>
typename std::enable_if<std::is_pointer<T>::value, T>::type from_js(const Napi::Value &v, size_t i)
{
    if (v.IsNull() || v.IsUndefined()) return nullptr;
    if (v.IsTypedArray()) {
        Napi::TypedArray ta = v.As<Napi::TypedArray>();
        uint8_t *base = static_cast<uint8_t *>(ta.ArrayBuffer().Data());
        if (!base) return nullptr; // zero-length buffer
        return reinterpret_cast<T>(base + ta.ByteOffset());
    }
    if (v.IsBigInt()) {
        bool lossless = false;
        uint64_t a = v.As<Napi::BigInt>().Uint64Value(&lossless);
        if (!lossless) throw Napi::RangeError::New(v.Env(), "bad address");
        return reinterpret_cast<T>(static_cast<uintptr_t>(a));
    }
    throw Napi::TypeError::New(v.Env(), "argument " + std::to_string(i) +
                                            " must be a TypedArray, a bigint address or null");
}

template <typename R>
Napi::Value to_js(Napi::Env env, R r)
{
    if constexpr (std::is_same<R, const char *>::value)
        return Napi::String::New(env, r ? r : "");
    else
        return Napi::Number::New(env, static_cast<double>(r));
}

// One wrapper per C function type; the function pointer travels as the
// Napi::Function's data.
template <typename F>
struct Wrap;

template <typename R, typename... A>
struct Wrap<R (*)(A...)> {
    using Fn = R (*)(A...);

    template <size_t... I>
    static Napi::Value invoke(const Napi::CallbackInfo &info, Fn fn, std::index_sequence<I...>)
    {
        if (info.Length() != sizeof...(A))
            throw Napi::TypeError::New(info.Env(), "expected " + std::to_string(sizeof...(A)) +
                                                       " arguments, got " +
                                                       std::to_string(info.Length()));
        if constexpr (std::is_void<R>::value) {
            fn(from_js<A>(info[I], I)...);
            return info.Env().Undefined();
        } else {
            return to_js<R>(info.Env(), fn(from_js<A>(info[I], I)...));
        }
    }

    static Napi::Value call(const Napi::CallbackInfo &info)
    {
        Fn fn = reinterpret_cast<Fn>(info.Data());
        return invoke(info, fn, std::index_sequence_for<A...>{});
    }
};

template <typename F>
void bind(Napi::Env env, Napi::Object &out, const char *name, F /*type witness*/, std::vector<std::string> &missing)
{
    void *sym = find_symbol(g_lib, name);
    if (!sym) {
        missing.push_back(name);
        return;
    }
    out.Set(name, Napi::Function::New(env, &Wrap<F>::call, name, sym));
}

std::string last_error_text()
{
    if (!g_last_error) return "";
    char buf[512];
    size_t len = 0;
    if (g_last_error(buf, sizeof buf, &len) != ZXV_OK) return "";
    return std::string(buf, len);
}

// ---- memory owned by the addon (secrets, handle slots) ----

Napi::Value Alloc(const Napi::CallbackInfo &info)
{
    size_t n = from_js<uint32_t>(info[0], 0);
    void *p = std::calloc(n ? n : 1, 1);
    if (!p) throw Napi::Error::New(info.Env(), "out of memory");
    return Napi::BigInt::New(info.Env(), static_cast<uint64_t>(reinterpret_cast<uintptr_t>(p)));
}

Napi::Value Free(const Napi::CallbackInfo &info)
{
    void *p = from_js<void *>(info[0], 0);
    size_t n = from_js<uint32_t>(info[1], 1);
    if (p && n && g_secure_zero) g_secure_zero(p, n);
    std::free(p);
    return info.Env().Undefined();
}

Napi::Value Read(const Napi::CallbackInfo &info)
{
    const uint8_t *p = from_js<const uint8_t *>(info[0], 0);
    size_t n = from_js<uint32_t>(info[1], 1);
    Napi::Uint8Array out = Napi::Uint8Array::New(info.Env(), n);
    if (n) std::memcpy(out.Data(), p, n);
    return out;
}

Napi::Value Write(const Napi::CallbackInfo &info)
{
    uint8_t *p = from_js<uint8_t *>(info[0], 0);
    Napi::Uint8Array src = info[1].As<Napi::Uint8Array>();
    if (src.ByteLength()) std::memcpy(p, src.Data(), src.ByteLength());
    return info.Env().Undefined();
}

// ---- generation on a worker thread, streamed back as progress ----

struct Tok {
    int32_t id = 0;
    std::string piece;
};

class GenerateWorker : public Napi::AsyncProgressQueueWorker<Tok> {
  public:
    GenerateWorker(Napi::Function on_token, Napi::Function on_done, const zxv_cap *cap,
                   zxv_session *session, std::vector<uint8_t> prompt, uint32_t max_tokens,
                   int32_t temp, uint32_t top_k, int32_t top_p, int32_t rep, uint32_t rep_n,
                   uint64_t seed, std::shared_ptr<std::atomic<bool>> cancel)
        : Napi::AsyncProgressQueueWorker<Tok>(on_done, "zxv.generate"), cap_(cap),
          session_(session), prompt_(std::move(prompt)), max_tokens_(max_tokens), temp_(temp),
          top_k_(top_k), top_p_(top_p), rep_(rep), rep_n_(rep_n), seed_(seed),
          cancel_(std::move(cancel))
    {
        on_token_ = Napi::Persistent(on_token);
    }

    void Execute(const ExecutionProgress &progress) override
    {
        progress_ = &progress;
        status_ = g_generate(cap_, session_, prompt_.data(), prompt_.size(), max_tokens_, temp_,
                             top_k_, top_p_, rep_, rep_n_, seed_, &GenerateWorker::on_token, this,
                             &n_prompt_, &n_generated_, &stop_);
        if (status_ != ZXV_OK) detail_ = last_error_text();
        progress_ = nullptr;
    }

    // Runs on the event loop. The JS callback returns false to stop; the
    // worker waits for this answer before computing the next token, so a
    // stop takes effect exactly after the token that asked for it, as on the
    // synchronous wasm backend (one event-loop round trip per token).
    void OnProgress(const Tok *data, size_t count) override
    {
        Napi::HandleScope scope(Env());
        for (size_t i = 0; i < count; i++) {
            Napi::Uint8Array piece = Napi::Uint8Array::New(Env(), data[i].piece.size());
            if (!data[i].piece.empty())
                std::memcpy(piece.Data(), data[i].piece.data(), data[i].piece.size());
            try {
                Napi::Value r = on_token_.Call({Napi::Number::New(Env(), data[i].id), piece});
                if (r.IsBoolean() && !r.As<Napi::Boolean>().Value()) cancel_->store(true);
            } catch (const Napi::Error &) {
                cancel_->store(true); // the JS layer reports its own callback errors
            }
        }
        std::lock_guard<std::mutex> lock(mu_);
        acked_ += count;
        cv_.notify_all();
    }

    void OnOK() override
    {
        Napi::Env env = Env();
        Callback().Call({Napi::Number::New(env, status_), Napi::Number::New(env, n_prompt_),
                         Napi::Number::New(env, n_generated_), Napi::Number::New(env, stop_),
                         Napi::String::New(env, detail_)});
    }

  private:
    static int32_t on_token(void *user, int32_t token, const uint8_t *piece, size_t len)
    {
        auto *self = static_cast<GenerateWorker *>(user);
        Tok t;
        t.id = token;
        t.piece.assign(reinterpret_cast<const char *>(piece), len);
        self->progress_->Send(&t, 1);
        std::unique_lock<std::mutex> lock(self->mu_);
        uint64_t mine = ++self->sent_;
        self->cv_.wait(lock, [&] { return self->acked_ >= mine; });
        return self->cancel_->load() ? 0 : 1;
    }

    const zxv_cap *cap_;
    zxv_session *session_;
    std::vector<uint8_t> prompt_;
    uint32_t max_tokens_;
    int32_t temp_;
    uint32_t top_k_;
    int32_t top_p_, rep_;
    uint32_t rep_n_;
    uint64_t seed_;
    std::shared_ptr<std::atomic<bool>> cancel_;
    Napi::FunctionReference on_token_;
    const ExecutionProgress *progress_ = nullptr;
    std::mutex mu_;
    std::condition_variable cv_;
    uint64_t sent_ = 0, acked_ = 0;
    int32_t status_ = ZXV_E_INTERNAL;
    uint32_t n_prompt_ = 0, n_generated_ = 0;
    int32_t stop_ = 0;
    std::string detail_;
};

// generate(cap, session, prompt, maxTokens, tempQ16, topK, topPQ16, repQ16,
//          repLastN, seed, onToken(id, piece), onDone(status, nPrompt,
//          nGenerated, stop, detail)) -> cancel()
Napi::Value Generate(const Napi::CallbackInfo &info)
{
    Napi::Env env = info.Env();
    if (info.Length() != 12) throw Napi::TypeError::New(env, "generate: expected 12 arguments");
    auto *cap = from_js<const zxv_cap *>(info[0], 0);
    auto *session = from_js<zxv_session *>(info[1], 1);
    Napi::Uint8Array p = info[2].As<Napi::Uint8Array>();
    std::vector<uint8_t> prompt(p.Data(), p.Data() + p.ByteLength());
    auto cancel = std::make_shared<std::atomic<bool>>(false);
    auto *w = new GenerateWorker(info[10].As<Napi::Function>(), info[11].As<Napi::Function>(), cap,
                                 session, std::move(prompt), from_js<uint32_t>(info[3], 3),
                                 from_js<int32_t>(info[4], 4), from_js<uint32_t>(info[5], 5),
                                 from_js<int32_t>(info[6], 6), from_js<int32_t>(info[7], 7),
                                 from_js<uint32_t>(info[8], 8), from_js<uint64_t>(info[9], 9), cancel);
    w->Queue();
    return Napi::Function::New(env, [cancel](const Napi::CallbackInfo &ci) {
        cancel->store(true);
        return ci.Env().Undefined();
    });
}

#define FN(name) bind(env, out, #name, &name, missing)

Napi::Value Load(const Napi::CallbackInfo &info)
{
    Napi::Env env = info.Env();
    std::string path = info[0].As<Napi::String>().Utf8Value();
    if (g_lib && path != g_lib_path)
        throw Napi::Error::New(env, "libzxv is already loaded from " + g_lib_path +
                                        "; one library per process");
    if (!g_lib) {
        std::string err;
        g_lib = open_library(path, err);
        if (!g_lib) throw Napi::Error::New(env, "cannot load " + path + ": " + err);
        g_lib_path = path;
    }
    Napi::Object out = Napi::Object::New(env);
    std::vector<std::string> missing;
    // Library, context, reference data.
    FN(zxv_version); FN(zxv_features); FN(zxv_status_name); FN(zxv_status_class);
    FN(zxv_last_error); FN(zxv_ctx_create); FN(zxv_ctx_destroy); FN(zxv_ctx_is_poisoned);
    FN(zxv_ctx_halt); FN(zxv_iso4217_by_alpha); FN(zxv_iso4217_by_numeric); FN(zxv_iso4217_name);
    FN(zxv_iso4217_count); FN(zxv_iso4217_at); FN(zxv_iso4217_published); FN(zxv_ccy_caveat);
    FN(zxv_au_is_member); FN(zxv_rail_numeric); FN(zxv_ccy_enable); FN(zxv_ccy_enable_with_minor);
    FN(zxv_ccy_register_private); FN(zxv_ccy_get); FN(zxv_ccy_enabled_count); FN(zxv_ccy_enabled_at);
    // Ledger, messages, netting, cards, VSS.
    FN(zxv_ledger_open_account); FN(zxv_ledger_post); FN(zxv_ledger_balance);
    FN(zxv_ledger_account_ccy); FN(zxv_ledger_account_name); FN(zxv_ledger_account_kind);
    FN(zxv_ledger_account_count); FN(zxv_ledger_entry_count); FN(zxv_ledger_entry_at);
    FN(zxv_ledger_check); FN(zxv_msg_create); FN(zxv_msg_destroy); FN(zxv_msg_set_text);
    FN(zxv_msg_set_amount); FN(zxv_msg_set_tx_count); FN(zxv_msg_add_entry); FN(zxv_msg_render);
    FN(zxv_netting_open); FN(zxv_netting_destroy); FN(zxv_netting_add_participant);
    FN(zxv_netting_submit); FN(zxv_netting_close); FN(zxv_netting_position_count);
    FN(zxv_netting_position_at); FN(zxv_card_check_digit); FN(zxv_card_issue); FN(zxv_card_reissue);
    FN(zxv_card_authorize); FN(zxv_vss_run); FN(zxv_vss_level_get);
    // ABI 1.1.
    FN(zxv_rat_normalize); FN(zxv_rat_add); FN(zxv_rat_sub); FN(zxv_rat_mul); FN(zxv_rat_div);
    FN(zxv_rat_cmp); FN(zxv_rat_parse); FN(zxv_rat_format); FN(zxv_rat_from_ieee754);
    FN(zxv_rat_to_q16); FN(zxv_lp_not); FN(zxv_lp_and); FN(zxv_lp_or); FN(zxv_lp_from_evidence);
    FN(zxv_lp_name); FN(zxv_cap_create); FN(zxv_cap_attenuate); FN(zxv_cap_revoke);
    FN(zxv_cap_set_attestation); FN(zxv_cap_info); FN(zxv_cap_check); FN(zxv_cap_destroy);
    FN(zxv_ledger_post_cap); FN(zxv_ledger_balance_cap); FN(zxv_ledger_check_cap);
    FN(zxv_lpres_create); FN(zxv_lpres_destroy); FN(zxv_lpres_attest); FN(zxv_lpres_evidence);
    FN(zxv_lpres_find); FN(zxv_lpres_state); FN(zxv_lpres_gate); FN(zxv_rmag_create);
    FN(zxv_rmag_destroy); FN(zxv_rmag_budget); FN(zxv_rmag_consume); FN(zxv_rmag_release);
    FN(zxv_rmag_set_limit); FN(zxv_rmag_status); FN(zxv_phase_create); FN(zxv_phase_destroy);
    FN(zxv_phase_now); FN(zxv_phase_advance); FN(zxv_phase_admit); FN(zxv_phase_set_health);
    FN(zxv_phase_defer); FN(zxv_phase_event); FN(zxv_phase_cancel); FN(zxv_phase_next_due);
    FN(zxv_wire_tag); FN(zxv_wire_pack); FN(zxv_wire_unpack); FN(zxv_wire_resolve); FN(zxv_wire_idle);
    FN(zxv_swarm_create); FN(zxv_swarm_destroy); FN(zxv_swarm_register); FN(zxv_swarm_set_active);
    FN(zxv_swarm_set_rate); FN(zxv_swarm_set_mood); FN(zxv_swarm_set_feeling);
    FN(zxv_swarm_begin_cycle); FN(zxv_swarm_consume); FN(zxv_swarm_allotment);
    FN(zxv_swarm_end_cycle); FN(zxv_swarm_level_capacity); FN(zxv_swarm_emotion_profile);
    FN(zxv_sha3_256); FN(zxv_shake256); FN(zxv_keccak256); FN(zxv_random); FN(zxv_secure_zero);
    FN(zxv_mlkem1024_keygen); FN(zxv_mlkem1024_keygen_derand); FN(zxv_mlkem1024_encaps);
    FN(zxv_mlkem1024_encaps_derand); FN(zxv_mlkem1024_decaps); FN(zxv_mldsa87_keygen);
    FN(zxv_mldsa87_keygen_seed); FN(zxv_mldsa87_sign); FN(zxv_mldsa87_sign_deterministic);
    FN(zxv_mldsa87_verify); FN(zxv_ed25519_keygen); FN(zxv_ed25519_public); FN(zxv_ed25519_sign);
    FN(zxv_ed25519_verify); FN(zxv_model_open); FN(zxv_model_open_memory); FN(zxv_model_destroy);
    FN(zxv_model_info); FN(zxv_tokenize); FN(zxv_detokenize); FN(zxv_session_create);
    FN(zxv_session_destroy); FN(zxv_session_reset); FN(zxv_session_position);
    FN(zxv_session_bind_budget); FN(zxv_lora_load); FN(zxv_lora_apply); FN(zxv_lora_destroy);
    FN(zxv_evm_encode_state); FN(zxv_evm_ledger_state);
    // ABI 1.2: the _cap forms.
    FN(zxv_ccy_enable_cap); FN(zxv_ccy_enable_with_minor_cap); FN(zxv_ccy_register_private_cap);
    FN(zxv_ledger_open_account_cap); FN(zxv_ledger_account_ccy_cap); FN(zxv_ledger_account_name_cap);
    FN(zxv_ledger_account_kind_cap); FN(zxv_ledger_account_count_cap); FN(zxv_ledger_entry_count_cap);
    FN(zxv_ledger_entry_at_cap); FN(zxv_msg_create_cap); FN(zxv_rmag_create_cap);
    FN(zxv_phase_create_cap); FN(zxv_lpres_create_cap); FN(zxv_swarm_create_cap);
    // ABI 1.3: the hexagram tag and the quarantine log
    FN(zxv_hex_pack); FN(zxv_hex_unpack); FN(zxv_hex_action); FN(zxv_hex_phase_next);
    FN(zxv_hex_step); FN(zxv_hex_set); FN(zxv_hex_forbid); FN(zxv_hex_and); FN(zxv_hex_or);
    FN(zxv_hex_not); FN(zxv_hex_polarity); FN(zxv_hex_from_lp); FN(zxv_hex_to_lp);
    FN(zxv_hex_name); FN(zxv_hex_action_name); FN(zxv_hexlog_create); FN(zxv_hexlog_destroy);
    FN(zxv_hexlog_append); FN(zxv_hexlog_count); FN(zxv_hexlog_head); FN(zxv_hexlog_at);
    FN(zxv_hexlog_verify);

    g_generate = reinterpret_cast<decltype(g_generate)>(find_symbol(g_lib, "zxv_generate"));
    g_last_error = reinterpret_cast<decltype(g_last_error)>(find_symbol(g_lib, "zxv_last_error"));
    g_secure_zero = reinterpret_cast<decltype(g_secure_zero)>(find_symbol(g_lib, "zxv_secure_zero"));
    if (!g_generate) missing.push_back("zxv_generate");
    if (!missing.empty()) {
        std::string m;
        for (auto &s : missing) m += (m.empty() ? "" : ", ") + s;
        throw Napi::Error::New(env, path + " lacks symbols: " + m);
    }
    out.Set("zxvjs_generate", Napi::Function::New(env, Generate, "zxvjs_generate"));
    out.Set("alloc", Napi::Function::New(env, Alloc, "alloc"));
    out.Set("free", Napi::Function::New(env, Free, "free"));
    out.Set("read", Napi::Function::New(env, Read, "read"));
    out.Set("write", Napi::Function::New(env, Write, "write"));
    out.Set("pointerBytes", Napi::Number::New(env, sizeof(void *)));
    out.Set("path", Napi::String::New(env, g_lib_path));
    return out;
}

Napi::Object Init(Napi::Env env, Napi::Object exports)
{
    exports.Set("load", Napi::Function::New(env, Load, "load"));
    return exports;
}

} // namespace

NODE_API_MODULE(zxv_addon, Init)
