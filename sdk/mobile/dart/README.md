<!-- Copyright (c) 2024-2026 Michael Laurence Curzi and 36N9 Genetics, LLC -->
<!-- SPDX-License-Identifier: Apache-2.0 -->

# ZXV for Dart and Flutter

Package `zxv`, version 1.6.0: [libzxv](../../libzxv/) from Dart with
`dart:ffi`. The 1.3 bindings are hand-written; the ABI 1.5 ones
(`lib/src/bindings15.dart`) were generated once from `zxv_api.h` and are
kept as source; the ABI 1.6 vino ones (`lib/src/bindings16.dart`) are
hand-written. All are checked against `zxv_api.h` by a test. ABI 1.6 or a
later 1.x is required and checked at load. Dart 3.8 or later.

## Quickstart

```dart
import 'package:zxv/zxv.dart';

void main() {
  final ctx = ZxvContext.create();
  try {
    final root = ctx.rootCapability(Permission.all);
    ctx.enableCurrency(root, 'NGN');
    final ledger = Ledger(root);
    final bank = ledger.openAccount('bank', 'NGN', AccountKind.issuer);
    final alice = ledger.openAccount('alice', 'NGN');
    ledger.post(bank, alice, 12550, 'NGN', 'top-up-1');
    print(ledger.balance(alice));                          // equity 12550
    print(Rational.parse('0.015') * Rational.of(12550));   // 188.25, exactly
  } finally {
    ctx.close(); // closes root and everything else made from ctx
  }
}
```

[`example/main.dart`](example/main.dart) is the same program, runnable with
`ZXV_LIBRARY_PATH=$PWD/../../libzxv/build dart run example/main.dart`.

## Install

Add the package by path or git (it is not on pub.dev):

```yaml
dependencies:
  zxv:
    path: ../ZEDEC-XERO-VULPINE-V1.0.0/sdk/mobile/dart
```

The package does not bundle libzxv; your app ships it. libzxv is found, on
first use, in this order: `Zxv.load(path)`; `ZXV_LIBRARY_PATH` (file or
directory); the copy bundled with the app (below); the system loader.

### Shipping libzxv

| Platform | What to ship | How the SDK finds it |
|---|---|---|
| Android `arm64-v8a`, `x86_64` | `libzxv.so` per ABI, built by [`../kotlin/scripts/build-libzxv-android.sh`](../kotlin/scripts/build-libzxv-android.sh); copy `build/libzxv-android/<abi>/libzxv.so` to `android/app/src/main/jniLibs/<abi>/` | `DynamicLibrary.open('libzxv.so')` |
| iOS (device + simulator) | `Zxv.xcframework` (static), built on a Mac by [`scripts/build-libzxv-xcframework.sh`](scripts/build-libzxv-xcframework.sh); add it to the Runner target | `DynamicLibrary.process()` |
| macOS, Linux, Windows desktop | `libzxv.dylib` / `libzxv.so` / `zxv.dll` from `make -C sdk/libzxv` next to the executable, or `ZXV_LIBRARY_PATH` | `DynamicLibrary.open` |

iOS: because the library is linked statically and found by symbol lookup,
keep its symbols in the app binary. In the Runner target set **Other Linker
Flags** to `-force_load $(PROJECT_DIR)/Zxv.xcframework/ios-arm64/libzxv.a`
(the simulator slice is `ios-arm64_x86_64-simulator`), and **Strip Style**
to **Non-Global Symbols** for Release, as Flutter's docs for static
libraries describe. Android: the APK must keep `libzxv.so` for every ABI you
ship; `flutter build apk --split-per-abi` works with the jniLibs layout.

## Concepts in Dart

| Concept ([SDK_DESIGN](../../docs/SDK_DESIGN.md)) | Dart |
|---|---|
| Exact numbers | `Rational` (`Rational.of`, `Rational.parse`, operators `+ - * /` and comparisons). The only `double` door is `Rational.fromDoubleExact(x)`; NaN and infinity throw `FloatRejectedException`. Dart's static types keep a `double` out of every other call. |
| Four-valued logic | `Truth.NEITHER/TRUE/FALSE/BOTH`, `and`, `or`, `not`. No `bool` without a policy: `toBool(onBoth:, onNeither:)`, `isCertainlyTrue`, `requireTrue()`. |
| Hexagram (ABI 1.3) | `Hexagram.of(HexBase, HexPolarity, HexPhase)`, `Hexagram.fromTag`, `Hexagram.fromTruth`; `action`, `step`, `advance`, `withState`, `forbid`, `and`, `or`, `not`, `toTruth()`. |
| Capabilities | `ctx.rootCapability(...)`, `attenuate`, `revoke`, `setAttestation`, `info`, `allows`, `require`. Every gated call passes one (the `*_cap` functions). |
| Errors | `ZxvUsageException` (+ `FloatRejectedException`, `NotADenominationException` -32, `TreatyException` -33 with `fault` and `offset`), `ZxvRefusedException` (+ `CapabilityDeniedException`, `BudgetExhaustedException`, `InsufficientFundsException`, `VerificationFailedException`, `NotImplementedException`, and for ABI 1.5 `CidException` -34, `ConsentException` -35, `ErasureException` -36, `PegException` -37, `LimitException` -38, `StepLimitException` -39, `OwnerApprovalRequiredException` -40, and for ABI 1.6 `RailException` -46, `CoverageException` -47, `ExpiredException` -49, `ChanceException` -50), `ZxvSystemException`; `LaneException` -48 is a `ZxvUsageException`. Each has `status`, `statusName`, `detail`. `ZxvStatus` names every code (-41..-45 and -51..-59 are reserved by the ABI and not defined). |
| Fail-closed | `ZxvFatalError` is an `Error`, not an `Exception`: `on Exception catch` and `on ZxvException catch` do not catch it. The context is poisoned in C and marked dead in Dart. `FatalPolicy.abort` ends the process with `exit(70)`. |
| Quarantine log | `QuarantineLog.create(cap, capacity)` (needs `ATTEST`), `append`, `count`, `head`, `record`, `verify`. |
| Budgets | `Budgets.create(cap).budget(...)`; `consume`, `tryConsume`, `release`, `setLimit`, `status`, `onExhausted`, and `Future<void> consumeAsync(amount, timeout:, cancel:)`. |
| Crypto | `Hashes.sha3_256/keccak256/shake256`; `Ed25519(cap)` and `MlDsa87(cap)`: `generate` (ML-DSA-87 also from a 32-byte `SecretKey` seed), `sign` (ML-DSA-87 also `signDeterministic`, with an optional context), `verify`, `isValid`. |
| Secrets | `SecretKey`: native memory the Dart GC never moves, wiped with `zxv_secure_zero` on `close()` and by a `Finalizer`. `exportBytesUnsafe()` is the only heap copy. |
| Lifetimes (R6) | Every handle is wrapped in a `NativeHandle` that closes its children before itself; a parent holds its children's handles, so a child is never destroyed after its parent. A Dart `Finalizer` on each wrapper closes the tree if it is collected unclosed. (`NativeFinalizer` is not used: it calls one C function per object and cannot order children before parents.) |

## ABI 1.5 groups

All eight groups are bound (`Zxv.features2()`, `Zxv.hasFeature2(Feature2.x)`),
with the 1.5 permission bits `Permission.AIPI`, `MINT` and `PARADOX` (bits 12
to 14; `HW_MATRIX` and `HW_RECONF` hold bits 10 and 11 so `bit == 1 << index`
stays true, though the ABI 1.4 hardware groups themselves are not bound).
uint64 values (amounts, faces, timestamps) travel in an `int` with their
bits kept (`-1` is `UINT64_MAX`); uint8, uint16 and uint32 arguments are
range-checked before the call, since Dart truncates an out-of-range `int`.
No call takes or returns a `double`. Small results are records (for example
`VHubQuote` is `({int amount, int vbills, int received})`).

| Group | Types | Capability |
|---|---|---|
| Treaty | `Treaty` (`parse`, `parseRecord`, `toSutra`, `toRecord`, `validate`, `fault`, `parties`, `obligations`, `run`), `TreatyResult`, `Pic18`, `TreatyFault`, `TreatyOp` | pure |
| CID / AIPI | `Cid` (`compute`, `raw`, `unixfs`, `parse`, `toText`, `toV1`, `verify`, `matches`, `providerKey`), `FunctionRecord` (`encode` with named arguments, `decode`), `CallRecord`, `ClaimRecord`, `AipiNode` (`put`, `putPrivate`, `get`, `publish`, `loadFunction`, `plan`, `info`) | node writes: `AIPI` |
| Coin | `Coins`, `Tokens`, `Octane`, `TokenHolding`, `FibonacciCode`, `TokenWallet` | wallet: `MINT` |
| Fee | `AssuranceFee` (`fee`, `charge`, `split`, `defaultPolicy`), `FeePolicy`, `FeeSplit`, `FeeBucket`, `FeeUnit` | pure |
| V-Bill hub | `VHub` (`pegMessage`, `addCurrency`, `setPeg`, `currency`, `openAccount`, `issue`, `balance`, `quote`, `swap`, `addLimit`, `nextEpoch`, `check`) | `LEDGER_POST` / `LEDGER_READ` |
| Freight | `FreightHeader`, `Freight` (`encode`, `decode`, `split`, `setCount`), `FreightMember`, `FreightJoin`, `OpStream` (`encode`, `expand`, `peek`, `bound`, `generate`), `OpStreamWriter`, `CidResolver`, `Generator` | pure |
| Web 4 | `AgentKey` (`card`, `seal`, `request`, `reply`, `sign`, `issueConsent`), `AgentCard`, `AgentDirectory`, `ToolManifest`, `Tool`, `ConsentToken`, `ConsentLog`, `Envelope`, `Web4.actionDigest` | keys: `CRYPTO_SIGN`; consent: `ATTEST` |
| Paradox | `ParadoxBook` (classify, registry, logs, `constructive`, `destructive`, `neutral`, `special`, `draft`, `review`, `pass`), `ParadoxPolicy`, `ParadoxHold`, `ParadoxGraph`, `Paradox` (pure functions), `RepairProof`, `Draft`, `ParadoxLogRecord`, `ParadoxQuarantineRecord` | writes: `PARADOX` |

Context-bound handles (`AipiNode`, `TokenWallet`, `VHub`, `AgentKey`, and the
paradox book, policy, hold and graph) are children of their context's
`NativeHandle`: they close with it and refuse every call once it halts.
Pure handles (`Treaty`, `FreightJoin`, `OpStreamWriter`, `AgentDirectory`,
`ToolManifest`, `ConsentLog`) have no context. Secret inputs never sit on
the Dart heap: the AIPI node key, Web 4 agent seeds and ML-DSA-87 seeds and
secret keys are `SecretKey`s.

`OpStream.expand(ops, limit, resolver: ...)` runs the `CidResolver` through a
`NativeCallable.isolateLocal` (with an `exceptionalReturn`), synchronously on
the calling thread inside the native call, so the resolver must be
synchronous and must not expand op streams itself. Each distinct CID and
length is asked once. A resolver's exception never crosses C: the callback
returns a failure status and the exception is rethrown unchanged after the
call; a wrong-length answer is a `ZxvUsageException`.

V-Bonds and derivatives are not part of this SDK.

## Vino Embed (ABI 1.6)

What an app or game needs to take part in Vino **without ever holding
anyone's value or keys** (`Feature2.vino`, `Permission.VINO` = bit 15;
`Permission.all1_5` keeps its value and `Permission.all1_6` adds the new
bit). Two lanes, which never meet:

- **Play lane** (`PlaySandbox`): a sandbox inside the app with its own play
  units, issuers, holders and Fibonacci-face vouchers, in process memory
  only. **Play units are not money**: they come from a faucet, are worth
  nothing, are lost on `close()`, and no call takes a sandbox and anything
  live, so they can never become live value.
- **Live lane**: real Vino stays in the user's own ZXV wallet. The app builds
  a `PaymentRequest`; the user approves it in their wallet, which signs a
  receipt (`VinoReceipt.sign`, the wallet's call); the app checks the
  receipt offline (`VinoReceipt.verify`). The app never sees a balance, a
  key or a custody account.

| Type | Members | Capability |
|---|---|---|
| `PlaySandbox` | `create`, `openIssuer`, `openHolder`, `open`, `back`, `unback`, `issue`, `transfer`, `setRail`, `redeem`, `voucher` (`PlayVoucher`), `account` (`PlayAccount`), `vouchers` (a `List<int>`), `check`, `close` | `VINO` |
| `PaymentRequest` | named-argument constructor (lane, merchant 32 B, nonce 16 B, amount, createdAt, expiresAt, memo, flags), `encode()` (160 bytes), `id()` (SHA-256, 32 bytes), `decode(bytes)`, `idOf(bytes)`, `hasChanceOutcome`, value `==` | pure |
| `VinoReceipt` | `message` (88 bytes), `sign` (wallet side), `verify`, `isValid` | `sign`: `CRYPTO_SIGN`; others pure |
| `SpendMeter` | `create(cap, dailyCap)`, `admit(day, amount)`, `tryAdmit` (an `Admission` record), `close` | `VINO` |
| `Vino` | sizes and limits: `requestBytes`, `requestIdBytes`, `receiptMessageBytes`, `memoMax`, `amountMax`, `maxAccounts`, `maxVouchers`, `flagChance`, key and signature sizes | |
| `VinoRail` (`bearer`, `ghost`), `VinoLane` (`play`, `live`), `VoucherState` (`active`, `redeemed`) | enums with `code` and `fromCode` | |

Rules the types carry (the full text is in `zxv_api.h`):

- An issuer may issue only while `10 * backing >= 18 * outstanding` (1.8x
  coverage, exact integers); an issue or `unback` that would break it is a
  `CoverageException` and changes nothing.
- A voucher is on exactly one rail, `bearer` (the note in hand) or `ghost`
  (its twin on the play ledger). A transfer must name that rail, else a
  `RailException`, so one coin is never spent on both.
- `redeem` returns a voucher to its issuer and retires it. **Nothing is paid
  out**: no call turns a voucher into backing, play units or anything else,
  and there is no interest, yield or return of any kind.
- A live request that marks a paid chance outcome (`Vino.flagChance`: loot
  boxes, paid draws) is refused when encoded (`ChanceException`). The allotment
  is a separate module, not a payment request.
- A receipt for an expired request, or approved outside
  `[createdAt, expiresAt]`, is an `ExpiredException`.
- `SpendMeter` counts what one device approved per day and refuses an amount
  that would pass the daily cap (`LimitException`; `tryAdmit` returns
  `(admitted: false, left: ...)` instead). It is a counter, not a legal
  check, and makes no exemption claim.
- Amounts, faces, ids and times are uint64 values in an `int` (bits kept);
  account ids are uint32, range-checked before the call. No `double`
  anywhere.
- The wallet's secret key is a `SecretKey` in native memory; libzxv reads it
  in place and `VinoReceipt` neither copies nor keeps it.

A play-lane shop (an issuer backs, issues a voucher to a player, the player
spends it at the shop and the shop redeems it):

```dart
final ctx = ZxvContext.create();
final play = PlaySandbox.create(ctx.rootCapability({Permission.VINO}));
final bank = play.openIssuer(1);
final player = play.openHolder(100);
final shop = play.openHolder(200);
play.back(bank, 18); // play units: not money
final coin = play.issue(bank, player, 8, VinoRail.bearer); // 10*18 >= 18*8
play.transfer(coin, player, shop, VinoRail.bearer); // spend on its active rail
play.redeem(coin, shop); // retired; nothing is paid out
assert(play.voucher(coin).state == VoucherState.redeemed);
assert(play.account(bank).faces == 0);
ctx.close(); // closes the capability and the sandbox too
```

A live payment request, the wallet's receipt, and the app's offline check
(the wallet half runs in the user's wallet, never in the app; it is here
only so the example runs):

```dart
const now = 1700000000;
final request = PaymentRequest(
  lane: VinoLane.live,
  merchant: List.filled(32, 7),
  nonce: List.filled(16, 9),
  amount: 1250,
  createdAt: now,
  expiresAt: now + 600,
  memo: 'Sword of Dawn',
);
final bytes = request.encode(); // 160 bytes; request.id() is its SHA-256

// Wallet side (the user's wallet, its own capability with CRYPTO_SIGN):
final walletCtx = ZxvContext.create();
final walletCap = walletCtx.rootCapability({Permission.CRYPTO_SIGN});
final wallet = MlDsa87(walletCap).generate();
final sig = VinoReceipt.sign(walletCap, bytes, wallet.secretKey, wallet.publicKey, now + 30);

// App side: pure and offline, no capability.
VinoReceipt.verify(bytes, wallet.publicKey, now + 30, sig, now + 60);
assert(PaymentRequest.decode(bytes) == request);
wallet.close();
walletCtx.close();
```

Honest limits: receipts use the reference ML-DSA-87, not a FIPS 140-3
validated module. The live flow here is the app's half only; approving a
request and posting the payment happen in the user's wallet (zxvd). Whether
a given deployment of Vino is store credit, a prepaid access product or
something else is a legal question pending counsel; nothing in this SDK
decides it. Minting live Vino, pegs, any fiat on-ramp or off-ramp, V-Bill
auctions and every operator action are not in the ABI and not here.

## Tests

```sh
make -C sdk/libzxv all fixtures
cd sdk/mobile/dart && dart pub get
ZXV_LIBRARY_PATH=$PWD/../../libzxv/build/libzxv.so dart test
dart analyze --fatal-infos
```

93 tests: every `vectors.json` section this package binds (status, rational
ops, parse and format, IEEE-754, Q16, `lpres` tables also against
`zxv_lp_*`, hashes, all 64 hexagram tags, the 8,192 `and`/`or` pairs,
`step`, `set`, polarity, the base mapping, the quarantine chain, Ed25519),
capability checks, attestation and revocation, halt and `ZxvFatalError`,
`FatalPolicy.abort` in a child process, close order, narrow-argument
range checks (Dart truncates an `int` passed as `Uint8`, so the SDK refuses
256 before it can become 0), budgets with `consumeAsync`, and an ABI drift
test that parses `zxv_api.h` and compares all 79 hand-written signatures
and all 202 ABI 1.5 ones and 21 ABI 1.6 vino ones (and checks that every
1.5 and 1.6 function is bound and that `FreightResolveC` matches
`zxv_freight_resolve_fn`).

ABI 1.5 (43 of the 76, mirroring the Kotlin and Java SDKs' 1.5 tests): the
`status_abi15` table and the typed exception for each of -32..-40;
`features2` and the new permission bits; use-after-close and idempotent
close for every 1.5 handle; closing a context closes its handles; a halted
context fails closed before reaching C; capability refusals (AIPI, MINT,
PARADOX, CRYPTO_SIGN, LEDGER_POST, foreign context); ML-DSA-87; and every
1.5 section of `vectors.json`: treaty (Sutra, records, refusals with fault
and offset, PIC S9(18), runs), CID and AIPI records and the offline node,
coins, tokens, Fibonacci codes, the wallet, the fee, the V-Bill hub
scenario (deterministic ML-DSA-87 pegs), freight headers, erasure decoding,
sets, op streams and the CIDREF resolver (found, missing, throwing, wrong
length), Web 4 (agents, cards, directory, digests, manifest, consent,
envelopes), and the paradox book run in order on one book.

ABI 1.6 vino (17 tests in `test/vino_test.dart`): the `status_abi16` table
and the typed exception for each of -46..-50 (and -51..-59 as unknown);
`Feature2.vino`, `Permission.VINO` and the 1.6 mask; every request vector
(exact bytes and id, refusals by status name, decode round trip) and every
decode refusal by changed byte; the receipt (seeded 0x31 wallet key,
message, deterministic signature, every check with its flipped byte), plus
a hedged `VinoReceipt.sign` and its refusals; the play scenario step by step
with the final accounts, voucher lists and vouchers; the meter scenario
including what is left on a refusal; voucher lists of 300 ids; use after
close; closing the context closes them; a capability without VINO and a
revoked one; the halted context; and both README examples as written.

## Honest limits

- **What was tested.** The package and its tests ran with the Dart 3.13 SDK
  on Linux x86-64 against the host libzxv. **No Flutter app was built and
  nothing ran on an Android or iOS device**: the Android `libzxv.so` files
  were cross-built with the NDK (see the Kotlin SDK) and the xcframework
  script needs a Mac (CI runs it on `macos-14`), but the loading paths for
  `DynamicLibrary.open('libzxv.so')` on Android and
  `DynamicLibrary.process()` on iOS are untested here.
- **Finalizers.** Dart does not promise to run a `Finalizer` (for example at
  isolate exit). Close contexts and secret keys yourself.
- **Isolates.** Handles belong to the isolate that made them. Do not send a
  context or capability to another isolate; create one there.
- **Least privilege, not a sandbox.** Code in the same process can make its
  own root capability.
- **Secrets.** `SecretKey.fromBytes` zeroes a modifiable list you pass, but
  the VM may already have copied it; swap and crash dumps are out of reach.
- **FATAL is catchable** by a bare `catch (e)`. The context stays poisoned;
  use `FatalPolicy.abort` when nothing may continue (and note that ending
  an iOS app yourself is against Apple's guidelines).
- **ABI 1.5 and 1.6 were tested on the host only** (Dart 3.13, Linux x86-64). The
  freight resolver's `NativeCallable.isolateLocal` path ran on the Dart VM,
  not under Flutter on a device.
- Not bound yet: ML-KEM-1024, the inference engine, phase clock, swarm, EVM,
  wire frames, the attestations registry, the ABI 1.4 hardware groups.
