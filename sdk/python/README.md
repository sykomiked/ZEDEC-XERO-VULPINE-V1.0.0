<!-- Copyright (c) 2024-2026 Michael Laurence Curzi and 36N9 Genetics, LLC -->
<!-- SPDX-License-Identifier: Apache-2.0 -->

# ZXV for Python (`zxv-sdk`)

The Python SDK for the ZXV platform core. It wraps `libzxv` (C ABI 1.6,
`sdk/libzxv/include/zxv_api.h`) with [cffi](https://cffi.readthedocs.io) in
ABI mode, so there is nothing to compile. It gives you:

- exact money and exact rationals (no floats anywhere),
- four-valued logic (`TRUE`, `FALSE`, `BOTH`, `NEITHER`),
- capabilities checked on every call,
- rational resource budgets with `asyncio` waiting,
- a logical phase clock with awaitable deferred events,
- UBH-168 wire frames in pure Python,
- the swarm token economy,
- SHA3, SHAKE256, Keccak-256, ML-KEM-1024, ML-DSA-87 and Ed25519 with wiped secret keys,
- a deterministic integer GGUF inference engine (sync, threaded and async streaming),
- EVM state-channel encoding,
- the ABI 1.5 groups: treaties, CIDs and AIPI records, Fibonacci coins and
  token octane, the assurance fee, a V-Bill FX hub, freight erasure coding,
  web4 agents with consent, and the paradox book,
- Vino Embed (ABI 1.6): play-lane vouchers, payment requests, wallet
  receipts checked offline, and a daily spend meter.

Requires Python 3.10+ and `cffi`. Fully typed (`py.typed`, `mypy --strict` clean).

## Install

```sh
# 1. Build the C library (once, from the repository root)
make -C sdk/libzxv

# 2a. Develop against the in-tree build
cd sdk/python
python3 -m venv .venv && . .venv/bin/activate
pip install -e '.[test]'
export ZXV_LIBRARY_PATH=$PWD/../libzxv/build     # a file or a directory

# 2b. Or build a wheel that bundles the library under zxv/_lib/
python scripts/bundle_lib.py                       # copies libzxv.so/.dylib/zxv.dll
pip install build && python -m build --wheel       # -> dist/zxv_sdk-1.6.0-py3-none-<platform>.whl
```

The library is found in this order: a path passed to `zxv.load_library()`,
then `ZXV_LIBRARY_PATH`, then the copy bundled in the wheel, then the system
loader path. The SDK refuses a library whose ABI is not 1.x with x >= 5 (it calls the
`zxv_features2` and group functions added in 1.5, the `zxv_hw_*` functions added in 1.4, the `zxv_hex_*` functions added in 1.3
and the `_cap` functions added in 1.2).
`import zxv` never loads the library; the first call that needs it does, so
the pure-Python parts (`Ubh168Frame`, `Truth`) work without it.

## Quickstart

```python
import zxv

with zxv.Context() as ctx:
    cap = ctx.root_capability()
    ctx.currencies.enable("NGN", cap=cap)
    ledger = zxv.Ledger(cap)
    bank = ledger.open_account("bank", "NGN", zxv.AccountKind.ISSUER)
    alice = ledger.open_account("alice", "NGN")
    ledger.post(bank, alice, 50_000, "NGN", "E2E-0001")     # 500.00 NGN, in kobo
    print(ledger.balance(alice).equity)                      # 50000
print(zxv.Rational.parse("0.1") + zxv.Rational.parse("0.2"))  # 0.3, exactly
```

## Concepts, in Python terms

**Context and handles.** `zxv.Context()` is one platform instance (a ledger,
its currencies, its state). Every object that owns a native handle
(`Context`, `Capability`, `Budgets`, `PhaseClock`, `Attestations`, `Swarm`,
`Model`, `Session`, `MxMessage`, `NettingCycle`, `SecretKey`) is a context
manager and has an idempotent `close()`; `__del__` closes as a backstop.
Children keep their parent alive, and closing a parent closes its open
children first, so close order never matters.

**Capabilities on every gated call (ABI 1.2).** Every operation that
changes or reads platform state goes through a `Capability`:
`Ledger(cap)` opens accounts (`LEDGER_POST`) and reads accounts and
journals (`LEDGER_READ`); `ctx.currencies.enable(..., cap=cap)` and
`register_private(..., cap=cap)` need `LEDGER_POST`; `MxMessage(cap, kind)`
needs `LEDGER_READ`; `Budgets`, `PhaseClock`, `Attestations` and `Swarm`
need `BUDGET`, `PHASE`, `ATTEST` and `SWARM` to be created. A capability
must belong to the context it is used on; one from another context raises
`CapabilityDeniedError`. The 1.0 forms without a capability
(`ctx.currencies.enable("NGN")`, `MxMessage(ctx, kind)`) still work and
emit a `DeprecationWarning`.

**Capabilities.** Every object that makes gated calls holds a `Capability`
and passes it on every call; there is no global "current capability".
`ctx.root_capability(Cap.ALL)` makes a root; `cap.attenuate(Cap.LEDGER_READ)`
makes a narrower child (never wider). `revoke()` revokes a capability and
all its descendants; `set_attestation(Truth.BOTH)` blocks it.
A denied call raises `CapabilityDeniedError`. `cap.check(bits)` returns a bool.

**Exactness: no floats.** `Rational` is immutable and hashable, reduced, with
int64 parts; arithmetic runs in C and raises on overflow instead of wrapping.
It equals, orders, hashes and does arithmetic exactly with `int`,
`fractions.Fraction` and `decimal.Decimal`. A `float` passed anywhere raises
`FloatRejectedError` (a `TypeError`) whose message points at the single
float entry point, `Rational.from_float_exact(x)`, which returns the exact
value of the double's bits (0.1 becomes 3602879701896397/36028797018963968).
`float(rational)` is refused too. Money is `int` minor units. Engine
parameters take rationals or exact text: `GenerateOptions(temperature="0.7")`.

**Four-valued logic.** `Truth` is an `enum.Enum` (`NEITHER=0`, `TRUE=1`,
`FALSE=2`, `BOTH=3`, the kernel's numbering). `bool(truth)` and `if truth:`
raise `TypeError`: convert on purpose with `is_certainly_true()`,
`to_bool(on_both=..., on_neither=...)` or `require_true()`. `&`, `|`, `~`
follow the C tables. `BOTH` is a held contradiction, not an error.

**Errors.** Every negative status maps through `zxv_status_class`:
`ZxvUsageError` (also a `ValueError`), the `ZxvRefusedError` family
(`CapabilityDeniedError`, `BudgetExhaustedError`, `InsufficientFundsError`,
`VerificationFailedError`, `ZxvNotImplementedError`), `ZxvSystemError`, and
`ZxvFatal`. Each carries `.status`, `.status_name` and `.detail`
(`zxv_last_error`).

**Fail closed.** `ZxvFatal` derives from `BaseException`, so
`except Exception:` cannot swallow it. On a FATAL status the C context is
poisoned (every later call fails with `ZXV_E_POISONED`) and the SDK marks
its `Context` dead, so every later call on any of its objects raises
`ZxvFatal` before reaching C. `Context(fatal_policy=FatalPolicy.ABORT)` (or
`zxv.set_default_fatal_policy`) calls `os.abort()` instead: the only form
nothing can catch. `ctx.halt(reason)` is the operator kill switch.

**Budgets and asyncio.** `Budgets(cap)` holds exact rational budgets.
`budget.consume(x)` raises `BudgetExhaustedError` when it would pass the
limit; `await budget.consume_async(x, timeout=...)` waits until a `release`
or a larger limit replenishes it (the C listener wakes waiters on any
thread), and is cancellable; `budget.consume_wait(x)` is the blocking form
for threads. `budgets.on_exhausted(cb)` reports denials.

**Phase clock.** `PhaseClock(cap)` counts logical ticks (27,720 per
fundamental). `clock.defer(tick)` returns a `DeferredEvent`;
`await event.wait_cleared()` or `await clock.until_tick(t)` completes when
the clock passes it. Drive it by hand with `clock.advance(n)` (tests,
simulations) or in real time with `driver = clock.start_realtime(tps)`, an
asyncio task (`await driver.stop()`).

**Engine.** `Model.open(cap, path, tokenizer_path)` loads a GGUF model
(read-only, share it across threads); `model.new_session(n_ctx)` makes a
conversation (one thread at a time; the SDK serializes it with a lock).
`session.generate(prompt, options)` returns a `GenerationResult` (text,
token ids, stop reason). Streaming: `for tok in session.stream(...)` (a
worker thread feeds a queue), `async for tok in session.stream_async(...)`,
and `await session.generate_async(...)`; generation runs on a worker thread
so the event loop stays free, and leaving the loop stops generation.
**The GIL is released during `zxv_generate`**: cffi releases it for every
foreign call, and `tests/test_engine.py::test_gil_is_released_during_generate`
proves it (a second thread counts while the main thread generates, with a
held-GIL control that counts zero). One session per thread gives real
parallelism on the CPU cores. `session.bind_budget(budget)` charges one
unit per prompt and generated token. Same seed and inputs give the same
tokens on every machine.

**Secrets.** `SecretKey` holds key material in a cffi-owned buffer
(`ffi.new`, outside Python objects) that is passed to C directly and wiped
with `zxv_secure_zero` on `close()`/`__exit__`/`__del__`. Only
`export_bytes_unsafe()` copies it into a Python `bytes` (and defeats the
wiping). It refuses to pickle.

**Wire framing.** `Ubh168Frame` packs, parses and resolves UBH-168 frames in
pure Python; tests check it byte for byte against `vectors.json` and the C
library (`to_bytes_native()`, `resolve_native()`).

## The hexagram tag (ABI 1.3)

A `Hexagram` tags one unit of work with a four-valued base, the polarity
of its evidence (entropic, neutral, forbidden, negentropic) and its phase
(potential -> emerging -> manifest -> dissolving), packed into one byte
0..63. Its action is proceed, wait, refuse, escalate or rollback. Every rule
comes from libzxv and is tested against `vectors.json` key `hex`.
Forbidden is absorbing, a phase moves one step at a time, and the base is
**not** numbered like `Truth` (TRUE is base 3 but Truth 1): convert only
with the explicit `to_truth()` / `Hexagram.from_truth(...)`. Combining BOTH with NEITHER follows
the `Truth` tables (AND gives NEITHER, OR gives BOTH); those two results
are pending an owner decision. A `QuarantineLog` (needs the ATTEST
capability) records forbidden work in a SHA-256 hash chain; when it is
full, append is refused with `ZXV_E_CAPACITY` and you must halt the
context. See [ABI.md, Changes in 1.3](../docs/ABI.md#changes-in-13).

```python
from zxv import Hexagram, HexBase, HexPolarity, HexPhase, HexAction, QuarantineLog

h = Hexagram(HexBase.TRUE, HexPolarity.NEGENTROPIC, HexPhase.POTENTIAL)
assert h.action is HexAction.PROCEED
h = h.advance()                         # EMERGING
h.step(HexPhase.DISSOLVING)             # ZxvRefusedError (ZXV_E_STATE): a jump
bad = h.forbid()                        # action ROLLBACK, forever
log = QuarantineLog(cap, capacity=1024) # needs Cap.ATTEST
seq = log.append(bad, reason=7, submitter=42, subject=digest32)
log.verify()
```

`bool(h)` raises: a tag has no truth value of its own.

## Hardware emulation and hardware updates (ABI 1.4)

`zxv.hardware` wraps the 55 `zxv_hw_*` functions; the concepts are in
[HW_EMULATION.md](../docs/HW_EMULATION.md) and the profile format in
[HW_PROFILE.md](../docs/HW_PROFILE.md).

* **Profiles** (`HwProfile`, plain data, no capability): `probe_host()`
  describes this machine, `reference()` is the VULPINE-class reference
  machine R1 (16 nodes), `present(host, r1)` marks each R1 component HARD or
  SOFT, `match` / `pair` say what runs natively, and `encode` / `decode` /
  `digest` give the canonical ZHP-1 bytes and identity. `set_ext` /
  `get_ext` carry an opaque per-node extension that is digested but never
  interpreted.
* **The matrix** (`HwMatrix(cap)`, needs `Cap.HW_MATRIX`): starts with the
  standard engines (`HwFn.SHA256`, `KECCAK`, `MAC`, `GCD`, `LOGIC64`,
  `TENSOR`), each a proven SOFT implementation. `providers()` /
  `function_info()` / `bound()` are discovery; `set_policy` states placement
  constraints (`HwPolicy.KEY_BEARING` work never leaves the machine and is
  never memoized; clearing it raises `ZXV_E_POLICY`). `add_provider(fn, res,
  kind, cost, impl)` adds a Python implementation `impl(op, data) -> bytes`
  that serves only after `attest()` reproduces the golden vectors.
* **Packages** (`HwReleaseKey.generate(cap)`, needs `Cap.CRYPTO_SIGN`): every
  image and contract package is signed with ML-DSA-87 **and**
  SLH-DSA-SHAKE-256s. The secret halves are `SecretKey`s wiped on `close()`.
  `key.build(...)` signs a package; SLH-DSA signing takes seconds, so it is
  a release step. `encode_vectors` builds a `HwImageKind.VECTORS` payload
  for `HwMatrix.apply_contract` (needs `Cap.HW_RECONF`).
* **Regions** (`HwRegion(cap, device, ...)`, needs `Cap.HW_RECONF`): a
  Python `device` (`load`, `readback` -> SHA-256 of the slot, `activate`)
  receives signed images through `stage` -> `trial` -> `commit` (or
  `rollback`, `tick`). Every step is an event (`on_event`) and a record in a
  hash-chained audit log (`log`, `log_entry`); a failing image is rolled
  back and quarantined, and a full log halts the context (fail closed).

`Cap.ALL` keeps its 1.1 value forever; use `Cap.ALL_1_4` (or add
`Cap.HW_MATRIX | Cap.HW_RECONF`) for a capability that can use this module.
Implementations and devices run synchronously on the calling thread; a
`ZxvError` raised there becomes that status in C, and any other exception
fails the C call with `ZXV_E_IO` and is re-raised from the SDK call. The
new statuses (`ZXV_E_NOPROVIDER` -25 through `ZXV_E_POLICY` -31) map
through `zxv_status_class` like every other.

```python
from zxv import Cap, HwFn, HwImageKind, HwKind, HwMatrix, HwProfile, HwRegion, HwReleaseKey

r1 = HwProfile.reference()
native, emulated = HwProfile.match(HwProfile.probe_host(), r1)
cap = ctx.root_capability(Cap.ALL_1_4)
mx = HwMatrix(cap)
digest = mx.call(HwFn.SHA256, b"abc")           # served by the proven SOFT engine
with HwReleaseKey.generate(cap) as key:          # secrets wiped on exit
    image = key.build(cap, HwImageKind.PARTIAL, target_class=0xD0, fn_id=0x200,
                      version=1, svn=1, payload=table)
region = HwRegion(cap, device, region_id=1, target_class=0xD0, matrix=mx, provider=fabric)
region.on_event(print)
region.trust_key(key.public_key)
region.stage(image); region.trial(5); region.commit()
```

## ABI 1.5 groups

ABI 1.5 adds eight feature groups (nine modules). `zxv.features2()` reports which are live
(`Feature2.TREATY`, `AIPI`, `COIN`, `FEE`, `VHUB`, `FREIGHT`, `WEB4`,
`PARADOX`); a group that is not built raises `ZxvNotImplementedError`.
`Cap.ALL` (1.1) and `Cap.ALL_1_4` keep their values forever; use
`Cap.ALL_1_5`, or add `Cap.AIPI`, `Cap.MINT` and `Cap.PARADOX`, for a
capability that reaches the new gated calls. The new statuses map to typed
exceptions:

| Status | Exception | Class |
|---|---|---|
| `ZXV_E_DENOM` -32 | `DenominationError` | usage |
| `ZXV_E_TREATY` -33 | `TreatyError` (`.fault`, `.offset`) | usage |
| `ZXV_E_CID` -34 | `CidMismatchError` | refused |
| `ZXV_E_CONSENT` -35 | `ConsentRequiredError` | refused |
| `ZXV_E_ERASURE` -36 | `ErasureError` | refused |
| `ZXV_E_PEG` -37 | `PegError` | refused |
| `ZXV_E_LIMIT` -38 | `LimitRefusedError` | refused |
| `ZXV_E_PARADOX_STEP` -39 | `ParadoxStepLimitError` | refused |
| `ZXV_E_PARADOX_OWNER` -40 | `OwnerApprovalRequiredError` | refused |

Every handle below is a context manager. Handles whose C object is not
thread-safe (`Treaty`, `AipiNode`, `TokenWallet`, `VHub`, `FreightJoin`,
`OpsWriter`, the web4 and paradox handles) serialize calls with a lock. The
`ffi` calls release the GIL. Variable-length outputs use the two-call
`ZXV_E_BUFFER` pattern internally, so you get `bytes` back. Each example
below runs as it stands.

### Treaties (`zxv.treaty`)

A treaty is a settlement batch in a fixed copybook record (`to_record`,
2542 bytes) or in canonical Sutra text (`to_sutra`). `run()` settles it in
integer arithmetic and returns a `TreatyResult` (its own handle). A refusal
raises `TreatyError` with the `TreatyFault` and the byte offset. A failed
parse leaves the treaty as it was. `pic_add/sub/mul/div` are the PIC S9(18)
helpers.

```python
import zxv
from zxv import Treaty

sutra = "TREATY N USD SETL\nPARTY A 900000 0 1/2\nPARTY B 900000 0 1/2\nOWE A B 300000\nEND\n"
with Treaty.from_sutra(sutra) as t, t.run() as result:
    print(t.party_count, [(leg.payer, leg.payee, leg.amount) for leg in result.legs], result.fees)
try:
    Treaty.from_sutra("TREATY")
except zxv.TreatyError as e:
    print(e.fault, e.offset)
```

Limits: a treaty is a batch description, not a contract in law. Signatures,
consent and authority live elsewhere. One asset per treaty. Dividend shares
are reported only; nothing is paid out of the bucket.

### CIDs and AIPI (`zxv.cid`, `zxv.aipi`)

`Cid` is an immutable, validated CID. You can `compute` one, build one with
`unixfs` (the same bytes as Kubo's `ipfs add`), or read one with `parse`.
`verify` and `matches` check data against it. `to_text` and `str()` give
the text forms. `AipiFunction`, `AipiCall` and `AipiClaim` encode and
decode the records. `AipiNode(cap)` is an offline, in-process block store;
it needs `Cap.AIPI`. A node `node_key` (a `SecretKey` or 32 bytes, wiped on
close) enables private, sealed blocks.

```python
import zxv
from zxv import AipiFunction, AipiNode, Cap, Cid
from zxv.aipi import AipiIntent, AipiVerify

with zxv.Context() as ctx:
    cap = ctx.root_capability(Cap.ALL_1_5)
    module = Cid.compute(b"module bytes")
    fn = AipiFunction.encode(intent=AipiIntent.FN, verify=AipiVerify.LOCAL, module=module,
                             entry="main", fuel_max=10)
    with AipiNode(cap) as node:
        node.put(b"module bytes")
        print(node.publish(fn), node.plan(fn.cid, b"input"))   # route LOCAL: nothing runs here
    print(Cid.unixfs(b"hello"), module.matches(b"module bytes"))
```

Limits: there is no network (no mesh, IPFS peers, gateway or libp2p) and
no EXECUTE, since running a function needs the Z-Wasm sandbox outside
libzxv. No money moves in this group. A CID proves which bytes are meant.
It does not prove that an output is right, and it does not make the bytes
available. Signing claims is the caller's job.

### Coins and AI tokens (`zxv.coin`)

These cover the Fibonacci coin ladder (`face`, `rung`, `split`,
`split_count`) and AI token tiers (`token_face`, `token_tier`, `Octane`).
`TokenHolding` is an immutable holding with `canonical`, `compressed`,
`fused`, `split`, `value` and `pieces`. The Fibonacci codes are
`code_encode`, `code_decode`, `code_put` and `code_get`. A `TokenWallet`
keeps a holding canonical at its octane; every change to it needs
`Cap.MINT`.

```python
import zxv
from zxv import Cap, Octane, TokenHolding, TokenWallet, coin

print(coin.split(100), TokenHolding.canonical(1_000_000, Octane.RACING).pieces)  # [10, 5, 3] 17
with zxv.Context() as ctx:
    cap = ctx.root_capability(Cap.ALL_1_5)
    with TokenWallet(cap, model_id=7, octane=Octane.RACING) as a, TokenWallet(cap, model_id=8) as b:
        a.mint(1_000_000)
        a.transfer(b, 1000)
        print(a.value, b.value, b.holding.pieces)
```

Limits: there is no premium. A dense token is worth exactly its face in
plain tokens; changing octane changes the number of pieces, never the value.
Minting VFV coins is not in this ABI.

### Assurance fee (`zxv.fee`)

The fee is 0.08889% of gross (`FEE_NUM / FEE_DEN`). The group provides
`assure`, carry-exact `assure_carry` and `FeeAccount`, the four-bucket
`split`, and `default_policy`.

```python
from zxv import fee

acct = fee.FeeAccount()
print([acct.charge(1_000_000) for _ in range(3)], acct.remainder)
print(list(fee.split(10_000)), fee.split(10_000).total)
```

Limits: these are the arithmetic rules only. How a fee, reserve or
dividend pool is treated in law is a question for counsel; nothing here is
a licence or a certification.

### V-Bill hub (`zxv.vhub`)

`VHub(cap)` is a demo FX hub with test currencies, priced through
V-Bills. Pegs are signed with ML-DSA-87 (`VHub.peg_message`, `set_peg`).
`add_limit` adds per-sender limit rules, and swaps fail closed without a
rule. The other calls are `quote`, `swap`, `balance` and `check`. Refusals
are typed: `PegError`, `LimitRefusedError` and `InsufficientFundsError`.

```python
import zxv
from zxv import Cap, VHub

with zxv.Context() as ctx:
    cap = ctx.root_capability(Cap.ALL_1_5)
    dsa = zxv.MlDsa87(cap)
    with dsa.generate() as ka, dsa.generate() as kb, VHub(cap) as hub:
        for slot, code, key in ((0, "TSTA", ka), (1, "TSTB", kb)):
            hub.add_currency(slot, code, 2, key.public_key, pool_funding=10_000, vbill_backing=1000)
            hub.set_peg(slot, 2, 1, 1, dsa.sign(key.secret_key, VHub.peg_message(slot, 2, 1, 1)))
            hub.add_limit(slot, 10**9)
        alice, bob = hub.open_account(0, 1), hub.open_account(1, 2)
        hub.issue(alice, 1000)
        print(hub.quote(0, 1, 100), hub.swap(0, 1, alice, bob, 100, tick=1).received)
```

Limits: test currencies only (ISO 4217 codes are refused), so there is no
real money. The hub's ledger is in memory and separate from the context
ledger. It is connected to no rail, central bank or issuer. ML-DSA here is
not FIPS 140-3 validated. V-Bills are likely securities in many
jurisdictions. A rule reports what it decided; it is not a legal check.

### Freight (`zxv.freight`)

A freight is 168 rows of 21 bytes, and any `k` rows rebuild a payload of
up to `k * 20` bytes. The pieces are:
- `FreightHeader` (`for_payload`, `parse`, `to_bytes`), `encode`, `decode`
  and `packets`.
- Sets: `split`, `iter_split`, `set_count` and `FreightJoin`.
- Op streams: `ops_encode`, `ops_expand` (with an optional CIDREF
  resolver), `ops_peek`, `OpsWriter` and `gen_run`.

The resolver is called on the calling thread. An exception it raises fails
the expansion and is re-raised to you; it never unwinds through C.

```python
from zxv import freight
from zxv.freight import FreightHeader

payload = b"any k rows rebuild the payload"
h = FreightHeader.for_payload(42, 10, payload)
rows = freight.packets(freight.encode(h, payload))
print(freight.decode(h, rows[158:]) == payload)              # the last 10 rows are enough
ops = freight.ops_encode(b"abc" * 20)
print(len(ops), freight.ops_expand(ops, limit=1024) == b"abc" * 20)
```

Limits: this is erasure coding only. A corrupted row is detected
(`VerificationFailedError`) but not located or repaired. The header is not
authenticated, so sign it if the sender matters. A freight is larger than
its payload. Op streams give no size guarantee: random data encodes
slightly larger.

### Web4 agents (`zxv.web4`)

`W4Agent(cap, name, now_ms)` holds an ML-DSA-65 key that never leaves the
handle and is wiped on close. It has a signed `card` (`verify_card`,
`W4Directory`). The other parts are:
- `W4Manifest`: a signed tool list.
- Consent: `action_digest`, `W4Agent.issue_consent` on the human's key, and
  `W4ConsentLog.check` (single use, revocable), which raises
  `ConsentRequiredError`.
- Envelopes: `seal`, `reply`, `decode_envelope` and `verify_envelope`.

```python
import zxv
from zxv import Cap, W4Agent, web4
from zxv.web4 import ConsentScope, W4ConsentLog, W4MsgKind

T0 = 1_700_000_000_000
with zxv.Context() as ctx:
    cap = ctx.root_capability(Cap.ALL_1_5)
    with W4Agent(cap, "alice", T0) as alice, W4Agent(cap, "bob", T0) as bob, \
            W4Agent(cap, "human", T0) as human, W4ConsentLog(16) as log:
        action = web4.action_digest("pay", alice.agent_id, bob.agent_id, 7, b"amount=100")
        token = human.issue_consent(alice.agent_id, action, ConsentScope.MONEY, max_vfv=1000,
                                    issued_ms=T0, expires_ms=T0 + 600_000)
        log.check(token, human_public_key=human.public_key, agent_id=alice.agent_id, action=action,
                  need=ConsentScope.MONEY, amount=100, now_ms=T0 + 1)    # spends the token
        env = alice.seal(bob.agent_id, W4MsgKind.REQUEST, req_id=7, tool="pay", payload=b"amount=100",
                         now_ms=T0 + 2, consent=token)
        web4.verify_envelope(env, alice.public_key)
        print(web4.decode_envelope(env).tool, repr(alice))
```

Limits: envelopes are signed, not encrypted. A consent token proves that a
key holder signed it. It cannot show that the holder is the right human or
that they understood the action. The receiving runtime (replay window, rate
limits), the Web 2 and Web 3 bridges and other agent protocols are not in
this ABI.

### Paradox book (`zxv.paradox`)

`ParadoxBook(cap)` (needs `Cap.PARADOX`) classifies a `Hexagram` tag into
a `Classification` (kind CONSTRUCTIVE, DESTRUCTIVE, NEUTRAL, SPECIAL or
NONE). It routes the tag to one operator:
- `constructive`: a bounded update of a `ParadoxPolicy`.
- `destructive`: contain and roll back in place; returns a `RepairProof`.
- `neutral`: hold the branches of a `ParadoxHold`.
- `special`: a frontier record, which can then be drafted and reviewed.

It can also run `graph_pass` over a `ParadoxGraph`. Every step goes to
hash-chained logs (`log_head`, `log_records`, `verify_log`). Changes that
touch money or devices raise `OwnerApprovalRequiredError` without
`owner_ok`.

```python
import zxv
from zxv import Cap, HexBase, HexPhase, HexPolarity, Hexagram, paradox
from zxv.paradox import ParadoxBook, ParadoxLog, ParadoxPolicy

with zxv.Context() as ctx:
    cap = ctx.root_capability(Cap.ALL_1_5)
    with ParadoxBook(cap, 64, 16) as book, ParadoxPolicy(cap, [0, 0], [100, 50], [50, 25]) as policy:
        tag = Hexagram(HexBase.BOTH, HexPolarity.NEUTRAL, HexPhase(3))
        c = book.classify(tag, evidence=(3, 1, 5, 1))           # improving evidence: constructive
        print(c.kind, c.action, book.constructive(policy, c, [60, 20], tension=5), policy.values)
        print(book.constructive(policy, c, [200, 20], tension=4))  # out of bounds: proposal for owner
        book.verify_log(ParadoxLog.OPS)
```

Limits: automatic changes happen only inside published bounds, and the
operators have no balance input. `owner_ok` is the caller's assertion,
since this library cannot check who the owner is inside one process.
Detection covers only the checks the caller runs. Destructive work is
contained, never answered. This is contradiction-tolerant bookkeeping; it
never makes both sides true. All of it is advisory to the owner.

## Vino Embed (ABI 1.6)

`zxv.vino` (`Feature2.VINO`) is what an app or game needs to take part in
Vino **without ever holding anyone's value or keys**. There are two lanes,
and they never meet:

- **PLAY**: `VinoPlay(cap)` is a sandbox inside your app, with its own
  play units, issuers, holders and Fibonacci-face vouchers. It lives in
  process memory only. Play units are not money and can never be
  converted into, redeemed for, or moved into live value: no call takes a
  play sandbox and anything live.
- **LIVE**: real Vino stays in the user's own ZXV wallet. Your app builds a
  `PaymentRequest`. The user approves it in their wallet, which signs a
  `Receipt` with its ML-DSA-87 key, and your app checks that receipt
  offline with `Receipt.verify`. Your app never sees a balance, a key or a
  custody account.

Some rules:

- **One rail at a time.** A voucher is one coin, held by one holder on
  exactly one rail: `VinoRail.BEARER` (a note in the hand) or
  `VinoRail.GHOST` (its twin on the play ledger). A transfer must name the
  active rail or it fails with `RailError`, so a voucher can never be spent
  twice. `set_rail` switches rails and moves nothing.
- **Coverage.** An issuer may only issue while `10 * backing >= 18 *
  outstanding` (1.8x, exact integers). An issue or `unback` that would
  break this fails with `CoverageError`.
- **Redeeming pays nothing out.** `redeem` returns a voucher to its issuer
  and retires it, so the issuer's outstanding falls. No call turns a
  voucher into backing, play units or anything else. There is no interest,
  yield or return of any kind.
- **Chance outcomes.** A LIVE request marked `RequestFlag.CHANCE` (a loot
  box, a paid draw) is refused with `ChanceOutcomeError`. The raffle is a
  separate module, not a payment request.
- **Daily cap.** `SpendMeter(cap, daily_cap).admit(day, amount)` counts
  what one device approved per day. An amount that would pass the cap is
  refused with `LimitRefusedError`, and its `.left` says what is left
  today. The meter is a counter, not a legal check, and makes no exemption
  claim.
- **Capabilities.** `VinoPlay` and `SpendMeter` need `Cap.VINO`, and so
  does every call on them (each method takes an optional `cap=`).
  `PaymentRequest.encode/decode/id`, `Receipt.message` and `Receipt.verify`
  are pure and need no capability. `Receipt.sign` needs `Cap.CRYPTO_SIGN`;
  it belongs to the wallet, not the app. `Cap.ALL_1_6` holds every bit as
  of 1.6, and the older `Cap.ALL*` constants keep their values.
- **Numbers.** Amounts, faces, ids and times are exact Python `int`s
  (uint64), and floats are refused. Times are seconds on your clock (Unix
  time is the convention).
- **Bytes.** Requests are 160 bytes, request ids 32, receipt messages 88.
  ML-DSA-87 public keys are 2592 bytes, secret keys 4896 and signatures 4627.

| Status | Exception | Class |
|---|---|---|
| `ZXV_E_RAIL` -46 | `RailError` | refused |
| `ZXV_E_COVERAGE` -47 | `CoverageError` | refused |
| `ZXV_E_LANE` -48 | `LaneError` | usage |
| `ZXV_E_EXPIRED` -49 | `RequestExpiredError` | refused |
| `ZXV_E_CHANCE` -50 | `ChanceOutcomeError` | refused |

**A play-lane shop.** An issuer backs, issues a voucher to a player, the
player spends it, and the receiver redeems it:

```python
import zxv
from zxv import Cap
from zxv.vino import VinoPlay, VinoRail

with zxv.Context() as ctx, VinoPlay(ctx.root_capability(Cap.VINO)) as play:
    shop = play.open_issuer(owner=1)          # account 0
    player = play.open_holder(owner=2)        # account 1
    friend = play.open_holder(owner=3)        # account 2
    play.back(shop, 18)                       # play backing from the faucet: worth nothing
    try:
        play.issue(shop, player, 13)          # 10*18 < 18*13: below 1.8x coverage
    except zxv.CoverageError:
        pass
    vid = play.issue(shop, player, 8, VinoRail.BEARER)    # one voucher, face 8
    play.transfer(vid, player, friend, VinoRail.BEARER)   # the player spends it
    play.redeem(vid, friend)                  # back to the shop and retired; nothing is paid out
    print(play.voucher(vid).state.name, play.account(shop).faces)   # REDEEMED 0
    play.check()                              # every invariant holds
```

**A live payment request, a wallet receipt and an offline check.** The
wallet half is shown only so the example runs. In a real deployment the
user's wallet (`zxvd`) signs, and the app never holds the wallet's key:

```python
import zxv
from zxv import Cap
from zxv.vino import PaymentRequest, Receipt, VinoLane

now = 1_700_000_000
merchant = bytes(32)                          # your payee id, e.g. SHA-256 of your key
req = PaymentRequest(VinoLane.LIVE, merchant, zxv.random_bytes(16), amount=1_250,
                     created_at=now, expires_at=now + 600, memo="Sword of Dawn")
data = req.encode()                           # 160 bytes to hand to the wallet
assert PaymentRequest.decode(data) == req

# --- wallet side (the user's wallet, not your app) ---
with zxv.Context() as wallet_ctx:
    wcap = wallet_ctx.root_capability(Cap.CRYPTO_SIGN)
    with zxv.MlDsa87(wcap).generate() as wallet:
        receipt = Receipt.sign(wcap, data, wallet.secret_key, wallet.public_key, approved_at=now + 30)
        wallet_pk = wallet.public_key

# --- app side, offline, no capability needed ---
checked = Receipt(data, wallet_pk, receipt.approved_at, receipt.signature)
checked.verify(now=now + 60)                  # raises VerificationFailedError or RequestExpiredError
print(checked.is_valid(now + 601))            # False: the request expired at now + 600
```

Honest limits: play units are not money, and they are lost when the
sandbox is closed. The LIVE flow here is only the app's half. Approving a
request happens in the user's wallet, which posts the payment on its own
ledger. This SDK has no way to mint live Vino, set pegs, or take any operator
action. ML-DSA-87 here is the reference
implementation, not FIPS 140-3 validated. Whether a given deployment of
Vino is store credit, a prepaid access product or something else is a
legal question pending counsel, and nothing in this SDK decides it.

## API overview

| Area | Python | Notes |
|---|---|---|
| Library | `zxv.load_library`, `abi_version`, `features`, `has_feature`, `Feature`, `status_name`, `status_class`, `last_error` | lazy load, version check |
| Context | `Context`, `ctx.halt`, `ctx.is_poisoned`, `ctx.currencies`, `FatalPolicy` | thread-safe |
| Capability | `ctx.root_capability`, `Capability.attenuate/revoke/set_attestation/check/require/info`, `Cap` | checked every call |
| Values | `Rational`, `Truth` | exact; no implicit bool |
| Ledger | `Ledger(cap).open_account/post/balance/journal/check`, `AccountKind`, `Balance`, `Rail` | int minor units |
| Currencies | `ctx.currencies.enable(alpha, cap=cap)`, `.register_private(code, mu, cap=cap)`, `.get/enabled`, `zxv.iso4217.lookup/by_numeric/codes` | ISO 4217; `enable`/`register_private` need `LEDGER_POST`; without `cap=` they are deprecated |
| ISO 20022 | `MxMessage(cap, MxKind.PACS008).set_text/set_amount/add_entry/render` | pacs.008, camt.053 live; needs `LEDGER_READ`, re-checked on every call; `MxMessage(ctx, ...)` is deprecated |
| Pending 1.0 groups | `NettingCycle`, `Cards`, `Vss` | `ZxvNotImplementedError` in this build (card check digits work) |
| Attestations | `Attestations(cap).attest/evidence/find/state/gate`, `Subject` | four-valued |
| Budgets | `Budgets(cap).create`, `Budget.consume/try_consume/consume_async/consume_wait/release/set_limit/status`, `on_exhausted` | exact rationals |
| Phase | `PhaseClock(cap).now/advance/admit/set_health/defer/until_tick/next_due/start_realtime`, `DeferredEvent.wait_cleared` | asyncio awaitables |
| Wire | `Ubh168Frame.build/idle/from_bytes/to_bytes/resolve`, `tag_of`, `WireTruth` | pure Python |
| Swarm | `Swarm(cap, levels, tpc).register/set_mood/set_feeling/begin_cycle/allotment/consume/end_cycle` | emoji as int or str |
| Hashes | `Hashes.sha3_256/shake256/keccak256`, `random_bytes` | pure functions |
| Crypto | `Ed25519(cap)`, `MlDsa87(cap)`, `MlKem1024(cap)`, `SecretKey`, `KeyPair`, `Encapsulation` | `verify` returns bool |
| Engine | `Model.open/from_bytes`, `Model.tokenize/detokenize/new_session/load_lora`, `Session.generate/stream/stream_async/generate_async/bind_budget/reset`, `GenerateOptions`, `StopReason` | GIL released |
| EVM | `StateChannel(cap, channel_id).encode_state/ledger_state`, `EvmState` | Keccak digest |
| Hardware | `HwProfile`, `HwMatrix(cap).call/shadow/memo/add_provider/attest/providers/set_policy/apply_contract`, `HwReleaseKey.generate/build`, `HwRegion(cap, device).stage/trial/commit/rollback/log` | needs `HW_MATRIX` / `HW_RECONF` |
| Library 1.5 | `features2`, `has_feature2`, `Feature2`, `Cap.ALL_1_5` | group bits |
| Treaty | `Treaty.from_sutra/from_record/to_sutra/to_record/validate/run`, `TreatyResult`, `pic_*` | `TreatyError.fault/offset` |
| CID / AIPI | `Cid.compute/unixfs/parse/verify/to_text/to_v1`, `AipiFunction/AipiCall/AipiClaim.encode/decode`, `AipiNode(cap).put/get/publish/load/plan` | needs `AIPI` |
| Coin | `coin.face/rung/split/token_face/token_tier/code_*`, `TokenHolding`, `TokenWallet(cap).mint/spend/transfer/set_octane` | wallet needs `MINT` |
| Fee | `fee.assure/assure_carry/split/default_policy`, `FeeAccount` | pure |
| V-Bill hub | `VHub(cap).add_currency/set_peg/open_account/issue/add_limit/quote/swap/balance/check` | test currencies only |
| Freight | `FreightHeader`, `encode/decode/split/set_count`, `FreightJoin`, `ops_encode/ops_expand/ops_peek/gen_run`, `OpsWriter` | resolver callbacks |
| Web4 | `W4Agent(cap)`, `verify_card`, `W4Directory`, `W4Manifest`, `action_digest`, `W4ConsentLog`, `decode_envelope/verify_envelope` | keys stay in handles |
| Paradox | `ParadoxBook(cap)`, `ParadoxPolicy`, `ParadoxHold`, `ParadoxGraph`, `Classification`, `RepairProof` | needs `PARADOX`; advisory |
| Library 1.6 | `Feature2.VINO`, `Cap.VINO`, `Cap.ALL_1_6` | group bit, cap bit |
| Vino | `VinoPlay(cap).open_account/open_issuer/open_holder/back/unback/issue/transfer/set_rail/redeem/voucher/account/vouchers/check`, `PaymentRequest.encode/decode/id`, `Receipt.message/sign/verify/is_valid`, `SpendMeter(cap, daily_cap).admit`, `VinoRail`, `VinoLane`, `VoucherState`, `RequestFlag` | play lane needs `VINO`; requests and verify are pure |

Every public class and method has a docstring: `help(zxv.Session)`.

## Examples

| File | Shows |
|---|---|
| [`examples/quickstart.py`](examples/quickstart.py) | ledger, attenuated capability, exact rationals, float refusal, `Truth` |
| [`examples/orchestrate_sessions.py`](examples/orchestrate_sessions.py) | AI orchestration: several `Session`s on several threads ("nodes") sharing one model, each bound to an exact budget sized by the swarm economy, admitted by the phase clock, model attested first, every answer signed with ML-DSA-87, and a bit-identical re-run |
| [`examples/async_pipeline.py`](examples/async_pipeline.py) | asyncio: real-time phase driver, `consume_async` waiting for a refill, async token streaming |

```sh
ZXV_LIBRARY_PATH=../libzxv/build python examples/orchestrate_sessions.py
```

Generation with budgets and streaming, briefly:

```python
with zxv.Budgets(cap) as budgets, zxv.Model.open(cap, "model.gguf") as model, \
        model.new_session(n_ctx=2048) as session:
    session.bind_budget(budgets.create("chat", zxv.Resource.TOKENS, 4096))
    opts = zxv.GenerateOptions(max_tokens=256, temperature="0.7", top_k=40, top_p="0.95", seed=7)
    for token in session.stream("Hello", opts):
        print(token.text, end="", flush=True)
```

## Tests

```sh
cd sdk/python
.venv/bin/python -m pytest -q        # uses ../libzxv/build unless ZXV_LIBRARY_PATH is set
.venv/bin/python -m mypy             # strict
```

The tests run against the real library and check `sdk/conformance/vectors.json`
(status classes, rationals, IEEE-754 boundary, Q16, logic tables, UBH-168
frames byte for byte, hashes, Ed25519, ML-DSA/ML-KEM digests, swarm
allotments, greedy and seeded generation tokens, EVM encoding and digest),
the hardware layer (R1, the standard engines, placement policy, Python
implementations, a signed image staged, tested and committed on a simulated
device with its events and audit log, a contract package),
the ABI 1.5 vectors (`status_abi15`, treaty, cid, aipi, coin, fee, vhub,
freight, web4, paradox) with handle lifetimes, buffer growth, every new
status's exception, capability refusals and a poisoned context,
the ABI 1.6 vectors (`status_abi16` and `vino`: request bytes and ids,
decode refusals, the fixed-seed receipt and its checks, the play scenario
and the meter scenario) with the same lifetime, buffer, capability and
poison checks for the play sandbox and the meter,
plus the fatal path via `zxv_ctx_halt` (including `FatalPolicy.ABORT` in a
subprocess), capability denial, budget exhaustion and async waiting, phase
deferral, the GIL release, and `test_cdef_drift.py`, which fails if any
`ZXV_API` function in the header is missing from, or differs in, the cffi
declarations in `zxv/_cdef.py`.

## Honest limits

- **CPU integer engine only.** There is no GPU or accelerator offload in
  ABI 1.2. "Nodes" in the orchestration example are threads in one process.
  What does carry over to real clusters is determinism: any node can re-run
  and check any other node's output bit for bit.
- **Capabilities are least privilege, not a sandbox.** Code in the same
  process can create its own root capability from the context. The process
  boundary (zxvd over IPC) is where it becomes enforcement.
- **Fatal is as strong as Python allows.** `ZxvFatal` can still be caught by
  a bare `except:` or `except BaseException:`; the C context stays poisoned
  regardless, and `FatalPolicy.ABORT` is the uncatchable form.
- **Secret wiping is best effort.** Bytes you pass in from Python (e.g. to
  `SecretKey.from_bytes`, or public data) may remain in the Python heap; the
  OS may swap pages; core dumps may contain keys. The crypto is not FIPS
  140-3 validated.
- **Handles and threads.** A `Context`, `Model`, `Budgets`, `PhaseClock`,
  `Attestations` and `Swarm` are safe to share across threads; a `Session`
  is serialized by a lock; an `MxMessage` or `NettingCycle` must be used by
  one thread at a time. Do not `close()` an object while another thread is
  still calling it.
- **Not implemented in this build:** LoRA (`Model.load_lora` raises
  `ZxvNotImplementedError`; `Feature.LORA` is never set), netting, card
  issuing/authorization, VSS, and the pacs.002/004/009 and camt.056/029
  message kinds.
- **camt.053 reconciliation is FATAL.** `zxv_msg_render` reports a closing
  balance that does not reconcile as `ZXV_E_INVARIANT` (FATAL class) without
  poisoning the context in C; the SDK follows the class contract and marks
  the context dead. Validate statements before rendering.
- **Timeouts are host seconds.** `timeout=` arguments (and nothing else)
  accept a float, since they are wall-clock durations that never cross the
  ABI. ZXV ticks are integers.
- **No network transport.** The library serializes ISO 20022 messages and
  keeps an in-process ledger; it is not connected to, or certified by, any
  payment scheme.
