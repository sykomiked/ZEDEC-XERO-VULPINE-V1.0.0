<!-- Copyright (c) 2024-2026 Michael Laurence Curzi and 36N9 Genetics, LLC -->
<!-- SPDX-License-Identifier: Apache-2.0 -->

# ZXV for Dart and Flutter

Package `zxv`: [libzxv](../../libzxv/) from Dart with `dart:ffi`. Hand-written
bindings (no code generation), checked against `zxv_api.h` by a test. ABI
1.3 or a later 1.x is required and checked at load. Dart 3.8 or later.

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
| Errors | `ZxvUsageException`, `ZxvRefusedException` (+ `CapabilityDeniedException`, `BudgetExhaustedException`, `InsufficientFundsException`, `VerificationFailedException`, `NotImplementedException`), `ZxvSystemException`, each with `status`, `statusName`, `detail`. |
| Fail-closed | `ZxvFatalError` is an `Error`, not an `Exception`: `on Exception catch` and `on ZxvException catch` do not catch it. The context is poisoned in C and marked dead in Dart. `FatalPolicy.abort` ends the process with `exit(70)`. |
| Quarantine log | `QuarantineLog.create(cap, capacity)` (needs `ATTEST`), `append`, `count`, `head`, `record`, `verify`. |
| Budgets | `Budgets.create(cap).budget(...)`; `consume`, `tryConsume`, `release`, `setLimit`, `status`, `onExhausted`, and `Future<void> consumeAsync(amount, timeout:, cancel:)`. |
| Crypto | `Hashes.sha3_256/keccak256/shake256`; `Ed25519(cap)`: `generate`, `sign`, `verify`, `isValid`. |
| Secrets | `SecretKey`: native memory the Dart GC never moves, wiped with `zxv_secure_zero` on `close()` and by a `Finalizer`. `exportBytesUnsafe()` is the only heap copy. |
| Lifetimes (R6) | Every handle is wrapped in a `NativeHandle` that closes its children before itself; a parent holds its children's handles, so a child is never destroyed after its parent. A Dart `Finalizer` on each wrapper closes the tree if it is collected unclosed. (`NativeFinalizer` is not used: it calls one C function per object and cannot order children before parents.) |

## Tests

```sh
make -C sdk/libzxv all fixtures
cd sdk/mobile/dart && dart pub get
ZXV_LIBRARY_PATH=$PWD/../../libzxv/build/libzxv.so dart test
dart analyze --fatal-infos
```

33 tests: every `vectors.json` section this package binds (status, rational
ops, parse and format, IEEE-754, Q16, `lpres` tables also against
`zxv_lp_*`, hashes, all 64 hexagram tags, the 8,192 `and`/`or` pairs,
`step`, `set`, polarity, the base mapping, the quarantine chain, Ed25519),
capability checks, attestation and revocation, halt and `ZxvFatalError`,
`FatalPolicy.abort` in a child process, close order, narrow-argument
range checks (Dart truncates an `int` passed as `Uint8`, so the SDK refuses
256 before it can become 0), budgets with `consumeAsync`, and an ABI drift
test that parses `zxv_api.h` and compares all 79 hand-written signatures.

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
- Not bound yet: ML-KEM-1024, ML-DSA-87, the inference engine, phase clock,
  swarm, EVM, wire frames, the attestations registry.
