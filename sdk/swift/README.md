<!-- Copyright (c) 2024-2026 Michael Laurence Curzi and 36N9 Genetics, LLC -->
<!-- SPDX-License-Identifier: Apache-2.0 -->

# ZXV Swift SDK

`Zxv` is the Swift package for the ZXV platform core. It runs on macOS (the ZXV desktop app), iOS (mobile and edge) and Linux (servers and
CI). The package has two targets:

- **`CZxv`**, a system library target. Its `module.modulemap` points at
  `sdk/libzxv/include/zxv_api.h` and links `libzxv`.
- **`Zxv`**, the Swift API. It provides final classes that clean up in `deinit`, an exact
  `Rational`, a `Truth` enum, typed throws for each error class, async/await,
  and `AsyncThrowingStream` token streaming.

The package follows [`sdk/docs/SDK_DESIGN.md`](../docs/SDK_DESIGN.md). It is written in the
Swift 6 language mode, so strict concurrency checking is on.

## Install

Build `libzxv` first, with `make -C sdk/libzxv`. Then build and test the package:

```sh
cd sdk/swift
swift build
swift test                                  # XCTest, no network
swift run zxv-quickstart ../conformance/fixtures
```

At build time `Package.swift` looks for `libzxv` in two places, in this order:

1. the directory in `ZXV_LIBRARY_PATH`
2. the in-repo build, `../libzxv/build`

When it finds the library, it adds `-L` and an rpath so `swift test` works without an install step. These are SwiftPM *unsafe flags*. SwiftPM allows them in a root package or a
local-path dependency. A package pulled from a registry should use the binary target described
below instead. Without either, the system linker path is used.

To depend on the package from another package:

```swift
.package(path: "../ZEDEC-XERO-VULPINE-V1.0.0/sdk/swift"),
// target dependency:
.product(name: "Zxv", package: "Zxv"),
```

When you create the first `ZxvContext`, it checks the library's `zxv_version`. It refuses a
library whose major version is not 1 or whose minor version is below 4 (ABI 1.4 added the
hardware group, 1.3 the hexagram tag, 1.2 the capability-gated `*_cap` forms the SDK calls).

## Quickstart

```swift
import Zxv
let ctx = try ZxvContext()
let root = try ctx.rootCapability()                 // every bit, attested TRUE
try ctx.enableCurrency("NGN", capability: root)     // needs .ledgerPost
let ledger = Ledger(root)
let bank = try ledger.openAccount("bank", currency: "NGN", kind: .issuer)
let ada = try ledger.openAccount("ada", currency: "NGN")
try ledger.post(from: bank, to: ada, amount: 125_075, currency: "NGN", reference: "salary-001")
print(try ledger.balance(ada).equity)               // 125075 (kobo, exact)
print(try Rational("0.1") + Rational("0.2"))        // 0.3
```

`Sources/ZxvQuickstart/main.swift` is a fuller runnable version of this quickstart. It also awaits a budget and a phase event,
streams tokens, and shows the fail-closed path.

## Concepts in Swift terms

| Concept | Swift form |
|---|---|
| Handles | `final class`es that free their C handle in `deinit`. Children hold strong references to their parents: a `Capability` keeps its `ZxvContext` alive, and a child capability keeps its parent alive. So the C destruction order (R6) always holds. |
| Errors | Typed throws, one type per error class: `ZxvUsageError`, `ZxvRefusedError` (whose `reason` is `.capabilityDenied`, `.budgetExhausted`, `.insufficientFunds`, `.verificationFailed`, `.notImplemented` or `.other`), `ZxvFatalError` and `ZxvSystemError`. Calls that touch state throw `ZxvError`, an enum with one case per class. Pure functions such as `Rational` arithmetic can fail only through misuse, so they throw `ZxvUsageError` alone. Every error carries `status`, `statusName` and `detail`. Write `catch .refused(let e) where e.reason == .insufficientFunds`. |
| Fail-closed | A FATAL status marks the context dead and throws `.fatal`. Every later call fails before it reaches C, and the C context is poisoned anyway. A thrown Swift error can always be caught, so `ZxvLibrary.fatalPolicy = .abort` is the uncatchable form: it prints the detail and calls `abort()`. |
| Capabilities | `CapabilitySet` is an `OptionSet` (`.ledgerRead`, `.cryptoSign`, ...). Each gated object (`Ledger`, `Budgets`, `MlDsa87`, ...) takes a `Capability` and uses it on every call, through the capability-gated `zxv_*_cap` C forms (ABI 1.2). Creating `Budgets`, `PhaseClock`, `Attestations` or `Swarm` needs its bit. `enableCurrency(_:minorUnits:capability:)` and `registerPrivateUnit(_:minorUnits:capability:)` need `.ledgerPost`; a capability of another context is refused (`.capabilityDenied`, `ZXV_E_CAP`). `Message(cap, kind:)` needs `.ledgerRead`, keeps the capability alive, and the library re-checks it on every call. The 1.0 forms without a capability (`enableCurrency(_:minorUnits:)`, `registerPrivateUnit(_:minorUnits:)`, `Message(ctx, kind:)`) still work but are `@available(*, deprecated)`. Narrow a capability with `attenuate(_:)`. |
| Exactness | `Rational` is a value type holding an `Int64` numerator and denominator. Its `+ - * /` operators are `throws(ZxvUsageError)` (`ZXV_E_RANGE` on overflow). `adding(_:)` and the other named forms return `nil` on overflow instead. It also accepts integer literals. Only `Rational.fromDoubleExact(_:)` takes a `Double`. |
| Four-valued logic | `enum Truth: UInt8 { case neither, true, false, both }` uses the C numbering. `and`, `or` and `not` match the C tables. There is no conversion to `Bool`. Use `isCertainlyTrue`, `toBool(onBoth:onNeither:)` or `requireTrue()`. |
| Async | `budget.consumeAsync(_:timeout:)` suspends until the budget is replenished. `DeferredEvent.awaitCleared()` and `PhaseClock.untilTick(_:)` suspend until the clock reaches the tick. `session.generateAsync` runs generation off the cooperative thread pool. All of them support task cancellation. |
| Streaming | `session.stream(prompt, options:)` returns an `AsyncThrowingStream<Token, Error>`. Generation runs on its own thread. If you break out of the `for try await` loop, generation stops at the next token. |
| Secrets | `SecretKey` keeps its bytes in a `malloc`ed buffer that is not a Swift array, so the bytes are never copied on write. `deinit` and `wipe()` overwrite it with `zxv_secure_zero`. `withUnsafeBytes` reads the key without copying it. `exportBytesUnsafe()` returns a copy that is not wiped. |
| Sendable | These types are `@unchecked Sendable` because the C library locks internally: `ZxvContext`, `Capability`, `Budgets`, `PhaseClock`, `Attestations`, `Swarm`, `Model` and `SecretKey`. `Session` is also `@unchecked Sendable`: the C session handle must be used by one thread at a time, so the SDK refuses overlapping calls with `ZXV_E_STATE`. `Message` follows the C rule (not thread-safe) and is not `Sendable`. |

## The hexagram tag (ABI 1.3)

A `Hexagram` tags one unit of work with a four-valued base, the polarity
of its evidence (entropic, neutral, forbidden, negentropic) and its phase
(potential -> emerging -> manifest -> dissolving), packed into one byte
0..63. Its action is proceed, wait, refuse, escalate or rollback. Every rule
comes from libzxv and is tested against `vectors.json` key `hex`.
Forbidden is absorbing, a phase moves one step at a time, and the base is
**not** numbered like `Truth` (TRUE is base 3 but Truth 1): convert only
with the explicit `toTruth()` / `Hexagram(truth:polarity:phase:)`. Combining BOTH with NEITHER follows
the `Truth` tables (AND gives NEITHER, OR gives BOTH); those two results
are pending an owner decision. A `QuarantineLog` (needs the ATTEST
capability) records forbidden work in a SHA-256 hash chain; when it is
full, append is refused with `ZXV_E_CAPACITY` and you must halt the
context. See [ABI.md, Changes in 1.3](../docs/ABI.md#changes-in-13).

```swift
var h = Hexagram(base: .true, polarity: .negentropic, phase: .potential)
h.action                                   // .proceed
h = h.advance()                            // .emerging
try h.step(to: .dissolving)                // throws .refused (ZXV_E_STATE): a jump
let bad = h.forbid()                       // .rollback, forever
let log = try QuarantineLog(cap, capacity: 1024)   // needs .attest
let seq = try log.append(bad, reason: 7, submitter: 42, subject: digest32)
try log.verify()
```

`Hexagram` is a `Hashable`, `Sendable` value with `&`, `|` and prefix `~`.

## Hardware emulation and hardware updates (ABI 1.4)

Three parts, documented in [HW_EMULATION.md](../docs/HW_EMULATION.md):

- `HwProfile`: ZHP-1 machine profiles (plain data, no capability).
  `probeHost()` describes this machine, `reference()` is the public
  reference machine R1 (16 nodes), `present(host:target:)` marks each node
  HARD or SOFT, `match` / `pair` say what runs natively and what is
  emulated; `encode`, `decode`, `digest`, `cap`, `setCap`, `ext`,
  `setExtension`.
- `HwMatrix` (needs `.hwMatrix`): functions served by interchangeable
  implementations that serve only after reproducing the golden vectors.
  Every matrix starts with the standard engines (`HwEngine.sha256`,
  `keccak`, `mac`, `gcd`, `logic64`, `tensor`), so `call` works on any
  device. `providers()` lists every implementation (`HwProviderInfo`: kind,
  proof, contract, flags, power class, calls); `setPolicy` states placement
  (`.keyBearing`, `.localOnly`, latency, power), never an implementation.
  `addProvider(_:resource:kind:cost:) { op, input in ... }` registers a
  Swift implementation. Contract packages (`trustKey`, `applyContract`)
  need `.hwReconf`.
- `HwRegion` (needs `.hwReconf`): hardware updates as images signed with
  ML-DSA-87 AND SLH-DSA-SHAKE-256s (`Hw.keygen`, `Hw.buildImage`, both
  need `.cryptoSign`; signing takes seconds), staged, tested, committed or
  rolled back on an `HwDevice` you supply (`load`, `readback`, `activate`).
  Every step is an `onEvent` callback and a record in a hash-chained audit
  log (`log`, `logEntry`).

`CapabilitySet.all` stays the ABI 1.1 bits (`ZXV_CAP_ALL` never changes);
ask for `.all1_4` or add `.hwMatrix` / `.hwReconf`.

```swift
let cap = try ctx.rootCapability(.all1_4)
let mx = try HwMatrix(cap)
let digest = try mx.call(HwEngine.sha256, Array("abc".utf8))   // any machine
for p in try mx.providers() { print(p.fnId, p.kind as Any, p.proof) }

let key = try Hw.keygen(cap)                                 // secrets wiped on release
let img = try Hw.buildImage(cap, kind: .partial, targetClass: 0xD0, fnId: 0x200,
                            version: 1, svn: 1, payload: bitstream, key: key)
let rg = try HwRegion(cap, matrix: mx, provider: fabricIndex, regionId: 1,
                      targetClass: 0xD0, device: myDevice)
try rg.trustKey(key.publicKey)
try rg.onEvent { event, reason, version in audit(event, reason, version) }
try rg.stage(img); try rg.trial(ticks: 5); try rg.commit()
```

Callbacks (implementations, the device, events) run on the calling thread
inside libzxv; an error never crosses into C: a `ZxvError` returns its
status, any other error `ZXV_E_IO`. A matrix keeps its implementations
alive and a region keeps its matrix and device alive; capture the matrix
`unowned` inside its own implementations. `ZXV_E_NOPROVIDER`,
`E_DISPUTE`, `E_QUARANTINED`, `E_ROLLBACK`, `E_BLOCKED` and `E_POLICY` are
`.refused` (reason `.other`), `E_TARGET` is `.usage`; a full audit log
poisons the context (`.fatal`), since a change that cannot be recorded is
not made.

## API overview

| Type | Main members | Needs |
|---|---|---|
| `ZxvLibrary` | `version`, `features`, `fatalPolicy`, `ensureCompatible()`, `statusName(_:)` | - |
| `ZxvContext` | `rootCapability(_:processId:identity:)`, `enableCurrency(_:minorUnits:capability:)`, `registerPrivateUnit(_:minorUnits:capability:)`, `enabledCurrencies`, `halt`, `isPoisoned` | `.ledgerPost` for the currency calls |
| `Capability` | `attenuate`, `revoke`, `setAttestation`, `info`, `check`, `require` | - |
| `Rational` | `init(_:_:)`, `init(_ text:)`, `fromQ16`, `fromDoubleExact`, `+ - * /`, `adding`..., `toQ16`, `description` | - |
| `Truth` | `and`, `or`, `not`, `init(positive:negative:)`, `toBool`, `isCertainlyTrue`, `requireTrue` | - |
| `Ledger` | `openAccount`, `post`, `balance`, `journal`, `check`, `accountName`, `accountCurrency`, `accountKind`, `accountCount` | `.ledgerPost` / `.ledgerRead` |
| `Message` | `init(_:kind:)` with a capability, `set`, `setAmount`, `render` (pacs.008, camt.053) | `.ledgerRead` |
| `Attestations` | `attest`, `evidence`, `find`, `state`, `gate` | `.attest` |
| `Budgets` / `Budget` | `create`, `close`; `consume`, `consumeAsync`, `release`, `setLimit`, `status`, `onExhausted` | `.budget` |
| `PhaseClock` / `DeferredEvent` | `now`, `advance`, `admit`, `setHealth`, `defer(until:)`, `untilTick`, `nextDue`, `startRealtime`, `stopRealtime`, `close`; `awaitCleared`, `cancel`, `state` | `.phase` |
| `Ubh168Frame` | `tag`, `pack` (pure Swift), `sPlus`, `sMinus`, `resolve`, `idle`, `packNative` | - |
| `Swarm` | `register`, `setMood`, `setFeeling`, `beginCycle`, `consume`, `allotment`, `endCycle`, `levelCapacity`, `emotionProfile` | `.swarm` |
| `Hashes` | `sha3_256`, `keccak256`, `shake256`; also `randomBytes`, `hexString`, `bytes(hex:)` | - |
| `MlKem1024` | `keygen()`, `keygen(d:z:)`, `encapsulate(to:)`, `encapsulate(to:m:)`, `decapsulate` | `.cryptoKem` |
| `MlDsa87` | `keygen()`, `keygen(seed:)`, `sign(_:with:context:deterministic:)`, `verify` | `.cryptoSign` |
| `Ed25519` | `keygen`, `publicKey(of:)`, `sign`, `verify` | `.cryptoSign` |
| `Model` / `Session` | `init(_:path:tokenizer:)`, `init(_:modelBytes:)`, `info`, `tokenize`, `detokenize`, `newSession`, `loadLora`; `generate`, `generateAsync`, `stream`, `reset`, `position`, `bindBudget` | `.aiInference` |
| `HwProfile` | `probeHost`, `reference`, `present(host:target:)`, `decode`, `encode`, `digest`, `addNode`, `setCap`, `cap`, `setExtension`, `ext`, `nodes`, `match`, `pair` | - |
| `HwMatrix` | `call`, `shadow`, `providers`, `providerInfo`, `functionInfo`, `bound`, `setPolicy`, `addProvider`, `setTraits`, `addDependency`, `attest`, `memo`, `log`, `logEntry`; `trustKey`, `applyContract` | `.hwMatrix`; `.hwReconf` for contracts |
| `HwRegion` | `stage`, `trial`, `commit`, `rollback`, `tick`, `info`, `onEvent`, `log`, `logEntry` | `.hwReconf` |
| `Hw` | `keygen`, `buildImage`, `vectorList` | `.cryptoSign` |
| `StateChannel` | `encodeState`, `ledgerState` | `.evm` (+ `.ledgerRead`) |

## Examples

Awaiting a budget:

```swift
let budgets = try Budgets(root)
let tokens = try budgets.create("tokens", resource: .tokens, limit: 10)
try tokens.consume(10)
async let ok: Void = tokens.consumeAsync(2, timeout: .seconds(5))
try tokens.release(3)          // the waiter wakes, retries, succeeds
try await ok
```

Awaiting a phase tick, with the clock driven by hand or in real time:

```swift
let clock = try PhaseClock(root)
let event = try clock.defer(until: phaseCycle)       // 27,720 ticks
try clock.advance(phaseCycle)                        // manual driver
try await event.awaitCleared()
try clock.startRealtime(ticksPerSecond: 27_720)      // or one fundamental per second
```

Streaming tokens, for example into SwiftUI:

```swift
let model = try Model(root, path: modelURL.path)
let session = try model.newSession()
let opts = GenerateOptions(maxTokens: 64, temperature: try Rational(7, 10), seed: 42)
for try await token in session.stream("Hello", options: opts) {
    text += String(decoding: token.piece, as: UTF8.self)    // @MainActor-bound view model
}
```

Signing an agent message with a post-quantum signature:

```swift
let dsa = MlDsa87(try root.attenuate(.cryptoSign))
let keys = try dsa.keygen()                  // keys.secretKey is wiped in deinit
let sig = try dsa.sign(Array(message.utf8), with: keys.secretKey, context: Array("agent".utf8))
let ok = try dsa.verify(sig, of: Array(message.utf8), publicKey: keys.publicKey, context: Array("agent".utf8))
```

## Shipping libzxv in apps

### macOS: the ZXV desktop .app

`libzxv.dylib` is built with the install name `@rpath/libzxv.dylib`
(see `sdk/libzxv/Makefile`). To embed it in the app:

1. Build a universal library and sign it:
   ```sh
   make -C sdk/libzxv CC="clang -arch arm64 -arch x86_64"   # or build per arch and `lipo -create`
   codesign --force --sign "Developer ID Application: …" --options runtime sdk/libzxv/build/libzxv.dylib
   ```
2. In Xcode, add the dylib to the app target's **Frameworks, Libraries, and
   Embedded Content** with **Embed & Sign**. Xcode copies it to
   `ZXV.app/Contents/Frameworks/libzxv.dylib`.
3. Set **Runpath Search Paths** (`LD_RUNPATH_SEARCH_PATHS`) to
   `@executable_path/../Frameworks`. The Swift module links against `@rpath/libzxv.dylib` and
   finds the library there at launch.
4. Make the header visible when building with Xcode. Either add `sdk/libzxv/include` to **Header
   Search Paths**, or depend on this package locally. The `CZxv` shim finds the header from the repository layout.
5. Notarize the `.app` as usual. The hardened runtime accepts the embedded dylib
   because it is signed with the same team ID.

### iOS (mobile and edge): an xcframework

iOS apps cannot load a loose `.dylib`, so libzxv has to be packaged as an XCFramework.
Use a static library, which is simplest and lets the App Store strip unused code, or a framework. You need one slice per platform:

```sh
# one static libzxv.a per slice, built with the iOS SDK clang (-target …):
#   ios-arm64                      (devices)
#   ios-arm64_x86_64-simulator     (simulators, lipo'd)
#   macos-arm64_x86_64             (optional: one artifact for both apps)
xcodebuild -create-xcframework \
  -library build/ios/libzxv.a            -headers sdk/libzxv/include \
  -library build/ios-sim/libzxv.a        -headers sdk/libzxv/include \
  -library build/macos/libzxv.a          -headers sdk/libzxv/include \
  -output Zxv.xcframework
```

The include folder needs a `module.modulemap` with `module CZxv { header
"zxv_api.h" export * }` so that `import CZxv` resolves against the framework. With that in place, a
release `Package.swift` replaces the system library target with
`.binaryTarget(name: "CZxv", url: "…/Zxv.xcframework.zip", checksum: "…")`, and the
`Zxv` target is unchanged. This repository does not build that xcframework yet; see Honest limits.

On iOS, keep inference inside a `Task` and bound it with a token `Budget`, because the
engine is CPU-only integer code. The engine and the swarm budget are deterministic, so
results match the desktop app bit for bit.

## Tests

```sh
cd sdk/swift && swift test          # macOS CI and Linux alike
ASAN_OPTIONS=detect_leaks=0 swift test --sanitize=address
```

`--sanitize=address` passes all 36 tests. LeakSanitizer reports only
allocations made by the XCTest runner and the Swift runtime. None of them
come from `Sources/Zxv` or libzxv, which is why `detect_leaks=0` is set above.
`--sanitize=thread` is not supported here: ThreadSanitizer and the fork in the
abort-policy test do not mix. That test polls the child and skips if the
child never finishes.

The XCTest suite (`Tests/ZxvTests`) runs against the real library and reads
`sdk/conformance/vectors.json`. It covers:

- status names and classes, and how each status maps to an error type
- rational operations, parsing, IEEE-754 boundaries and Q16
- the lpres tables (in Swift and in C)
- UBH-168 frames byte for byte, from pure Swift and from C, plus idle frames
- SHA3, SHAKE and Keccak
- Ed25519, and the ML-DSA-87 and ML-KEM-1024 digests
- swarm allotments
- greedy and seeded generation tokens, through the sync, async and stream paths, including callback stop and budget stop
- the EVM state encoding and digest
- capability denial and revocation
- budget exhaustion, `consumeAsync` wake-ups on release and on a larger limit, timeout, cancellation and `close()`
- phase deferral with the manual and real-time drivers
- the fatal path through `zxv_ctx_halt`, including `.abort` in a forked child
- two-instance lockstep determinism
- the hardware group (ABI 1.4): profiles (R1, the host, presentation, the
  canonical round trip, extensions), the standard engines (SHA-256, HMAC,
  gcd), placement policy (key-bearing MAC never goes REMOTE), Swift
  implementations with proof, failover, shadow disputes, the memo and
  versioned contracts, and a signed region update (stage, trial, commit,
  replay refusal, audit log, contract package, fail closed on a full log)

If you keep the conformance files elsewhere, set `ZXV_CONFORMANCE_DIR` to their directory.

## Honest limits

- **Capabilities are not a sandbox.** Inside one process they are a discipline of least privilege. Code in
  the same address space can create its own root capability.
- **Secrets.** `SecretKey` wipes only its own buffer. It cannot wipe the Swift arrays you
  passed to `SecretKey(copying:)`, copies that the runtime or `Data` made, swapped
  pages, or core dumps. It does not `mlock` its buffer. The cryptography is not FIPS 140-3 validated.
- **Fatal errors can be caught.** Swift errors can always be caught. Only `fatalPolicy = .abort` is
  terminal. The C context stays poisoned whatever the policy.
- **Not tested on Apple platforms.** Everything here was built and tested on Linux with the Swift 6.0.3 toolchain.
  The macOS and iOS embedding steps above are described, not exercised. No xcframework is built yet.
- **`async` APIs use untyped throws.** They can also throw `CancellationError` and `ZxvCancelled`, so their throws are untyped. The synchronous APIs use typed throws.
- **Threads.** `stream` and `generateAsync` each use a dedicated `Thread`, because the C
  generation call blocks. `Budgets` waiters wake on the C listener. `close()` cancels them, and
  `deinit` cannot run while a waiter still holds the registry.
- **Not built in this libzxv.** Netting, card issuing and VSS are pending in the kernel and not wrapped here. Only the PAN
  check digit is available. `Model.loadLora` throws `.refused(.notImplemented)` while
  `Features.lora` is off.
- **Naming.** The entry point is `ZxvLibrary`, not `Zxv`, because a type with the module's name would
  break module-qualified names such as `Zxv.Model`.
