<!-- Copyright (c) 2024-2026 Michael Laurence Curzi and 36N9 Genetics, LLC -->
<!-- SPDX-License-Identifier: Apache-2.0 -->

# ZXV C++ SDK

The C++ SDK is one header, `include/zxv.hpp`. It needs C++20 and declares everything in the `zxv` namespace. It wraps the C library
`libzxv` (`sdk/libzxv/include/zxv_api.h`) with RAII handles, exceptions,
exact rationals and `std::future`-based waiting. It follows the cross-language
rules in [`sdk/docs/SDK_DESIGN.md`](../docs/SDK_DESIGN.md). Unreal Engine and
other C++ game engines use this SDK too (see [Game engines](#game-engines-unreal-and-deterministic-lockstep)).

## Install

You need a C++20 compiler (gcc 13+, clang 18+ with libstdc++ or libc++, or Apple clang from
Xcode 15 or later; tested with gcc 13 and clang 18 on Linux; CI also builds with the default
Xcode on macOS 15), CMake 3.20 or later and a built `libzxv` (`make -C sdk/libzxv`). Where the
standard library has no `std::jthread` or `<stop_token>` (Apple's libc++ in Xcode 15 and 16),
`zxv.hpp` uses its bundled fallback `zxv::stop_source`, `zxv::stop_token`, `zxv::stop_callback`
and `zxv::jthread`; elsewhere those names are the `std` types. `-DZXV_FORCE_STOP_TOKEN_FALLBACK`
forces the fallback (the `test_zxv_stop_fallback` ctest target does this).

```sh
cmake -S sdk/cpp -B build            # finds libzxv (see below)
cmake --build build
ctest --test-dir build --output-on-failure
```

CMake looks for `libzxv` in this order:

1. `-DZXV_LIBRARY=/path/libzxv.so -DZXV_INCLUDE_DIR=/path/include`
2. the `ZXV_LIBRARY_PATH` environment variable (a file or a directory)
3. the in-repo build, `sdk/libzxv/build`
4. the system linker path

To link `libzxv.a` instead, pass `-DZXV_STATIC=ON`. In your own project:

```cmake
add_subdirectory(path/to/sdk/cpp)            # or copy zxv.hpp + link libzxv yourself
target_link_libraries(my_app PRIVATE zxv::zxv)
```

At run time the SDK calls `zxv_version` once. It refuses a library whose
major version is not 1 or whose minor version is below 3 (ABI 1.3 added the
hexagram tag, 1.2 the capability-gated `*_cap` forms the SDK calls).

## Quickstart

```cpp
#include <zxv.hpp>
zxv::Context ctx;                                   // one platform instance
zxv::Capability root = ctx.root_capability();       // all bits, attested TRUE
ctx.enable_currency(root, "NGN");                  // needs caps::ledger_post
zxv::Ledger ledger(root);
auto bank = ledger.open_account("bank", "NGN", zxv::AccountKind::issuer);
auto ada  = ledger.open_account("ada", "NGN");
ledger.post(bank, ada, 125075, "NGN", "salary-001"); // exact kobo, no floats
std::cout << ledger.balance(ada).equity << "\n";     // 125075
std::cout << zxv::Rational::parse("0.1") + zxv::Rational::parse("0.2"); // 0.3
```

`examples/quickstart.cpp` is a longer version of this quickstart that you can run (`build/zxv_quickstart`).

## Concepts in C++ terms

| Concept | C++ form |
|---|---|
| Handles | Move-only RAII classes (`Context`, `Capability`, `Budgets`, `PhaseClock`, `Model`, `Session`, `SecretKey`, ...). Children keep their parents alive through `shared_ptr` cores, so the C rule "destroy children before the context" (R6) cannot be broken. |
| Errors | `zxv::Exception` (derives from `std::runtime_error`) carries `status()`, `status_name()` and `detail()`. Its subclasses are `UsageError` (and `FloatRejected`), `RefusedError` (`CapabilityDenied`, `BudgetExhausted`, `InsufficientFunds`, `VerificationFailed`, `NotImplemented`) and `SystemError`. |
| Fail-closed | A FATAL status (`ZXV_E_POISONED` or `ZXV_E_INVARIANT`) marks the context dead and throws `zxv::FatalError`. `FatalError` does not derive from `std::exception`, so `catch (const std::exception&)` cannot swallow it. `zxv::set_fatal_policy(zxv::FatalPolicy::abort)` calls `std::abort()` instead, which no handler can stop. The C context is poisoned either way, so retrying cannot succeed. |
| Capabilities | Each gated object (`Ledger`, `Budgets`, `MlDsa87`, ...) is built from a `Capability` and passes it on every call, through the capability-gated `zxv_*_cap` C forms (ABI 1.2). Creating `Budgets`, `PhaseClock`, `Attestations` or `Swarm` needs its bit. `Context::enable_currency(cap, ...)` and `register_private_unit(cap, ...)` need `ledger_post`; a capability of another context throws `CapabilityDenied` (`ZXV_E_CAP`). `Message(cap, kind)` needs `ledger_read`, keeps the capability alive, and the library re-checks it on every call. You narrow a capability with `attenuate(zxv::caps::ledger_read)`. There is no global capability. The 1.0 forms without a capability (`enable_currency(alpha)`, `register_private_unit(code, mu)`, `Message(ctx, kind)`) still work but are `[[deprecated]]`. |
| Exactness | `zxv::Rational` holds an int64 numerator over a positive int64 denominator, always reduced. The operators throw `UsageError(ZXV_E_RANGE)` when a result overflows, and `checked_add` and the other checked operations return `std::nullopt` instead. With gcc and clang the operations are `constexpr`. A `float` or `double` does not convert to `Rational` (that constructor is deleted). The only way in is `Rational::from_double_exact(x)`, which returns the exact value: 0.1 becomes 3602879701896397/36028797018963968. |
| Four-valued logic | `enum class zxv::Truth { neither, true_, false_, both }` uses the C numbering. `&`, `|` and `~` follow the C tables. `Truth` has no conversion to `bool`. To get a `bool`, use `is_certainly_true`, `to_bool(t, on_both, on_neither)` or `require_true`. |
| Buffers | Inputs are `std::span<const std::uint8_t>` (`zxv::as_bytes("text")` for strings). Outputs are `std::vector` or `std::array`. |
| Secrets | `zxv::SecretKey` owns its bytes and wipes them with `zxv_secure_zero` when it is destroyed or move-assigned over. `bytes()` returns a view. `export_bytes_unsafe()` returns a copy that is not wiped. |
| Budgets | `budget.consume(r)` throws `BudgetExhausted`. `consume_async(r, zxv::stop_token, timeout)` returns a `std::future<void>`. A worker waits on a condition variable that wakes when the C listener reports a replenishment (a `release` or a larger limit). |
| Phase ticks | `PhaseClock::advance(n)` is the manual driver, for tests and lockstep. `start_realtime(ticks_per_second)` starts a `zxv::jthread` that advances the clock in real time. `defer(tick).await_cleared()` and `until_tick(t)` return a `std::shared_future<void>`. |
| Generation | `session.generate(prompt, opts, on_token)` takes an optional `std::function` callback. `session.stream(prompt, opts)` returns a `TokenStream` that you can loop over with range-for. Sampling options are `Rational` values, converted to Q16.16. |

## The hexagram tag (ABI 1.3)

A `Hexagram` tags one unit of work with a four-valued base, the polarity
of its evidence (entropic, neutral, forbidden, negentropic) and its phase
(potential -> emerging -> manifest -> dissolving), packed into one byte
0..63. Its action is proceed, wait, refuse, escalate or rollback. Every rule
comes from libzxv and is tested against `vectors.json` key `hex`.
Forbidden is absorbing, a phase moves one step at a time, and the base is
**not** numbered like `Truth` (TRUE is base 3 but Truth 1): convert only
with the explicit `to_truth()` / `Hexagram::from_truth(...)`. Combining BOTH with NEITHER follows
the `Truth` tables (AND gives NEITHER, OR gives BOTH); those two results
are pending an owner decision. A `QuarantineLog` (needs the ATTEST
capability) records forbidden work in a SHA-256 hash chain; when it is
full, append is refused with `ZXV_E_CAPACITY` and you must halt the
context. See [ABI.md, Changes in 1.3](../docs/ABI.md#changes-in-13).

```cpp
zxv::Hexagram h{zxv::HexBase::true_, zxv::HexPolarity::negentropic, zxv::HexPhase::potential};
h.action();                                // HexAction::proceed
h = h.advance();                           // emerging
h.step(zxv::HexPhase::dissolving);         // throws RefusedError (ZXV_E_STATE): a jump
auto bad = h.forbid();                     // rollback, forever
zxv::QuarantineLog log(cap, 1024);         // needs caps::attest
auto seq = log.append(bad, 7, 42, digest32);   // std::span<const uint8_t, 32>
log.verify();
```

`Hexagram` is a trivially copyable value with `&`, `|`, `~` and `==`, and
no conversion to `bool` or `Truth`.

## API overview

| Type | Main members | Needs |
|---|---|---|
| free functions | `version()`, `features()`, `has_feature()`, `status_name()`, `status_class()`, `iso4217()`, `card_check_digit()`, `random_bytes()`, `to_hex()`, `from_hex()` | - |
| `Context` | `root_capability(bits, pid, identity)`, `enable_currency(cap, ...)`, `register_private_unit(cap, ...)`, `enabled_currencies`, `halt`, `is_poisoned` | `ledger_post` for the currency calls |
| `Capability` | `attenuate`, `revoke`, `set_attestation`, `info`, `check`, `require` | - |
| `Rational` | `of`, `parse`, `from_q16`, `from_double_exact`, `+ - * /`, `<=>`, `checked_*`, `to_string`, `to_q16` | - |
| `Truth` | `& | ~`, `truth_from_evidence`, `to_bool`, `is_certainly_true`, `require_true`, `to_string` | - |
| `Ledger` | `open_account`, `post`, `balance`, `journal`, `check`, `account_name`, `account_currency`, `account_kind`, `account_count` | `ledger_post` / `ledger_read` |
| `Message` | `Message(cap, kind)`, `set`, `set_amount`, `set_tx_count`, `add_entry`, `render` (pacs.008, camt.053) | `ledger_read` |
| `Attestations` | `attest`, `evidence`, `find`, `state`, `gate` | `attest` |
| `Budgets` / `Budget` | `create`; `consume`, `consume_wait`, `consume_async`, `release`, `set_limit`, `status`, `on_exhausted` | `budget` |
| `PhaseClock` / `DeferredEvent` | `now`, `advance`, `admit`, `set_health`, `defer`, `until_tick`, `next_due`, `start_realtime`, `stop_realtime`; `await_cleared`, `cancel`, `state` | `phase` |
| `Ubh168Frame` | `make_tag`, `pack` (constexpr), `s_plus`, `s_minus`, `resolve`, `idle`, `pack_native` | - |
| `Swarm` | `register_model`, `set_mood`, `set_feeling`, `begin_cycle`, `consume`, `allotment`, `end_cycle`, `level_capacity`, `emotion_profile` | `swarm` |
| `Hashes` | `sha3_256`, `keccak256`, `shake256` | - |
| `MlKem1024` | `keygen`, `keygen_derand`, `encapsulate`, `encapsulate_derand`, `decapsulate` | `crypto_kem` |
| `MlDsa87` | `keygen`, `keygen_seed`, `sign`, `sign_deterministic`, `verify` | `crypto_sign` |
| `Ed25519` | `keygen`, `public_key`, `sign`, `verify` | `crypto_sign` |
| `Model` / `Session` / `TokenStream` | `open`, `open_memory`, `info`, `tokenize`, `detokenize`, `new_session`, `load_lora`; `generate`, `stream`, `reset`, `position`, `bind_budget` | `ai_inference` |
| `StateChannel` | `encode_state`, `ledger_state` | `evm` (+ `ledger_read`) |

## Examples

Waiting for a budget to be replenished:

```cpp
zxv::Budgets budgets(root);
auto tokens = budgets.create("tokens", zxv::Resource::tokens, 10);
tokens.consume(10);
std::future<void> f = tokens.consume_async(2, {}, std::chrono::seconds(5));
tokens.release(3);       // the waiter wakes and consumes
f.get();                 // or throws BudgetExhausted on timeout
```

Phase deferral:

```cpp
zxv::PhaseClock clock(root);
auto ev = clock.defer(zxv::phase_cycle);       // due at tick 27,720
clock.advance(zxv::phase_cycle);               // manual driver
ev.await_cleared().get();                      // completes
clock.start_realtime(27720);                   // or: one fundamental per second
```

Streaming tokens:

```cpp
auto model = zxv::Model::open(root, "model.gguf");
auto session = model.new_session();
zxv::GenerateOptions opts;
opts.max_tokens = 64;
opts.temperature = zxv::Rational::of(7, 10);   // exact 0.7
opts.seed = 42;
for (const zxv::Token& t : session.stream("Hello", opts)) std::cout << t.piece << std::flush;
```

Signing an agent message with ML-DSA-87:

```cpp
zxv::MlDsa87 dsa(root.attenuate(zxv::caps::crypto_sign));
auto kp = dsa.keygen();                         // kp.secret_key is wiped on scope exit
auto sig = dsa.sign(kp.secret_key, zxv::as_bytes(msg), zxv::as_bytes("agent"));
bool ok = dsa.verify(kp.public_key, zxv::as_bytes(msg), sig, zxv::as_bytes("agent"));
```

## Game engines: Unreal and deterministic lockstep

In a lockstep game, every peer runs the same simulation from the same inputs
and sends only inputs over the network. A companion AI (the Chiglet) can run inside
that simulation only if its inference produces the same result on every machine. The ZXV engine
does all its arithmetic in integers. Given the same model file, seed and prompt, it produces the same tokens on every
machine, operating system and CPU. The tests check this against the shared answer key.

The rules for lockstep use:

- **Derive everything from the frame.** Compute each frame's seed from the frame number, and its
  prompt from that frame's inputs. Never use wall-clock time or `std::random_device`.
- **Each frame is a pure function.** Call `session.reset()`, then `generate(...)` with a
  bounded `max_tokens`. Alternatively, keep one session per peer and feed it the same turns in the same
  order.
- **Use logical time.** Drive a `PhaseClock` with `advance(ticks_per_frame)`, not
  `start_realtime`. At 60 frames per fundamental, a frame is 462 ticks.
- **Use exact math for game state.** Use `Rational` or integer minor units. Floats are only allowed at
  `from_double_exact`, which shows the drift instead of hiding it.

`examples/lockstep.cpp` runs two independent peers for five frames. It checks that the
peers' tokens match on every frame and prints a state checksum:

```
frame 0 tick 462: 6 tokens, in sync
...
state checksum peer A 399246b9be7f74df, peer B 399246b9be7f74df
```

To set this SDK up in an Unreal module's `Build.cs`:

```csharp
CppStandard = CppStandardVersion.Cpp20;
bEnableExceptions = true;   // the SDK reports errors with exceptions
PublicIncludePaths.Add(Path.Combine(ZxvRoot, "include"));          // zxv.hpp + zxv_api.h
PublicAdditionalLibraries.Add(Path.Combine(ZxvRoot, "lib", "libzxv.so")); // or .dylib / .lib
RuntimeDependencies.Add("$(BinaryOutputDir)/libzxv.so", Path.Combine(ZxvRoot, "lib", "libzxv.so"));
```

Generate on the game thread only if `max_tokens` is small. For larger generations, use a worker thread or
`session.stream`. Results stay deterministic either way, because determinism depends on the seed and the inputs, not on which thread runs the generation.

## Tests

```sh
CXX=g++     cmake -S sdk/cpp -B build-gcc   && cmake --build build-gcc   && ctest --test-dir build-gcc
CXX=clang++ cmake -S sdk/cpp -B build-clang && cmake --build build-clang && ctest --test-dir build-clang
CXX=g++ cmake -S sdk/cpp -B build-asan -DZXV_SANITIZE=ON && cmake --build build-asan && ctest --test-dir build-asan
```

Every target compiles with `-Wall -Wextra -Werror -Wshadow -Wconversion -Wsign-conversion`.
`tests/test_zxv.cpp` runs against the real library and reads
`sdk/conformance/vectors.json` directly, using `tests/mini_json.hpp`. It needs no network. It checks:

- status names and classes, and the exception type for every status
- rational operations, parsing, IEEE-754 boundaries and Q16, plus 20,000 random cases where the constexpr host arithmetic is compared with C
- the lpres tables
- the `hex` key: all 64 hexagram tags, actions, `and`/`or`/`not`, `step`/`with_state` refusals, the Truth mapping, and the quarantine chain and its `ZXV_E_CAPACITY`, `ZXV_E_CAP` and fail-closed refusals
- UBH-168 frames byte for byte (host and C) and idle frames
- SHA3, SHAKE and Keccak
- Ed25519, and the ML-DSA-87 and ML-KEM-1024 digests
- swarm allotments
- greedy and seeded generation tokens, streaming, callback stop and budget stop
- the EVM state encoding and digest
- capability denial and revocation
- budget exhaustion, `consume_async` wake-up, timeout and cancellation
- phase deferral with both drivers
- the fatal path through `zxv_ctx_halt`, including `FatalPolicy::abort` in a forked child
- two-peer lockstep

## Honest limits

- **Capabilities are not a sandbox.** Inside one process they are a discipline of least privilege. Code in
  the same address space can create its own root capability. Capabilities become enforcement only at a process boundary.
- **Secrets.** `SecretKey` wipes its own buffer. It cannot wipe copies you made
  before handing bytes to `from_bytes`, pages the OS swapped out, or core dumps. It does
  not `mlock` its buffer. The cryptography is not FIPS 140-3 validated.
- **FatalError can still be caught.** C++ has no uncatchable exception: `catch (...)` stops
  `FatalError`. Only `FatalPolicy::abort` is truly terminal. The C context stays
  poisoned in every case.
- **Async waits use threads.** `consume_async` runs one detached worker thread per pending wait.
  Destroying the `Budgets` owner cancels all of them. A `TokenStream` holds one worker thread, and so does a
  running `PhaseClock` real-time driver.
- **Not built in this libzxv.** Netting, card issuing and VSS are pending in the kernel, and this SDK does not wrap them. Only the PAN check digit is
  wrapped. LoRA (`Model::load_lora`) throws `NotImplemented` while `ZXV_FEAT_LORA` is off.
- **MSVC is untested.** Without `__int128`, `Rational` arithmetic calls the C
  library and is not `constexpr`. The repository builds `libzxv` for Linux and macOS only, so there
  is no `zxv.dll` yet.
- **Unreal needs exceptions.** Unreal turns exceptions off by default, so you must set `bEnableExceptions = true` in
  the module that includes `zxv.hpp`.
