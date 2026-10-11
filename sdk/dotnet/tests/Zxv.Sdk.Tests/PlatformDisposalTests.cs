// Copyright (c) 2024-2026 Michael Laurence Curzi and 36N9 Genetics, LLC
// SPDX-License-Identifier: Apache-2.0

using System;
using Zxv.Currencies;
using Zxv.Ledger;
using Zxv.Messaging;

namespace Zxv.Sdk.Tests;

/// <summary>
/// Disposing a platform makes it observably disposed at once, while native
/// teardown waits for the children that still hold the context (ABI.md R6).
/// </summary>
public sealed class PlatformDisposalTests
{
    [Fact]
    public void Disposed_platform_throws_even_while_a_capability_is_alive()
    {
        ZxvPlatform p = TestPlatform.Create();
        using Capability root = p.CreateRootCapability();

        p.Dispose();

        Assert.Throws<ObjectDisposedException>(() => p.IsHalted);
        Assert.Throws<ObjectDisposedException>(() => p.Halt("late"));
        Assert.Throws<ObjectDisposedException>(() => p.CreateRootCapability());
        Assert.Throws<ObjectDisposedException>(() => p.Ledger);
        Assert.Throws<ObjectDisposedException>(() => p.Currencies);
        Assert.Throws<ObjectDisposedException>(() => p.Messages);
        Assert.Throws<ObjectDisposedException>(() => p.Payments);
        Assert.Throws<ObjectDisposedException>(() => p.Statements);
        Assert.Throws<ObjectDisposedException>(() => p.CreateBudgets(root));
        Assert.Throws<ObjectDisposedException>(() => p.CreateQuarantineLog(root, 4));

        // The capability is a child with its own lifetime: it keeps working
        // until it is disposed, and keeps the native context alive meanwhile.
        Assert.False(root.GetInfo().IsRevoked);
        Assert.False(p.Context.Handle.IsClosed);
    }

    [Fact]
    public void Services_obtained_before_dispose_throw_afterwards()
    {
        ZxvPlatform p = TestPlatform.Create();
        using Capability root = p.CreateRootCapability();
        LedgerService ledger = p.Ledger;
        CurrencyService currencies = p.Currencies;
        MessageService messages = p.Messages;
        CapabilityLedger view = ledger.WithCapability(root);
        Account issuer = view.OpenAccount("Issuer", "NGN", AccountKind.Issuer);
        Money one = Money.FromMinorUnits(1, currencies.GetEnabled("NGN"));

        p.Dispose();

        Assert.Throws<ObjectDisposedException>(() => ledger.GetBalances(issuer.Id));
        Assert.Throws<ObjectDisposedException>(() => ledger.Post(new Transfer(issuer.Id, issuer.Id, one, "X")));
        Assert.Throws<ObjectDisposedException>(() => ledger.TryVerify(out _));
        Assert.Throws<ObjectDisposedException>(() => ledger.WithCapability(root));
        Assert.Throws<ObjectDisposedException>(() => view.GetBalances(issuer.Id));
        Assert.Throws<ObjectDisposedException>(() => view.OpenAccount("Late", "NGN"));
        Assert.Throws<ObjectDisposedException>(() => currencies.GetEnabled("NGN"));
        Assert.Throws<ObjectDisposedException>(() => currencies.Enable(root, "KES"));
        Assert.Throws<ObjectDisposedException>(() => messages.IsSupported(MxMessageKind.Pacs008));
    }

    [Fact]
    public void Native_context_is_destroyed_after_the_last_child()
    {
        ZxvPlatform p = TestPlatform.Create();
        Capability root = p.CreateRootCapability();
        Capability child = root.Attenuate(CapabilityRights.LedgerRead);
        Interop.ZxvContextHandle ctx = p.Context.Handle;

        p.Dispose();
        Assert.False(ctx.IsClosed);

        root.Dispose(); // the child still holds the root, and through it the context
        Assert.False(ctx.IsClosed);
        Assert.Equal(CapabilityRights.LedgerRead, child.Rights);

        child.Dispose(); // child, then root, then the context: R6
        Assert.True(ctx.IsClosed);
    }

    [Fact]
    public void Disposed_capability_throws_while_its_child_still_works()
    {
        using ZxvPlatform p = TestPlatform.Create();
        Capability root = p.CreateRootCapability();
        using Capability child = root.Attenuate(CapabilityRights.LedgerRead);

        root.Dispose();
        root.Dispose(); // idempotent

        Assert.Throws<ObjectDisposedException>(() => root.GetInfo());
        Assert.Throws<ObjectDisposedException>(() => root.Attenuate(CapabilityRights.LedgerRead));
        Assert.Throws<ObjectDisposedException>(() => root.Demand(CapabilityRights.LedgerRead));
        Assert.False(child.GetInfo().IsRevoked);
        child.Demand(CapabilityRights.LedgerRead);
    }

    [Fact]
    public void Without_children_the_context_is_destroyed_at_once()
    {
        ZxvPlatform p = TestPlatform.Create();
        Interop.ZxvContextHandle ctx = p.Context.Handle;

        p.Dispose();

        Assert.True(ctx.IsClosed);
        Assert.Throws<ObjectDisposedException>(() => p.IsHalted);
    }

    [Fact]
    public void Dispose_is_idempotent_and_wins_over_halted()
    {
        ZxvPlatform p = TestPlatform.Create();
        p.Halt("test");
        Assert.True(p.IsHalted);

        p.Dispose();
        p.Dispose();

        Assert.Throws<ObjectDisposedException>(() => p.IsHalted);
    }
}
