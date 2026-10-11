# Copyright (c) 2024-2026 Michael Laurence Curzi and 36N9 Genetics, LLC
# SPDX-License-Identifier: Apache-2.0
"""Quickstart: exact money, exact rationals, four-valued logic, capabilities.

Run:  ZXV_LIBRARY_PATH=../libzxv/build python examples/quickstart.py
"""

import zxv
from zxv import Cap, Rational, Truth

with zxv.Context() as ctx:
    cap = ctx.root_capability()                     # every capability
    ctx.currencies.enable("NGN", cap=cap)           # ISO 4217, 2 minor units
    ledger = zxv.Ledger(cap)
    bank = ledger.open_account("bank", "NGN", zxv.AccountKind.ISSUER)
    alice = ledger.open_account("alice", "NGN")
    ledger.post(bank, alice, 50_000, "NGN", "E2E-0001")  # 500.00 NGN, in kobo
    print("alice:", ledger.balance(alice))

    # Hand a read-only view to code that should not move money.
    auditor = zxv.Ledger(cap.attenuate(Cap.LEDGER_READ))
    try:
        auditor.post(alice, bank, 1, "NGN", "nope")
    except zxv.CapabilityDeniedError as e:
        print("auditor cannot post:", e.status_name)

# Exact arithmetic: 0.1 + 0.2 is 0.3, and a float is never accepted silently.
print(Rational.parse("0.1") + Rational.parse("0.2"))            # 0.3
print(repr(Rational.from_float_exact(0.1)))                     # the double's exact value
try:
    Rational(1) + 0.1  # type: ignore[operator]
except zxv.FloatRejectedError as e:
    print("float refused:", type(e).__name__)

# Four-valued logic: BOTH is a held contradiction, never a silent bool.
evidence = Truth.from_evidence(positive=2, negative=1)
print(evidence, evidence & Truth.TRUE, evidence.to_bool(on_both=False, on_neither=False))
