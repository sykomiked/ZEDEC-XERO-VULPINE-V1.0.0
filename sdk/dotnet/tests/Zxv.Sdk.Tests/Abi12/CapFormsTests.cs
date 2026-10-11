// Copyright (c) 2024-2026 Michael Laurence Curzi and 36N9 Genetics, LLC
// SPDX-License-Identifier: Apache-2.0

using System;
using System.Linq;
using Zxv.Interop;
using Zxv.Ledger;
using Zxv.Messaging;

namespace Zxv.Sdk.Tests.Abi12;

/// <summary>ABI 1.2: every gated call goes through a capability (the <c>zxv_*_cap</c> forms).</summary>
public sealed class CapFormsTests : IDisposable
{
    private static readonly string[] Enabled = { "NGN", "GHS", "VFV" };

    private static readonly DateTimeOffset At = new(2026, 10, 9, 12, 0, 0, TimeSpan.Zero);

    private readonly ZxvPlatform _p = TestPlatform.Create();

    public void Dispose() => _p.Dispose();

    private static void Denied(Action call)
    {
        var ex = Assert.Throws<CapabilityDeniedException>(call);
        Assert.Equal(ZxvStatus.CapabilityDenied, ex.Status);
    }

    [Fact]
    public void Currencies_need_LedgerPost()
    {
        using ZxvPlatform p = ZxvPlatform.Create();
        using Capability root = p.CreateRootCapability();
        using Capability post = root.Attenuate(CapabilityRights.LedgerPost);
        using Capability read = root.Attenuate(CapabilityRights.LedgerRead);

        Assert.Equal("NGN", p.Currencies.Enable(post, "NGN").Code);
        Assert.Equal((byte?)2, p.Currencies.Enable(post, "GHS", 2).MinorUnits);
        Assert.False(p.Currencies.RegisterPrivateUnit(post, "VFV", 2).IsIso4217);
        Denied(() => p.Currencies.Enable(read, "KES"));
        Denied(() => p.Currencies.Enable(read, "KES", 2));
        Denied(() => p.Currencies.RegisterPrivateUnit(read, "VFX", 2));
        Assert.Equal(Enabled, p.Currencies.ListEnabled().Select(c => c.Code));
    }

    [Fact]
    public void Accounts_and_journals_need_the_right_bit()
    {
        using Capability root = _p.CreateRootCapability();
        using Capability post = root.Attenuate(CapabilityRights.LedgerPost);
        using Capability read = root.Attenuate(CapabilityRights.LedgerRead);
        CapabilityLedger writer = _p.Ledger.WithCapability(post);
        CapabilityLedger reader = _p.Ledger.WithCapability(read);

        Account issuer = writer.OpenAccount("Issuer", "NGN", AccountKind.Issuer);
        Account alice = writer.OpenAccount("Alice", "NGN");
        Denied(() => reader.OpenAccount("Nope", "NGN"));
        writer.Post(new Transfer(issuer.Id, alice.Id, Money.FromMinorUnits(700, _p.Currencies.GetEnabled("NGN")), "CAP12-1"));

        Assert.Equal(alice, reader.GetAccount(alice.Id));
        Assert.Equal(new[] { issuer, alice }, reader.ListAccounts());
        Assert.Equal(1, reader.GetEntryCount(alice.Id));
        LedgerEntry line = Assert.Single(reader.GetEntries(alice.Id));
        Assert.Equal("CAP12-1", line.Reference);
        Assert.Equal(700, reader.GetAvailable(alice.Id).MinorUnits);

        Denied(() => writer.GetAccount(alice.Id));
        Denied(() => writer.ListAccounts());
        Denied(() => writer.GetEntryCount(alice.Id));
        Denied(() => writer.GetEntries(alice.Id));
    }

    [Fact]
    public void Messages_need_LedgerRead()
    {
        using Capability root = _p.CreateRootCapability();
        using Capability read = root.Attenuate(CapabilityRights.LedgerRead);
        using Capability post = root.Attenuate(CapabilityRights.LedgerPost);
        Money amount = Money.FromMinorUnits(250_050, _p.Currencies.GetEnabled("NGN"));
        var pacs = new Pacs008Message("MSG-1", At, new PartyAccount("Ada", "NG0001"), new PartyAccount("Kwame", "GH0002"), amount, "E2E-1");
        var camt = new Camt053Message("STMT-1", At, "NG0001", amount, amount, Array.Empty<StatementEntry>());

        Assert.Contains("2500.50", _p.Messages.BuildPacs008(read, pacs).Xml, StringComparison.Ordinal);
        Assert.Contains("camt.053", _p.Messages.BuildCamt053(read, camt).Xml, StringComparison.Ordinal);
        Denied(() => _p.Messages.BuildPacs008(post, pacs));
        Denied(() => _p.Messages.BuildCamt053(post, camt));
        read.SetAttestation(Truth.Neither);
        Denied(() => _p.Messages.BuildPacs008(read, pacs));
    }

    [Fact]
    public void Handle_creates_need_their_bit()
    {
        using Capability root = _p.CreateRootCapability();
        using Capability none = root.Attenuate(CapabilityRights.LedgerRead);
        Denied(() => _p.CreateBudgets(none).Dispose());
        Denied(() => _p.CreatePhaseClock(none).Dispose());
        Denied(() => _p.CreateAttestations(none).Dispose());
        Denied(() => _p.CreateSwarm(none, 2, 10).Dispose());

        using Capability b = root.Attenuate(CapabilityRights.Budget);
        using Capability ph = root.Attenuate(CapabilityRights.Phase);
        using Capability at = root.Attenuate(CapabilityRights.Attest);
        using Capability sw = root.Attenuate(CapabilityRights.Swarm);
        _p.CreateBudgets(b).Dispose();
        _p.CreatePhaseClock(ph).Dispose();
        _p.CreateAttestations(at).Dispose();
        _p.CreateSwarm(sw, 2, 10).Dispose();
    }

    [Fact]
    public void A_capability_of_another_platform_is_refused()
    {
        using ZxvPlatform other = ZxvPlatform.Create();
        using Capability foreign = other.CreateRootCapability();
        Money amount = Money.FromMinorUnits(1, _p.Currencies.GetEnabled("NGN"));
        var pacs = new Pacs008Message("MSG-1", At, new PartyAccount("Ada", "NG0001"), new PartyAccount("Kwame", "GH0002"), amount, "E2E-1");

        Assert.Throws<ArgumentException>(() => _p.Currencies.Enable(foreign, "USD"));
        Assert.Throws<ArgumentException>(() => _p.Currencies.Enable(foreign, "USD", 2));
        Assert.Throws<ArgumentException>(() => _p.Currencies.RegisterPrivateUnit(foreign, "VFX", 2));
        Assert.Throws<ArgumentException>(() => _p.Messages.BuildPacs008(foreign, pacs));
        Assert.Throws<ArgumentException>(() => _p.Ledger.WithCapability(foreign));
        Assert.Throws<ArgumentException>(() => _p.CreateBudgets(foreign));
        Assert.False(_p.Currencies.IsEnabled("USD"));
        Assert.False(other.Currencies.IsEnabled("USD"));
    }
}
