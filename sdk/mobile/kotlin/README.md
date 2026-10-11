<!-- Copyright (c) 2024-2026 Michael Laurence Curzi and 36N9 Genetics, LLC -->
<!-- SPDX-License-Identifier: Apache-2.0 -->

# ZXV for Kotlin (Android and JVM)

A Kotlin SDK for [libzxv](../../libzxv/) that runs on Android. Android has no
Java FFM API (the [Java SDK](../../java/) needs Java 22 FFM), so this SDK
reaches libzxv through a small JNI shim, [`jni/zxv_jni.c`](jni/zxv_jni.c): one
mechanical C function per libzxv call, with all policy (capabilities, errors,
lifetimes) in Kotlin where it is tested.

Package `org.zedec.zxv.mobile`, ABI 1.3 or a later 1.x (checked at load).

| Module | What | Built with |
|---|---|---|
| `:zxv` | Kotlin/JVM library and the host tests | Gradle + CMake (host JNI shim) |
| `:zxv-android` | Android library (`.aar`) for `arm64-v8a` and `x86_64` | Gradle + AGP + NDK |

## Quickstart

```kotlin
import org.zedec.zxv.mobile.*

ZxvContext.create().use { ctx ->
    val root = ctx.rootCapability(Permission.all())
    ctx.enableCurrency(root, "NGN")
    val ledger = Ledger(root)
    val bank = ledger.openAccount("bank", "NGN", AccountKind.ISSUER)
    val alice = ledger.openAccount("alice", "NGN")
    ledger.post(bank, alice, 12_550, "NGN", "top-up-1")
    println(ledger.balance(alice))                       // Balance(debitMinor=12550, ...)
    println(Rational.parse("0.015") * Rational.of(12_550)) // 188.25, exactly
    val reader = root.attenuate(Permission.LEDGER_READ)  // hand this to UI code
}
```

## Install

### Android app

1. Build libzxv for each ABI with the NDK, then the AAR:

   ```sh
   make -C sdk/libzxv all                                     # host build (tests)
   ANDROID_NDK_HOME=$ANDROID_HOME/ndk/27.2.12479018 \
     sdk/mobile/kotlin/scripts/build-libzxv-android.sh        # arm64-v8a x86_64
   gradle -p sdk/mobile/kotlin -Pzxv.android=true :zxv-android:assembleRelease
   # -> sdk/mobile/kotlin/android/build/outputs/aar/zxv-android-release.aar
   ```

2. Add the AAR to your app (`implementation(files("libs/zxv-android-release.aar"))`
   plus `org.jetbrains.kotlinx:kotlinx-coroutines-core`). It carries
   `lib/<abi>/libzxv.so` and `lib/<abi>/libzxvjni.so`; they load on first use
   with `System.loadLibrary`, so no configuration is needed. The AAR also
   ships R8 keep rules for the native methods.

Shipping libzxv per ABI:

| ABI | Devices | Built by default |
|---|---|---|
| `arm64-v8a` | every current phone and tablet | yes |
| `x86_64` | emulators, ChromeOS | yes |
| `armeabi-v7a`, `x86` | old 32-bit devices | `build-libzxv-android.sh armeabi-v7a x86` and `-Pzxv.abis=...` (not tested) |

`minSdk` is 24. Both libraries are linked for 16 KB pages (Android 15+).
The libzxv build script works around two portability gaps in libzxv's
Makefile on the NDK without changing the library (see "Honest limits").

### Desktop JVM (and the tests)

```sh
make -C sdk/libzxv all fixtures
gradle -p sdk/mobile/kotlin :zxv:test                          # runs on the Gradle JDK
gradle -p sdk/mobile/kotlin :zxv:test -Pzxv.testJavaHome=/path/to/jdk-25
```

The test task builds `libzxvjni` with CMake against `sdk/libzxv/build` and
passes the paths. In your own JVM program, either put both libraries on
`java.library.path`, or set them explicitly:

```kotlin
Zxv.load("/opt/zxv/libzxv.so", "/opt/zxv/libzxvjni.so") // or -Dzxv.library / -Dzxv.jni.library
```

Search order for libzxv: `Zxv.load`, `-Dzxv.library`, `ZXV_LIBRARY_PATH` (file
or directory), then `System.loadLibrary("zxv")`. For the shim: the explicit
path, `-Dzxv.jni.library`, `ZXV_JNI_LIBRARY_PATH`, a `libzxvjni` next to the
libzxv that was loaded, then `System.loadLibrary("zxvjni")`. On JDK 22+ run
with `--enable-native-access=ALL-UNNAMED`.

## Concepts in Kotlin

| Concept ([SDK_DESIGN](../../docs/SDK_DESIGN.md)) | Kotlin |
|---|---|
| Exact numbers | `Rational` (`of`, `parse`, `from(BigDecimal)`, operators `+ - * /`). The only float door is `Rational.fromDoubleExact(x)`; NaN and infinity throw `FloatRejectedException`. |
| Four-valued logic | `Truth.NEITHER/TRUE/FALSE/BOTH`, `and`/`or`/`!`. No `Boolean` without a policy: `toBoolean(onBoth, onNeither)`, `isCertainlyTrue`, `requireTrue()`. |
| Hexagram (ABI 1.3) | `Hexagram.of(HexBase, HexPolarity, HexPhase)`, `fromTag`, `fromTruth`; `action`, `step`, `advance`, `withState`, `forbid`, `and`/`or`/`!`, `toTruth()`. The base is not numbered like `Truth`; there is no numeric conversion. |
| Capabilities | `ctx.rootCapability(...)`, `attenuate`, `revoke`, `setAttestation`, `info`, `allows`, `require`. Every gated call passes one (the `*_cap` functions). |
| Errors | `ZxvUsageException` (+ `FloatRejectedException`), `ZxvRefusedException` (+ `CapabilityDenied`, `BudgetExhausted`, `InsufficientFunds`, `VerificationFailed`, `NotImplemented`), `ZxvSystemException`; each has `status`, `statusName`, `detail`. |
| Fail-closed | `ZxvFatalError` is a `kotlin.Error`, outside the `ZxvException` family, so `catch (e: Exception)` does not swallow it. The context is poisoned in C and marked dead in Kotlin; every later call fails. `FatalPolicy.ABORT` ends the process with `Runtime.halt(70)`. |
| Quarantine log | `QuarantineLog.create(cap, capacity)` (needs `ATTEST`), `append`, `count`, `head`, `record`, `verify`. A full log is REFUSED (`ZXV_E_CAPACITY`): halt the context. |
| Budgets | `Budgets.create(cap).budget(name, Resource, limit)`; `consume`, `tryConsume`, `release`, `setLimit`, `status`, `onExhausted`, and the coroutine `suspend fun consumeAsync(amount, timeout)`, which waits for a release or a larger limit and is cancelled with its coroutine. |
| Crypto | `Hashes.sha3_256/keccak256/shake256`; `Ed25519(cap)` with `generate`, `sign`, `verify`, `isValid`. |
| Secrets | `SecretKey`: C-heap memory (never on the Java heap), wiped with `zxv_secure_zero` on `close()` and by a GC backstop. `exportBytesUnsafe()` is the only heap copy. |
| Lifetimes (R6) | Every handle object is `Closeable`. Closing a context closes everything made from it, children first, after in-flight calls return (a read/write lock per handle). A `PhantomReference` backstop (works on every Android API level) closes unreachable trees. |

### Coroutines and budgets

```kotlin
val tokens = Budgets.create(cap).budget("tokens", Resource.TOKENS, Rational.of(1_000))
lifecycleScope.launch {
    tokens.consumeAsync(Rational.of(32), timeout = 5.seconds) // suspends while exhausted
    runInference()
}
// elsewhere, on any thread:
tokens.release(Rational.of(32))                                // wakes the waiter
```

The wait is driven by this SDK's own `release`/`setLimit` calls (the registry
owns its `zxv_rmag` handle), so no native callback ever enters the JVM: the
shim creates no JNI global references and no thread attaches.

## API overview

| Type | Main members |
|---|---|
| `Zxv` | `load`, `version`, `features`, `hasFeature`, `statusName`, `statusClass` |
| `ZxvContext` | `create(policy)`, `rootCapability`, `enableCurrency`, `registerPrivateCurrency`, `isPoisoned`, `halt`, `close` |
| `Capability` | `attenuate`, `revoke`, `setAttestation`, `info`, `allows`, `require`, `close` |
| `Ledger` | `openAccount`, `account`, `accountCount`, `post`, `balance`, `journal`, `check` |
| `Rational`, `Truth`, `Hexagram` | value types above |
| `QuarantineLog`, `Budgets`/`Budget`, `Hashes`, `Ed25519`, `SecretKey` | above |

Not bound yet (use the [Java SDK](../../java/) on a desktop JVM, or ask):
ML-KEM-1024, ML-DSA-87, the inference engine (`Model`, `Session`), phase
clock, swarm, EVM, wire frames, attestations registry, ISO 20022 messages.

## Tests

`gradle -p sdk/mobile/kotlin :zxv:test` runs 34 tests (JUnit 5) on the host
JVM against the real libzxv and the JNI shim:

- `vectors.json`: status names and classes, rational ops, parse and format,
  IEEE-754 bits, Q16, the `lpres` tables (host copy and `zxv_lp_*`), hashes,
  all 64 hexagram tags, the 64x64 `and`/`or` tables, `step`, `set`,
  polarity cases, the base mapping, the quarantine chain (also recomputed
  with `java.security.MessageDigest`), and the Ed25519 vector;
- capabilities on every call, attestation, revocation, foreign-context caps,
  halt and `ZxvFatalError` not swallowed by `catch (e: Exception)`,
  `FatalPolicy.ABORT` in a child JVM (exit 70, no `catch`/`finally` ran),
  close order, out-of-range narrow arguments;
- `consumeAsync`: wake on release and on a larger limit, timeout, cancel;
- shim drift: the Kotlin `external` set equals the C file's function set.

## Honest limits

- **What was tested where.** The Kotlin code and the JNI shim were tested on
  a desktop JVM (Linux x86-64, JDK 21 and 25). The Android AAR was built
  for `arm64-v8a` and `x86_64` with NDK r27 and AGP, and Android lint ran
  clean of API-level errors, but **the tests were not run on an Android
  device or emulator**. The same Kotlin sources and the same C file are
  used on both, so the host tests cover the logic, not the device loader.
- **libzxv on the NDK.** libzxv's Makefile is not NDK-ready as is: it calls
  `getrandom()` (declared by bionic only from API 28) and links `-lpthread`
  (not shipped by the NDK). `scripts/build-libzxv-android.sh` builds with
  `-DZXV_HAVE_ARC4RANDOM=1` (bionic's `arc4random_buf`, every API level)
  and an empty `libpthread.a`, leaving libzxv itself untouched.
- **Least privilege, not a sandbox.** Code in the same app process can make
  its own root capability from a context it can reach.
- **Secrets.** Key bytes live in C memory and are wiped on `close()`.
  `SecretKey.fromBytes` zeroes the array you pass, but the GC may already
  have copied it; swap, core dumps and crash reporters are outside the
  SDK's control.
- **FATAL is catchable** by `catch (t: Throwable)` and Kotlin's
  `runCatching`. The context stays poisoned regardless; use
  `FatalPolicy.ABORT` when nothing may continue.
- **GC backstop.** Close contexts explicitly (an Activity or ViewModel can
  outlive any GC hint). A capability not closed lives until its context
  closes or becomes unreachable.
- A Kotlin/Native (iOS) target is not provided; for iOS use the
  [Swift SDK](../../swift/) or the [Dart SDK](../dart/) in Flutter.
