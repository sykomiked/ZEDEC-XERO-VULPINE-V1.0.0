# Copyright (c) 2024-2026 Michael Laurence Curzi and 36N9 Genetics, LLC
# SPDX-License-Identifier: Apache-2.0
"""Capabilities (attenuation, revocation, attestation) and the fatal path."""

from __future__ import annotations

import os
import signal
import subprocess
import sys

import pytest

import zxv
from zxv import Cap, Truth


def test_attenuation_never_widens(ctx: zxv.Context, cap: zxv.Capability) -> None:
    ro = cap.attenuate(Cap.LEDGER_READ)
    assert ro.check(Cap.LEDGER_READ) and not ro.check(Cap.LEDGER_POST)
    wider = ro.attenuate(Cap.ALL)
    assert wider.caps == Cap.LEDGER_READ
    with pytest.raises(zxv.CapabilityDeniedError):
        wider.require(Cap.LEDGER_POST)
    info = cap.info
    assert info.caps == Cap.ALL and info.attestation is Truth.TRUE and not info.revoked


def test_unknown_bits_refused(ctx: zxv.Context) -> None:
    with pytest.raises(zxv.ZxvUsageError) as e:
        ctx.root_capability(1 << 40)  # type: ignore[arg-type]
    assert e.value.status_name == "ZXV_E_ARG"


def test_identity_and_process_id(ctx: zxv.Context) -> None:
    c = ctx.root_capability(Cap.SWARM, process_id=42, identity=bytes(range(21)))
    assert c.info.process_id == 42 and c.caps == Cap.SWARM
    with pytest.raises(ValueError):
        ctx.root_capability(identity=b"short")


def test_contradictory_attestation_blocks_with_detail(ctx: zxv.Context, cap: zxv.Capability) -> None:
    cap.set_attestation(Truth.BOTH)
    assert not cap.check(Cap.LEDGER_READ)
    with pytest.raises(zxv.CapabilityDeniedError) as e:
        cap.require(Cap.LEDGER_READ)
    assert "contradictory" in e.value.detail
    cap.set_attestation(Truth.TRUE)
    assert cap.check(Cap.LEDGER_READ)


def test_capability_denial_on_ledger_and_revocation(ctx: zxv.Context, cap: zxv.Capability) -> None:
    ctx.currencies.enable("NGN", 2, cap=cap)
    ledger = zxv.Ledger(cap)
    a = ledger.open_account("issuer", "NGN", zxv.AccountKind.ISSUER)
    b = ledger.open_account("alice", "NGN")
    ledger.post(a, b, 500, "NGN", "r1")
    ro = cap.attenuate(Cap.LEDGER_READ)
    ro_ledger = zxv.Ledger(ro)
    with pytest.raises(zxv.CapabilityDeniedError):
        ro_ledger.post(a, b, 1, "NGN", "r2")
    assert ro_ledger.balance(b).equity == 500
    cap.revoke()
    assert ro.info.revoked or not ro.check(Cap.LEDGER_READ)
    with pytest.raises(zxv.CapabilityDeniedError):
        ro_ledger.balance(b)


def test_fatal_path_via_halt(ctx: zxv.Context, cap: zxv.Capability) -> None:
    ctx.currencies.enable("NGN", 2, cap=cap)
    ledger = zxv.Ledger(cap)
    a = ledger.open_account("issuer", "NGN", zxv.AccountKind.ISSUER)
    b = ledger.open_account("alice", "NGN")
    ctx.halt("test halt")
    assert ctx.is_poisoned
    # except Exception cannot swallow it
    with pytest.raises(zxv.ZxvFatal) as e:
        try:
            ledger.post(a, b, 1, "NGN", "r3")
        except Exception:  # noqa: BLE001 - this is the point of the test
            pytest.fail("ZxvFatal was caught by `except Exception`")
    assert e.value.status_name == "ZXV_E_POISONED" and zxv.status_class(e.value.status) is zxv.StatusClass.FATAL
    assert ctx.dead
    # the SDK handle is dead: every later call fails the same way, even ones
    # that would not reach C
    with pytest.raises(zxv.ZxvFatal):
        ledger.balance(b)
    with pytest.raises(zxv.ZxvFatal):
        cap.attenuate(Cap.LEDGER_READ)
    with pytest.raises(zxv.ZxvFatal):
        zxv.Budgets(cap)
    ctx.close()  # a dead context can still be destroyed
    assert ctx.closed


def test_fatal_policy_abort_terminates_the_process() -> None:
    code = (
        "import zxv\n"
        "ctx = zxv.Context(fatal_policy=zxv.FatalPolicy.ABORT)\n"
        "cap = ctx.root_capability()\n"
        "ctx.halt('operator kill switch')\n"
        "try:\n"
        "    zxv.Ledger(cap).balance(0)\n"
        "except BaseException:\n"
        "    print('caught: abort did not happen')\n"
    )
    r = subprocess.run([sys.executable, "-c", code], capture_output=True, text=True, env=dict(os.environ),
                       timeout=60)
    assert "caught" not in r.stdout
    if sys.platform != "win32":
        assert r.returncode == -signal.SIGABRT
    assert "FatalPolicy.ABORT" in r.stderr


def test_closing_parent_closes_children(ctx: zxv.Context) -> None:
    root = ctx.root_capability()
    child = root.attenuate(Cap.LEDGER_READ | Cap.BUDGET)
    grandchild = child.attenuate(Cap.LEDGER_READ)
    b = zxv.Budgets(child)
    root.close()
    assert child.closed and grandchild.closed and b.closed
    with pytest.raises(zxv.ZxvUsageError):
        child.check(Cap.LEDGER_READ)
    ctx.close()
    ctx.close()  # idempotent


def test_context_managers_everywhere() -> None:
    with zxv.Context() as ctx, ctx.root_capability() as cap, zxv.Budgets(cap) as budgets, \
            zxv.PhaseClock(cap) as clock, zxv.Attestations(cap) as reg, zxv.Swarm(cap, 2, 10) as sw:
        assert not (budgets.closed or clock.closed or reg.closed or sw.closed)
    assert ctx.closed and cap.closed and budgets.closed and clock.closed
