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
library whose major version is not 1 or whose minor version is below 5 (ABI 1.5 added the
treaty, cid/aipi, coin, fee, vhub, freight, web4 and paradox groups, 1.4 the hardware group,
1.3 the hexagram tag, 1.2 the capability-gated `*_cap` forms the SDK calls).

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

## ABI 1.5 groups

ABI 1.5 adds eight groups, each under its own Swift namespace matching a C
group. `ZxvLibrary.features2` (a `Features2` option set) reports which ones
are built. `CapabilitySet.all` keeps its 1.1 value and `.all1_4` its 1.4
value. Use `.all1_5` for every bit; it adds `.aipi`, `.mint` and `.paradox`.

The new statuses map to the existing error classes:

- `ZXV_E_DENOM` and `ZXV_E_TREATY` are `.usage`. `ZxvUsageError` has
  `isDenomination` and `isTreaty`, and a treaty error carries
  `treatyFault` (code, offset and `Treaty.Fault`).
- `ZXV_E_CID`, `E_CONSENT`, `E_ERASURE`, `E_PEG`, `E_LIMIT`,
  `E_PARADOX_STEP` and `E_PARADOX_OWNER` are `.refused`. Their reasons are
  `.cidMismatch`, `.consentDenied`, `.erasure`, `.pegRefused`,
  `.limitRefused`, `.paradoxStepLimit` and `.ownerApprovalRequired`.

The same rules hold in every group:

- **Results.** Variable-length results come back as `[UInt8]` or `String`;
  the two-call buffer pattern is hidden.
- **Value types.** Fixed byte structures are value types: `Cid`,
  `Freight.Header`, `Web4.ConsentToken`, `Paradox.RepairProof`,
  `Paradox.Draft` and `VHub.PegMessage`.
- **Handles.** Handles are final classes that keep their capability and
  context alive and release in `deinit`. `close()` releases a handle
  early; it is idempotent and waits for a call in progress. After that,
  use throws `.usage` (`ZXV_E_ARG`) and never reaches C.
- **Poisoned contexts.** Once the context halts, gated calls fail closed
  with `.fatal` (`ZXV_E_POISONED`).
- **Numbers.** 64-bit values stay 64-bit, and there is no floating point.
- **Callbacks.** The `Freight.expand` resolver runs inside libzxv. An
  error it throws is kept and rethrown by `expand`, so it never unwinds
  through C.

Each block below runs as written inside a throwing function after
`import Zxv`. `Tests/ZxvTests/ReadmeSnippetTests.swift` runs the same code
and checks the results its comments claim.

### Treaties (`Treaty`, pure)

A treaty has two canonical spellings of one batch of settlement work: a
COBOL copybook record (`record()`, 2542 bytes) and Sutra text (`sutra()`).
`run()` gives a deterministic report, not a payment. `Treaty.picAdd`,
`picSub`, `picMul` and `picDiv` provide PIC S9(18) arithmetic.

```swift
let t = try Treaty(sutra: "TREATY CORRIDOR-7 USD NET\nPARTY ACME 5000 4000 1/2\n"
                        + "PARTY BETA 3000 3000 1/2\nOWE ACME BETA 1200\nEND\n")
let same = try Treaty(record: try t.record())          // try same.isEqual(to: t) == true
let r = try t.run()                                     // r.legs(), r.parties(), r.record()
do { _ = try Treaty(sutra: "TREATY") } catch {
    if case .usage(let u) = error { print(u.treatyFault as Any) }   // .name at offset 0
}
let d = try Treaty.picDiv(-7, 2)                        // quotient -3, remainder -1
_ = (same, r, d)
```

### Content identifiers (`Cid`, pure)

These CIDs are byte-identical to Kubo's. `text()` gives Kubo's form
("b..." base32, "Qm..." for v0), and `text(base: .base58btc)` gives the
"z..." form. Other members are `info()`, `toV1()` and `matches(_:)`.

```swift
let c = try Cid.compute(Array("abc".utf8))              // CIDv1 raw sha2-256
let text = try c.text()                                 // "bafkrei..."
let back = try Cid(text: text)                          // == c
try c.verify(Array("abc".utf8))                         // .refused(.cidMismatch) on other bytes
let big = try Cid.unixfs([UInt8](repeating: 0, count: 300_000))   // dag-pb above 256 KiB
_ = (back, big)
```

### AIPI (`Aipi`)

Function, call and claim records (`encodeFunction`, `decodeFunction`,
`functionCid`, `encodeCall`, `callCid`, `encodeClaim`, `decodeClaim`,
`providerKey`) are pure. `Aipi.Node` is an offline block store that
needs `.aipi` and re-hashes every block it returns. It has no network,
so `plan` answers `.local` or `.unavailable`.

```swift
let ctx = try ZxvContext()
let cap = try ctx.rootCapability(.all1_5)
let node = try Aipi.Node(cap, storeBytes: 1 << 20, maxBlocks: 64)
let module = try node.put(Array("zxv module bytes".utf8))
let spec = Aipi.FunctionSpec(verify: .checker, fuelMax: 1_000_000, module: module,
                             entry: "main", checker: "check")
let fn = try node.publish(try Aipi.encodeFunction(spec))    // function CID
let call = try Aipi.callCid(fn, input: Array("hello".utf8))
let runsHere = try node.plan(fn) == .local                  // true: the module is held
_ = (call, runsHere)
```

### Fibonacci coins and swarm tokens (`Coin`, `SwarmToken`)

`Coin` holds the VFV coin ladder: faces are F(r+1) for rungs 1 to 45. Any
other face is `.usage` (`ZXV_E_DENOM`).

`SwarmToken` holds the 64 token tiers, their octane and their Fibonacci
codes. All of these are pure: `canonical`, `isCanonical`, `compress`,
`fuse`, `split`, `codePut` and `codeGet`.

`SwarmToken.Wallet` needs `.mint`. It keeps its holding canonical at its
octane and has no balance outside itself. Density carries no premium: a
denser holding is worth exactly the same.

```swift
let rungs = try Coin.split(100)                             // [10, 5, 3]: 89 + 8 + 3
let counts = try SwarmToken.canonical(value: 1000, octane: SwarmToken.Octane.midgrade)
var buf = [UInt8](repeating: 0, count: 16), bit: UInt32 = 0
try SwarmToken.codePut(&buf, bit: &bit, value: 100)         // bit == 11
let ctx = try ZxvContext()
let w = try SwarmToken.Wallet(try ctx.rootCapability(.mint), modelId: 7,
                              octane: SwarmToken.Octane.regular)
try w.mint(1000); try w.spend(1)                            // try w.info().value == 999
_ = (rungs, counts)
```

### The assurance fee (`Fee`, pure)

The fee is 8889 / 10^7 of the gross, rounded down. A `Fee.Carry` keeps the
remainder so that many small charges add up exactly. `split` gives the
four buckets: 50/25/15/10%, with every remainder unit going to the reserve
floor.

```swift
let f = try Fee.assure(1_000_000)                           // 888
var acct = Fee.Carry()                                      // store acct.remainder per account
for _ in 0..<8 { _ = try acct.charge(100) }
let parts = try Fee.split(f)                                // [445, 222, 133, 88]
let policy = try Fee.defaultPolicy()
_ = (parts, policy)
```

### The V-Bill hub (`VHub`)

This is a demonstration hub with test currencies only, so ISO 4217 codes
are refused. A peg is set only with the issuer's ML-DSA-87 signature over
`pegMessage`. Swaps (`quote`, `swap`) fail closed until each currency
has a limit rule (`addLimit`). The hub needs `.ledgerPost`, plus
`.ledgerRead` for reads.

```swift
let ctx = try ZxvContext()
let cap = try ctx.rootCapability(.all1_5)
let dsa = MlDsa87(cap)
let key = try dsa.keygen()
let hub = try VHub.Hub(cap)
try hub.addCurrency(slot: 0, code: "ZTA", minorUnits: 2, issuerPublicKey: key.publicKey,
                    poolFunding: 500_000, vbillBacking: 10_000)
let msg = try VHub.pegMessage(slot: 0, num: 350, den: 1, epoch: 1)
try hub.setPeg(slot: 0, num: 350, den: 1, epoch: 1, signature: try dsa.sign(msg.bytes, with: key.secretKey))
let acct = try hub.openAccount(slot: 0, owner: 50)
try hub.issue(account: acct, amount: 1_000_000)
let postings = try hub.check()                              // A = L + E, DEBIT == CREDIT, hash chain
_ = postings
```

### Freight (`Freight`, pure)

A freight is 168 rows of 21 bytes, and any k rows rebuild the payload
(Reed-Solomon). A corrupted or missing row set is `.refused(.erasure)` or
a verification failure. `Freight.Join` reassembles split members.

Op streams (`OpsWriter`, `encodeOps`, `peek`, `expand`, `generate`)
describe a payload by literals, copies, generators and CID references.
There are no compression promises: an op stream can be larger than its
output.

```swift
let payload = Array("any k rows rebuild the payload".utf8)
let h = try Freight.Header.forPayload(freightId: 42, k: 10, payload: payload)
let packets = try Freight.encode(h, payload: payload)       // 3528 bytes
let back = try Freight.decode(h, packets: Array(packets.suffix(210)))   // == payload
let ops = try Freight.OpsWriter(declaredTotal: 5, maxBytes: 4096)
    .literal(Array("<".utf8)).cidref(try Cid.compute(Array("abc".utf8)), length: 3)
    .literal(Array(">".utf8)).finish()
let out = try Freight.expand(ops, limit: 1024) { _, _ in Array("abc".utf8) }   // "<abc>"
_ = (back, out)
```

### Web 4 agents (`Web4`)

An agent's ML-DSA-65 key is held inside its handle. `close()` or `deinit`
wipes the key, and this SDK never logs it. The group covers signed cards,
manifests (at most `Web4.maxTools` = 16 tools), consent tokens, a
directory and envelopes.

There is no network transport: `seal` and `reply` produce bytes, and you
move them. Consent for money or personal data must be presented, and it
is single-use. `ConsentLog.prune` returns the number of entries remaining.

```swift
let ctx = try ZxvContext()
let cap = try ctx.rootCapability(.all1_5)
let now: UInt64 = 1_700_000_000_000
let alice = try Web4.Agent(cap, name: "alice", nowMs: now)
let human = try Web4.Agent(cap, name: "human", nowMs: now)
let m = try Web4.Manifest(version: 1, issuedMs: now, expiresMs: now + 86_400_000)
try m.addTool(Web4.Tool(name: "pay", priceVfv: 150, ratePerMin: 60, burst: 10, flags: .money, maxInput: 256))
try m.sign(with: alice)
let id = try alice.agentId()
let action = try Web4.actionDigest(tool: "pay", from: id, to: id, requestId: 7, payload: Array("amount=100".utf8))
let token = try human.issueConsent(agent: id, action: action, scope: .money, maxVfv: 1000,
                                   issuedMs: now, expiresMs: now + 600_000)
let log = try Web4.ConsentLog(capacity: 16)
try log.check(token, humanPublicKey: try human.publicKey(), agent: id, action: action,
              need: .money, amount: 500, nowMs: now + 1)
// A second check of the same token throws .refused(.consentDenied): single use.
```

### The paradox engine (`Paradox`)

The engine classifies a Both/Neither tag into one of four kinds and runs
the fixed, logged operator for that kind. Every operator needs
`.paradox`, and automatic changes happen only inside published bounds.
The operators are on `Paradox.Book`: `constructive`, `destructive`,
`neutral` (with `Hold`), `special`, `draft`/`review` and `pass` (with
`Graph`).

Policies flagged `.money` or `.device` always go to the owner
(`.refused(.ownerApprovalRequired)`), and `ownerOk` is your assertion.
Destructive work is contained, never answered.

```swift
let ctx = try ZxvContext()
let cap = try ctx.rootCapability(.all1_5)
let book = try Paradox.Book(cap, logCapacity: 64, registryCapacity: 16)
let c = try book.classify(Hexagram.all[39], evidence: Paradox.Evidence(pos0: 3, neg0: 1, pos1: 5, neg1: 1))
let policy = try Paradox.Policy(cap, lower: [0, 0, 10], upper: [100, 50, 20], initial: [50, 25, 15])
let o = try book.constructive(policy, kind: c.kind, tag: c.tag, synth: [60, 20, 12], tension: 5)
var state = [UInt8](repeating: UInt8(ascii: "X"), count: 16)
let k = try book.destructive(kind: .destructive, tag: Hexagram.all[35], state: &state,
                             checkpoint: [UInt8](repeating: UInt8(ascii: "A"), count: 16),
                             containment: Paradox.Containment(steps: 10, stepLimit: 8, spent: 3, budget: 100,
                                                              trace: [1, 2, 3, 2], invariantOk: true,
                                                              pattern: Array("loop".utf8), submitter: 7))
let ok = try k.proof?.verify() ?? false                     // state is "AAAA..." again
_ = (o, ok)
```

## Vino Embed (ABI 1.6)

ABI 1.6 adds one group, `Vino`: what an app or game needs to take part in
Vino without ever holding anyone's value or keys.
`ZxvLibrary.features2.contains(.vino)` reports it. `CapabilitySet.all1_6`
is every bit, adding `.vino`; `.all`, `.all1_4` and `.all1_5` keep their
values.

There are two lanes, and they never meet:

- **Play** (`Vino.Play`): a sandbox inside the app, with its own play
  units, issuers, holders and vouchers, in process memory only. Play units
  come from a faucet (`back`), are not money, are lost when the sandbox is
  closed, and can never be converted into, redeemed for or moved into live
  value: no call takes a play sandbox and anything live.
- **Live**: real Vino stays in the user's own ZXV wallet, never in the app.
  The app builds a payment request (`Vino.Request`); the user approves it
  in their wallet, which signs a receipt (`Vino.signReceipt`, the wallet's
  call); the app checks the receipt offline (`Vino.verifyReceipt`). The
  app never sees a balance, a key or a custody account.

A **voucher** is one play coin with a Fibonacci ladder face (1, 2, 3, 5,
8, ...). It is held by one holder on exactly one rail at a time:
`.bearer` (a note in the hand) or `.ghost` (its twin on the play ledger).
A transfer must name the active rail, so a voucher can never be spent
twice. **Coverage**: an issuer may issue only while
`10 * backing >= 18 * outstanding` (1.8x, exact integers). **Redeeming**
hands a voucher back to its issuer and retires it; nothing is paid out to
anyone, and there is no interest, yield or return of any kind.

The new statuses map to the existing error classes: `ZXV_E_RAIL`,
`E_COVERAGE`, `E_EXPIRED` and `E_CHANCE` are `.refused`, with reasons
`.railMismatch`, `.coverageRefused`, `.requestExpired` and
`.paidChanceRefused`; `ZXV_E_LANE` is `.usage` (`isLane`). `Rail`, `Lane`
and `AccountKind` are structs with named values (`.bearer`, `.live`,
`.issuer`, ...) rather than enums, so a raw value from elsewhere reaches
the library and is refused there rather than guessed at.

A play-lane shop: the issuer backs, issues a voucher to a player, the
player spends it at a friend and the friend redeems it.

```swift
let ctx = try ZxvContext()
let cap = try ctx.rootCapability(.vino)
let play = try Vino.Play(cap)
let shop = try play.openIssuer(owner: 1)           // account ids count up from 0
let player = try play.openHolder(owner: 100)
let friend = try play.openHolder(owner: 101)
try play.back(issuer: shop, amount: 18)            // play units from the faucet; worth nothing
let v = try play.issue(issuer: shop, holder: player, face: 8)   // 10*18 >= 18*8: allowed; id 1
do { try play.issue(issuer: shop, holder: player, face: 13) }   // 10*18 < 18*(8+13): refused
catch where error.refusal == .coverageRefused {}
try play.transfer(v, from: player, to: friend, rail: .bearer)   // spend it on its active rail
do { try play.transfer(v, from: friend, to: player, rail: .ghost) }
catch where error.refusal == .railMismatch {}      // not its rail: one coin, one rail
try play.redeem(v, holder: friend)                 // retired; nothing is paid out
let retired = try play.voucher(v).state == .redeemed   // true
let empty = try play.vouchers(friend).isEmpty          // true
try play.check()                                   // re-checks every invariant
```

A live payment request, a wallet receipt and an offline check. The wallet
part normally runs in the user's wallet (zxvd), not in the app; it is here
only so the example runs on its own.

```swift
// App side: a plain value, encoded to 160 canonical bytes. Its id is SHA-256 of them.
let req = Vino.Request(lane: .live,
                       merchant: [UInt8](repeating: 0x01, count: 32),   // the payee's id
                       nonce: try randomBytes(16),                      // unique per request
                       amount: 1_234_567,                               // VFV minor units
                       createdAt: 1_700_000_000,
                       expiresAt: 1_700_000_600,                        // ten minutes to approve
                       memo: "Sword of Dawn")                           // 0...64 printable ASCII
let encoded = try req.encode()                     // encoded.bytes (160), encoded.id (32)

// Wallet side (for this example only): approve with the wallet's ML-DSA-87 key.
let walletCtx = try ZxvContext()
let walletCap = try walletCtx.rootCapability(.cryptoSign)
let wallet = try MlDsa87(walletCap).keygen()
let sig = try Vino.signReceipt(walletCap, request: encoded.bytes, walletSecretKey: wallet.secretKey,
                               walletPublicKey: wallet.publicKey, approvedAt: 1_700_000_100)

// App side again, offline: no capability, no network.
let paid = try Vino.isReceiptValid(request: encoded.bytes, walletPublicKey: wallet.publicKey,
                                   approvedAt: 1_700_000_100, signature: sig, now: 1_700_000_200)   // true
let late = try Vino.isReceiptValid(request: encoded.bytes, walletPublicKey: wallet.publicKey,
                                   approvedAt: 1_700_000_100, signature: sig, now: 1_700_000_601)   // false
let same = try Vino.Request.decode(encoded.bytes)  // == req
```

A live request marking a paid chance outcome (`.chance`: a loot box or a
paid draw) is refused when it is encoded (`.paidChanceRefused`); the
raffle is a separate module, not a payment request. A deployment that sets
a per-device daily cap counts approvals with `Vino.SpendMeter`:

```swift
let ctx = try ZxvContext()
let cap = try ctx.rootCapability(.vino)
let meter = try Vino.SpendMeter(cap, dailyCap: 100)   // at most 100 per day index
let left = try meter.admit(day: 19_700, amount: 60)   // 40 left today
let d = try meter.tryAdmit(day: 19_700, amount: 41)   // admitted false, left 40: nothing counted
```

The meter is a counter, not a legal check, and makes no exemption claim.
`Play` and `SpendMeter` are internally serialized (any thread may call
them), keep their capability and context alive, and follow the same
`close()` rules as the ABI 1.5 handles. `VinoTests` runs these three
examples verbatim.

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
| `Vino.Play` | `open`, `openIssuer`, `openHolder`, `back`, `unback`, `issue`, `transfer`, `setRail`, `redeem`, `voucher`, `account`, `vouchers`, `check`, `close` | `.vino` |
| `Vino.Request` | `lane`, `flags`, `merchant`, `nonce`, `amount`, `createdAt`, `expiresAt`, `memo`; `encode`, `decode`, `decodeWithId` | - |
| `Vino` receipts | `receiptMessage`, `verifyReceipt`, `isReceiptValid` (pure); `signReceipt` (wallet side) | `.cryptoSign` to sign only |
| `Vino.SpendMeter` | `admit`, `tryAdmit`, `close` | `.vino` |

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

`--sanitize=address` passes all 74 tests. LeakSanitizer reports only
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
- the ABI 1.5 groups (`Abi15Tests`): every vector in the `status_abi15`,
  `treaty`, `cid`, `aipi`, `coin`, `fee`, `vhub`, `freight`, `web4` and
  `paradox` sections, including the refusal status names. The suite also
  covers closed-handle refusals and close during a call, the buffer-growth
  path, the error type for each new status, capability refusals per
  group, and a poisoned context.
- the README's ABI 1.5 examples (`ReadmeSnippetTests`)
- the vino group (ABI 1.6, `VinoTests`): every vector in the `status_abi16`
  and `vino` sections exactly (request bytes, ids and refusals, one-byte
  decode refusals, the receipt message, the deterministic signature and
  every verify check, the ordered play scenario with its final accounts,
  voucher lists and vouchers, and the meter scenario through `admit` and
  `tryAdmit`), plus closed handles, handles that outlive their context
  object, the voucher list's two-call path (600 vouchers), the reason for
  each new status, capability refusals (no `.vino`, revoked, unattested),
  a poisoned context and the README's Vino examples

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
- **ABI 1.5 limits.**
  - **No network transport.** AIPI nodes, freight and web4 envelopes
    produce and check bytes, and you move them.
  - **The vhub is a demonstration.** It uses test currencies, refuses
    ISO 4217 codes and is not a payment system.
  - **The owner decides.** Money and device policies, registry entries
    and drafts need the owner, and `ownerOk` is the caller's assertion.
  - **No compression or density promises.** Freight op streams make no
    compression promise, and token density carries no premium.
  - **Reports, not payments.** A treaty run is a report.
  - **The paradox engine only contains.** It contains, holds and routes
    contradictions, and detects only through the checks you run. It never
    acts against a sender.
  - **Out-of-range codes.** Swift enums cannot hold out-of-range C codes,
    so those inputs are refused at compile time instead of returning
    `ZXV_E_ARG`.
- **Vino Embed limits.** Play units are not money, are lost when the sandbox is closed, and can never
  become live value. The live flow here is the app's half only: approval happens in the user's wallet, which
  posts the payment on its own ledger. Receipts use the reference ML-DSA-87, not FIPS 140-3 validated.
  `SpendMeter` is a counter, not a legal check. Whether a given deployment of Vino is store credit, a
  prepaid access product or something else is a legal question pending counsel; this SDK does not decide it.
- **Naming.** The entry point is `ZxvLibrary`, not `Zxv`, because a type with the module's name would
  break module-qualified names such as `Zxv.Model`.
