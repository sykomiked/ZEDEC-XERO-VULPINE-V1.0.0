<!-- Copyright (c) 2024-2026 Michael Laurence Curzi and 36N9 Genetics, LLC -->
<!-- SPDX-License-Identifier: Apache-2.0 -->

# ZXV SDK for the JVM

Java bindings for `libzxv`, the one C surface of the ZXV platform
(`sdk/libzxv/include/zxv_api.h`, ABI 1.4). It is built on the Java
**Foreign Function & Memory API** (`java.lang.foreign`, final since Java 22),
not JNI: there is no glue library to compile, and downcalls, upcalls and
off-heap memory are plain Java. Kotlin and Scala use it directly.

- Module `org.zedec.zxv`, packages `org.zedec.zxv` (core), `.ledger`,
  `.trust`, `.budget`, `.phase`, `.wire`, `.swarm`, `.crypto`, `.engine`, `.evm`.
- Requires Java 22 or later (built with `--release 22`, tested on JDK 25)
  and a 64-bit JVM.
- No runtime dependencies.

## Install

Build the jar (the C library must already be built in `sdk/libzxv/build`):

```sh
export JAVA_HOME=/path/to/jdk-22-or-later
cd sdk/java
mvn package                 # compiles, runs the tests, writes target/zxv-1.3.0.jar
mvn install                 # optional: puts org.zedec:zxv:1.3.0 in ~/.m2
```

```xml
<dependency>
  <groupId>org.zedec</groupId>
  <artifactId>zxv</artifactId>
  <version>1.3.0</version>
</dependency>
```

When `sdk/libzxv/build/libzxv.so` exists, the build bundles it in the jar
under `/native/linux-x86_64/`. Set `-Dzxv.native.platform=` (and point
`zxv.native.dir` at the right build) to bundle another platform.

FFM calls into native code are "restricted": grant access when you run.

```sh
java --enable-native-access=org.zedec.zxv  -p zxv-1.3.0.jar ...   # module path
java --enable-native-access=ALL-UNNAMED    -cp zxv-1.3.0.jar ...  # class path
```

### Where the library comes from

On first use the SDK loads `libzxv` from the first of:

1. the path passed to `Zxv.load(Path)` (call it before anything else);
2. the `zxv.library` system property;
3. the `ZXV_LIBRARY_PATH` environment variable (a file, or a directory holding it);
4. the copy bundled in the jar under `/native/<os>-<arch>/`, extracted to a temp file;
5. `System.mapLibraryName("zxv")` (`libzxv.so`, `libzxv.dylib`, `zxv.dll`) on the system loader path.

It refuses a library whose ABI major is not 1 or whose minor is below 4
(it calls the `zxv_hw_*` functions added in ABI 1.4, the `zxv_hex_*`
functions added in 1.3 and the `_cap` functions added in 1.2).
`Zxv.libraryLocation()` tells you which one won.

## Quickstart

```java
try (ZxvContext ctx = ZxvContext.create();
     Capability cap = ctx.rootCapability(Permission.all())) {
    ctx.enableCurrency(cap, "NGN");
    Ledger ledger = Ledger.of(cap);
    Account bank = ledger.openAccount("bank", "NGN", AccountKind.ISSUER);
    Account ada  = ledger.openAccount("ada",  "NGN", AccountKind.HOLDER);
    ledger.post(bank, ada, 150_00, "NGN", "E2E-0001");          // 150.00 NGN, exact
    System.out.println(ledger.balance(ada).equity());             // 15000
    System.out.println(Rational.parse("0.1").add(Rational.parse("0.2")));  // 0.3
}
```

A longer runnable tour is `examples/Quickstart.java`:

```sh
mvn -q package -DskipTests
java --enable-native-access=org.zedec.zxv -p target/zxv-1.3.0.jar --add-modules org.zedec.zxv \
     examples/Quickstart.java ../conformance/fixtures
```

## Concepts, in Java terms

**Lifetime.** Every native handle is an `AutoCloseable`: `ZxvContext`,
`Capability`, `Budgets`, `PhaseClock`, `Swarm`, `Attestations`, `Model`,
`Session`, `SecretKey`. Use try-with-resources. Closing a parent closes what
was made from it first (a context closes its capabilities, budgets, models;
a model its sessions; a capability the capabilities attenuated from it), and
`close()` waits for calls in flight on that handle, so a handle is never
freed under a running call. Calls on a closed handle throw
`IllegalStateException`. There is no finalizer for these handles: an
unclosed context is a leak.

**Capabilities.** Every object that makes gated calls holds a `Capability`
and passes it on every call; libzxv checks it every time (live, permission
held, attestation `TRUE`). Make one root capability and hand narrower ones
to the parts that need less:

```java
Capability teller = root.attenuate(Permission.LEDGER_READ, Permission.LEDGER_POST);
Ledger.of(teller).post(...);           // ok
MlDsa87.with(teller).generateKeyPair(); // CapabilityDeniedException
root.revoke();                          // teller is revoked too
```

Since ABI 1.2 every gated operation takes a capability: currencies
(`ctx.enableCurrency(cap, "NGN")`, `ctx.registerPrivateCurrency(cap, "VFV", 2)`)
and opening accounts need `LEDGER_POST`; `Ledger.account/accounts/journal`
need `LEDGER_READ`; `Budgets.create`, `PhaseClock.create`,
`Attestations.create` and `Swarm.create` need `BUDGET`, `PHASE`, `ATTEST`
and `SWARM`. A capability from a different context is refused with
`CapabilityDeniedException` (`ZXV_E_CAP`). The 1.0 forms without a
capability (`ctx.enableCurrency("NGN")`, ...) still work and are
`@Deprecated(since = "1.2")`.

**Exactness.** No public method takes or returns `float`/`double` except
`Rational.fromDoubleExact(double)`, which returns the double's exact value
(`0.1` is `3602879701896397/36028797018963968`). Money is `long` minor
units. Everything else that is fractional is a `Rational`: build it with
`Rational.of(3, 4)`, `Rational.parse("12.75")` or `Rational.from(BigDecimal)`.
Overflow raises `ZxvUsageException` (`ZXV_E_RANGE`); nothing wraps or rounds.
Engine parameters are `Rational`s converted to Q16.16 (`toQ16()`).

**Four-valued logic.** `Truth` is an enum `NEITHER(0)`, `TRUE(1)`,
`FALSE(2)`, `BOTH(3)` with `and`/`or`/`not` from the kernel tables. There is
no `toBoolean()` without a policy: `isCertainlyTrue()`,
`toBoolean(onBoth, onNeither)`, or `requireTrue()`.

**Errors.** Every negative status becomes an exception carrying `status()`,
`statusName()` and `detail()` (the `zxv_last_error` text, read on the
calling thread right after the call):

| Class | Java type | When |
|---|---|---|
| USAGE | `ZxvUsageException` (`FloatRejectedException`) | caller bug; do not retry |
| REFUSED | `ZxvRefusedException`: `CapabilityDeniedException`, `BudgetExhaustedException`, `InsufficientFundsException`, `VerificationFailedException`, `NotImplementedException` | handle it |
| FATAL | `ZxvFatalError extends Error` | context halted; never retry |
| SYSTEM | `ZxvSystemException` | I/O, memory, internal |

Signature checks return `boolean` (`false` on `ZXV_E_VERIFY`) because that is
what a verifier is for.

**Fail-closed.** When an invariant breaks (or an operator calls
`ctx.halt(reason)`), libzxv poisons the context; the SDK marks it dead and
throws `ZxvFatalError`, which `catch (Exception e)` does not catch. Every
later call on the context or anything made from it throws it again, without
reaching C. `ZxvContext.create(FatalPolicy.ABORT)` instead halts the JVM
(`Runtime.halt(70)`): no `finally`, no shutdown hooks.

**Secrets.** `SecretKey` holds Ed25519 seeds, ML-DSA-87 secret keys,
ML-KEM-1024 decapsulation keys and shared secrets in off-heap memory from a
shared `Arena`. `close()` wipes it with `zxv_secure_zero` and frees it; a
`java.lang.ref.Cleaner` does the same for a key that became unreachable
unclosed. Key pairs and encapsulations are `AutoCloseable` records that close
their secret. The only heap copy is `exportBytesUnsafe()`.

**Zero copy.** Byte buffers can be `MemorySegment`s: `Hashes.sha3_256(seg)`
reads heap or native memory in place (pure functions are linked as
critical), `Ubh168Frame.writeTo(seg, offset)` and `parse(seg)` frame directly
in native buffers, and `Ed25519.sign(sk, msgSeg, sigSeg)` signs into native
memory.

**Async.** `Budget.consumeAsync(amount[, timeout])` returns a
`CompletableFuture<Void>` that completes when the consume fits, waking on
each replenishment (a `release` or a larger limit); cancel it to stop
waiting. `PhaseClock` has a manual driver (`advance(n)`, deterministic) and a
real-time one (`startRealtime(ticksPerSecond)` on a
`ScheduledExecutorService`); `DeferredEvent.awaitCleared()` and
`untilTick(t)` are futures. `Session.generateAsync` runs on a virtual thread.

**Streaming generation.** The engine calls back once per token through an
FFM upcall stub. Use it as a callback (`generate(prompt, opts, listener)`,
return `false` to stop), as a `Stream<Token>` (`stream(...)`, close it to
stop), as a `Flow.Publisher<Token>` with back-pressure (`publisher(...)`),
or as a `CompletableFuture<GenerateResult>` (`generateAsync(...)`). An
exception in your listener stops generation and is rethrown; it never
crosses into C.

## The hexagram tag (ABI 1.3)

A `Hexagram` tags one unit of work with a four-valued base, the polarity
of its evidence (entropic, neutral, forbidden, negentropic) and its phase
(potential -> emerging -> manifest -> dissolving), packed into one byte
0..63. Its action is proceed, wait, refuse, escalate or rollback. Every rule
comes from libzxv and is tested against `vectors.json` key `hex`.
Forbidden is absorbing, a phase moves one step at a time, and the base is
**not** numbered like `Truth` (TRUE is base 3 but Truth 1): convert only
with the explicit `toTruth()` / `Hexagram.fromTruth(...)`. Combining BOTH with NEITHER follows
the `Truth` tables (AND gives NEITHER, OR gives BOTH); those two results
are pending an owner decision. A `QuarantineLog` (needs the ATTEST
capability) records forbidden work in a SHA-256 hash chain; when it is
full, append is refused with `ZXV_E_CAPACITY` and you must halt the
context. See [ABI.md, Changes in 1.3](../docs/ABI.md#changes-in-13).

```java
Hexagram h = new Hexagram(HexBase.TRUE, HexPolarity.NEGENTROPIC, HexPhase.POTENTIAL);
h.action();                              // PROCEED
h = h.advance();                         // EMERGING
h.step(HexPhase.DISSOLVING);             // ZxvRefusedException (ZXV_E_STATE): a jump
Hexagram bad = h.forbid();               // ROLLBACK, forever
try (QuarantineLog log = QuarantineLog.create(cap, 1024)) {   // needs ATTEST
    long seq = log.append(bad, 7, 42, digest32);
    log.verify();
}
```

`org.zedec.zxv.trust.QuarantineLog`; `Hexagram`, `HexBase`, `HexPolarity`,
`HexPhase` and `HexAction` are in `org.zedec.zxv`.

## Hardware emulation and hardware updates (ABI 1.4)

Package `org.zedec.zxv.hw` wraps the 55 `zxv_hw_*` functions
([HW_EMULATION.md](../docs/HW_EMULATION.md), [HW_PROFILE.md](../docs/HW_PROFILE.md)).
Everything is integer; uint32 values travel as `int` bits.

- **Profiles** (`HwProfile`, plain data, no capability): `probeHost()`, the
  reference machine R1 (`reference()`, 16 nodes), `present(host, r1)`,
  `match`/`pair`, `encode`/`decode`/`digest`, nodes, capabilities
  (`HwCapKey`, `HwClass`) and the opaque per-node extension.
- **Matrix** (`HwMatrix`, needs the new `HW_MATRIX` permission): every
  matrix starts with the standard engines (`HwFunction.SHA256`, `KECCAK`,
  `MAC`, `GCD`, `LOGIC64`, `TENSOR`), each served by a proven SOFT
  implementation. Discovery (`providers()`, `functionInfo`, `bound`),
  placement (`setPolicy`, `setTraits`; key-bearing work never goes
  REMOTE), `call`/`shadow`/`memo`, versioned contracts, and your own
  implementations as Java lambdas (`addProvider`, proven by `attest`).
- **Signed packages** (`crypto.HwPackageSigner`, needs `CRYPTO_SIGN`):
  release keys (`generateKeyPair()`; both secret keys are off-heap
  `SecretKey`s, wiped on close) and images or contract packages signed with
  ML-DSA-87 **and** SLH-DSA-SHAKE-256s. SLH-DSA signing takes about two
  seconds: it is a release step.
- **Regions** (`HwRegion`, needs the new `HW_RECONF` permission): stage,
  trial, commit, rollback, tick, info; every step is an `HwEvent` sent to
  your listener and a record in a hash-chained audit log (`log()`,
  `logEntry(i)`, also on the matrix for contract changes). The device is
  three callbacks (`HwRegion.Device`: load, read back, activate).

```java
try (HwMatrix mx = HwMatrix.create(cap)) {                       // HW_MATRIX
    byte[] d = mx.call(HwFunction.SHA256, 0, "abc".getBytes(UTF_8)); // ba7816bf...
    int fabric = mx.addProvider(fnId, resId, HwKind.FABRIC, 1, (op, in) -> device.run(in));
    HwPackageSigner signer = HwPackageSigner.with(cap);            // CRYPTO_SIGN
    try (HwPackageSigner.KeyPair key = signer.generateKeyPair();
         HwRegion rg = HwRegion.create(cap, mx, fabric, 1, 0xD0, null, device, 64)) { // HW_RECONF
        rg.trustKey(key.publicKey());
        rg.onEvent((region, event, reason, version) -> log.info(event + " v" + version));
        rg.stage(signer.buildImage(HwPackageSigner.ImageKind.PARTIAL, 0xD0, null, fnId, 1, 1, bitstream, key));
        rg.trial(5);                                                // proven against the golden vectors
        rg.commit();                                                // permanent; floor raised
    }
}
```

Callbacks (implementations, the device, the event listener) are FFM upcall
stubs owned by the matrix or region and freed after it; they run on the
calling thread inside the native call. An exception never crosses into C:
a `ZxvException` thrown by an implementation or device becomes its status
(so a device can report `ZXV_E_IO`), anything else is rethrown from the
call that ran it, and a listener's exception goes to the thread's
uncaught-exception handler. New statuses (`ZXV_E_NOPROVIDER` -25 through
`ZXV_E_POLICY` -31) map by class like every other: `ZXV_E_TARGET` is
USAGE, the rest REFUSED. `Permission.all()` now includes the two 1.4 bits.

## API overview

| Type | Package | What it does |
|---|---|---|
| `Zxv` | core | `load`, `version`, `features`, `statusName`, `statusClass` |
| `ZxvContext` | core | create, `rootCapability`, `enableCurrency(cap, ...)`, `registerPrivateCurrency(cap, ...)`, `currency`, `enabledCurrencies`, `halt`, `isPoisoned` |
| `Capability`, `Permission` | core | `attenuate`, `revoke`, `setAttestation`, `allows`, `require`, `info` |
| `Rational` | core | exact `add`/`subtract`/`multiply`/`divide`/`compareTo`, `parse`, `from(BigDecimal)`, `fromDoubleExact`, `toQ16` |
| `Truth` | core | four-valued `and`/`or`/`not`, `fromEvidence`, explicit-policy conversions |
| `FatalPolicy`, `ZxvException` family, `ZxvFatalError` | core | errors |
| `Ledger`, `Account`, `Balance`, `JournalEntry`, `Iso4217` | `ledger` | accounts, `post`, `balance`, `journal`, `check`; ISO 4217 table |
| `Attestations`, `Subject` | `trust` | `attest`, `evidence`, `state`, `find`, `isGateClear` |
| `Budgets`, `Budget`, `Resource` | `budget` | `consume`, `tryConsume`, `consumeAsync`, `release`, `setLimit`, `status`, `onExhausted` |
| `PhaseClock`, `DeferredEvent` | `phase` | `now`, `advance`, `admit`, `setHealth`, `defer`, `untilTick`, `startRealtime` |
| `Ubh168Frame`, `WireTruth` | `wire` | pure-Java UBH-168 `of`/`parse`/`idle`/`toBytes`/`writeTo`, `resolve` |
| `Swarm` | `swarm` | `register`, `setMood`, `setFeeling`, `beginCycle`, `consume`, `allotment`, `endCycle` |
| `Hashes` | `crypto` | SHA3-256, SHAKE256, Keccak-256, `random` |
| `Ed25519`, `MlDsa87`, `MlKem1024`, `SecretKey` | `crypto` | key generation (random and deterministic), sign/verify, encapsulate/decapsulate |
| `Model`, `Session`, `GenerateOptions`, `GenerateResult`, `Token` | `engine` | `open`, `tokenize`, `newSession`, `generate`/`stream`/`publisher`/`generateAsync`, `bindBudget`, `loadLora` |
| `StateChannel`, `Party`, `EvmState` | `evm` | `encodeState`, `ledgerState` (ABI encoding and keccak digest) |
| `HwProfile`, `HwClass`, `HwCapKey` | `hw` | ZHP-1 `probeHost`, `reference`, `present`, `match`, `pair`, `encode`/`decode`/`digest`, caps, extensions |
| `HwMatrix`, `HwFunction`, `HwKind`, `HwVector` | `hw` | standard engines, `call`, `shadow`, `memo`, `addProvider`, `attest`, `providers`, `setPolicy`, `setTraits`, `applyContract`, `log` |
| `HwRegion`, `HwEvent`, `HwAuditRecord` | `hw` | `stage`, `trial`, `commit`, `rollback`, `tick`, `info`, `onEvent`, `log`, `logEntry` |
| `HwPackageSigner` | `crypto` | release keys, `buildImage`, `buildContract` (ML-DSA-87 + SLH-DSA) |

## Examples

Budgets that wait instead of failing:

```java
try (Budgets budgets = Budgets.create(cap)) {
    Budget gpu = budgets.budget("gpu", Resource.COMPUTE_CYCLES, Rational.of(100), true);
    gpu.onExhausted(b -> log.info("{} is exhausted", b));
    gpu.consumeAsync(Rational.of(30), Duration.ofSeconds(2))
       .thenRun(() -> runJob())
       .exceptionally(e -> { log.warn("no budget in time"); return null; });
}
```

Deterministic generation, streamed, charged to a token budget:

```java
try (Model model = Model.open(cap, Path.of("model.gguf"), null);
     Session chat = model.newSession();
     Budgets budgets = Budgets.create(cap)) {
    chat.bindBudget(budgets.budget("tokens", Resource.TOKENS, Rational.of(512), true));
    var opts = GenerateOptions.builder().maxTokens(64)
            .temperature(Rational.parse("0.7")).topK(40).seed(42).build();
    try (Stream<Token> tokens = chat.stream("Hello", opts)) {
        tokens.forEach(t -> System.out.print(t.text()));
    }
}
```

Post-quantum agent message signing and key exchange:

```java
MlDsa87 dsa = MlDsa87.with(cap);
try (MlDsa87.KeyPair kp = dsa.generateKeyPair()) {
    byte[] sig = dsa.sign(kp.secretKey(), message, "agent".getBytes(UTF_8));
}
MlKem1024 kem = MlKem1024.with(cap);
try (var kp = kem.generateKeyPair();
     var enc = kem.encapsulate(kp.encapsulationKey());
     SecretKey ss = kem.decapsulate(kp.decapsulationKey(), enc.ciphertext())) {
    assert ss.contentEquals(enc.sharedSecret());
}
```

Phase ticks in a simulation and in real time:

```java
try (PhaseClock clock = PhaseClock.create(cap)) {
    clock.defer(PhaseClock.CYCLE).awaitCleared().thenRun(() -> System.out.println("one fundamental"));
    clock.advance(PhaseClock.CYCLE);                       // runs the callback here
    try (var driver = clock.startRealtime(27_720)) {       // one fundamental per second
        clock.untilTick(clock.now() + 2 * PhaseClock.CYCLE).join();
    }
}
```

### Kotlin

```kotlin
import org.zedec.zxv.*
import org.zedec.zxv.ledger.*

fun main() {
    ZxvContext.create().use { ctx ->
        ctx.rootCapability(Permission.all()).use { cap ->
            ctx.enableCurrency(cap, "NGN")
            val ledger = Ledger.of(cap)
            val bank = ledger.openAccount("bank", "NGN", AccountKind.ISSUER)
            val ada = ledger.openAccount("ada", "NGN", AccountKind.HOLDER)
            ledger.post(bank, ada, 150_00, "NGN", "E2E-0001")
            println(ledger.balance(ada).equity)            // records expose properties

            val half = Rational.of(1, 2)
            println(half.add(Rational.parse("0.25")))       // 0.75

            when (Truth.TRUE.and(Truth.BOTH)) {             // an exhaustive when over four states
                Truth.TRUE -> println("act")
                Truth.FALSE -> println("refuse")
                Truth.BOTH -> println("contradiction: escalate")
                Truth.NEITHER -> println("wait for evidence")
            }
        }
    }
}

// Coroutines: CompletableFuture.await() from kotlinx-coroutines-jdk8
// suspend fun spend(b: Budget) = b.consumeAsync(Rational.of(2)).await()
```

### Scala

```scala
import scala.util.Using
import org.zedec.zxv.*
import org.zedec.zxv.ledger.*

@main def run(): Unit =
  Using.Manager { use =>
    val ctx = use(ZxvContext.create())
    val cap = use(ctx.rootCapability(Permission.all()))
    ctx.enableCurrency(cap, "NGN")
    val ledger = Ledger.of(cap)
    val bank = ledger.openAccount("bank", "NGN", AccountKind.ISSUER)
    val ada  = ledger.openAccount("ada", "NGN", AccountKind.HOLDER)
    ledger.post(bank, ada, 15000L, "NGN", "E2E-0001")
    println(ledger.balance(ada).equity())

    Truth.TRUE.and(Truth.BOTH) match
      case Truth.TRUE    => println("act")
      case Truth.BOTH    => println("contradiction: escalate")
      case _             => println("do not act")
  }.get   // rethrows; note ZxvFatalError is an Error, which Using does not swallow either
```

## Build and test

```sh
export JAVA_HOME=/opt/tools/jdk-25.0.4.1+1    # any JDK 22+
mvn test        # 53 tests against the real libzxv and sdk/conformance/vectors.json
```

Surefire runs the tests on the class path with
`--enable-native-access=ALL-UNNAMED -Dzxv.library=../libzxv/build/libzxv.so`.
They check the conformance vectors (status classes, rationals, IEEE-754
boundary, Q16, the lpres tables in Java and C, UBH-168 frames byte for byte
in pure Java and in C, hashes, Ed25519, ML-DSA-87 and ML-KEM-1024 digests,
swarm allotments, greedy and seeded generation tokens, EVM encoding and
digest), and the behaviour around them: capability denial and revocation,
contradictory attestations, budget exhaustion, `consumeAsync` waking,
timing out and cancelling, phase deferral (manual and real time), every
streaming form, budget-bound generation, LoRA's `NotImplementedException`,
the fatal path through `zxv_ctx_halt`, `FatalPolicy.ABORT` in a child JVM,
and ABI 1.4 (`HwTest`): R1, the standard engines (SHA-256 of "abc", HMAC
RFC 4231), key-bearing MAC refusing a REMOTE implementation, power and
latency placement, Java implementations with failover, nested calls,
shadow disputes and the memo, and a signed update staged, tried and
committed on a simulated two-slot device with its events, audit log and a
contract package (three SLH-DSA signatures, about six seconds).

## Honest limits

- **Capabilities are least privilege, not a sandbox.** Inside one JVM, any
  code that can reach a `ZxvContext` can mint a root capability. Isolation
  needs a process boundary.
- **Secrets.** Keys generated by the SDK never touch the heap, but bytes you
  pass in as `byte[]` (`SecretKey.copyOf`, seeds) were on the heap first and
  the garbage collector may have copied them; overwrite your arrays. The SDK
  cannot prevent swapping or core dumps of native memory, and the `Cleaner`
  backstop runs only when the GC notices the key is unreachable.
- **Not validated crypto.** ML-DSA-87, ML-KEM-1024 and Ed25519 come from
  libzxv, which is not FIPS 140-3 validated.
- **Fatal is as strong as the JVM allows.** `ZxvFatalError` is an `Error`,
  so `catch (Throwable t)` still catches it; the context stays poisoned
  regardless. Only `FatalPolicy.ABORT` is uncatchable.
- **Thread-local error text.** `detail()` is read from libzxv's thread-local
  last error immediately after each call. Virtual threads only unmount at
  blocking points, which the SDK never has between the call and that read,
  but this relies on that JVM behaviour.
- **Sessions are serial.** One generation at a time per `Session` (others
  wait); a `Stream` you abandon without closing keeps its producer virtual
  thread (and the session) blocked, so always close it.
- **Callbacks run inside native calls.** `TokenListener`s and
  `Budget.onExhausted` handlers run on the thread inside libzxv; do not close
  the handle they belong to from inside them (the SDK refuses with
  `IllegalStateException`).
- **Not wrapped yet.** ISO 20022 message rendering, netting cycles, cards
  and VSS checks from `zxv_api.h` are not exposed by this SDK version
  (several are `NOTIMPL` stubs in the library anyway). LoRA is declared but
  raises `NotImplementedException` while `Feature.LORA` is off.
- **Bundled binaries.** The build bundles only the platform it runs on;
  multi-platform jars need each platform's `libzxv` built and added under
  `/native/<os>-<arch>/`.
- **No network transport.** Like libzxv, this SDK serializes and keeps an
  in-process ledger; it does not connect to any payment scheme.
