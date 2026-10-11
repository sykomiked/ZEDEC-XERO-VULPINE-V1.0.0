<!-- Copyright (c) 2024-2026 Michael Laurence Curzi and 36N9 Genetics, LLC -->
<!-- SPDX-License-Identifier: Apache-2.0 -->

# Four-valued logic, the hexagram tag and the quarantine log

**Who this is for:** developers who must decide what to do with evidence
that is missing or contradicts itself: a fraud flag, a sensor reading, a
model's output.
**What you will be able to do:** combine `Truth` values, turn a state into a
recommended action, and record forbidden states in a tamper-evident log.

## Four values instead of two

A `bool` has two values. Real evidence often has four outcomes:

| `Truth` | Number | Means |
|---|---|---|
| `NEITHER` | 0 | no evidence either way |
| `TRUE` | 1 | evidence for, none against |
| `FALSE` | 2 | evidence against, none for |
| `BOTH` | 3 | evidence for and against: a conflict |

Python examples (run against the SDK; every SDK has the same names):

```python
from zxv import Truth

Truth.from_evidence(3, 0)          # Truth.TRUE      (3 for, 0 against)
Truth.from_evidence(2, 1)          # Truth.BOTH
Truth.from_evidence(0, 0)          # Truth.NEITHER

Truth.TRUE & Truth.BOTH            # Truth.BOTH
Truth.FALSE | Truth.NEITHER        # Truth.NEITHER
~Truth.BOTH                        # Truth.BOTH
```

Two results, `BOTH & NEITHER` (gives `NEITHER`) and `BOTH | NEITHER` (gives
`BOTH`), are still waiting on a decision by the project owner. Do not build
logic that depends on those two cells. The SDKs take the tables from libzxv,
so if the decision changes them, every SDK changes together.

### No silent conversion to `bool`

`if truth:` raises `TypeError`. You must say what `BOTH` and `NEITHER` mean
for your case:

```python
t = Truth.BOTH
t.to_bool(on_both=False, on_neither=False)    # False: you chose this
t.is_certainly_true()                         # False
t.require_true("model attested")              # raises UncertainTruthError
```

## The hexagram tag: state, direction, stage

A `Hexagram` packs three things into one 6-bit tag (64 states):

- **base**: the truth value, one of `NEITHER`, `FALSE`, `BOTH`, `TRUE`.
  The base is **not** numbered like `Truth` (base `TRUE` is 3, `Truth.TRUE`
  is 1). Convert only with `Hexagram.from_truth(...)` and `to_truth()`.
- **polarity**: is the evidence getting clearer (`NEGENTROPIC`), murkier
  (`ENTROPIC`), steady (`NEUTRAL`), or has a hard limit been crossed
  (`FORBIDDEN`)?
- **phase**: where the item is in its life: `POTENTIAL` → `EMERGING` →
  `MANIFEST` → `DISSOLVING`, then back to `POTENTIAL`.

Each tag maps to one recommended action. The phase does not change it:

| Base | Polarity | Action |
|---|---|---|
| any | `FORBIDDEN` | `ROLLBACK` |
| `TRUE` | `NEUTRAL` or `NEGENTROPIC` | `PROCEED` |
| `TRUE` | `ENTROPIC` | `WAIT` |
| `FALSE` | any other | `REFUSE` |
| `BOTH` | `NEGENTROPIC` | `WAIT` |
| `BOTH` | `NEUTRAL` or `ENTROPIC` | `ESCALATE` (to a person) |
| `NEITHER` | any other | `WAIT` |

```python
from zxv import Hexagram, HexBase, HexPolarity, HexPhase

h = Hexagram(HexBase.TRUE, HexPolarity.NEGENTROPIC, HexPhase.POTENTIAL)
h.name, h.tag, h.action.name       # ('true/negentropic/potential', 60, 'PROCEED')
h = h.advance()                    # true/negentropic/emerging
h.step(HexPhase.DISSOLVING)        # raises ZxvRefusedError (ZXV_E_STATE): phases cannot be skipped

bad = h.forbid()                   # true/forbidden/emerging, action ROLLBACK
bad.advance().name                 # 'true/forbidden/manifest': still forbidden
bad.with_state(HexBase.TRUE, HexPolarity.NEUTRAL)   # raises ZXV_E_STATE
```

**Forbidden is absorbing.** Once a tag is forbidden, no step, combination or
state change can clear it. To recover you start a new item, on purpose.

The action is a recommendation for your code. It does not run anything by
itself.

## The quarantine log

When something reaches a forbidden state, you can record it in a
`QuarantineLog`. Each record holds the tag, a reason code, who submitted it,
a 32-byte subject (usually a hash of the thing you quarantined), and a hash
that links it to the record before. Changing any old record breaks the chain,
and `verify()` finds it. This is tamper-**evident**, not tamper-proof:
someone who controls the process can still replace the whole log.

```python
import zxv
from zxv import Cap, QuarantineLog

with zxv.Context() as ctx:
    root = ctx.root_capability()
    attest = root.attenuate(Cap.ATTEST)            # the log needs ATTEST
    with QuarantineLog(attest, capacity=2) as log:
        subject = zxv.Hashes.sha3_256(b"posting E2E-0042")
        seq = log.append(bad, reason=7, submitter=42, subject=subject)   # seq 0
        log.verify()                                 # OK
        log.append(h, reason=1, submitter=1, subject=subject)
        # ^ raises ZxvUsageError (ZXV_E_ARG): only forbidden tags go in the log
```

Three rules to remember:

1. Appending a tag that is not forbidden is refused with `ZXV_E_ARG`.
2. When the log is full, `append` raises `ZXV_E_CAPACITY`. Your code **must**
   then halt the context (`ctx.halt("quarantine log full")`). Carrying on
   would mean forbidden states go unrecorded.
3. If `verify()` finds a broken chain, the context is halted for you
   (`ZXV_E_INVARIANT`, a FATAL status). See
   [Errors and fail-closed](errors-and-fail-closed.md).

### Checking the chain yourself

The chain uses plain SHA-256, so an auditor can recompute it without ZXV.
This loop, run against a real log, matches `log.head`:

```python
import hashlib, struct

prev = bytes(32)
for rec in log:
    h = hashlib.sha256(
        b"zxv/lpx-qlog/v1"
        + struct.pack("<Q", rec.seq)                  # sequence, 8 bytes little-endian
        + bytes([rec.tag.tag, rec.reason])
        + struct.pack("<I", rec.submitter)            # 4 bytes little-endian
        + rec.subject + prev
    ).digest()
    assert rec.prev == prev and h == rec.hash
    prev = h
assert prev == log.head
```

## In C

```c
uint8_t tag, next;
int32_t act;
zxv_hex_pack(ZXV_HEX_BASE_TRUE, ZXV_HEX_POL_NEGENTROPIC, ZXV_HEX_PHASE_POTENTIAL, &tag);
zxv_hex_action(tag, &act);           /* zxv_hex_action_name(act) is "proceed" */
zxv_hex_forbid(tag, &next);          /* "true/forbidden/potential" -> "rollback" */
zxv_hexlog_create(attest, 1024, &log);
zxv_hexlog_append(attest, log, next, 7, 42, subject, &seq);
zxv_hexlog_verify(log);
```

The full rules are in [SDK_DESIGN §4](../SDK_DESIGN.md#4-four-valued-logic)
and [ABI changes in 1.3](../ABI.md#changes-in-13).

## Next

- [Capabilities](capabilities.md): attestation uses `Truth` too.
- [Calling C and inbound data](calling-c-and-inbound-data.md): inbound
  records are labelled with a hexagram tag.
