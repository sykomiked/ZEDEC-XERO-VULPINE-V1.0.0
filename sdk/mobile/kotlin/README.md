<!-- Copyright (c) 2024-2026 Michael Laurence Curzi and 36N9 Genetics, LLC -->
<!-- SPDX-License-Identifier: Apache-2.0 -->

# ZXV for Kotlin (Android and JVM)

A Kotlin SDK for [libzxv](../../libzxv/) that runs on Android. Android has no
Java FFM API (the [Java SDK](../../java/) needs Java 22 FFM), so this SDK
reaches libzxv through a small JNI shim, [`jni/zxv_jni.c`](jni/zxv_jni.c) and,
for the ABI 1.5 groups, [`jni/zxv_jni15.c`](jni/zxv_jni15.c), and for the
ABI 1.6 vino group, [`jni/zxv_jni16.c`](jni/zxv_jni16.c): one
mechanical C function per libzxv call, with all policy (capabilities, errors,
lifetimes) in Kotlin where it is tested.

Package `org.zedec.zxv.mobile`, version 1.6.0, ABI 1.6 or a later 1.x (checked at
load, together with the shim's own ABI).

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
| Errors | `ZxvUsageException` (+ `FloatRejectedException`, `NotADenominationException` -32, `TreatyException` -33 with `fault` and `offset`), `ZxvRefusedException` (+ `CapabilityDenied`, `BudgetExhausted`, `InsufficientFunds`, `VerificationFailed`, `NotImplemented`, and for ABI 1.5 `CidException` -34, `ConsentException` -35, `ErasureException` -36, `PegException` -37, `LimitException` -38, `StepLimitException` -39, `OwnerApprovalRequiredException` -40, and for ABI 1.6 `RailException` -46, `CoverageException` -47, `ExpiredException` -49, `ChanceException` -50), `ZxvSystemException`; `LaneException` -48 is a `ZxvUsageException`. Each has `status`, `statusName`, `detail`. `ZxvStatus` names every code (-41..-45 and -51..-59 are reserved by the ABI and not defined). |
| Fail-closed | `ZxvFatalError` is a `kotlin.Error`, outside the `ZxvException` family, so `catch (e: Exception)` does not swallow it. The context is poisoned in C and marked dead in Kotlin; every later call fails. `FatalPolicy.ABORT` ends the process with `Runtime.halt(70)`. |
| Quarantine log | `QuarantineLog.create(cap, capacity)` (needs `ATTEST`), `append`, `count`, `head`, `record`, `verify`. A full log is REFUSED (`ZXV_E_CAPACITY`): halt the context. |
| Budgets | `Budgets.create(cap).budget(name, Resource, limit)`; `consume`, `tryConsume`, `release`, `setLimit`, `status`, `onExhausted`, and the coroutine `suspend fun consumeAsync(amount, timeout)`, which waits for a release or a larger limit and is cancelled with its coroutine. |
| Crypto | `Hashes.sha3_256/keccak256/shake256`; `Ed25519(cap)` and `MlDsa87(cap)` with `generate`, `sign`, `verify`, `isValid` (ML-DSA-87 also `generate(seed: SecretKey)` and `signDeterministic`, for the V-Bill hub's pegs). |
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

## ABI 1.5 groups

All eight groups are bound (`Zxv.features2()` / `Zxv.hasFeature(Feature2.X)`),
with the 1.5 permission bits `Permission.AIPI`, `MINT` and `PARADOX` (bits 12
to 14; `HW_MATRIX` and `HW_RECONF` hold bits 10 and 11 so the ordinals stay
the bit numbers, though the hardware groups themselves are not bound here).
uint64 values (amounts, faces, timestamps) travel in a `Long` with their bits
kept, as in the Java SDK; uint32 values are `Long`, uint8 values `Int`.

| Group | Types | Capability |
|---|---|---|
| Treaty | `Treaty` (`parse`, `parseRecord`, `toSutra`, `toRecord`, `validate`, `fault`, `parties`, `obligations`, `run`), `TreatyResult`, `Pic18`, `TreatyFault`, `TreatyOp` | pure |
| CID / AIPI | `Cid` (`compute`, `raw`, `unixfs`, `parse`, `toText`, `toV1`, `verify`, `matches`, `providerKey`), `FunctionRecord` (+ `Builder`), `CallRecord`, `ClaimRecord`, `AipiNode` (`put`, `putPrivate`, `get`, `publish`, `loadFunction`, `plan`, `info`) | node writes: `AIPI` |
| Coin | `Coins`, `Tokens`, `Octane`, `TokenHolding`, `FibonacciCode`, `TokenWallet` | wallet: `MINT` |
| Fee | `AssuranceFee` (`fee`, `charge`, `split`, `defaultPolicy`), `FeePolicy`, `FeeBucket`, `FeeUnit` | pure |
| V-Bill hub | `VHub` (`pegMessage`, `addCurrency`, `setPeg`, `currency`, `openAccount`, `issue`, `balance`, `quote`, `swap`, `addLimit`, `nextEpoch`, `check`) | `LEDGER_POST` / `LEDGER_READ` |
| Freight | `FreightHeader`, `Freight` (`encode`, `decode`, `split`, `setCount`), `FreightJoin`, `OpStream` (`encode`, `expand`, `peek`, `bound`, `generate`), `OpStreamWriter`, `CidResolver`, `Generator` | pure |
| Web 4 | `AgentKey` (`card`, `seal`, `request`, `reply`, `sign`, `issueConsent`), `AgentCard`, `AgentDirectory`, `ToolManifest`, `Tool`, `ConsentToken`, `ConsentLog`, `Envelope`, `Web4.actionDigest` | keys: `CRYPTO_SIGN`; consent: `ATTEST` |
| Paradox | `ParadoxBook` (classify, registry, logs, `constructive`, `destructive`, `neutral`, `special`, `draft`, `review`, `pass`), `ParadoxPolicy`, `ParadoxHold`, `ParadoxGraph`, `Paradox` (pure functions), `RepairProof`, `Draft`, `LogRecord`, `QuarantineRecord` | writes: `PARADOX` |

Context-bound handles (`AipiNode`, `TokenWallet`, `VHub`, `AgentKey`, and the
paradox book, policy, hold and graph) close with their context and fail
closed once it halts; pure handles (`Treaty`, `FreightJoin`,
`OpStreamWriter`, `AgentDirectory`, `ToolManifest`, `ConsentLog`) have no
context. Secret inputs never sit on the Java heap: the AIPI node key, Web 4
agent seeds and ML-DSA-87 seeds and keys are `SecretKey`s.

`OpStream.expand(ops, limit, resolver)` never runs the `CidResolver` inside
a native call: the shim passes libzxv a table of the CIDs resolved so far,
reports the first one missing, and Kotlin resolves it and expands again. A
resolver's exception is rethrown unchanged, a wrong-length answer is a
`ZxvUsageException`, and no JNI callback, global reference or thread attach
is ever made. The cost is one extra expansion per distinct CIDREF.

V-Bonds and derivatives are not part of this SDK.

## Vino Embed (ABI 1.6)

What an app or game needs to take part in Vino **without ever holding
anyone's value or keys** (`Feature2.VINO`, `Permission.VINO` = bit 15;
`Permission.ALL_1_5` keeps its value and `Permission.ALL_1_6` adds the new
bit). Two lanes, which never meet:

- **Play lane** (`PlaySandbox`): a sandbox inside the app with its own play
  units, issuers, holders and Fibonacci-face vouchers, in process memory
  only. **Play units are not money**: they come from a faucet, are worth
  nothing, are lost on `close()`, and no call takes a sandbox and anything
  live, so they can never become live value.
- **Live lane**: real Vino stays in the user's own ZXV wallet. The app builds
  a `PaymentRequest`; the user approves it in their wallet, which signs a
  receipt (`VinoReceipt.sign`, the wallet's call); the app checks the receipt
  offline (`VinoReceipt.verify`). The app never sees a balance, a key or a
  custody account.

| Type | Members | Capability |
|---|---|---|
| `PlaySandbox` | `create`, `openIssuer`, `openHolder`, `open`, `back`, `unback`, `issue`, `transfer`, `setRail`, `redeem`, `voucher`, `account`, `vouchers`, `check`, `close`; `Voucher`, `Account` | `VINO` |
| `PaymentRequest` | constructor (lane, merchant 32 B, nonce 16 B, amount, createdAt, expiresAt, memo, flags), `encode()` (160 bytes), `id()` (SHA-256, 32 bytes), `decode(bytes)`, `idOf(bytes)`, `hasChanceOutcome` | pure |
| `VinoReceipt` | `message` (88 bytes), `sign` (wallet side), `verify`, `isValid` | `sign`: `CRYPTO_SIGN`; others pure |
| `SpendMeter` | `create(cap, dailyCap)`, `admit(day, amount)`, `tryAdmit`, `close` | `VINO` |
| `Vino` | sizes and limits: `REQUEST_BYTES`, `REQUEST_ID_BYTES`, `RECEIPT_MESSAGE_BYTES`, `MEMO_MAX`, `AMOUNT_MAX`, `MAX_ACCOUNTS`, `MAX_VOUCHERS`, `FLAG_CHANCE`, key and signature sizes | |
| `VinoRail` (`BEARER`, `GHOST`), `VinoLane` (`PLAY`, `LIVE`), `VoucherState` (`ACTIVE`, `REDEEMED`) | enums with `code` and `fromCode` | |

Rules the types carry (the full text is in `zxv_api.h`):

- An issuer may issue only while `10 * backing >= 18 * outstanding` (1.8x
  coverage, exact integers); an issue or `unback` that would break it is
  `CoverageException` and changes nothing.
- A voucher is on exactly one rail, `BEARER` (the note in hand) or `GHOST`
  (its twin on the play ledger). A transfer must name that rail, else
  `RailException`, so one coin is never spent on both.
- `redeem` returns a voucher to its issuer and retires it. **Nothing is paid
  out**: no call turns a voucher into backing, play units or anything else,
  and there is no interest, yield or return of any kind.
- A LIVE request that marks a paid chance outcome (`Vino.FLAG_CHANCE`: loot
  boxes, paid draws) is refused when encoded (`ChanceException`). The allotment
  is a separate module, not a payment request.
- A receipt for an expired request, or approved outside
  `[createdAt, expiresAt]`, is `ExpiredException`.
- `SpendMeter` counts what one device approved per day and refuses an amount
  that would pass the daily cap (`LimitException`; `tryAdmit` returns
  `Admission(admitted = false, left)` instead). It is a counter, not a legal
  check, and makes no exemption claim.
- Amounts, faces, ids and times are uint64 values in a `Long` (bits kept;
  print with `java.lang.Long.toUnsignedString`); account ids are uint32 in a
  `Long`. No floating point anywhere.
- The wallet's secret key is a `SecretKey` passed by address; it is never
  copied to the heap and `VinoReceipt` keeps no reference to it.

A play-lane shop (an issuer backs, issues a voucher to a player, the player
spends it at the shop and the shop redeems it):

```kotlin
ZxvContext.create().use { ctx ->
    ctx.rootCapability(Permission.VINO).use { cap ->
        PlaySandbox.create(cap).use { play ->
            val bank = play.openIssuer(owner = 1)
            val player = play.openHolder(owner = 100)
            val shop = play.openHolder(owner = 200)
            play.back(bank, 18)                                // play units: not money
            val coin = play.issue(bank, player, 8, VinoRail.BEARER) // 10*18 >= 18*8
            play.transfer(coin, player, shop, VinoRail.BEARER) // spend on its active rail
            play.redeem(coin, shop)                            // retired; nothing is paid out
            check(play.voucher(coin).state == VoucherState.REDEEMED)
            check(play.account(bank).faces == 0L)              // outstanding back to 0
        }
    }
}
```

A live payment request, the wallet's receipt, and the app's offline check
(the wallet half runs in the user's wallet, never in the app; it is here
only so the example runs):

```kotlin
val now = 1_700_000_000L
val request = PaymentRequest(
    VinoLane.LIVE, merchant = ByteArray(32) { 7 }, nonce = ByteArray(16) { 9 },
    amount = 1_250, createdAt = now, expiresAt = now + 600, memo = "Sword of Dawn",
)
val bytes = request.encode()                     // 160 bytes; request.id() is its SHA-256

// Wallet side (user's wallet, its own capability with CRYPTO_SIGN):
ZxvContext.create().use { ctx ->
    ctx.rootCapability(Permission.CRYPTO_SIGN).use { walletCap ->
        MlDsa87(walletCap).generate().use { wallet ->
            val sig = VinoReceipt.sign(walletCap, bytes, wallet.secretKey, wallet.publicKey, now + 30)

            // App side: pure and offline, no capability.
            VinoReceipt.verify(bytes, wallet.publicKey, now + 30, sig, now = now + 60)
            check(PaymentRequest.decode(bytes) == request)
        }
    }
}
```

Honest limits: receipts use the reference ML-DSA-87, not a FIPS 140-3
validated module. The live flow here is the app's half only; approving a
request and posting the payment happen in the user's wallet (zxvd). Whether a
given deployment of Vino is store credit, a prepaid access product or
something else is a legal question pending counsel; nothing in this SDK
decides it. Minting live Vino, pegs, any fiat on-ramp or off-ramp, V-Bill
auctions and every operator action are not in the ABI and not here.

## API overview

| Type | Main members |
|---|---|
| `Zxv` | `load`, `version`, `features`, `features2`, `hasFeature`, `statusName`, `statusClass` |
| `ZxvContext` | `create(policy)`, `rootCapability`, `enableCurrency`, `registerPrivateCurrency`, `isPoisoned`, `halt`, `close` |
| `Capability` | `attenuate`, `revoke`, `setAttestation`, `info`, `allows`, `require`, `close` |
| `Ledger` | `openAccount`, `account`, `accountCount`, `post`, `balance`, `journal`, `check` |
| `Rational`, `Truth`, `Hexagram` | value types above |
| `QuarantineLog`, `Budgets`/`Budget`, `Hashes`, `Ed25519`, `MlDsa87`, `SecretKey` | above |
| ABI 1.5 | see the table above |
| ABI 1.6 (vino) | `PlaySandbox`, `PaymentRequest`, `VinoReceipt`, `SpendMeter`, `Vino`, `VinoRail`, `VinoLane`, `VoucherState` (see Vino Embed) |

Not bound yet (use the [Java SDK](../../java/) on a desktop JVM, or ask):
ML-KEM-1024, the inference engine (`Model`, `Session`), phase clock, swarm,
EVM, wire frames, attestations registry, ISO 20022 messages, the ABI 1.4
hardware groups.

## Tests

`gradle -p sdk/mobile/kotlin :zxv:test` runs 91 tests (JUnit 5) on the host
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
- ABI 1.5 (40 tests, mirroring the Java SDK's): the `status_abi15` table and
  the typed exception for each of -32..-40; `features2` and the new
  permission bits; use-after-close and idempotent close for every 1.5
  handle; closing a context closes its handles; a halted context fails
  closed before reaching C; capability refusals (AIPI, MINT, PARADOX,
  CRYPTO_SIGN, foreign context); ML-DSA-87; and every 1.5 section of
  `vectors.json`: treaty (Sutra, records, refusals with fault and offset,
  PIC S9(18), runs), CID and AIPI records and the offline node, coins,
  tokens, Fibonacci codes, the wallet, the fee, the V-Bill hub scenario
  (deterministic ML-DSA-87 pegs), freight headers, erasure decoding, sets,
  op streams and the CIDREF resolver (found, missing, throwing, wrong
  length), Web 4 (agents, cards, directory, digests, manifest, consent,
  envelopes), and the paradox book run in order on one book;
- ABI 1.6 vino (16 tests): the `status_abi16` table and the typed exception
  for each of -46..-50; `Feature2.VINO`, `Permission.VINO` and the 1.6
  mask; every request vector (exact bytes and id, refusals by status name,
  decode round trip) and every decode refusal by changed byte; the receipt
  (seeded 0x31 wallet key, message, deterministic signature, every check
  with its flipped byte), plus a hedged `VinoReceipt.sign` and its
  refusals; the play scenario step by step with the final accounts,
  voucher lists and vouchers; the meter scenario including what is left on
  a refusal; voucher lists of 300 ids; use after close; closing the context
  closes them; a capability without VINO and a revoked one; the halted
  context; and both README examples;
- shim drift: the Kotlin `external` sets of `Jni`, `Jni15` and `Jni16` equal
  the C files' function sets (202 functions in `zxv_jni15.c`), and every
  `zxv_vino_*` function in `zxv_api.h` is called by `zxv_jni16.c`.

## Honest limits

- **What was tested where.** The Kotlin code and the JNI shim were tested on
  a desktop JVM (Linux x86-64, JDK 21 and 25). The Android AAR was built
  for `arm64-v8a` and `x86_64` with NDK r27 and AGP, and Android lint ran
  clean of API-level errors, but **the tests were not run on an Android
  device or emulator**. The same Kotlin sources and the same C file are
  used on both, so the host tests cover the logic, not the device loader.
  The ABI 1.5 bindings (`zxv_jni15.c` and the 1.5 Kotlin types) were
  tested on the host JVM only (JDK 21, Linux x86-64); `zxv_jni15.c` was
  also compiled with clang under `-Wall -Wextra -Werror`, but the AAR has
  not been rebuilt with the NDK since they were added. The same holds for
  the ABI 1.6 vino binding (`zxv_jni16.c`, `Vino.kt`): host JVM only.
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
