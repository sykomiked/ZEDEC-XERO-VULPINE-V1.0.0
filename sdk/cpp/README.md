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
major version is not 1 or whose minor version is below 5 (ABI 1.5 added
the eight groups below, 1.4 hardware emulation and hardware updates, 1.3
the hexagram tag, 1.2 the
capability-gated `*_cap` forms the SDK calls).

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

## Hardware emulation and hardware updates (ABI 1.4)

Everything lives in `zxv::hw`. Profiles are plain data; `hw::Matrix` needs
`caps::hw_matrix`; `hw::Region` and contract packages need
`caps::hw_reconf`; `hw::Release` (keys and packages) needs
`caps::crypto_sign`. `caps::all` keeps its 1.1 value, so use
`caps::all_1_4` for every bit. New exceptions: `NoProvider`, `Disputed`,
`Quarantined`, `RollbackRefused`, `ImageBlocked`, `PolicyDenied` (all
`RefusedError`) and `TargetMismatch` (`UsageError`). Everything is
integers. See [HW_EMULATION.md](../docs/HW_EMULATION.md).

```cpp
namespace hw = zxv::hw;
auto r1 = hw::Profile::reference();                          // R1: 16 nodes
auto m = hw::Profile::match(hw::Profile::probe_host(), r1);  // m.native, m.emulated
zxv::Context ctx;
auto cap = ctx.root_capability(zxv::caps::all_1_4);
hw::Matrix mx(cap);                                          // standard engines, proven SOFT
auto digest = mx.call(hw::fn_sha256, 0, bytes);              // ba7816bf... for "abc"
auto p = mx.add_provider(0x100, hw::res_host, hw::Kind::soft, 10,
                         [](std::uint32_t op, auto in, auto out) { /* ... */ return in.size(); });
mx.attest(p);                                                // serves only once proven

hw::Release rel(cap);
auto key = rel.keygen();                                     // secret halves are zxv::SecretKey
auto image = rel.build({hw::ImageKind::partial, 0xD0, {}, 0x100, 1, 1}, payload, key);
hw::Region rg(cap, &mx, fabric_provider, 1, 0xD0, std::nullopt,
              hw::Device{load, readback, activate}, 64);
rg.on_event([](const hw::Event& e) { /* staged, tested, activated, ... */ });
rg.trust_key(key.public_key);
rg.stage(image); rg.trial(5); rg.commit();
```

`hw::Profile`: `probe_host`, `reference`, `present`, `encode`/`decode`,
`clone`, `digest`, `add_node`, `set_cap`/`cap`/`find_cap`, `set_ext`/`ext`,
`nodes`, `match`, `pair`. `hw::Matrix`: `add_function`, `add_vector`,
`set_contract`, `set_policy` (`policy::key_bearing` is sticky and keeps work
local), `function_info`, `add_resource`, `set_resource`, `add_provider`,
`set_traits`, `add_dependency`, `enable`, `attest`, `providers` /
`provider_info` / `find_provider`, `bound`, `call`, `call_into`, `shadow`,
`memo`, `trust_key`, `apply_contract`, `log` / `log_entry`. `hw::Region`:
`trust_key`, `stage`, `trial`, `commit`, `rollback`, `tick`, `info`,
`on_event`, `log` / `log_entry` (records carry a `Hexagram` tag). A region
keeps its matrix alive.

Callbacks are `std::function`s behind `noexcept` trampolines: no exception
crosses C. A `zxv::Exception` thrown by a provider or device operation is
reported as its status, anything else as `ZXV_E_INTERNAL`; event handler
exceptions are swallowed. A provider may call the matrix again for the
functions it declared with `add_dependency`.

## ABI 1.5 groups

ABI 1.5 adds eight groups, each in its own namespace, matching the C groups.
`zxv::features2()` / `has_feature2(feature2::...)` report which are built.
`caps::all` keeps its 1.1 value and `caps::all_1_4` its 1.4 value; use
`caps::all_1_5` for every bit, which adds `caps::aipi`, `caps::mint` and
`caps::paradox`. New statuses map to new exception types:
`DenominationError` and `TreatyError` (`UsageError`; a `TreatyError` carries
`fault_code()` and `offset()`), and `CidMismatch`, `ConsentDenied`,
`ErasureError`, `PegError`, `LimitRefused`, `ParadoxStepLimit` and
`OwnerApprovalRequired` (all `RefusedError`).

The same rules apply in every group. Variable-length results come back as
`std::vector` or `std::string`: the two-call buffer pattern is hidden.
Fixed byte structures are value types (`cid::Cid`, `freight::Header`,
`web4::ConsentToken`, `paradox::RepairProof`, `paradox::Draft`,
`vhub::PegMessage`). Handles are move-only and keep their capability and
context alive. Using a moved-from handle throws `UsageError`
(`ZXV_E_ARG`) and never reaches C. Gated calls fail closed with
`FatalError` (`ZXV_E_POISONED`) once the context halts. 64-bit values stay
64-bit, and there is no floating point anywhere.

Each block below runs as written inside `int main()` after
`#include <zxv.hpp>`.

### Treaties (`zxv::treaty`, pure)

A treaty has two canonical spellings of one batch of settlement work: a
COBOL copybook record (`record()`, 2542 bytes) and Sutra text (`sutra()`).
`run()` gives a deterministic report, not a payment.

```cpp
namespace tr = zxv::treaty;
auto t = tr::Treaty::from_sutra("TREATY CORRIDOR-7 USD NET\nPARTY ACME 5000 4000 1/2\n"
                                "PARTY BETA 3000 3000 1/2\nOWE ACME BETA 1200\nEND\n");
auto same = tr::Treaty::from_record(t.record());      // same == t
auto r = t.run();                                     // r.legs(), r.parties(), r.record()
try { tr::Treaty::from_sutra("TREATY"); }
catch (const zxv::TreatyError& e) { /* tr::fault_of(e) == tr::Fault::name, e.offset() == 0 */ }
auto d = tr::pic_div(-7, 2);                          // {-3, -1}: PIC S9(18) arithmetic
(void) same; (void) r; (void) d;
```

### Content identifiers (`zxv::cid`, pure)

These CIDs are byte-identical to Kubo's. The default text form is Kubo's
("b..." base32, "Qm..." for v0), and `Base::base58btc` gives the "z..."
form.

```cpp
auto c = zxv::cid::Cid::compute(zxv::as_bytes("abc"));   // CIDv1 raw sha2-256
std::string text = c.to_text();                          // "bafkrei..."
auto back = zxv::cid::Cid::parse(text);                  // == c
c.verify(zxv::as_bytes("abc"));                          // CidMismatch on other bytes
auto big = zxv::cid::Cid::unixfs(std::vector<std::uint8_t>(300000)); // dag-pb above 256 KiB
(void) back; (void) big;
```

### AIPI (`zxv::aipi`)

These are function, call and claim records, which are pure. `aipi::Node`
is an offline block store (`caps::aipi`) that re-hashes every block it
returns. It has no network: `plan()` answers `Route::local` or
`Route::unavailable`.

```cpp
namespace ai = zxv::aipi;
zxv::Context ctx;
auto cap = ctx.root_capability(zxv::caps::all_1_5);
ai::Node node(cap, 1 << 20, 64);
ai::FunctionSpec f;
f.module = node.put(zxv::as_bytes("zxv module bytes"));
f.entry = "main"; f.checker = "check"; f.verify = ai::Verify::checker; f.fuel_max = 1000000;
auto fn = node.publish(ai::encode_function(f));            // function CID
auto call = ai::call_cid(fn, zxv::as_bytes("hello"));
bool runs_here = node.plan(fn) == ai::Route::local;        // true: the module is held
(void) call; (void) runs_here;
```

### Fibonacci coins and swarm tokens (`zxv::coin`, `zxv::token`)

`coin` holds the VFV coin ladder (faces F(r+1), rungs 1..45); any other face
is `DenominationError`. `token` holds the 64 token tiers, their octane and
Fibonacci codes, all pure. `token::Wallet` needs `caps::mint`, keeps its
holding canonical at its octane, and has no balance outside itself. Density
carries no premium: a denser holding is worth exactly the same.

```cpp
auto rungs = zxv::coin::split(100);                 // {10, 5, 3}: 89 + 8 + 3
auto counts = zxv::token::canonical(1000, zxv::token::octane::midgrade);
std::vector<std::uint8_t> buf(zxv::token::code_max_bytes);
std::uint32_t bit = 0;
zxv::token::code_put(buf, bit, 100);                // 11 bits
zxv::Context ctx;
zxv::token::Wallet w(ctx.root_capability(zxv::caps::mint), 7, zxv::token::octane::regular);
w.mint(1000); w.spend(1);                           // w.info().value == 999
(void) rungs; (void) counts;
```

### The assurance fee (`zxv::fee`, pure)

The fee is 8889 / 10^7 of the gross, rounded down. A `fee::Carry` keeps the
remainder so that many small charges add up exactly. `split()` gives the four
buckets (50/25/15/10%, with every remainder unit going to the reserve floor).

```cpp
auto f = zxv::fee::assure(1000000);                 // 888
zxv::fee::Carry acct;                               // store acct.remainder() per account
for (int i = 0; i < 8; ++i) (void) acct.charge(100);
auto parts = zxv::fee::split(f);                    // {445, 222, 133, 88}
auto policy = zxv::fee::default_policy();
(void) parts; (void) policy;
```

### The V-Bill hub (`zxv::vhub`)

This is a demonstration hub with test currencies only: ISO 4217 codes are
refused. Pegs are set only with the issuer's ML-DSA-87 signature over
`peg_message()`. Swaps fail closed until each currency has a limit rule.
It needs `caps::ledger_post`, plus `caps::ledger_read` for reads.

```cpp
namespace vh = zxv::vhub;
zxv::Context ctx;
auto cap = ctx.root_capability(zxv::caps::all_1_5);
zxv::MlDsa87 dsa(cap);
auto key = dsa.keygen();
vh::Hub hub(cap);
hub.add_currency(0, "ZTA", 2, key.public_key, 500000, 10000);
hub.set_peg(0, 350, 1, 1, dsa.sign(key.secret_key, vh::peg_message(0, 350, 1, 1)));
auto acct = hub.open_account(0, 50);
hub.issue(acct, 1000000);
std::uint64_t postings = hub.check();               // A = L + E, DEBIT == CREDIT, hash chain
(void) postings;
```

### Freight (`zxv::freight`, pure)

A freight is 168 rows of 21 bytes, and any k rows rebuild the payload
(Reed-Solomon). A corrupted or missing row set is `ErasureError` or
`VerificationFailed`. Op streams describe a payload by literals, copies,
generators and CID references. There are no compression promises: an op
stream can be larger than its output. The `expand()` resolver is a
`std::function`, and an exception it throws is rethrown by `expand()`,
never unwound through C.

```cpp
namespace fr = zxv::freight;
auto payload = zxv::as_bytes("any k rows rebuild the payload");
auto h = fr::Header::for_payload(42, 10, payload);
auto packets = fr::encode(h, payload);              // 3528 bytes
std::vector<std::uint8_t> last10(packets.end() - 210, packets.end());
auto back = fr::decode(h, last10);                  // == payload
auto ops = fr::OpsWriter(5, 4096).literal(zxv::as_bytes("<"))
               .cidref(zxv::cid::Cid::compute(zxv::as_bytes("abc")), 3)
               .literal(zxv::as_bytes(">")).finish();
auto out = fr::expand(ops, 1024, [](std::span<const std::uint8_t>, std::span<std::uint8_t> o) {
    std::copy_n("abc", 3, o.begin()); return true;  // your content store
});                                                 // "<abc>"
(void) back; (void) out;
```

### Web 4 agents (`zxv::web4`)

Agents hold ML-DSA-65 keys inside their handle; destroying the handle wipes
the key, and this SDK never logs it. The group covers signed cards,
manifests, consent tokens and envelopes. There is no network transport:
you move the bytes. Consent for money or personal data must be presented and
is single-use.

```cpp
namespace w4 = zxv::web4;
zxv::Context ctx;
auto cap = ctx.root_capability(zxv::caps::all_1_5);
w4::Agent alice(cap, "alice", 1700000000000), human(cap, "human", 1700000000000);
w4::Manifest m(1, 1700000000000, 1700086400000);
m.add_tool({"pay", 150, 60, 10, w4::tool::money, 256});
m.sign(alice);
auto action = w4::action_digest("pay", alice.agent_id(), alice.agent_id(), 7, zxv::as_bytes("amount=100"));
auto token = human.issue_consent(alice.agent_id(), action, w4::scope::money, 1000,
                                 1700000000000, 1700000600000);
w4::ConsentLog log(16);
log.check(&token, human.public_key(), alice.agent_id(), action, w4::scope::money, 500, 1700000000001);
// A second check of the same token throws ConsentDenied (single use).
```

### The paradox engine (`zxv::paradox`)

The engine classifies a Both/Neither tag into one of four kinds and runs the
fixed, logged operator for that kind. Every operator needs
`caps::paradox`. Automatic changes happen only inside published bounds.
Policies flagged `flag::money` or `flag::device` always go to the owner,
and `owner_ok` is your assertion. Destructive work is contained, never
answered.

```cpp
namespace px = zxv::paradox;
zxv::Context ctx;
auto cap = ctx.root_capability(zxv::caps::all_1_5);
px::Book book(cap, 64, 16);
auto c = book.classify(zxv::Hexagram::from_tag(39), px::Evidence{3, 1, 5, 1});  // constructive
std::vector<std::int64_t> lo{0, 0, 10}, hi{100, 50, 20}, init{50, 25, 15};
px::Policy policy(cap, lo, hi, init);
auto o = book.constructive(policy, c.kind, c.tag, std::vector<std::int64_t>{60, 20, 12}, 5);
std::vector<std::uint8_t> state(16, 'X'), checkpoint(16, 'A');
auto k = book.destructive(px::Kind::destructive, zxv::Hexagram::from_tag(35), state, checkpoint,
                          {10, 8, 3, 100, {1, 2, 3, 2}, true, {'l', 'o', 'o', 'p'}, 7});
bool ok = k.proof && k.proof->verify();             // state is "AAAA..." again
(void) o; (void) ok;
```

## Vino Embed (ABI 1.6)

ABI 1.6 adds one group, `zxv::vino`: what an app or game needs to take part
in Vino without ever holding anyone's value or keys. `has_feature2(feature2::vino)`
reports it. `caps::all_1_6` is every bit, adding `caps::vino`; the older
`caps::all`, `all_1_4` and `all_1_5` keep their values.

There are two lanes, and they never meet:

- **Play** (`vino::Play`): a sandbox inside the app, with its own play
  units, issuers, holders and vouchers, in process memory only. Play units
  come from a faucet (`back`), are not money, are lost when the sandbox is
  destroyed, and can never be converted into, redeemed for or moved into
  live value: no call takes a play sandbox and anything live.
- **Live**: real Vino stays in the user's own ZXV wallet, never in the app.
  The app builds a payment request (`vino::Request`); the user approves it
  in their wallet, which signs a receipt (`vino::sign_receipt`, the wallet's
  call); the app checks the receipt offline (`vino::verify_receipt`). The
  app never sees a balance, a key or a custody account.

A **voucher** is one play coin with a Fibonacci ladder face (1, 2, 3, 5,
8, ...; `coin::face`). It is held by one holder on exactly one rail at a
time: `Rail::bearer` (a note in the hand) or `Rail::ghost` (its twin on the
play ledger). A transfer must name the active rail, so a voucher can never
be spent twice. **Coverage**: an issuer may issue only while
`10 * backing >= 18 * outstanding` (1.8x, exact integers). **Redeeming**
hands a voucher back to its issuer and retires it; nothing is paid out to
anyone, and there is no interest, yield or return of any kind.

New statuses map to new exception types: `RailMismatch`, `CoverageRefused`,
`RequestExpired` and `PaidChanceRefused` (all `RefusedError`), and
`LaneError` (`UsageError`).

| Type | Main members | Needs |
|---|---|---|
| `vino::Play` | `open`, `open_issuer`, `open_holder`, `back`, `unback`, `issue`, `transfer`, `set_rail`, `redeem`, `voucher`, `account`, `vouchers`, `check` | `vino` |
| `vino::Request` | fields `lane`, `flags`, `merchant`, `nonce`, `amount`, `created_at`, `expires_at`, `memo`; `encode`, `bytes`, `id`, `decode`, `decode_with_id` | - |
| receipts | `receipt_message`, `verify_receipt`, `receipt_is_valid` (pure); `sign_receipt` (wallet side) | `crypto_sign` to sign |
| `vino::SpendMeter` | `admit`, `try_admit` | `vino` |

A play-lane shop: the issuer backs, issues a voucher to a player, the
player spends it at a friend and the friend redeems it.

```cpp
namespace vino = zxv::vino;
zxv::Context ctx;
auto cap = ctx.root_capability(zxv::caps::vino);
vino::Play play(cap);
auto shop = play.open_issuer(1);           // account ids count up from 0
auto player = play.open_holder(100);
auto friend_ = play.open_holder(101);
play.back(shop, 18);                       // play units from the faucet; worth nothing
auto v = play.issue(shop, player, 8);      // 10*18 >= 18*8, so allowed; returns id 1
try { play.issue(shop, player, 13); }      // 10*18 < 18*(8+13): refused, nothing changed
catch (const zxv::CoverageRefused&) {}
play.transfer(v, player, friend_, vino::Rail::bearer);   // spend it on its active rail
try { play.transfer(v, friend_, player, vino::Rail::ghost); }
catch (const zxv::RailMismatch&) {}        // not its rail: one coin, one rail
play.redeem(v, friend_);                   // retired; nothing is paid out
bool retired = play.voucher(v).state == vino::VoucherState::redeemed;   // true
bool empty = play.vouchers(friend_).empty();                            // true
play.check();                              // re-checks every invariant
(void) retired; (void) empty;
```

A live payment request, a wallet receipt and an offline check. The wallet
part normally runs in the user's wallet (zxvd), not in the app; it is here
only so the example runs on its own.

```cpp
namespace vino = zxv::vino;
// App side: a plain value, encoded to 160 canonical bytes. Its id is SHA-256 of them.
vino::Request req;
req.lane = vino::Lane::live;
req.merchant.fill(0x01);                   // the payee's id
zxv::random_bytes(req.nonce);              // unique per request
req.amount = 1234567;                      // VFV minor units: exact integers
req.created_at = 1700000000;
req.expires_at = 1700000600;               // ten minutes to approve
req.memo = "Sword of Dawn";                // 0..64 printable ASCII
auto encoded = req.encode();               // encoded.bytes (160), encoded.id (32)

// Wallet side (for this example only): approve with the wallet's ML-DSA-87 key.
zxv::Context wallet_ctx;
auto wallet_cap = wallet_ctx.root_capability(zxv::caps::crypto_sign);
auto wallet = zxv::MlDsa87(wallet_cap).keygen();
auto sig = vino::sign_receipt(wallet_cap, encoded.bytes, wallet.secret_key, wallet.public_key, 1700000100);

// App side again, offline: no capability, no network.
bool paid = vino::receipt_is_valid(encoded.bytes, wallet.public_key, 1700000100, sig, 1700000200);   // true
bool late = vino::receipt_is_valid(encoded.bytes, wallet.public_key, 1700000100, sig, 1700000601);   // false: expired
auto same = vino::Request::decode(encoded.bytes);   // == req
(void) paid; (void) late; (void) same;
```

A live request marking a paid chance outcome (`flag::chance`: a loot box or
a paid draw) is refused when it is encoded (`PaidChanceRefused`); the
raffle is a separate module, not a payment request. A deployment that sets
a per-device daily cap counts approvals with `vino::SpendMeter`:

```cpp
zxv::Context ctx;
auto cap = ctx.root_capability(zxv::caps::vino);
zxv::vino::SpendMeter meter(cap, 100);       // at most 100 per day index
auto left = meter.admit(19700, 60);          // 40 left today
auto d = meter.try_admit(19700, 41);         // {admitted = false, left = 40}: nothing counted
(void) left; (void) d;
```

The meter is a counter, not a legal check, and makes no exemption claim.
Play and SpendMeter are internally serialized (any thread may call them),
move-only, and keep their capability and context alive.

## API overview

| Type | Main members | Needs |
|---|---|---|
| free functions | `version()`, `features()`, `has_feature()`, `features2()`, `has_feature2()`, `status_name()`, `status_class()`, `iso4217()`, `card_check_digit()`, `random_bytes()`, `to_hex()`, `from_hex()` | - |
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
| `Hexagram` / `QuarantineLog` | `action`, `advance`, `step`, `forbid`; `append`, `record`, `verify` | `attest` for the log |
| `hw::Profile` | `probe_host`, `reference`, `present`, `encode`, `decode`, `digest`, `match`, `pair` | - |
| `hw::Matrix` | `call`, `shadow`, `add_provider`, `attest`, `providers`, `set_policy`, `memo`, `apply_contract`, `log` | `hw_matrix` (+ `hw_reconf` for contracts) |
| `hw::Release` | `keygen`, `build` | `crypto_sign` |
| `hw::Region` | `stage`, `trial`, `commit`, `rollback`, `tick`, `info`, `on_event`, `log` | `hw_reconf` |
| `treaty::Treaty` / `Result` | `from_sutra`, `from_record`, `sutra`, `record`, `parties`, `obligations`, `validate`, `run`; `parties`, `legs`, `fees`, `record`; `pic_add/sub/mul/div` | - |
| `cid::Cid` | `compute`, `unixfs`, `parse`, `from_bytes`, `to_text`, `info`, `to_v1`, `matches`, `verify` | - |
| `aipi` / `aipi::Node` | `encode_function`, `decode_function`, `function_cid`, `encode_call`, `call_cid`, `encode_claim`, `decode_claim`, `provider_key`; `put`, `get`, `publish`, `load`, `plan`, `info` | `aipi` for the node |
| `coin` / `token` / `token::Wallet` | `face`, `rung`, `split`; `face`, `tier`, `value`, `canonical`, `compress`, `fuse`, `split`, `code_put`, `code_get`; `mint`, `spend`, `transfer_to`, `set_octane`, `info`, `counts` | `mint` for the wallet |
| `fee` | `assure`, `assure_carry`, `Carry`, `split`, `default_policy` | - |
| `vhub::Hub` | `add_currency`, `set_peg`, `open_account`, `issue`, `balance`, `add_limit`, `next_epoch`, `quote`, `swap`, `check`; `peg_message` | `ledger_post` / `ledger_read` |
| `freight` | `Header`, `encode`, `decode`, `split`, `Join`, `set_count`, `OpsWriter`, `encode_ops`, `expand`, `peek`, `generate` | - |
| `web4` | `Agent` (`card`, `issue_consent`, `seal`, `reply`), `Manifest`, `Directory`, `ConsentLog`, `verify_card`, `action_digest`, `decode_envelope`, `verify_envelope` | `crypto_sign` for keys and signing, `attest` for consent |
| `paradox::Book` | `classify`, `registry_*`, `log_*`, `constructive`, `destructive`, `neutral`, `special`, `draft`, `review`, `pass`; `Policy`, `Hold`, `Graph`, `RepairProof`, `Draft` | `paradox` |
| `vino::Play` / `SpendMeter` | `open_issuer`, `open_holder`, `back`, `unback`, `issue`, `transfer`, `set_rail`, `redeem`, `voucher`, `account`, `vouchers`, `check`; `admit`, `try_admit` | `vino` |
| `vino` requests and receipts | `Request` (`encode`, `decode`), `receipt_message`, `verify_receipt`, `receipt_is_valid`; `sign_receipt` | `crypto_sign` to sign only |

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
- ABI 1.4, mirroring `test_zxv_abi14.c`: R1 and host profiles, round trip, extensions and pairing; the standard engines (SHA-256 of "abc", HMAC RFC 4231, gcd), placement (MAC never goes REMOTE), proof, dependencies, failover, shadow disputes, memo, contracts; and a full stage / trial / commit / quarantine cycle with events, the audit log and fail-closed when it fills (three SLH-DSA signings, about 5 s)
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

`tests/test_zxv_abi15.cpp` (the `test_zxv_abi15` target) checks the ABI 1.5 groups against the
`status_abi15`, `treaty`, `cid`, `aipi`, `coin`, `fee`, `vhub`, `freight`, `web4` and `paradox` sections of
`vectors.json` exactly, refusal status names included: every status and its exception type; treaty
records, Sutra text, faults with offsets and runs; Kubo CIDs; AIPI records; the coin ladder, token tiers,
codes and a wallet; fee carries and splits; the vhub scenario (signed pegs, quotes, swaps, limits, every
balance); freight headers, packets, row-subset decodes, sets and op streams (with a resolver that throws);
web4 cards, manifests, consent checks and envelopes (signed bodies; ML-DSA-65 signatures are hedged); and
the paradox engine end to end, with every log record and head. It also covers moved-from handles, the
buffer-growth path, capability refusals (a capability without `aipi`, `mint` or `paradox` gets
`CapabilityDenied`) and fail-closed handles after `Context::halt`.

`tests/test_zxv_abi16.cpp` (the `test_zxv_abi16` target) replays the `status_abi16` and `vino` sections
exactly: every request's bytes, id and refusal status, each one-byte decode refusal, the receipt message,
the deterministic signature and every verify check, the ordered play scenario with its final accounts,
voucher lists and vouchers, and the meter scenario through both `admit` and `try_admit`. It also covers
moved-from handles, handles that outlive their `Context` object, the voucher list's two-call path (600
vouchers), the exception type for each new status, capability refusals (no `vino`, revoked, unattested)
and fail-closed play and meter handles after `Context::halt`.

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
- **ABI 1.5 limits.** There is no network transport: AIPI nodes, freight and web4 envelopes produce and
  check bytes, and you move them. The vhub is a demonstration hub with test currencies; it refuses ISO 4217
  codes and is not a payment system. Money and device policies, registry entries and drafts need the
  owner, and `owner_ok` is the caller's assertion. Freight op streams make no compression promise. Token
  density carries no premium, and a treaty run is a report, not a payment. The paradox engine contains,
  holds and routes contradictions. It detects only through the checks you run, and it never acts against a sender.
- **Vino Embed limits.** Play units are not money, are lost when the sandbox is destroyed, and can never
  become live value. The live flow here is the app's half only: approval happens in the user's wallet, which
  posts the payment on its own ledger. Receipts use the reference ML-DSA-87, not FIPS 140-3 validated.
  `SpendMeter` is a counter, not a legal check. Whether a given deployment of Vino is store credit, a
  prepaid access product or something else is a legal question pending counsel; this SDK does not decide it.
- **Unreal needs exceptions.** Unreal turns exceptions off by default, so you must set `bEnableExceptions = true` in
  the module that includes `zxv.hpp`.
