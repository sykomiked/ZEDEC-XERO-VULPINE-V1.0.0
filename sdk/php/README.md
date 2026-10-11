<!-- Copyright (c) 2024-2026 Michael Laurence Curzi and 36N9 Genetics, LLC -->
<!-- SPDX-License-Identifier: Apache-2.0 -->

# zedec/zxv: the PHP SDK for ZXV

A small PHP 8.1+ wrapper of `libzxv`, the one C library every ZXV SDK shares
(`sdk/libzxv/include/zxv_api.h`, ABI 1.3), over PHP's FFI extension. It
follows `sdk/docs/SDK_DESIGN.md`, so its nouns match every other ZXV SDK.
Every file uses `declare(strict_types=1)`.

## Install

Requirements: PHP >= 8.1 with the FFI extension (`php -m | grep FFI`) and
`libzxv`. In the CLI, PHP's default `ffi.enable=preload` already allows FFI;
under php-fpm/Apache set `ffi.enable=1` (or preload the package).

```sh
composer require zedec/zxv           # or, without Composer:
require "/path/to/sdk/php/autoload.php";
```

The library is found, in order: `Zxv::loadLibrary($path)`,
`ZXV_LIBRARY_PATH`, a copy in this package's `lib/` directory, the
in-repository build `sdk/libzxv/build/`, then the system loader path. A
library whose ABI is not 1.x (x >= 3) is refused (the SDK calls the
`zxv_hex_*` functions added in ABI 1.3 and the `_cap` functions added in 1.2).

Tests run with plain PHP (no PHPUnit needed), against the real library and
`sdk/conformance/vectors.json`:

```sh
cd sdk/php
php tests/run.php            # or: composer test
php examples/tour.php
```

## Quickstart

```php
<?php
declare(strict_types=1);
require "vendor/autoload.php";
use Zedec\Zxv\{Context, Ledger};

$ctx = new Context();
$cap = $ctx->rootCapability();                     // every gated call carries a capability
$ctx->enableCurrency("NGN", cap: $cap);          // needs LEDGER_POST
$ledger = new Ledger($cap);
$bank = $ledger->openAccount("bank", "NGN", Ledger::ISSUER);
$ada = $ledger->openAccount("ada", "NGN");
$ledger->post($bank, $ada, 1_500_075, "NGN", "E2E-0001");   // exact kobo, never floats
echo $ledger->balance($ada)->equity, "\n";           // 1500075
```

## Concepts in PHP terms

**Exactness.** No public method takes or returns a `float`. Amounts are
`int` minor units; fractions are `Rational` (immutable, reduced, int64
numerator/denominator, arithmetic in C so overflow throws instead of
wrapping): `Rational::of(3, 4)`, `Rational::parse("12.75")`,
`Rational::from("0.1")`. `strict_types` only governs calls made from files
that declare it, so the SDK also checks at run time: wherever an amount is
accepted, a `float` throws `FloatRejectedException`. The one door for floats
is named so nobody walks through it by accident:
`Rational::fromDoubleExact(0.1)` returns the double's exact value,
`3602879701896397/36028797018963968`.

**Four-valued logic.** `Truth::neither()`, `Truth::true()`, `Truth::false()`,
`Truth::both()` (values 0..3 as the kernel numbers them; interned, compare
with `===`). `BOTH` is a held contradiction. PHP makes every object truthy,
so **`if ($truth)` is always taken, even for `Truth::false()`**. Use an
explicit policy: `isCertainlyTrue()`, `toBool(onBoth:, onNeither:)` or
`requireTrue()`. Logic is `and()`, `or()`, `not()` from libzxv's tables.
`Truth` is a class (not an enum) so the kernel's coming 2-bit `mode` fits;
`$truth->mode` is 0 today.

**Capabilities.** `$ctx->rootCapability(Capability::ALL)` creates a root;
`$cap->attenuate(Capability::LEDGER_READ)` a narrower child; `revoke()` kills
it and its descendants; `setAttestation(Truth::both())` blocks it. Every
object that makes gated calls (`Ledger`, `Budgets`, `PhaseClock`, `Swarm`,
`Attestations`, `Ed25519`, `MlDsa87`, `MlKem1024`, `Model`, `StateChannel`)
takes a capability in its constructor. There is no global capability.

Since ABI 1.2 every gated operation carries a capability:
`$ctx->enableCurrency("NGN", cap: $cap)` and
`$ctx->registerPrivateCurrency("VFV", 2, cap: $cap)` need `LEDGER_POST`, as
does `Ledger::openAccount`; `Ledger::account`, `accountCount` and `journal`
need `LEDGER_READ`; `new Message($cap, Message::PACS008)` needs
`LEDGER_READ` and re-checks it on every call; `Budgets`, `PhaseClock`,
`Attestations` and `Swarm` need `BUDGET`, `PHASE`, `ATTEST` and `SWARM` to be
constructed. A capability of another context throws
`CapabilityDeniedException`. The forms without a capability
(`$ctx->enableCurrency("NGN")`, `new Message($ctx, ...)`) still work and
raise `E_USER_DEPRECATED`.

**Errors.** Every negative status maps through `zxv_status_class`
(namespace `Zedec\Zxv\Exception`):

| Class | PHP type | Meaning |
|---|---|---|
| USAGE | `ZxvUsageException extends \InvalidArgumentException` (`FloatRejectedException` below it) | caller bug |
| REFUSED | `ZxvRefusedException extends \RuntimeException`; `CapabilityDeniedException`, `BudgetExhaustedException`, `InsufficientFundsException`, `VerificationFailedException`, `NotImplementedException` | handle it |
| FATAL | `ZxvFatalError extends \Error` (not `\Exception`) | context halted; never retry |
| SYSTEM | `ZxvSystemException extends \RuntimeException` | environment |

Each has `getStatus()`, `getStatusName()` and `getDetail()` (from
`zxv_last_error`). The non-fatal ones implement the `ZxvException` interface,
so `catch (ZxvException $e)` never catches a fatal.

**Fail closed.** After `ZXV_E_POISONED` / `ZXV_E_INVARIANT` (or
`$ctx->halt()`), the C context refuses gated calls, and the SDK marks its
`Context` dead and refuses every call on it and its children itself.
`ZxvFatalError` is an `\Error`, so `catch (\Exception $e)` does not swallow
it. For a truly uncatchable stop, `Zxv::setFatalPolicy(FatalPolicy::Abort)`
prints the error and ends the process with exit status 70 through libc
`_exit` (no `finally`, destructors or shutdown functions run).

**Budgets and async.** PHP has one thread, so async is cooperative, on
Fibers. `Budget::consume()` throws `BudgetExhaustedException`;
`Budget::consumeAsync($amount, timeoutMs:)` returns a `Future` that waits
while the budget is exhausted and retries each time a `release()` or larger
`setLimit()` replenishes it (the epoch moves); it fails at once on an invalid
amount; `cancel()` stops it. `Future::await()` inside a Fiber started by
`Loop::run()` suspends that Fiber so others run; outside a Fiber it drives
the loop itself. `onExhausted(fn)` sees every denied consume. Embed in
ReactPHP, Amp or Revolt by calling `Loop::tick()` from one of their timers.

**Phase ticks.** `PhaseClock::defer($tick)` returns a `DeferredEvent`;
`awaitCleared()` is a `Future` that completes when the clock passes the tick.
Drive the clock with `advance($n)` (tests, simulations) or
`startRealtime($ticksPerSecond)` (a `Loop` ticker reading `hrtime`). One
fundamental is `PhaseClock::CYCLE` = 27,720 ticks.

**Secrets.** Secret keys and shared secrets are `SecretKey`: native memory
allocated outside PHP's zval heap, wiped with `zxv_secure_zero` on `close()`
and in the destructor (PHP's reference counting runs it as soon as the last
reference goes). `__debugInfo()` never shows the bytes; they become a PHP
string only through `exportBytesUnsafe()`.

**Engine.** `Model::open($cap, $path, $tokenizer)`, `$model->newSession()`,
`$session->generate($prompt, new GenerateOptions(...), fn ($piece, $id) => ...)`
streams tokens to the callback (return `false` to stop). Sampling
parameters are exact (`temperature: Rational::of(7, 10)`), sent as Q16.16.
`Model::loadLora()` throws `NotImplementedException`: LoRA is not built into
libzxv yet (`ZXV_FEAT_LORA` is off).

**Wire frames.** `Ubh168Frame` packs and unpacks the 21-octet UBH-168
frame in pure PHP (`toBytes()`, `fromBytes()`), so a service can frame
without the native library; `nativeBytes()` cross-checks against C and
`resolve()` runs the C correlation resolver.

## The hexagram tag (ABI 1.3)

A `Hexagram` tags one unit of work with a four-valued base, the polarity
of its evidence (entropic, neutral, forbidden, negentropic) and its phase
(potential -> emerging -> manifest -> dissolving), packed into one byte
0..63. Its action is proceed, wait, refuse, escalate or rollback. Every rule
comes from libzxv and is tested against `vectors.json` key `hex`.
Forbidden is absorbing, a phase moves one step at a time, and the base is
**not** numbered like `Truth` (TRUE is base 3 but Truth 1): convert only
with the explicit `toTruth()` / `Hexagram::fromTruth(...)`. Combining BOTH with NEITHER follows
the `Truth` tables (AND gives NEITHER, OR gives BOTH); those two results
are pending an owner decision. A `QuarantineLog` (needs the ATTEST
capability) records forbidden work in a SHA-256 hash chain; when it is
full, append is refused with `ZXV_E_CAPACITY` and you must halt the
context. See [ABI.md, Changes in 1.3](../docs/ABI.md#changes-in-13).

```php
$h = Hexagram::of(HexBase::True, HexPolarity::Negentropic, HexPhase::Potential);
$h->action();                            // HexAction::Proceed
$h = $h->advance();                      // Emerging
$h->step(HexPhase::Dissolving);          // ZxvRefusedException (ZXV_E_STATE): a jump
$bad = $h->forbid();                     // Rollback, forever
$log = new QuarantineLog($cap, 1024);    // needs Capability::ATTEST
$seq = $log->append($bad, 7, 42, $digest32);
$log->verify();
```

The four field types are backed enums; tags are interned (compare with
`===`). Every PHP object is truthy, so never write `if ($h)`.

## API overview

| PHP (`Zedec\Zxv\...`) | Wraps | Main methods |
|---|---|---|
| `Zxv` | version, features | `abiVersion`, `features`, `hasFeature`, `loadLibrary`, `randomBytes`, `setFatalPolicy` |
| `Context` | `zxv_ctx` | `rootCapability`, `enableCurrency(alpha, mu, cap:)`, `registerPrivateCurrency(code, mu, cap:)`, `currency`, `halt`, `isPoisoned`, `close` |
| `Capability` | `zxv_cap` | `attenuate`, `revoke`, `check`, `allows`, `setAttestation`, `info` |
| `Rational` | `zxv_rat_*` | `of`, `parse`, `from`, `fromDoubleExact`, `add sub mul div`, `compare`, `equals`, `__toString`, `toQ16` |
| `Truth` | `zxv_lp_*` | `neither() true() false() both()`, `and`, `or`, `not`, `fromEvidence`, `isCertainlyTrue`, `toBool`, `requireTrue` |
| `Ledger`, `Balance` | `zxv_ledger_*` | `openAccount`, `post`, `balance`, `check`, `journal`, `account`, `accountCount` |
| `Iso4217`, `Currency` | ISO 4217 table | `lookup`, `byNumeric`, `name`, `codes`, `published` |
| `Message` | `zxv_msg_*` | `new Message($cap, kind)`, `set`, `amount`, `render` (pacs.008, camt.053) |
| `Attestations` | `zxv_lpres_*` | `attest`, `evidence`, `find`, `state`, `isGateClear` |
| `Budgets`, `Budget` | `zxv_rmag_*` | `budget`; `consume`, `consumeAsync`, `release`, `setLimit`, `status`, `onExhausted` |
| `PhaseClock`, `DeferredEvent` | `zxv_phase_*` | `now`, `advance`, `admit`, `setHealth`, `defer`, `nextDue`, `startRealtime`; `awaitCleared`, `cancel` |
| `Future`, `Loop` | host (Fibers) | `await`, `poll`, `cancel`, `isDone`; `run`, `spawn`, `tick`, `addTicker` |
| `Ubh168Frame` | `zxv_wire_*` | `tag`, `toBytes`, `fromBytes`, `nativeBytes`, `resolve`, `idle` |
| `Swarm` | `zxv_swarm_*` | `register`, `setMood`, `setFeeling`, `beginCycle`, `consume`, `allotment`, `endCycle`, `levelCapacity` |
| `Hashes` | SHA3, SHAKE, Keccak | `sha3_256`, `shake256`, `keccak256` |
| `Ed25519`, `MlDsa87`, `MlKem1024`, `KeyPair` | signatures, KEM | `generate`, `sign`, `verify`, `verifyOrThrow`; `encapsulate`, `decapsulate` |
| `SecretKey` | native secret | `close`, `bytesize`, `exportBytesUnsafe`, `fromBytesUnsafe` |
| `Model`, `Session`, `GenerateOptions`, `GenerateResult` | engine | `open`, `tokenize`, `detokenize`, `info`, `newSession`; `generate`, `reset`, `bindBudget` |
| `StateChannel`, `ChannelState` | `zxv_evm_*` | `encodeState`, `ledgerState` (for `sdk/solidity/ZxvStateChannel.sol`) |

## Examples

```php
// Exact rationals and the float boundary
echo Rational::parse("0.1")->add("0.2");      // 0.3, exactly
echo Rational::of(1, 3);                      // 1/3
Rational::of(0.5);                            // throws FloatRejectedException

// One Fiber waits for a budget, another replenishes it
$gpu = (new Budgets($cap))->budget("gpu", Budgets::COMPUTE_CYCLES, 10);
$gpu->consume(10);
$f = $gpu->consumeAsync(3, timeoutMs: 5000);
Loop::run(fn () => $f->await(), fn () => $gpu->release(3));

// Await a deferred event
$clock = new PhaseClock($cap);
$done = $clock->defer(PhaseClock::CYCLE)->awaitCleared();
$clock->advance(PhaseClock::CYCLE);
$done->await();

// Post-quantum signature
$dsa = new MlDsa87($cap);
$keys = $dsa->generate();
$sig = $dsa->sign($keys->secretKey, "agent message", "zxv");
$dsa->verify($keys->publicKey, "agent message", $sig, "zxv");   // true
$keys->close();                                                 // wipes the key
```

`examples/tour.php` runs all of this and more (engine, EVM state, the fatal
path).

## Honest limits

- **Capabilities are least privilege, not a sandbox.** Any PHP code in the
  process can create a root capability or call libzxv through FFI. The
  process boundary (zxvd over IPC) is where it becomes enforcement.
- **Truthiness.** Every `Truth` object is truthy in PHP and that cannot be
  changed: `if ($truth)` silently treats `FALSE`, `BOTH`, `NEITHER` as true.
- **`ZxvFatalError` is catchable** by `catch (\Throwable)` or
  `catch (\Error)`. The context stays dead either way; only
  `FatalPolicy::Abort` stops the process.
- **No parallelism.** PHP runs one thread; FFI calls (key generation,
  ML-DSA, `generate`) block it. `Future`/`Loop` give cooperative waiting on
  Fibers, not background work, and there is no `generateAsync`. Callbacks
  from C (`onExhausted`, token streaming) run on that same thread.
- **Integer range.** PHP ints are signed 64-bit: uint64 values (nonces,
  ticks, token counts) are limited to `PHP_INT_MAX`.
- **Secrets.** `SecretKey` memory is wiped on `close()`/destruction, but
  bytes you pass in as PHP strings (seeds, messages, `fromBytesUnsafe`) live
  in the PHP heap and are not wiped; the temporary native copies made of
  seeds are freed, not wiped. Swap and core dumps are outside the SDK's
  control.
- **Handle lifetime.** Child handles are freed by `close()` or when their
  `Context` closes; each child keeps its `Context` alive.
- **FFI declarations** live in `src/zxv.h` (PHP's FFI cannot read the real
  header's preprocessor lines) and must be kept in sync with `zxv_api.h`.
- **Not certified.** The cryptography is not FIPS 140-3 validated; libzxv
  serializes ISO 20022 but has no network transport. LoRA and the
  pacs.002/004/009, camt.056/029 builders are not built (they throw
  `NotImplementedException`); netting, card issuance and VSS are stubs in
  libzxv and are not wrapped yet.
