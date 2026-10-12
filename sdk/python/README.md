<!-- Copyright (c) 2024-2026 Michael Laurence Curzi and 36N9 Genetics, LLC -->
<!-- SPDX-License-Identifier: Apache-2.0 -->

# ZXV for Python (`zxv-sdk`)

The Python SDK for the ZXV platform core. It wraps `libzxv` (C ABI 1.4,
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
- EVM state-channel encoding.

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
pip install build && python -m build --wheel       # -> dist/zxv_sdk-1.1.0-py3-none-<platform>.whl
```

The library is found in this order: a path passed to `zxv.load_library()`,
then `ZXV_LIBRARY_PATH`, then the copy bundled in the wheel, then the system
loader path. The SDK refuses a library whose ABI is not 1.x with x >= 4 (it calls the
`zxv_hw_*` functions added in 1.4, the `zxv_hex_*` functions added in 1.3
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
