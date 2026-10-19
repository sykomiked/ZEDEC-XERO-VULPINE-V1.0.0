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

## ABI 1.5: treaties, content ids, coins, fees, the V-Hub, freight, Web4 agents, paradoxes

ABI 1.5 adds eight feature groups. `Zxv.features2` lists the live ones
(`:treaty`, `:aipi`, `:coin`, `:fee`, `:vhub`, `:freight`, `:web4`,
`:paradox`); `Zxv.feature?(:web4)` checks one. Three capability bits are
new: `:aipi`, `:mint` and `:paradox`. `:all` and `:all_1_4` do not grow, so
ask for `root_capability(:all_1_5)` or add the bits by name. A capability
without them gets `CapabilityDeniedError` (`ZXV_E_CAP`).

New statuses: `ZXV_E_DENOM` and `ZXV_E_TREATY` are usage errors
(`ZxvUsageError`; a treaty error is a `TreatyError` with `fault` and
`offset`). `ZXV_E_CID`, `E_ERASURE`, `E_PEG`, `E_LIMIT` and
`E_PARADOX_STEP` are refusals (`ZxvRefusedError`), `E_CONSENT` is a
`ConsentRequiredError` and `E_PARADOX_OWNER` an `OwnerApprovalRequiredError`
(both refusals). Every 64-bit amount is an `Integer`; floats are refused.
Every handle has `close` and is also freed by the GC (pure handles) or with
its `Context` (handles made from a capability); a closed handle raises
`ZxvUsageError` (`ZXV_E_STATE`).

The examples below assume:

```ruby
require "zxv"
ctx = Zxv::Context.new
cap = ctx.root_capability(:all_1_5)
```

### Treaties (`Zxv::Treaty`)

A treaty is a small multilateral settlement written in Sutra text (or its
fixed-width COBOL copybook record). `run` nets the obligations, posts the
legs with the assurance fee for `SETL` and reports each party's balance and
dividend; it runs in a private engine and needs no capability.
`Treaty::Pic` is PIC S9(18) arithmetic with COBOL's size-error rules.

```ruby
t = Zxv::Treaty.parse(<<~SUTRA)
  TREATY CORRIDOR-7 USD SETL
  PARTY ACME 5000000 4000 1/3
  PARTY BETA 3000000 3000 1/7
  PARTY GAMMA 900000 0 11/21
  OWE ACME BETA 1200000
  OWE BETA GAMMA 700000
  OWE GAMMA ACME 100000
  END
SUTRA
r = t.run
r.legs.map(&:to_h)   # => [{from: 0, to: 2, amount: 600000}, {from: 0, to: 1, amount: 500000}]
r.fees               # => 977
t.to_record          # the 2542-byte copybook record
begin
  Zxv::Treaty.parse("TREATY n USD NET\nPARTY A 1 1 1/1\nEND\n")
rescue Zxv::TreatyError => e
  [e.fault, e.offset] # => [:name, 7]
end
```

### Content ids and AIPI (`Zxv::Cid`, `Zxv::Aipi`)

`Cid` is an IPFS-compatible content id (CIDv1 sha2-256; CIDv0 parses).
`Cid.compute` hashes bytes as one block, `Cid.unixfs` gives the id
`ipfs add` would. AIPI names a function by the CID of its record (module,
entry, how a remote result is accepted) and a call by the CID of
(function, input). `Aipi::Node` (needs `:aipi`) is a local block store that
verifies every block it returns.

```ruby
cid = Zxv::Cid.compute("hello world")
cid.to_s              # => "bafkreifzjut3te2nhyekklss27nh3k72ysco7y32koao5eei66wof36n5e"
cid.verify("hello world")  # => true

node = Zxv::Aipi::Node.new(cap)
mod = node.put("zxv module bytes")
fn = Zxv::Aipi::FunctionRecord.encode(intent: :fn, verify: :checker, determinism: :deterministic,
                                      fuel_max: 1_000_000, module_cid: mod, entry: "main", checker: "check")
fn_cid = node.publish(fn)
Zxv::Aipi.call_cid(fn_cid, "hello").to_s(:base58btc)
node.plan(fn_cid, "hello")   # => :local (this library is offline)
```

### Coins and tokens (`Zxv::Coin`, `Zxv::Token`, `Zxv::TokenWallet`)

Coin faces are Fibonacci rungs; `Coin.split` gives the unique fewest-coin
split. Token holdings are 64 counts, one per tier; `Token.canonical` puts a
value in its one canonical form at an octane. Density earns nothing: a dense
token is worth exactly its face. `TokenWallet` (needs `:mint`) keeps a
holding canonical at its octane.

```ruby
Zxv::Coin.split(1_000_000)                     # => [29, 25, 23, 11, 9] (5 coins)
Zxv::Token.value(Zxv::Token.canonical(1_000_000, :racing))   # => [1000000, 17]
w = Zxv::TokenWallet.new(cap, octane: :racing)
w.mint(1_000_000)
w.pieces                                       # => 17
```

`Coin.split` refuses (`ZXV_E_RANGE`) a split above `max_coins:` (16,777,216
by default) instead of allocating gigabytes; count large splits with
`Coin.split_count`.

### The assurance fee (`Zxv::Fee`)

`fee(g) = floor(g * 8889 / 10^7)`, exact for every 64-bit gross, charged
once per transaction. Keep the carry per account so many small charges pay
what one charge of their sum pays.

```ruby
Zxv::Fee.assure(1_000_000)                       # => 888
fee, carry = Zxv::Fee.assure_with_carry(100, carry: 0)   # => [0, 888900]
Zxv::Fee.split(100)  # => {reserve_floor: 50, vbill_dividend: 25, infra_bounty: 15, regen_capital: 10}
```

### The V-Hub (`Zxv::VHub`)

A hub of test currencies swapped through V-Bills on a triple-entry ledger.
Each currency's issuer sets its peg with an ML-DSA-87 signature over
`VHub.peg_message`; swaps fail closed until each currency has a limit rule,
and must be exact under both pegs. Needs `:ledger_post` (and `:ledger_read`).

```ruby
dsa = Zxv::MlDsa87.new(cap)
issuer = dsa.generate
hub = Zxv::VHub.new(cap)
%w[ZTA ZTB].each_with_index do |code, slot|
  hub.add_currency(slot, code: code, minor_units: 2, issuer_public_key: issuer.public_key,
                         pool_funding: 500_000, vbill_backing: 10_000)
end
[[0, 350], [1, 120]].each do |slot, num|
  msg = Zxv::VHub.peg_message(slot, num: num, den: 1, epoch: 1)
  hub.set_peg(slot, num: num, den: 1, epoch: 1, signature: dsa.sign(issuer.secret_key, msg))
  hub.add_limit(slot, 2**64 - 1)
end
alice = hub.open_account(0, owner: 50)
bob = hub.open_account(1, owner: 51)
hub.issue(alice, 100_000)
hub.swap(from: 0, to: 1, sender: alice, receiver: bob, amount: 35_000, tick: 1).to_h
# => {vbills: 100, received: 12000}
hub.check!                                     # postings so far; raises if the ledger broke
issuer.close
```

### Freight (`Zxv::Freight`)

A freight is 168 rows of 21 bytes; any `k` distinct rows rebuild a payload
of up to `k * 20` bytes (Reed-Solomon erasure coding with a SHA-256 check).
`Freight.split` / `Freight::Join` carry larger payloads as a set.
`Freight::Ops` encodes an op stream (literal, copy, CID reference, seeded
generator) that the receiver expands up to a limit it chooses; a CID
reference is resolved by your block, and an exception in it never crosses
into C (the call fails cleanly).

```ruby
payload = "ZXV freight: any 10 of 168 rows rebuild this."
hdr = Zxv::Freight::Header.for_payload(payload, freight_id: 42, k: 10)
rows = Zxv::Freight.rows(Zxv::Freight.encode(hdr, payload))
Zxv::Freight.decode(hdr, rows.sample(10)) == payload   # => true

ops = Zxv::Freight::Ops.encode("abc" * 100, with_sha: true)   # 55 bytes
Zxv::Freight::Ops.expand(ops, limit: 4096).bytesize          # => 300
```

### Web4 agents (`Zxv::Web4`)

An agent is an ML-DSA-65 identity with a signed card; agents exchange
signed envelopes, publish signed tool manifests, and act on a human's
behalf only with a consent token the human signed, checked once against a
`ConsentLog`. Agent keys stay inside the `Agent` handle and are wiped when
it is closed or its context closes; they are never returned. Creating and
signing needs `:crypto_sign` (consent tokens: `:attest`).

```ruby
alice = Zxv::Web4::Agent.new(cap, name: "alice", now_ms: 1_700_000_000_000)
bob = Zxv::Web4::Agent.new(cap, name: "bob", now_ms: 1_700_000_000_000)
Zxv::Web4.verify_card(bob.card).name           # => "bob"
req = alice.seal(to: bob.agent_id, kind: :request, tool: "pay", req_id: 7,
                 payload: "amount=100", now_ms: 1_700_000_000_500)
Zxv::Web4::Envelope.new(req).verify(alice.public_key)   # => true
rsp = bob.reply(req, kind: :response, payload: "ok", final: true, now_ms: 1_700_000_000_900)
Zxv::Web4::Envelope.new(rsp).payload           # => "ok"
```

### Paradoxes (`Zxv::Paradox`)

Classifies a contradiction by its hexagram tag (and evidence trend) as
constructive, destructive, neutral or special, and handles each the one way
its kind allows, writing every step to hash-chained logs: a constructive
policy update is applied, kept, rolled back or proposed to the owner; a
destructive one is contained (state restored from a checkpoint, a repair
proof made); a neutral one holds every branch until evidence decides; a
special one is recorded for review. Needs `:paradox`. A policy flagged
`:money` or `:device` is never changed automatically.

```ruby
book = Zxv::Paradox::Book.new(cap)
policy = Zxv::Paradox::Policy.new(cap, lo: [0, 0], hi: [100, 50], initial: [50, 25])
book.constructive(policy, kind: :constructive, tag: 0x2F, synth: [60, 20], tension: 5)  # => :applied
policy.values                                  # => [60, 20]
r = book.destructive(kind: :destructive, tag: 0x23, state: "X" * 16, checkpoint: "A" * 16,
                     trace: [1, 2, 3, 2], pattern: "loop:A->B->A", submitter: 7)
[r.outcome, r.state, r.proof.valid?]           # => [:contained, "AAAAAAAAAAAAAAAA", true]
book.verify_log(:ops)
```

## Vino Embed (ABI 1.6)

`Zxv::Vino` is what an app or game needs to take part in Vino without ever
holding anyone's value or keys. It has two lanes that never meet:

- **PLAY** (`Vino::PlaySandbox`): a sandbox inside your app with its own
  play units, issuers, holders and vouchers, in process memory only. Play
  units are not money and are lost when the sandbox closes. No call takes a
  play sandbox and anything live, so a play unit can never be converted to,
  redeemed for, or moved into live value.
- **LIVE** (`Vino::PaymentRequest`, `Vino::Receipt`): real Vino stays in
  the user's own ZXV wallet. Your app builds a payment request, the user
  approves it in their wallet, which signs a receipt, and your app checks
  the receipt offline. Your app never sees a balance, a key or a custody
  account.

Concepts:

- **Vouchers.** A voucher is one coin with a Fibonacci face (1, 2, 3, 5,
  8, ...), held by one holder on exactly one active rail: `:bearer` (a note
  in the holder's hand) or `:ghost` (its twin on the play ledger). Spending
  names the active rail, so a voucher can never be spent twice
  (`WrongRailError`, `ZXV_E_RAIL`). `set_rail` switches rails and moves
  nothing.
- **Coverage.** An issuer may only issue, or withdraw backing, while
  `10 * backing >= 18 * outstanding` (1.8x, exact integers); otherwise
  `CoverageError` (`ZXV_E_COVERAGE`) and nothing changes.
- **Redeeming** returns a voucher to its issuer and retires it. Nothing is
  paid out to anyone. There is no call that turns a voucher into backing or
  play units, and no interest, yield or return of any kind.
- **Payment requests** are plain immutable values. `encode` gives the
  canonical 160 bytes, `id` their SHA-256 (32 bytes), `PaymentRequest.decode`
  reads them back. A LIVE request marked `FLAG_CHANCE` (a paid chance
  outcome such as a loot box) is refused (`ChanceRefusedError`,
  `ZXV_E_CHANCE`); the allotment is a separate module, not a payment request.
  A lane other than `:play`/`:live` is a `LaneError` (`ZXV_E_LANE`).
- **Receipts** are the wallet's ML-DSA-87 signature over 88 bytes
  (`Receipt.message`). `Receipt#verify!(now:)` is pure and offline: it
  raises `VerificationFailedError` for a wrong signature and `ExpiredError`
  (`ZXV_E_EXPIRED`) for an expired request or an approval outside its
  window; `valid?(now:)` answers true/false instead. `Receipt.sign` is the
  wallet's half (it needs `:crypto_sign` and a `SecretKey`, which it uses
  once and does not keep); an app never holds a wallet key.
- **Daily cap.** `Vino::SpendMeter` counts what one device approved per day
  and refuses an amount that would pass the cap (`DailyCapExceededError`,
  `ZXV_E_LIMIT`, with `#left`). It is a counter, not a legal check.
- **Capabilities.** The sandbox and the meter need the new `:vino` bit
  (`1 << 15`). `:all_1_5` does not include it: ask for
  `root_capability(:all_1_6)` (`Capability::ALL_1_6`) or add `:vino`.
  `Zxv.feature?(:vino)` (feature bit `0x100`) says whether the library has
  the group. Requests, receipt messages and verification need no capability.
- **Statuses.** `ZXV_E_RAIL`, `E_COVERAGE`, `E_EXPIRED` and `E_CHANCE` are
  refusals (`WrongRailError`, `CoverageError`, `ExpiredError`,
  `ChanceRefusedError`, all `ZxvRefusedError`); `ZXV_E_LANE` is a usage
  error (`LaneError < ZxvUsageError`). Amounts, ids and times are exact
  `Integer`s; floats are refused.

A play-lane shop: the shop backs its vouchers with play units, issues one
to a player, the player spends it at a friend, who redeems it.

```ruby
require "zxv"

Zxv::Context.open do |ctx|
  cap = ctx.root_capability(:all_1_6)
  play = Zxv::Vino::PlaySandbox.new(cap)
  shop = play.open_issuer(700)
  player = play.open_holder(801)
  friend = play.open_holder(802)

  play.back(shop, 100)                          # play units: not money
  id = play.issue(issuer: shop, holder: player, face: 34, rail: :bearer)
  play.transfer(id, from: player, to: friend, rail: :bearer)   # spend on its active rail
  play.redeem(id, holder: friend)               # retired; nothing is paid out
  play.voucher(id).state                        # => :redeemed
  play.account(shop).faces                      # => 0 outstanding
  play.check!
  play.close
end
```

A live payment request, the wallet's receipt, and the app's offline check.
The wallet half is shown only to make the example run; in a real
deployment it happens in the user's wallet, never in your app.

```ruby
require "zxv"

Zxv::Context.open do |ctx|
  now = 1_700_000_000
  request = Zxv::Vino::PaymentRequest.new(
    lane: :live, merchant: Zxv::Hashes.sha3_256("shop key"), nonce: Zxv.random_bytes(16),
    amount: 1_500, created_at: now, expires_at: now + 600, memo: "Sword of Dawn"
  )
  bytes = request.encode                        # 160 bytes, sent to the wallet

  # --- in the user's wallet (not the app) ---
  wallet_cap = ctx.root_capability(:crypto_sign)
  wallet = Zxv::MlDsa87.new(wallet_cap).generate
  receipt = Zxv::Vino::Receipt.sign(wallet_cap, request: bytes, wallet_secret_key: wallet.secret_key,
                                                wallet_public_key: wallet.public_key, approved_at: now + 30)
  wallet.close

  # --- back in the app: offline, no capability ---
  got = Zxv::Vino::Receipt.new(request: bytes, wallet_public_key: receipt.wallet_public_key,
                               approved_at: receipt.approved_at, signature: receipt.signature)
  got.verify!(now: now + 60)                    # raises unless genuine and in time
  got.request_id == request.id                  # => true
  got.valid?(now: now + 601)                    # => false (expired)

  meter = Zxv::Vino::SpendMeter.new(ctx.root_capability(:vino), daily_cap: 10_000)
  meter.admit(day: now / 86_400, amount: request.amount)   # => 8500 left today
end
```

## API overview

| Ruby | Wraps | Main methods |
|---|---|---|
| `Zxv` | version, features | `abi_version`, `features`, `features2`, `feature?`, `load_library`, `random_bytes`, `fatal_policy=` |
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
| `Zxv::Treaty`, `Treaty::Result`, `Treaty::Pic` | `zxv_treaty_*` | `parse`, `from_record`, `to_sutra`, `to_record`, `validate`, `parties`, `obligations`, `run`; `legs`, `parties`, `fees`; `add`, `sub`, `mul`, `divide` |
| `Zxv::Cid` | `zxv_cid_*` | `compute`, `unixfs`, `parse`, `to_s`, `to_v1`, `verify`, `provider_key`, `digest` |
| `Zxv::Aipi` | `zxv_aipi_*` | `FunctionRecord.encode`, `CallRecord`, `ClaimRecord`, `call_cid`; `Node#put`, `get`, `publish`, `load_function`, `plan` |
| `Zxv::Coin`, `Zxv::Token`, `Zxv::TokenWallet` | `zxv_coin_*`, `zxv_token_*` | `face`, `rung`, `split`, `split_count`; `canonical`, `value`, `compress`, `fuse`, `split`, `encode_codes`; `mint`, `spend`, `transfer`, `octane=` |
| `Zxv::Fee` | `zxv_fee_*` | `assure`, `assure_with_carry`, `split`, `default_policy` |
| `Zxv::VHub` | `zxv_vhub_*` | `peg_message`, `add_currency`, `set_peg`, `open_account`, `issue`, `add_limit`, `quote`, `swap`, `check!`, `balances` |
| `Zxv::Freight` | `zxv_freight_*` | `Header.for_payload`, `encode`, `decode`, `split`, `Join`, `Ops.encode`, `Ops.expand`, `Ops::Writer`, `Ops.generate` |
| `Zxv::Web4` | `zxv_w4_*` | `Agent#card`, `seal`, `reply`, `issue_consent`; `verify_card`, `action_digest`, `Directory`, `Manifest`, `ConsentLog#check!`, `Envelope` |
| `Zxv::Paradox` | `zxv_paradox_*` | `classify`, `action`; `Book#classify`, `registry_add`, `constructive`, `destructive`, `neutral`, `special`, `draft`, `review`, `pass`, `log_records`, `verify_log`; `Policy`, `Hold`, `Graph` |
| `Zxv::Vino` (ABI 1.6) | `zxv_vino_*` | `PlaySandbox#open_issuer`, `open_holder`, `back`, `unback`, `issue`, `transfer`, `set_rail`, `redeem`, `voucher`, `account`, `vouchers`, `check!`; `PaymentRequest#encode`, `id`, `.decode`, `.id_of`; `Receipt.message`, `.sign`, `#verify!`, `#valid?`; `SpendMeter#admit` |
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
- **ABI 1.5 groups are tools, not institutions.** Treaties, the V-Hub,
  coins, tokens and the fee are exact arithmetic and bookkeeping on test
  currencies; they are not a bank, a payment licence or legal, tax or
  securities advice, and nothing here is certified or compliant with any
  regime. The V-Hub's pegs are whatever an issuer signs. AIPI's node is
  offline: `plan` only reports `:local` or `:unavailable`. Freight erasure
  coding recovers lost rows, not tampered ones (a bad row fails the hash);
  op streams create no information. Web4 consent tokens prove a human's key
  signed, not that the human understood; showing the action is your app's
  job. The paradox engine classifies by the tag and evidence you give it and
  writes logs; it decides nothing for the owner, and money or device
  policies are only ever proposed.
- **Vino Embed is the app's half only.** Play units are not money, are
  worth nothing and are lost on close; no play-lane call reaches live value.
  Approving a payment happens in the user's wallet (zxvd), which posts it on
  its own ledger. The spend meter is a counter, not a legal check, and makes
  no exemption claim. Receipts use the reference ML-DSA-87 implementation,
  not a FIPS 140-3 validated module. Whether a given deployment of Vino is
  store credit, a prepaid access product or something else is a legal
  question pending counsel; nothing in this SDK decides it.
- **Web4 agent keys** live in libzxv memory inside the `Agent` handle and
  are wiped when it is closed (or its context closes); the SDK never returns
  or logs them. A `seed:` passed as a String is copied to a native buffer
  that is zeroed after use, but the Ruby String itself is not wiped; pass a
  `SecretKey` to keep the seed off the Ruby heap.
- **Not certified.** The cryptography is not FIPS 140-3 validated; libzxv
  serializes ISO 20022 but has no network transport. LoRA and the
  pacs.002/004/009, camt.056/029 builders are not built in this libzxv (they
  raise `Zxv::NotImplementedError`); netting cycles, card issuance and VSS
  checks are stubs in libzxv and are not wrapped by this SDK yet.
