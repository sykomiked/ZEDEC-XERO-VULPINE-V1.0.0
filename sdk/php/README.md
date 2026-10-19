<!-- Copyright (c) 2024-2026 Michael Laurence Curzi and 36N9 Genetics, LLC -->
<!-- SPDX-License-Identifier: Apache-2.0 -->

# zedec/zxv: the PHP SDK for ZXV

A small PHP 8.1+ wrapper of `libzxv`, the one C library every ZXV SDK shares
(`sdk/libzxv/include/zxv_api.h`, ABI 1.5), over PHP's FFI extension. It
follows `sdk/docs/SDK_DESIGN.md`, so its nouns match every other ZXV SDK.
Every file uses `declare(strict_types=1)`.

## Install

Requirements: a 64-bit PHP >= 8.1 (the SDK refuses to load on a 32-bit
build, where `int` cannot hold the 64-bit values libzxv uses) with the FFI
extension (`php -m | grep FFI`) and `libzxv`. In the CLI, PHP's default `ffi.enable=preload` already allows FFI;
under php-fpm/Apache set `ffi.enable=1` (or preload the package).

```sh
composer require zedec/zxv           # or, without Composer:
require "/path/to/sdk/php/autoload.php";
```

The library is found, in order: `Zxv::loadLibrary($path)`,
`ZXV_LIBRARY_PATH`, a copy in this package's `lib/` directory, the
in-repository build `sdk/libzxv/build/`, then the system loader path. A
library whose ABI is not 1.x (x >= 4) is refused (the SDK calls the
`zxv_hw_*` functions added in ABI 1.4, the `zxv_hex_*` functions added in
1.3 and the `_cap` functions added in 1.2). A 1.4 library still loads; the
ABI 1.5 classes then throw `NotImplementedException`.

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

## Hardware emulation and hardware updates (ABI 1.4)

Three parts, documented in [HW_EMULATION.md](../docs/HW_EMULATION.md):

- `HwProfile`: ZHP-1 machine profiles (plain data, no capability).
  `probeHost()` describes this machine, `reference()` is the public
  reference machine R1 (16 nodes), `present($host, $target)` marks each node
  HARD or SOFT, `match()` / `pair()` say what runs natively and what is
  emulated; `encode`, `decode`, `digest`, `cap`, `setCap`, `ext`, `setExt`.
- `HwMatrix` (needs `Capability::HW_MATRIX`): functions served by
  interchangeable implementations that serve only after reproducing the
  golden vectors. Every matrix starts with the standard engines
  (`Hw::FN_SHA256`, `FN_KECCAK`, `FN_MAC`, `FN_GCD`, `FN_LOGIC64`,
  `FN_TENSOR`), so `call()` works on any device. `providers()` lists every
  implementation (`HwProviderInfo`: kind, proof, contract, flags, power
  class, calls); `setPolicy()` states placement (`Hw::POLICY_KEY_BEARING`,
  `POLICY_LOCAL_ONLY`, latency, power), never an implementation.
  `addProvider(fn, res, kind, cost, fn (int $op, string $in) => ...)`
  registers a PHP implementation. Contract packages (`trustKey`,
  `applyContract`) need `HW_RECONF`.
- `HwRegion` (needs `HW_RECONF`): hardware updates as images signed with
  ML-DSA-87 AND SLH-DSA-SHAKE-256s (`Hw::keygen`, `Hw::buildImage`, both
  need `CRYPTO_SIGN`; signing takes seconds), staged, tested, committed or
  rolled back on a device you supply as three callables (load, readback,
  activate). Every step is an `onEvent` callback and a record in a
  hash-chained audit log (`log`, `logEntry`, `HwLogEntry`).

`Capability::ALL` stays the ABI 1.1 bits (`ZXV_CAP_ALL` never changes); ask
for `Capability::ALL_1_4` or add `HW_MATRIX` / `HW_RECONF`.

```php
$cap = $ctx->rootCapability(Capability::ALL_1_4);
$mx = new HwMatrix($cap);
$digest = $mx->call(Hw::FN_SHA256, "abc");           // any machine
foreach ($mx->providers() as $p) { echo $p->fnId, " ", $p->kind, " ", $p->proof, "\n"; }

$key = Hw::keygen($cap);                            // secrets in native memory
$img = Hw::buildImage($cap, Hw::IMG_PARTIAL, 0xD0, 0x200, 1, 1, $bitstream, $key);
$rg = new HwRegion($cap, $mx, $fabricIndex, 1, 0xD0, $load, $readback, $activate);
$rg->trustKey($key->publicKey);
$rg->onEvent(fn (int $ev, int $reason, int $version) => audit($ev, $reason, $version));
$rg->stage($img)->trial(5)->commit();
$key->close();                                      // wipes both secret keys
```

PHP callbacks (implementations, the device, events) run through FFI
closures on the calling thread inside libzxv. An exception never crosses
into C: a ZXV exception returns its status, anything else `ZXV_E_IO`. The
closures stay referenced until the context closes. `ZXV_E_NOPROVIDER`,
`E_DISPUTE`, `E_QUARANTINED`, `E_ROLLBACK`, `E_BLOCKED` and `E_POLICY` are
refusals (`ZxvRefusedException`), `E_TARGET` is a usage error; a full audit
log poisons the context (`ZxvFatalError`), since a change that cannot be
recorded is not made.

## ABI 1.5: treaties, content ids, coins, fees, the V-Hub, freight, Web4 agents, paradoxes

ABI 1.5 adds eight feature groups. `Zxv::features2()` lists the live ones
(`treaty`, `aipi`, `coin`, `fee`, `vhub`, `freight`, `web4`, `paradox`);
`Zxv::hasFeature("web4")` checks one. With a pre-1.5 library the SDK still
loads, `features2()` is empty and 1.5 calls throw `NotImplementedException`.
Three capability bits are new: `Capability::AIPI`, `MINT` and `PARADOX`.
`ALL` and `ALL_1_4` do not grow, so ask for
`rootCapability(Capability::ALL_1_5)` or add the bits. A capability without
them gets `CapabilityDeniedException` (`ZXV_E_CAP`).

New statuses: `ZXV_E_DENOM` and `ZXV_E_TREATY` are usage errors
(`ZxvUsageException`; a treaty error is a `TreatyException` with `fault` and
`offset`). `ZXV_E_CID`, `E_ERASURE`, `E_PEG`, `E_LIMIT` and `E_PARADOX_STEP`
are refusals (`ZxvRefusedException`), `E_CONSENT` is a
`ConsentRequiredException` and `E_PARADOX_OWNER` an
`OwnerApprovalRequiredException` (both refusals).

**64-bit values.** Every uint64 (amounts, ids, times in ms) is an `int`; one
above `PHP_INT_MAX` is passed and returned as its decimal string
(`"18446744073709551615"`). Floats are refused (`FloatRejectedException`),
`2.0` included. The SDK refuses to load on a 32-bit PHP build.

**Handles.** Every handle has `close()`. Pure handles (`Treaty`,
`TreatyResult`, `FreightJoin`, `FreightOpsWriter`, `Web4Directory`,
`Web4Manifest`, `Web4ConsentLog`) are also freed by their destructor;
handles made from a capability (`AipiNode`, `TokenWallet`, `VHub`,
`Web4Agent`, `Paradox*`) are freed with their `Context`, before it. A closed
handle throws `ZxvUsageException` (`ZXV_E_STATE`).

The examples below assume:

```php
use Zedec\Zxv\{Capability, Context};
$ctx = new Context();
$cap = $ctx->rootCapability(Capability::ALL_1_5);
```

### Treaties (`Treaty`, `TreatyResult`, `TreatyPic`)

A treaty is a small multilateral settlement written in Sutra text (or its
fixed-width COBOL copybook record). `run()` nets the obligations, posts the
legs with the assurance fee for `SETL` and reports each party's balance and
dividend; it runs in a private engine and needs no capability. `TreatyPic`
is PIC S9(18) arithmetic with COBOL's size-error rules.

```php
use Zedec\Zxv\Treaty;
use Zedec\Zxv\Exception\TreatyException;

$t = Treaty::parse("TREATY CORRIDOR-7 USD SETL\nPARTY ACME 5000000 4000 1/3\nPARTY BETA 3000000 3000 1/7\n"
    . "PARTY GAMMA 900000 0 11/21\nOWE ACME BETA 1200000\nOWE BETA GAMMA 700000\nOWE GAMMA ACME 100000\nEND\n");
$r = $t->run();
$r->legs();      // [["from" => 0, "to" => 2, "amount" => 600000], ["from" => 0, "to" => 1, "amount" => 500000]]
$r->fees();      // 977
$t->toRecord();  // the 2542-byte copybook record
try {
    Treaty::parse("TREATY n USD NET\nPARTY A 1 1 1/1\nEND\n");
} catch (TreatyException $e) {
    [$e->fault, $e->offset];   // ["name", 7]
}
```

### Content ids and AIPI (`Cid`, `Aipi`, `AipiNode`, `Aipi*Record`)

`Cid` is an IPFS-compatible content id (CIDv1 sha2-256; CIDv0 parses).
`Cid::compute` hashes bytes as one block, `Cid::unixfs` gives the id
`ipfs add` would. AIPI names a function by the CID of its record (module,
entry, how a remote result is accepted) and a call by the CID of
(function, input). `AipiNode` (needs `AIPI`) is a local block store that
verifies every block it returns.

```php
use Zedec\Zxv\{Aipi, AipiFunctionRecord, AipiNode, Cid};

$cid = Cid::compute("hello world");
(string) $cid;               // "bafkreifzjut3te2nhyekklss27nh3k72ysco7y32koao5eei66wof36n5e"
$cid->verify("hello world"); // true

$node = new AipiNode($cap);
$mod = $node->put("zxv module bytes");
$fn = AipiFunctionRecord::encode("fn", "checker", "deterministic", 1000000, $mod, "main", checker: "check");
$fnCid = $node->publish($fn);
Aipi::callCid($fnCid, "hello")->text(Cid::BASE58BTC);
$node->plan($fnCid, "hello");   // "local" (this library is offline)
```

### Coins and tokens (`Coin`, `Token`, `TokenWallet`)

Coin faces are Fibonacci rungs; `Coin::split` gives the unique fewest-coin
split. Token holdings are 64 counts, one per tier; `Token::canonical` puts a
value in its one canonical form at an octane. Density earns nothing: a
dense token is worth exactly its face. `TokenWallet` (needs `MINT`) keeps a
holding canonical at its octane.

```php
use Zedec\Zxv\{Coin, Token, TokenWallet};

Coin::split(1000000);                              // [29, 25, 23, 11, 9] (5 coins)
Token::value(Token::canonical(1000000, "racing")); // [1000000, 17]
$w = new TokenWallet($cap, "racing");
$w->mint(1000000);
$w->pieces();                                      // 17
```

`Coin::split` refuses (`ZXV_E_RANGE`) a split above `$maxCoins`
(16,777,216 by default) instead of allocating gigabytes; count large splits
with `Coin::splitCount`.

### The assurance fee (`Fee`)

`fee(g) = floor(g * 8889 / 10^7)`, exact for every 64-bit gross, charged
once per transaction. Keep the carry per account so many small charges pay
what one charge of their sum pays.

```php
use Zedec\Zxv\Fee;

Fee::assure(1000000);                          // 888
[$fee, $carry] = Fee::assureWithCarry(100, 0); // [0, 888900]
Fee::split(100);  // ["reserveFloor" => 50, "vbillDividend" => 25, "infraBounty" => 15, "regenCapital" => 10]
Fee::assure("18446744073709551615");           // 16397310807120420
```

### The V-Hub (`VHub`)

A hub of test currencies swapped through V-Bills on its own in-process
ledger. Each currency's issuer sets its peg with an ML-DSA-87 signature over
`VHub::pegMessage`; swaps fail closed until each currency has a limit rule,
and must be exact under both pegs. Needs `LEDGER_POST` (and `LEDGER_READ`).

```php
use Zedec\Zxv\{MlDsa87, VHub};

$dsa = new MlDsa87($cap);
$issuer = $dsa->generate();
$hub = new VHub($cap);
foreach (["ZTA", "ZTB"] as $slot => $code) {
    $hub->addCurrency($slot, $code, 2, $issuer->publicKey, 500000, 10000);
}
foreach ([[0, 350], [1, 120]] as [$slot, $num]) {
    $hub->setPeg($slot, $num, 1, 1, $dsa->sign($issuer->secretKey, VHub::pegMessage($slot, $num, 1, 1)));
    $hub->addLimit($slot, "18446744073709551615");
}
$alice = $hub->openAccount(0, 50);
$bob = $hub->openAccount(1, 51);
$hub->issue($alice, 100000);
$hub->swap(0, 1, $alice, $bob, 35000, 1);      // ["vbills" => 100, "received" => 12000]
$hub->check();                                 // postings so far; throws if the ledger broke
$issuer->close();
```

### Freight (`Freight`, `FreightHeader`, `FreightJoin`, `FreightOps`, `FreightOpsWriter`)

A freight is 168 rows of 21 bytes; any `k` distinct rows rebuild a payload
of up to `k * 20` bytes (Reed-Solomon erasure coding with a SHA-256 check).
`Freight::split` / `FreightJoin` carry larger payloads as a set.
`FreightOps` encodes an op stream (literal, copy, CID reference, seeded
generator) that the receiver expands up to a limit it chooses; a CID
reference is resolved by your callable, which is kept alive for the call,
and an exception in it never crosses into C (the call fails cleanly).

```php
use Zedec\Zxv\{Freight, FreightHeader, FreightOps};

$payload = "ZXV freight: any 10 of 168 rows rebuild this.";
$hdr = FreightHeader::forPayload($payload, 42, 10);
$rows = Freight::rows(Freight::encode($hdr, $payload));
shuffle($rows);
Freight::decode($hdr, array_slice($rows, 0, 10)) === $payload;   // true

$ops = FreightOps::encode(str_repeat("abc", 100), true);         // 55 bytes
strlen(FreightOps::expand($ops, 4096));                          // 300
```

### Web4 agents (`Web4`, `Web4Agent`, `Web4Envelope`, `Web4Manifest`, `Web4Directory`, `Web4ConsentLog`)

An agent is an ML-DSA-65 identity with a signed card; agents exchange
signed envelopes, publish signed tool manifests, and act on a human's
behalf only with a consent token the human signed, checked once against a
`Web4ConsentLog`. Agent keys stay inside the `Web4Agent` handle and are
wiped when it is closed or its context closes; they are never returned and
never appear in `var_dump`. Creating and signing needs `CRYPTO_SIGN`
(consent tokens: `ATTEST`).

```php
use Zedec\Zxv\{Web4, Web4Agent, Web4Envelope};

$alice = new Web4Agent($cap, "alice", 1700000000000);
$bob = new Web4Agent($cap, "bob", 1700000000000);
Web4::verifyCard($bob->card())["name"];                 // "bob"
$req = $alice->seal($bob->agentId(), "request", "pay", 7, 1700000000500, "amount=100");
(new Web4Envelope($req))->verify($alice->publicKey());  // true
$rsp = $bob->reply($req, "response", 1700000000900, "ok", final: true);
(new Web4Envelope($rsp))->payload;                      // "ok"
```

### Paradoxes (`Paradox`, `ParadoxBook`, `ParadoxPolicy`, `ParadoxHold`, `ParadoxGraph`)

Classifies a contradiction by its hexagram tag (and evidence trend) as
constructive, destructive, neutral or special, and handles each the one way
its kind allows, writing every step to hash-chained logs: a constructive
policy update is applied, kept, rolled back or proposed to the owner; a
destructive one is contained (state restored from a checkpoint, a repair
proof made); a neutral one holds every branch until evidence decides; a
special one is recorded for review. Needs `PARADOX`. A policy flagged
`money` or `device` is never changed automatically.

```php
use Zedec\Zxv\{ParadoxBook, ParadoxPolicy};

$book = new ParadoxBook($cap);
$policy = new ParadoxPolicy($cap, [0, 0], [100, 50], [50, 25]);
$book->constructive($policy, 5, synth: [60, 20], kind: "constructive", tag: 0x2F);   // "applied"
$policy->values();                                                                  // [60, 20]
$r = $book->destructive(str_repeat("X", 16), str_repeat("A", 16), kind: "destructive", tag: 0x23,
    trace: [1, 2, 3, 2], pattern: "loop:A->B->A", submitter: 7);
[$r["outcome"], $r["state"], $r["proof"]->isValid()];   // ["contained", "AAAAAAAAAAAAAAAA", true]
$book->verifyLog("ops");
```

## Vino Embed (ABI 1.6)

The `Vino*` classes are what an app or game needs to take part in Vino
without ever holding anyone's value or keys. There are two lanes, and they
never meet:

- **PLAY** (`VinoPlay`): a sandbox inside your app with its own play units,
  issuers, holders and vouchers, in process memory only. Play units are not
  money and are lost when the sandbox closes. No call takes a play sandbox
  and anything live, so a play unit can never be converted to, redeemed
  for, or moved into live value.
- **LIVE** (`VinoPaymentRequest`, `VinoReceipt`): real Vino stays in the
  user's own ZXV wallet. Your app builds a payment request, the user
  approves it in their wallet, which signs a receipt, and your app checks
  the receipt offline. Your app never sees a balance, a key or a custody
  account.

Concepts:

- **Vouchers.** A voucher is one coin with a Fibonacci face (1, 2, 3, 5, 8,
  ...), held by one holder on exactly one active rail: `VinoRail::Bearer` (a
  note in the holder's hand) or `VinoRail::Ghost` (its twin on the play
  ledger). Spending names the active rail, so a voucher can never be spent
  twice (`WrongRailException`, `ZXV_E_RAIL`). `setRail()` switches rails and
  moves nothing. `voucher()` returns a `VinoVoucher` (state
  `VinoVoucherState::Active` or `Redeemed`), `account()` a `VinoAccount`,
  and `vouchers()` the list of active voucher ids.
- **Coverage.** An issuer may only issue, or withdraw backing, while
  `10 * backing >= 18 * outstanding` (1.8x, exact integers); otherwise
  `CoverageException` (`ZXV_E_COVERAGE`) and nothing changes.
- **Redeeming** returns a voucher to its issuer and retires it. Nothing is
  paid out to anyone. There is no call that turns a voucher into backing or
  play units, and no interest, yield or return of any kind.
- **Payment requests** are plain immutable values. `encode()` gives the
  canonical 160 bytes, `id()` their SHA-256 (32 bytes), and
  `VinoPaymentRequest::decode()` reads them back. A LIVE request with
  `Vino::FLAG_CHANCE` (a paid chance outcome such as a loot box) is refused
  (`ChanceRefusedException`, `ZXV_E_CHANCE`); the allotment is a separate
  module, not a payment request. A lane other than `VinoLane::Play`/`Live`
  is a `LaneException` (`ZXV_E_LANE`).
- **Receipts** are the wallet's ML-DSA-87 signature over 88 bytes
  (`VinoReceipt::message()`). `verify($now)` is pure and offline: it throws
  `VerificationFailedException` for a wrong signature and `ExpiredException`
  (`ZXV_E_EXPIRED`) for an expired request or an approval outside its
  window; `isValid($now)` answers true/false instead. `VinoReceipt::sign()`
  is the wallet's half (it needs `Capability::CRYPTO_SIGN` and a
  `SecretKey`, which it uses once and does not keep); an app never holds a
  wallet key.
- **Daily cap.** `VinoSpendMeter` counts what one device approved per day
  and refuses an amount that would pass the cap
  (`DailyCapExceededException`, `ZXV_E_LIMIT`, with `$left`). It is a
  counter, not a legal check.
- **Capabilities.** The sandbox and the meter need the new
  `Capability::VINO` (`1 << 15`). `ALL_1_5` does not include it: ask for
  `rootCapability(Capability::ALL_1_6)` or add `VINO`.
  `Zxv::hasFeature("vino")` (feature bit `0x100`) says whether the library
  has the group; against an older library the SDK still loads and `Vino*`
  calls throw `NotImplementedException`. Requests, receipt messages and
  verification need no capability.
- **Statuses.** `ZXV_E_RAIL`, `E_COVERAGE`, `E_EXPIRED` and `E_CHANCE` are
  refusals (`WrongRailException`, `CoverageException`, `ExpiredException`,
  `ChanceRefusedException`, all `ZxvRefusedException`); `ZXV_E_LANE` is a
  usage error (`LaneException`, a `ZxvUsageException`). Amounts, ids and
  times are uint64 (`int`, or a decimal string past `PHP_INT_MAX`); floats
  are refused.

A play-lane shop: the shop backs its vouchers with play units, issues one
to a player, the player spends it at a friend, who redeems it.

```php
use Zedec\Zxv\{Capability, Context, VinoPlay, VinoRail};

$ctx = new Context();
$cap = $ctx->rootCapability(Capability::ALL_1_6);
$play = new VinoPlay($cap);
$shop = $play->openIssuer(700);
$player = $play->openHolder(801);
$friend = $play->openHolder(802);

$play->back($shop, 100);                          // play units: not money
$id = $play->issue($shop, $player, 34, VinoRail::Bearer);
$play->transfer($id, $player, $friend, VinoRail::Bearer);   // spend on its active rail
$play->redeem($id, $friend);                      // retired; nothing is paid out
$play->voucher($id)->state;                       // VinoVoucherState::Redeemed
$play->account($shop)->faces;                     // 0 outstanding
$play->check();
$ctx->close();
```

A live payment request, the wallet's receipt, and the app's offline check.
The wallet half is shown only to make the example run; in a real
deployment it happens in the user's wallet, never in your app.

```php
use Zedec\Zxv\{Capability, Context, Hashes, MlDsa87, VinoLane, VinoPaymentRequest, VinoReceipt, VinoSpendMeter, Zxv};

$ctx = new Context();
$now = 1700000000;
$request = new VinoPaymentRequest(VinoLane::Live, Hashes::sha3_256("shop key"), Zxv::randomBytes(16),
    amount: 1500, createdAt: $now, expiresAt: $now + 600, memo: "Sword of Dawn");
$bytes = $request->encode();                      // 160 bytes, sent to the wallet

// --- in the user's wallet (not the app) ---
$walletCap = $ctx->rootCapability(Capability::CRYPTO_SIGN);
$wallet = (new MlDsa87($walletCap))->generate();
$receipt = VinoReceipt::sign($walletCap, $bytes, $wallet->secretKey, $wallet->publicKey, $now + 30);
$wallet->close();

// --- back in the app: offline, no capability ---
$got = new VinoReceipt($bytes, $receipt->walletPublicKey, $receipt->approvedAt, $receipt->signature);
$got->verify($now + 60);                          // throws unless genuine and in time
$got->requestId() === $request->id();             // true
$got->isValid($now + 601);                        // false (expired)

$meter = new VinoSpendMeter($ctx->rootCapability(Capability::VINO), 10000);
$meter->admit(intdiv($now, 86400), $request->amount);   // 8500 left today
$ctx->close();
```

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
| `HwProfile` | `zxv_hw_profile_*` | `probeHost`, `reference`, `present`, `decode`, `create`, `encode`, `digest`, `addNode`, `setCap`, `cap`, `setExt`, `ext`, `nodes`, `match`, `pair` |
| `HwMatrix`, `HwProviderInfo` | `zxv_hw_*` matrix | `call`, `shadow`, `providers`, `providerInfo`, `functionInfo`, `bound`, `setPolicy`, `addProvider`, `setTraits`, `attest`, `memo`, `trustKey`, `applyContract`, `log`, `logEntry` |
| `HwRegion`, `Hw`, `HwReleaseKey`, `HwLogEntry` | `zxv_hw_region_*`, packages | `stage`, `trial`, `commit`, `rollback`, `tick`, `info`, `onEvent`, `log`; `Hw::keygen`, `Hw::buildImage`, `Hw::vectorList` |
| `StateChannel`, `ChannelState` | `zxv_evm_*` | `encodeState`, `ledgerState` (for `sdk/solidity/ZxvStateChannel.sol`) |
| `Treaty`, `TreatyResult`, `TreatyPic` | `zxv_treaty_*` (1.5) | `parse`, `fromRecord`, `toSutra`, `toRecord`, `validate`, `party`, `obligation`, `run`; `legs`, `fees`, `party`; `add sub mul divide` |
| `Cid`, `Aipi`, `AipiNode`, `AipiFunctionRecord`, `AipiCallRecord`, `AipiClaimRecord` | `zxv_cid_*`, `zxv_aipi_*` (1.5) | `compute`, `unixfs`, `parse`, `text`, `toV1`, `verify`, `providerKey`; `callCid`; `put`, `get`, `publish`, `loadFunction`, `plan`, `info`; `encode` |
| `Coin`, `Token`, `TokenWallet` | `zxv_coin_*`, `zxv_token_*` (1.5) | `face`, `rung`, `split`, `splitCount`; `canonical`, `value`, `compress`, `fuse`, `split`, `encodeCodes`, `decodeCodes`; `mint`, `spend`, `transfer`, `setOctane`, `counts` |
| `Fee` | `zxv_fee_*` (1.5) | `assure`, `assureWithCarry`, `split`, `defaultPolicy` |
| `VHub` | `zxv_vhub_*` (1.5) | `pegMessage`, `addCurrency`, `setPeg`, `openAccount`, `issue`, `addLimit`, `nextEpoch`, `quote`, `swap`, `balance(s)`, `check` |
| `Freight`, `FreightHeader`, `FreightJoin`, `FreightOps`, `FreightOpsWriter` | `zxv_freight_*` (1.5) | `encode`, `decode`, `rows`, `split`, `setCount`; `forPayload`, `build`; `add`, `status`, `result`; `encode`, `expand`, `peek`, `generate`; `literal`, `copy`, `cidref`, `seed`, `finish` |
| `Web4`, `Web4Agent`, `Web4Envelope`, `Web4Manifest`, `Web4Directory`, `Web4ConsentLog` | `zxv_w4_*` (1.5) | `verifyCard`, `actionDigest`, `consentInfo`; `card`, `seal`, `reply`, `issueConsent`; `verify`; `addTool`, `sign`, `encode`, `decode`, `toJson`; `add`, `find`; `check`, `revoke`, `prune` |
| `Vino`, `VinoPlay`, `VinoVoucher`, `VinoAccount`, `VinoPaymentRequest`, `VinoReceipt`, `VinoSpendMeter`, `VinoRail`, `VinoLane`, `VinoVoucherState` | `zxv_vino_*` (1.6) | `openIssuer`, `openHolder`, `back`, `unback`, `issue`, `transfer`, `setRail`, `redeem`, `voucher`, `account`, `vouchers`, `check`; `encode`, `id`, `decode`, `idOf`; `message`, `sign`, `verify`, `isValid`; `admit` |
| `Paradox`, `ParadoxBook`, `ParadoxPolicy`, `ParadoxHold`, `ParadoxGraph` | `zxv_paradox_*` (1.5) | `signature`, `classify`, `action`; `registryAdd`, `constructive`, `destructive`, `neutral`, `special`, `draft`, `review`, `pass`, `logRecords`, `verifyLog`; `values`, `proposal`; `add`, `evidence`; `addNode`, `addDependency`, `state`, `info` |

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
- **Integer range.** PHP ints are signed 64-bit. Before ABI 1.5, uint64
  values (nonces, ticks, token counts) are limited to `PHP_INT_MAX`; the
  ABI 1.5 classes take and return values above it as decimal strings.
- **Secrets.** `SecretKey` memory is wiped on `close()`/destruction, but
  bytes you pass in as PHP strings (seeds, messages, `fromBytesUnsafe`) live
  in the PHP heap and are not wiped; the temporary native copies made of
  seeds are freed, not wiped. Swap and core dumps are outside the SDK's
  control.
- **Handle lifetime.** Child handles are freed by `close()` or when their
  `Context` closes; each child keeps its `Context` alive.
- **FFI declarations** live in `src/zxv.h` and `src/zxv15.h` (PHP's FFI
  cannot read the real header's preprocessor lines) and must be kept in sync
  with `zxv_api.h`.
- **ABI 1.5 groups are tools, not institutions.** A treaty run, a fee split
  and a V-Hub swap are exact arithmetic on in-process ledgers: no real money
  moves, nothing is connected to a bank, rail or issuer, and nothing here is
  legal, tax or securities advice. V-Hub currencies are test currencies
  only. AIPI and freight fetch nothing from a network; freight detects a
  corrupted row but does not repair it, and its header is not
  authenticated. A Web4 consent token proves a key holder signed, not that
  the right human understood; envelopes are signed, not encrypted. Paradox
  handling contains and routes contradictions inside published bounds;
  `$ownerOk` is your assertion, which the library cannot check.
- **Vino Embed is the app's half only.** Play units are not money, are
  worth nothing and are lost on close; no play-lane call reaches live value.
  Approving a payment happens in the user's wallet (zxvd), which posts it on
  its own ledger. The spend meter is a counter, not a legal check, and makes
  no exemption claim. Receipts use the reference ML-DSA-87 implementation,
  not a FIPS 140-3 validated module. Whether a given deployment of Vino is
  store credit, a prepaid access product or something else is a legal
  question pending counsel; nothing in this SDK decides it.
- **Web4 agent keys** stay inside `Web4Agent` and are wiped on close; a seed
  you pass as a PHP string is copied to a native buffer that is zeroed after
  use, but the PHP string itself lives in the PHP heap (pass a `SecretKey`
  to avoid that). FFI callbacks (a freight CID resolver) keep their
  trampoline until the request ends.
- **Not certified.** The cryptography is not FIPS 140-3 validated; libzxv
  serializes ISO 20022 but has no network transport. LoRA and the
  pacs.002/004/009, camt.056/029 builders are not built (they throw
  `NotImplementedException`); netting, card issuance and VSS are stubs in
  libzxv and are not wrapped yet.
