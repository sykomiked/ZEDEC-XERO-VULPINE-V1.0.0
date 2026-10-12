<!-- Copyright (c) 2024-2026 Michael Laurence Curzi and 36N9 Genetics, LLC -->
<!-- SPDX-License-Identifier: Apache-2.0 -->

# zxv: the Ruby SDK for ZXV

A small, idiomatic Ruby wrapper of `libzxv`, the one C library every ZXV SDK
shares (`sdk/libzxv/include/zxv_api.h`, ABI 1.4). It follows the cross-SDK
rules in `sdk/docs/SDK_DESIGN.md`, so the nouns are the same as in every
other ZXV SDK.

## Install

Requirements: Ruby >= 3.1, the `ffi` gem, and `libzxv` (`libzxv.so` /
`.dylib` / `zxv.dll`).

```sh
gem install ffi                # the only gem dependency
cd sdk/ruby
gem build zxv.gemspec && gem install zxv-1.1.0.gem
```

The library is found, in order: the path passed to `Zxv.load_library(path)`,
`ZXV_LIBRARY_PATH`, a copy placed in `lib/zxv/native/` before building the
gem, the in-repository build `sdk/libzxv/build/`, then the system loader
path. A library whose ABI is not 1.x (x >= 4) is refused (the SDK calls the
`zxv_hw_*` functions added in ABI 1.4, the `zxv_hex_*` functions added in
1.3 and the `_cap` functions added in 1.2).

Run the tests (Minitest, against the real library and
`sdk/conformance/vectors.json`):

```sh
cd sdk/ruby
ruby -e 'require "rake"; Rake.application.run(["test"])'   # or: rake test
ruby -Ilib examples/tour.rb
```

## Quickstart

```ruby
require "zxv"

Zxv::Context.open do |ctx|
  cap = ctx.root_capability                       # every gated call carries a capability
  ctx.enable_currency("NGN", cap: cap)            # needs :ledger_post
  ledger = Zxv::Ledger.new(cap)
  bank = ledger.open_account("bank", "NGN", kind: :issuer)
  ada  = ledger.open_account("ada", "NGN")
  ledger.post(bank, ada, 1_500_075, "NGN", "E2E-0001")   # exact kobo, no floats
  puts ledger.balance(ada).equity                  # => 1500075
end
```

## Concepts in Ruby terms

**Exactness.** No public method takes or returns a `Float`. Amounts are
`Integer` minor units; fractions are `Zxv::Rational` (immutable, reduced,
int64 numerator/denominator, arithmetic in C so overflow raises instead of
wrapping). Build one with `Zxv::Rational.of(3, 4)`, `Zxv::Rational.parse("12.75")`
or `Zxv::Rational.from(x)` for exact host values (`Integer`, Ruby's
`::Rational`, `BigDecimal`). Ruby is dynamic, so every entry point checks its
arguments at run time: a `Float` raises `Zxv::FloatRejectedError`. The one
door for floats is named so nobody walks through it by accident:
`Zxv::Rational.from_double_exact(0.1)` returns the double's exact value,
`3602879701896397/36028797018963968`. Inside `module Zxv`, `Rational` means
this class; Ruby's own is `::Rational` (convert with `#to_r`).

**Four-valued logic.** `Zxv::Truth::NEITHER`, `TRUE`, `FALSE`, `BOTH`
(values 0..3, as the kernel numbers them). `BOTH` is a held contradiction.
Ruby cannot make an object falsy: **every `Truth` is truthy, including
`Truth::FALSE`**, so `if truth` is always a bug. Use an explicit policy:
`truth.certainly_true?`, `truth.to_boolean(on_both:, on_neither:)` or
`truth.require_true!`. As a guard, `!truth` and `not truth` raise
`Zxv::TruthConversionError`. Logic is `a.and(b)`, `a.or(b)`, `a.not`
(also `&`, `|`, `~`), computed by libzxv's tables. `#mode` is reserved for
the kernel's coming 2-bit mode field (always 0 today).

**Capabilities.** `ctx.root_capability(:all)` creates a root;
`cap.attenuate(:ledger_read)` makes a narrower child; `cap.revoke` kills it
and its descendants; `cap.attestation = Zxv::Truth::BOTH` blocks it. Every
object that makes gated calls (`Ledger`, `Budgets`, `PhaseClock`, `Swarm`,
`Attestations`, `Ed25519`, `MlDsa87`, `MlKem1024`, `Model`, `StateChannel`)
takes a capability when you create it and passes it on every call. There is
no global "current capability".

Since ABI 1.2 every gated operation carries a capability:
`ctx.enable_currency("NGN", cap: cap)` and
`ctx.register_private_currency("VFV", 2, cap: cap)` need `:ledger_post`, as
does `Ledger#open_account`; `Ledger#account`, `#account_count` and
`#journal` need `:ledger_read`; `Zxv::Message.new(cap, :pacs008)` needs
`:ledger_read` and re-checks it on every call; `Budgets`, `PhaseClock`,
`Attestations` and `Swarm` need `:budget`, `:phase`, `:attest` and `:swarm`
to be created. A capability of another context raises
`CapabilityDeniedError`. The forms without a capability
(`ctx.enable_currency("NGN")`, `Zxv::Message.new(ctx, kind)`) still work and
emit a `:deprecated` warning (shown under `ruby -w`).

**Errors.** Every negative status maps through `zxv_status_class`:

| Class | Ruby type | Meaning |
|---|---|---|
| USAGE | `Zxv::ZxvUsageError < ArgumentError` (`FloatRejectedError` below it) | caller bug |
| REFUSED | `Zxv::ZxvRefusedError < StandardError`; `CapabilityDeniedError`, `BudgetExhaustedError`, `InsufficientFundsError`, `VerificationFailedError`, `Zxv::NotImplementedError` | handle it |
| FATAL | `Zxv::ZxvFatal < Exception` (not `StandardError`) | context halted; never retry |
| SYSTEM | `Zxv::ZxvSystemError < StandardError` | environment |

Each carries `status`, `status_name` and `detail` (from `zxv_last_error`).
`rescue Zxv::StatusError` catches the first, second and fourth rows and
never the fatal one. (`Zxv::NotImplementedError` is unrelated to Ruby's
`::NotImplementedError`.)

**Fail closed.** After `ZXV_E_POISONED` / `ZXV_E_INVARIANT` (or
`ctx.halt`), the C context refuses every gated call, and the SDK marks its
handle dead and refuses every call on it and its children itself. `ZxvFatal`
derives from `Exception`, so a bare `rescue` or `rescue => e` does not
swallow it. For a truly uncatchable stop set `Zxv.fatal_policy = :abort`:
the process ends at once with exit status 70 (`Process.exit!`, no `ensure`
blocks, no `at_exit`).

**Budgets and async.** `Budget#consume` raises `BudgetExhaustedError`;
`Budget#consume_async(amount, timeout:)` returns a `Zxv::Future` that waits
while the budget is exhausted and retries each time a `release` or a larger
`limit=` replenishes it (the epoch moves); it fails at once on an invalid
amount, and supports `cancel`. `on_exhausted { |budget| ... }` sees every
denied consume. A `Future` runs on a Ruby thread: `value(timeout)`,
`wait`, `done?`, `cancel`.

**Phase ticks.** `PhaseClock#defer(tick)` returns a `DeferredEvent`;
`event.await_cleared` is a `Future` completing when the clock passes the
tick. Drive the clock manually with `advance(n)` (tests, simulations) or with
`start_realtime(ticks_per_second)` (a Ruby thread). One fundamental is
`PhaseClock::CYCLE` = 27,720 ticks.

**Secrets.** Secret keys and shared secrets are `Zxv::SecretKey`: memory
allocated outside the Ruby heap, wiped with `zxv_secure_zero` on `close` and
by a finalizer as a backstop. The bytes become a Ruby `String` only through
`export_bytes_unsafe`.

**Engine.** `Zxv::Model.open(cap, path, tokenizer)`, `model.new_session`,
`session.generate(prompt, Zxv::GenerateOptions.new(...)) { |piece, id| ... }`
streams tokens to the block (return `:stop` to end early), and
`generate_async` returns a `Future`. Sampling parameters are exact
(`temperature: Zxv::Rational.of(7, 10)`), converted to Q16.16. Generation
releases the GVL. `Model#load_lora` raises `Zxv::NotImplementedError`:
LoRA is not built into libzxv yet (`ZXV_FEAT_LORA` is off).

**Wire frames.** `Zxv::Ubh168Frame` packs and unpacks the 21-octet UBH-168
frame in pure Ruby (`to_bytes`, `from_bytes`), so a service can frame without
the native library; `native_bytes` cross-checks against C and `resolve` runs
the C correlation resolver.

## The hexagram tag (ABI 1.3)

A `Hexagram` tags one unit of work with a four-valued base, the polarity
of its evidence (entropic, neutral, forbidden, negentropic) and its phase
(potential -> emerging -> manifest -> dissolving), packed into one byte
0..63. Its action is proceed, wait, refuse, escalate or rollback. Every rule
comes from libzxv and is tested against `vectors.json` key `hex`.
Forbidden is absorbing, a phase moves one step at a time, and the base is
**not** numbered like `Truth` (TRUE is base 3 but Truth 1): convert only
with the explicit `to_truth` / `Hexagram.from_truth(...)`. Combining BOTH with NEITHER follows
the `Truth` tables (AND gives NEITHER, OR gives BOTH); those two results
are pending an owner decision. A `QuarantineLog` (needs the ATTEST
capability) records forbidden work in a SHA-256 hash chain; when it is
full, append is refused with `ZXV_E_CAPACITY` and you must halt the
context. See [ABI.md, Changes in 1.3](../docs/ABI.md#changes-in-13).

```ruby
h = Zxv::Hexagram.of(:true, :negentropic, :potential)
h.action                                 # => :proceed
h = h.advance                            # :emerging
h.step(:dissolving)                      # ZxvRefusedError (ZXV_E_STATE): a jump
bad = h.forbid                           # :rollback, forever
log = Zxv::QuarantineLog.new(cap, capacity: 1024)     # needs :attest
seq = log.append(bad, reason: 7, submitter: 42, subject: digest32)
log.verify
```

Fields are Symbols; tags are frozen and interned. `!h` raises
TruthConversionError, but Ruby still treats every object as truthy in an
`if`, so never write `if h`.

## Hardware emulation and hardware updates (ABI 1.4)

Three parts, documented in [HW_EMULATION.md](../docs/HW_EMULATION.md):

* `Zxv::HwProfile`: ZHP-1 machine profiles (plain data, no capability).
  `probe_host` describes this machine, `reference` is the public reference
  machine R1 (16 nodes), `present(host, target)` marks each node HARD or
  SOFT, `match` / `pair` say what runs natively and what is emulated;
  `encode`, `decode`, `digest`, `cap`, `set_cap`, `ext`, `set_ext`.
* `Zxv::HwMatrix` (needs `:hw_matrix`): functions served by interchangeable
  implementations that serve only after reproducing the golden vectors.
  Every matrix starts with the standard engines (`Hw::FN_SHA256`,
  `FN_KECCAK`, `FN_MAC`, `FN_GCD`, `FN_LOGIC64`, `FN_TENSOR`), so `call`
  works on any device. `providers` lists every implementation (kind, proof,
  contract, flags, power class, calls); `set_policy` states placement
  (`:key_bearing`, `:local_only`, latency, power), never an implementation.
  `add_provider { |op, input| ... }` registers a Ruby implementation.
  Contract packages (`trust_key`, `apply_contract`) need `:hw_reconf`.
* `Zxv::HwRegion` (needs `:hw_reconf`): hardware updates as images signed
  with ML-DSA-87 AND SLH-DSA-SHAKE-256s (`Hw.keygen`, `Hw.build_image`,
  both need `:crypto_sign`; signing takes seconds), staged, tested,
  committed or rolled back on a device object you supply (`load`,
  `readback`, `activate`). Every step is an `on_event` callback and a record
  in a hash-chained audit log (`log`, `log_entry`).

`:all` stays the ABI 1.1 bits (`ZXV_CAP_ALL` never changes); ask for
`root_capability(:all_1_4)` or add `:hw_matrix` / `:hw_reconf`.

```ruby
cap = ctx.root_capability(:all_1_4)
mx = Zxv::HwMatrix.new(cap)
mx.call(Zxv::Hw::FN_SHA256, "abc")                     # 32 bytes, any machine
mx.providers.map { |p| [p.fn_id, p.kind, p.proof] }    # discovery

key = Zxv::Hw.keygen(cap)                              # secrets off the Ruby heap
img = Zxv::Hw.build_image(cap, kind: :partial, target_class: 0xD0, fn_id: 0x200,
                          version: 1, svn: 1, payload: bitstream, key: key)
rg = Zxv::HwRegion.new(cap, region_id: 1, target_class: 0xD0, device: my_device,
                       matrix: mx, provider: fabric_index)
rg.trust_key(key.public_key)
rg.on_event { |event, reason, version| audit(event, reason, version) }
rg.stage(img).trial(5).commit
key.close                                              # wipes both secret keys
```

Ruby callbacks (implementations, devices, events) run on the calling thread
inside libzxv. An exception never crosses into C: an error with a status
returns that status, any other exception becomes `ZXV_E_IO`. The callbacks
stay referenced for the life of the context. Errors: `ZXV_E_NOPROVIDER`,
`E_DISPUTE`, `E_QUARANTINED`, `E_ROLLBACK`, `E_BLOCKED`, `E_POLICY` are
refusals (`ZxvRefusedError`), `E_TARGET` is a usage error; a full audit log
poisons the context (`ZxvFatal`), since a change that cannot be recorded is
not made.

## API overview

| Ruby | Wraps | Main methods |
|---|---|---|
| `Zxv` | version, features | `abi_version`, `features`, `feature?`, `load_library`, `random_bytes`, `fatal_policy=` |
| `Zxv::Context` | `zxv_ctx` | `open { }`, `root_capability`, `enable_currency(alpha, cap:)`, `register_private_currency(code, mu, cap:)`, `currencies`, `halt`, `poisoned?`, `close` |
| `Zxv::Capability` | `zxv_cap` | `attenuate`, `revoke`, `check`, `allows?`, `attestation=`, `info` |
| `Zxv::Rational` | `zxv_rat_*` | `of`, `parse`, `from`, `from_double_exact`, `+ - * /`, `<=>`, `to_s`, `to_r`, `to_q16` |
| `Zxv::Truth` | `zxv_lp_*` | `NEITHER TRUE FALSE BOTH`, `and`, `or`, `not`, `from_evidence`, `certainly_true?`, `to_boolean`, `require_true!` |
| `Zxv::Ledger` | `zxv_ledger_*` | `open_account`, `post`, `balance`, `check`, `journal`, `account` |
| `Zxv::Iso4217`, `Zxv::Currency` | ISO 4217 table | `lookup`, `by_numeric`, `name`, `codes`, `published` |
| `Zxv::Message` | `zxv_msg_*` | `new(cap, kind)`, `[]=`, `amount`, `add_entry`, `render` (pacs.008, camt.053) |
| `Zxv::Attestations` | `zxv_lpres_*` | `attest`, `evidence`, `find`, `state`, `gate_clear?` |
| `Zxv::Budgets` / `Zxv::Budget` | `zxv_rmag_*` | `budget`, `consume`, `consume_async`, `release`, `limit=`, `status`, `on_exhausted` |
| `Zxv::PhaseClock` / `DeferredEvent` | `zxv_phase_*` | `now`, `advance`, `admit`, `set_health`, `defer`, `next_due`, `start_realtime`; `await_cleared`, `cancel` |
| `Zxv::Ubh168Frame` | `zxv_wire_*` | `tag`, `new`, `to_bytes`, `from_bytes`, `native_bytes`, `resolve`, `idle` |
| `Zxv::Swarm` | `zxv_swarm_*` | `register`, `set_mood`, `set_feeling`, `begin_cycle`, `consume`, `allotment`, `end_cycle`, `level_capacity` |
| `Zxv::Hashes` | SHA3, SHAKE, Keccak | `sha3_256`, `shake256`, `keccak256` |
| `Zxv::Ed25519`, `Zxv::MlDsa87`, `Zxv::MlKem1024` | signatures, KEM | `generate`, `sign`, `verify`, `verify!`; `encapsulate`, `decapsulate` |
| `Zxv::SecretKey` | off-heap secret | `close`, `bytesize`, `export_bytes_unsafe`, `from_bytes_unsafe` |
| `Zxv::Model` / `Zxv::Session` | engine | `open`, `tokenize`, `detokenize`, `info`, `new_session`; `generate`, `generate_async`, `reset`, `bind_budget` |
| `Zxv::HwProfile` | `zxv_hw_profile_*` | `probe_host`, `reference`, `present`, `decode`, `encode`, `digest`, `add_node`, `set_cap`, `cap`, `set_ext`, `ext`, `nodes`, `match`, `pair` |
| `Zxv::HwMatrix` | `zxv_hw_*` matrix | `call`, `shadow`, `providers`, `provider_info`, `function_info`, `bound`, `set_policy`, `add_provider`, `set_traits`, `attest`, `memo`, `trust_key`, `apply_contract`, `log`, `log_entry` |
| `Zxv::HwRegion`, `Zxv::Hw` | `zxv_hw_region_*`, packages | `stage`, `trial`, `commit`, `rollback`, `tick`, `info`, `on_event`, `log`; `Hw.keygen`, `Hw.build_image`, `Hw.vector_list` |
| `Zxv::StateChannel` | `zxv_evm_*` | `encode_state`, `ledger_state` (for `sdk/solidity/ZxvStateChannel.sol`) |
| `Zxv::Future` | host threads | `value`, `wait`, `done?`, `cancel` |

## Examples

```ruby
# Exact rationals and the float boundary
Zxv::Rational.parse("0.1") + Zxv::Rational.parse("0.2")   # => 0.3 exactly
Zxv::Rational.of(1, 3).to_s                               # => "1/3"
Zxv::Rational.of(0.5)                                     # raises Zxv::FloatRejectedError

# Wait for a budget
gpu = Zxv::Budgets.new(cap).budget("gpu", resource: :compute_cycles, limit: 10)
gpu.consume(10)
f = gpu.consume_async(3, timeout: 5)
gpu.release(3)
f.value

# Await a deferred event
clock = Zxv::PhaseClock.new(cap)
done = clock.defer(Zxv::PhaseClock::CYCLE).await_cleared
clock.advance(Zxv::PhaseClock::CYCLE)
done.value

# Post-quantum signatures, keys off the Ruby heap
dsa = Zxv::MlDsa87.new(cap)
keys = dsa.generate
sig = dsa.sign(keys.secret_key, "agent message", context: "zxv")
dsa.verify(keys.public_key, "agent message", sig, context: "zxv")   # => true
keys.close                                                          # wipes the secret key
```

`examples/tour.rb` runs all of the above and more (engine, EVM state, the
fatal path).

## Honest limits

- **Capabilities are least privilege, not a sandbox.** Any Ruby code in the
  process can call `ctx.root_capability` or reach `Zxv::Native` directly.
  The process boundary (zxvd over IPC) is where it becomes enforcement.
- **Truthiness.** Ruby makes every `Truth` truthy and that cannot be changed;
  `!truth` raises, but `if truth` / `unless truth` / `truth && x` do not
  call any method and silently treat `FALSE`, `BOTH` and `NEITHER` as true.
- **`ZxvFatal` is catchable** by `rescue Exception` or `rescue Zxv::ZxvFatal`.
  The context stays dead either way (C and the SDK both refuse it); only
  `fatal_policy = :abort` stops the process.
- **Secrets.** `SecretKey` memory is wiped on `close` and by a finalizer,
  but finalizers run only when the GC decides to. Bytes you pass in from Ruby
  Strings (seeds, `from_bytes_unsafe`, messages) live in the GC heap, may be
  copied by it, and are not wiped. Seeds passed as Strings (`seed:`, `d:`,
  `z:`, `m:`) are copied into temporary native buffers that are freed but
  not wiped. Swap and core dumps are outside the SDK's control.
- **Handle lifetime.** Child handles (budgets, clocks, sessions, models...)
  are freed by `close` or, at the latest, when their `Context` closes or is
  garbage collected; dropping a `Model` without `close` keeps its memory
  until then. Use the block forms.
- **Async is thread-based.** `Future` uses one Ruby thread per waiter and
  wakes on replenish/advance events with a 250 ms poll as a backstop; there
  is no Fiber scheduler integration.
- **Not certified.** The cryptography is not FIPS 140-3 validated; libzxv
  serializes ISO 20022 but has no network transport. LoRA and the
  pacs.002/004/009, camt.056/029 builders are not built in this libzxv (they
  raise `Zxv::NotImplementedError`); netting cycles, card issuance and VSS
  checks are stubs in libzxv and are not wrapped by this SDK yet.
