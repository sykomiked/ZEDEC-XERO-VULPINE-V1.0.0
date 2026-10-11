# Copyright (c) 2024-2026 Michael Laurence Curzi and 36N9 Genetics, LLC
# SPDX-License-Identifier: Apache-2.0
"""Ledger, currencies, ISO 4217 and ISO 20022 rendering."""

from __future__ import annotations

import pytest

import zxv
from zxv import Cap


def test_post_balance_journal_check(ctx: zxv.Context, cap: zxv.Capability) -> None:
    ctx.currencies.enable("NGN", cap=cap)
    assert "NGN" in ctx.currencies and ctx.currencies.enabled() == ["NGN"]
    ledger = zxv.Ledger(cap)
    bank = ledger.open_account("bank", "NGN", zxv.AccountKind.ISSUER)
    alice = ledger.open_account("alice", "NGN")
    bob = ledger.open_account("bob", "NGN")
    assert ledger.accounts() == [bank, alice, bob]
    assert ledger.account_name(alice) == "alice" and ledger.account_currency(bob) == "NGN"
    assert ledger.account_kind(bank) is zxv.AccountKind.ISSUER
    e1 = ledger.post(bank, alice, 50_000, "NGN", "E2E-1")
    e2 = ledger.post(alice, bob, 20_000, "NGN", "E2E-2")
    assert e2 > e1
    assert ledger.balance(alice) == zxv.Balance(50_000, 20_000, 30_000)
    assert ledger.balance(bank).equity == -50_000
    j = ledger.journal(alice)
    assert [(x.amount, x.counterparty, x.reference) for x in j] == [(50_000, bank, "E2E-1"),
                                                                      (-20_000, bob, "E2E-2")]
    with pytest.raises(zxv.InsufficientFundsError):
        ledger.post(bob, alice, 20_001, "NGN", "E2E-3")
    with pytest.raises(zxv.ZxvUsageError):
        ledger.post(bank, alice, 1, "NGN", "E2E-1")  # duplicate reference
    with pytest.raises(zxv.FloatRejectedError):
        ledger.post(bank, alice, 1.5, "NGN", "E2E-4")  # type: ignore[arg-type]
    ledger.check()


def test_private_units_and_iso4217(ctx: zxv.Context, cap: zxv.Capability) -> None:
    ctx.currencies.register_private("VFV", 2, cap=cap)
    vfv = ctx.currencies.get("VFV")
    assert vfv is not None and zxv.CurrencyFlag.PRIVATE in vfv.flags
    assert zxv.iso4217.lookup("VFV") is None
    ngn = zxv.iso4217.lookup("NGN")
    assert ngn is not None and ngn.numeric == 566 and ngn.minor_units == 2 and ngn.name == "Naira"
    assert zxv.iso4217.by_numeric(566) == ngn
    assert zxv.iso4217.by_numeric(777) is None
    assert "Vino" in zxv.iso4217.caveat(777) or zxv.iso4217.caveat(777)
    assert zxv.iso4217.is_african_union_member("NG") and not zxv.iso4217.is_african_union_member("FR")
    assert [r.numeric for r in zxv.Rail] == [555, 777, 888]
    assert "NGN" in zxv.iso4217.codes()


def test_pacs008_render(cap: zxv.Capability) -> None:
    with zxv.MxMessage(cap, zxv.MxKind.PACS008) as m:
        (m.set_text(zxv.MxField.MSG_ID, "MSG-0001")
          .set_text(zxv.MxField.CRE_DT_TM, "2026-10-09T12:00:00Z")
          .set_text(zxv.MxField.DEBTOR_NAME, "Ada & Sons <Lagos>")
          .set_text(zxv.MxField.DEBTOR_ACCT, "NG0001")
          .set_text(zxv.MxField.CREDITOR_NAME, "Kwame Mensah"))
        with pytest.raises(zxv.ZxvUsageError) as e:
            m.render()
        assert e.value.status_name == "ZXV_E_FIELD"
        m.set_text(zxv.MxField.CREDITOR_ACCT, "GH0002").set_text(zxv.MxField.END_TO_END_ID, "E2E-1")
        with pytest.raises(zxv.ZxvUsageError):
            m.set_text(zxv.MxField.CCY, "VFV")  # private units never go on the wire
        m.set_text(zxv.MxField.CCY, "NGN").set_amount(zxv.MxAmount.SETTLEMENT, 250050, 2)
        xml = m.render()
    assert "2500.50" in xml and "Ada &amp; Sons &lt;Lagos&gt;" in xml


def test_pending_groups_raise_not_implemented(ctx: zxv.Context) -> None:
    assert zxv.Cards.check_digit_valid(zxv.CardNetwork.DRAGON, "4111111111111111")
    assert not zxv.Cards.check_digit_valid(zxv.CardNetwork.DRAGON, "4111111111111112")
    if not zxv.has_feature(zxv.Feature.NETTING):
        with pytest.raises(zxv.ZxvNotImplementedError):
            zxv.NettingCycle(ctx, "C1", "NGN")
    if not zxv.has_feature(zxv.Feature.VSS):
        with pytest.raises(zxv.ZxvNotImplementedError):
            zxv.Vss(ctx).level()


# ------------------------------------------------- ABI 1.2: the _cap forms


def test_cap_forms_need_the_right_bit(ctx: zxv.Context, cap: zxv.Capability) -> None:
    post = cap.attenuate(Cap.LEDGER_POST)
    read = cap.attenuate(Cap.LEDGER_READ)
    # LEDGER_POST: enable, register_private, open_account
    ctx.currencies.enable("NGN", cap=post)
    ctx.currencies.enable("GHS", 2, cap=post)
    ctx.currencies.register_private("VFV", 2, cap=post)
    assert ctx.currencies.enabled() == ["NGN", "GHS", "VFV"]
    for call in (lambda: ctx.currencies.enable("KES", cap=read),
                 lambda: ctx.currencies.enable("KES", 2, cap=read),
                 lambda: ctx.currencies.register_private("VFX", 2, cap=read),
                 lambda: zxv.Ledger(read).open_account("x", "NGN")):
        with pytest.raises(zxv.CapabilityDeniedError) as e:
            call()
        assert e.value.status_name == "ZXV_E_CAP"
    a = zxv.Ledger(post).open_account("bank", "NGN", zxv.AccountKind.ISSUER)
    b = zxv.Ledger(post).open_account("alice", "NGN")
    zxv.Ledger(post).post(a, b, 7, "NGN", "R-1")
    # LEDGER_READ: account metadata and journals
    r = zxv.Ledger(read)
    assert r.accounts() == [a, b] and r.account_name(b) == "alice"
    assert r.account_currency(b) == "NGN" and r.account_kind(a) is zxv.AccountKind.ISSUER
    assert [x.reference for x in r.journal(b)] == ["R-1"]
    p = zxv.Ledger(post)
    for call in (p.accounts, lambda: p.account_name(b), lambda: p.account_currency(b),
                 lambda: p.account_kind(b), lambda: p.journal(b)):
        with pytest.raises(zxv.CapabilityDeniedError):
            call()
    # messages need LEDGER_READ and re-check the cap on every call
    with pytest.raises(zxv.CapabilityDeniedError):
        zxv.MxMessage(post, zxv.MxKind.PACS008)
    m = zxv.MxMessage(read, zxv.MxKind.PACS008)
    m.set_text(zxv.MxField.MSG_ID, "M-1")
    read.set_attestation(zxv.Truth.BOTH)
    with pytest.raises(zxv.CapabilityDeniedError):
        m.set_text(zxv.MxField.MSG_ID, "M-2")
    read.close()
    assert m.closed  # closing the cap closes the message first
    # handle creation needs BUDGET / PHASE / ATTEST / SWARM
    for ctor, bit in ((zxv.Budgets, Cap.BUDGET), (zxv.PhaseClock, Cap.PHASE),
                      (zxv.Attestations, Cap.ATTEST)):
        with pytest.raises(zxv.CapabilityDeniedError):
            ctor(post)
        ctor(cap.attenuate(bit)).close()
    with pytest.raises(zxv.CapabilityDeniedError):
        zxv.Swarm(post, 2, 10)
    zxv.Swarm(cap.attenuate(Cap.SWARM), 2, 10).close()


def test_cap_from_another_context_is_refused(ctx: zxv.Context) -> None:
    with zxv.Context() as other:
        foreign = other.root_capability()
        for call in (lambda: ctx.currencies.enable("NGN", cap=foreign),
                     lambda: ctx.currencies.enable("NGN", 2, cap=foreign),
                     lambda: ctx.currencies.register_private("VFV", 2, cap=foreign)):
            with pytest.raises(zxv.CapabilityDeniedError) as e:
                call()
            assert e.value.status_name == "ZXV_E_CAP" and "different context" in e.value.detail
        assert ctx.currencies.enabled() == [] and other.currencies.enabled() == []


def test_no_cap_forms_are_deprecated(ctx: zxv.Context) -> None:
    with pytest.warns(DeprecationWarning):
        ctx.currencies.enable("NGN")
    with pytest.warns(DeprecationWarning):
        ctx.currencies.enable("GHS", 2)
    with pytest.warns(DeprecationWarning):
        ctx.currencies.register_private("VFV", 2)
    with pytest.warns(DeprecationWarning):
        zxv.MxMessage(ctx, zxv.MxKind.PACS008).close()
    assert ctx.currencies.enabled() == ["NGN", "GHS", "VFV"]
