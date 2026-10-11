<!-- Copyright (c) 2024-2026 Michael Laurence Curzi and 36N9 Genetics, LLC -->
<!-- SPDX-License-Identifier: Apache-2.0 -->

# Capabilities: give each part of your program only what it needs

**Who this is for:** anyone structuring a real application on ZXV, and
anyone reviewing one.
**What you will be able to do:** make narrow capabilities, pass them to the
right code, revoke them, and explain what they do and do not protect.

## What a capability is

A **capability** is a handle that carries a set of permission bits. Every
call that reads or changes state takes one, and libzxv checks the bits on
every call. A call without the right bit is refused with `ZXV_E_CAP`
(`CapabilityDeniedError` in Python, `CapabilityDeniedException` in Java and
C#).

| Bit | Allows |
|---|---|
| `LEDGER_READ` | balances, journals, account names |
| `LEDGER_POST` | opening accounts, posting, enabling currencies |
| `CRYPTO_SIGN` | ML-DSA-87 and Ed25519 keys and signatures |
| `CRYPTO_KEM` | ML-KEM-1024 key exchange |
| `AI_INFERENCE` | loading and running models |
| `BUDGET` | resource budgets |
| `PHASE` | the phase clock |
| `ATTEST` | the quarantine log; attesting inbound records |
| `SWARM` | the swarm token economy |
| `EVM` | EVM state-channel encoding |

## Mint once, then narrow

You make one **root** capability per context, then derive narrower ones with
`attenuate`. Attenuating can only remove bits, never add them.

```python
import zxv
from zxv import Cap

with zxv.Context() as ctx:
    root = ctx.root_capability()
    ctx.currencies.enable("NGN", cap=root)
    teller = root.attenuate(Cap.LEDGER_READ | Cap.LEDGER_POST)
    auditor = teller.attenuate(Cap.LEDGER_READ)

    bank = zxv.Ledger(teller).open_account("bank", "NGN", zxv.AccountKind.ISSUER)
    ada = zxv.Ledger(teller).open_account("ada", "NGN")
    zxv.Ledger(teller).post(bank, ada, 500, "NGN", "E2E-1")
    zxv.Ledger(auditor).balance(ada).equity              # 500
    zxv.Ledger(auditor).post(ada, bank, 100, "NGN", "E2E-2")
    # ^ raises CapabilityDeniedError (ZXV_E_CAP)
```

If you ask `attenuate` for a bit the parent does not hold, you get a
capability with no bits at all (`auditor.attenuate(Cap.LEDGER_POST).caps` is
`0`). It is not an error, and it never widens anything.

**Do not pass the root around.** Keep it where the program starts, make the
narrow capabilities there, and hand each module only the one it needs. The
framework kits enforce this: the root is never injectable (see
[Spring and ASP.NET Core](frameworks.md)).

## Revoking

`revoke()` turns a capability off, and every capability derived from it too:

```python
teller.revoke()
zxv.Ledger(auditor).balance(ada)     # raises CapabilityDeniedError: auditor came from teller
```

## Attestation: a capability can be in doubt

Each capability also carries an **attestation**, a [`Truth`](four-valued-logic.md)
value saying how much you trust its holder. Only a capability attested
`TRUE` (the default) is accepted. `FALSE`, `NEITHER` (no evidence yet) and
`BOTH` (contradictory evidence about the holder) are all refused on every
call:

```python
clerk = root.attenuate(Cap.LEDGER_READ)
clerk.set_attestation(zxv.Truth.BOTH)
zxv.Ledger(clerk).balance(ada)
# CapabilityDeniedError: "ledger_balance: attestation is BOTH (contradictory attestation)"
```

The attestation is copied when you attenuate. A child made before the parent
was marked `BOTH` keeps its own value, so mark the right capability, or
revoke the parent to cut off its children.

## Other refusals

- A capability from another context is refused with `ZXV_E_CAP`.
- In C, use the `*_cap` forms, such as `zxv_ledger_post_cap(cap, ...)`
  (added in ABI 1.2). The older forms that take a context are deprecated.

```c
zxv_cap *root, *reader;
zxv_cap_create(ctx, 1, NULL, ZXV_CAP_ALL, &root);
zxv_cap_attenuate(root, ZXV_CAP_LEDGER_READ, &reader);
zxv_ledger_balance_cap(reader, ada, NULL, NULL, &equity);   /* allowed */
/* zxv_ledger_post_cap(reader, ...) returns ZXV_E_CAP */
zxv_cap_destroy(reader);                                     /* children first */
zxv_cap_destroy(root);
```

## What capabilities do not do

Capabilities are **least privilege inside one process**. They stop your own
code from calling the wrong thing by mistake, and they make it clear in
review who can do what. They are **not a sandbox**: code running in the same
process can read memory, call libzxv with another handle, or skip the SDK
altogether. To contain code you do not trust, run it in a separate process
with [Tri-Isolate](writing-a-plugin.md#step-5-run-it-in-its-own-process),
and read [the security model](../SECURITY.md).

## Next

- [Errors and fail-closed](errors-and-fail-closed.md)
- [SDK_DESIGN §6](../SDK_DESIGN.md#6-capabilities-on-every-call)
