// Copyright (c) 2024-2026 Michael Laurence Curzi and 36N9 Genetics, LLC
// SPDX-License-Identifier: Apache-2.0

// These tests keep the 1.0 context surface covered: it is deprecated since
// ABI 1.2 (no capability) but must keep working (sdk/docs/STABILITY.md).
#pragma warning disable CS0618

using System;
using System.Text.Json;
using Microsoft.Extensions.DependencyInjection;
using Zxv.Evm;
using Zxv.Interop;
using Zxv.Ledger;

namespace Zxv.Sdk.Tests.Abi11;

/// <summary>Capabilities, attestations, the gated ledger, EVM encoding and the fail-closed path.</summary>
public sealed class CapabilityAndFatalTests : IDisposable
{
    private readonly ZxvPlatform _p = TestPlatform.Create();

    public void Dispose() => _p.Dispose();

    [Fact]
    public void Attenuation_narrows_and_never_widens()
    {
        using Capability root = _p.CreateRootCapability(processId: 7);
        Assert.Equal(CapabilityRights.All, root.Rights);
        Assert.Equal(7u, root.GetInfo().ProcessId);
        Assert.Equal(Truth.True, root.Attestation);

        using Capability ro = root.Attenuate(CapabilityRights.LedgerRead);
        Assert.True(ro.Allows(CapabilityRights.LedgerRead));
        Assert.False(ro.Allows(CapabilityRights.LedgerPost));
        using Capability wider = ro.Attenuate(CapabilityRights.All);
        Assert.Equal(CapabilityRights.LedgerRead, wider.Rights);
        Assert.Throws<CapabilityDeniedException>(() => wider.Demand(CapabilityRights.LedgerPost));
    }

    [Fact]
    public void Contradictory_attestation_blocks_every_gated_call()
    {
        using Capability root = _p.CreateRootCapability();
        root.SetAttestation(Truth.Both);
        var ex = Assert.Throws<CapabilityDeniedException>(() => root.Demand(CapabilityRights.LedgerRead));
        Assert.Contains("contradictory", ex.Detail, StringComparison.Ordinal);
        root.SetAttestation(Truth.True);
        root.Demand(CapabilityRights.LedgerRead);
    }

    [Fact]
    public void Gated_ledger_checks_the_capability_and_revocation_cascades()
    {
        using Capability root = _p.CreateRootCapability();
        using Capability ro = root.Attenuate(CapabilityRights.LedgerRead);
        Account issuer = _p.Ledger.OpenAccount("Issuer", "NGN", AccountKind.Issuer);
        Account alice = _p.Ledger.OpenAccount("Alice", "NGN");
        Money m = Money.FromMinorUnits(500, _p.Currencies.GetEnabled("NGN"));

        _p.Ledger.WithCapability(root).Post(new Transfer(issuer.Id, alice.Id, m, "CAP-1"));
        Assert.Throws<CapabilityDeniedException>(() => _p.Ledger.WithCapability(ro).Post(new Transfer(issuer.Id, alice.Id, m, "CAP-2")));
        Assert.Equal(500, _p.Ledger.WithCapability(ro).GetBalances(alice.Id).Equity.MinorUnits);
        Assert.Throws<InsufficientFundsException>(() => _p.Ledger.WithCapability(root).Post(new Transfer(alice.Id, issuer.Id, m with { MinorUnits = 501 }, "CAP-3")));
        _p.Ledger.WithCapability(root).Verify();

        root.Revoke();
        Assert.True(ro.IsRevoked);
        Assert.Throws<CapabilityDeniedException>(() => _p.Ledger.WithCapability(ro).GetBalances(alice.Id));

        using ZxvPlatform other = ZxvPlatform.Create();
        using Capability foreign = other.CreateRootCapability();
        Assert.Throws<ArgumentException>(() => _p.Ledger.WithCapability(foreign));
        Assert.Throws<ArgumentException>(() => _p.CreateBudgets(foreign));
    }

    [Fact]
    public void Attestations_hold_contradictions_and_gate_actuators()
    {
        using Capability root = _p.CreateRootCapability();
        using Attestations reg = _p.CreateAttestations(root);
        int i = reg.Attest("model.gguf", AttestationSubject.ModelPack, Truth.True, "loader", Hashes.Sha3256("weights"u8));
        Assert.True(reg.IsGateClear);
        reg.AddEvidence(i, positive: true);
        reg.AddEvidence(i, positive: false);
        Assert.Equal(new AttestationState(Truth.Both, 2, 1), reg.GetState(i));
        Assert.False(reg.IsGateClear);
        Assert.Equal(i, reg.Find("model.gguf"));
        Assert.Null(reg.Find("missing"));

        using Capability noAttest = root.Attenuate(CapabilityRights.LedgerRead);
        Assert.Throws<CapabilityDeniedException>(() => _p.CreateAttestations(noAttest)); // ABI 1.2: creating needs Attest
        using Capability attest = root.Attenuate(CapabilityRights.Attest);
        using Attestations reg2 = _p.CreateAttestations(attest);
        attest.Revoke();
        Assert.Throws<CapabilityDeniedException>(() => reg2.Attest("x", AttestationSubject.Key, Truth.True, "me"));
    }

    [Fact]
    public void Evm_state_matches_vectors_and_ledger_state_matches_balances()
    {
        JsonElement v = Conformance.Vectors.GetProperty("evm_state");
        using Capability cap = _p.CreateRootCapability(CapabilityRights.Evm | CapabilityRights.LedgerRead | CapabilityRights.LedgerPost);
        var channel = new StateChannel(cap);
        byte[] id = Conformance.Hex(v.GetProperty("channel_id").GetString()!);
        var parties = new ChannelParty[3];
        for (int k = 0; k < 3; k++)
        {
            parties[k] = new ChannelParty(Conformance.Hex(v.GetProperty("addresses")[k].GetString()!), v.GetProperty("balances")[k].GetInt64());
        }

        EvmState s = channel.EncodeState(id, v.GetProperty("nonce").GetUInt64(), parties);
        Assert.Equal(v.GetProperty("encoding").GetString(), Conformance.ToHex(s.Encoding.Span));
        Assert.Equal(v.GetProperty("digest").GetString(), Conformance.ToHex(s.Digest.Span));
        Assert.Equal(Conformance.ToHex(Hashes.Keccak256(s.Encoding.Span)), Conformance.ToHex(s.Digest.Span));

        Account a = _p.Ledger.OpenAccount("a", "NGN", AccountKind.Issuer);
        Account b = _p.Ledger.OpenAccount("b", "NGN");
        _p.Ledger.WithCapability(cap).Post(new Transfer(a.Id, b.Id, Money.FromMinorUnits(250, _p.Currencies.GetEnabled("NGN")), "EVM-1"));
        byte[] addrA = new byte[20], addrB = new byte[20];
        addrA[19] = 1;
        addrB[19] = 2;
        EvmState fromLedger = channel.LedgerState(new byte[32], 1, "NGN", new[] { new LedgerChannelParty(addrA, a.Id), new LedgerChannelParty(addrB, b.Id) });
        EvmState explicitState = channel.EncodeState(new byte[32], 1, new[] { new ChannelParty(addrA, -250), new ChannelParty(addrB, 250) });
        Assert.Equal(10 * 32, fromLedger.Encoding.Length);
        Assert.True(fromLedger.Digest.Span.SequenceEqual(explicitState.Digest.Span));

        using Capability noEvm = cap.Attenuate(CapabilityRights.LedgerRead);
        Assert.Throws<CapabilityDeniedException>(() => new StateChannel(noEvm).EncodeState(id, 1, parties));
    }

    [Fact]
    public void Halted_context_is_fatal_outside_the_ZxvException_family()
    {
        using Capability cap = _p.CreateRootCapability();
        (Account issuer, Account holder) = TestPlatform.Funded(_p, "NGN", 1_000);
        Money one = Money.FromMinorUnits(1, _p.Currencies.GetEnabled("NGN"));
        Assert.False(_p.IsHalted);

        _p.Halt("operator kill switch (test)");
        Assert.True(_p.IsHalted);

        // The context-level API: the native library refuses, the SDK raises the terminal type.
        var fatal = Assert.Throws<ZxvFatalException>(() => _p.Ledger.Post(new Transfer(issuer.Id, holder.Id, one, "AFTER-HALT")));
        Assert.Equal(ZxvStatus.Poisoned, fatal.Status);
        Assert.Equal("ZXV_E_POISONED", fatal.StatusName);
        Assert.False(typeof(ZxvException).IsAssignableFrom(typeof(ZxvFatalException)));

        // A catch-all for ordinary refusals does not swallow it.
        Assert.Throws<ZxvFatalException>(() =>
        {
            try
            {
                _p.Ledger.WithCapability(cap).GetBalances(holder.Id);
            }
            catch (ZxvException)
            {
                Assert.Fail("ZxvFatalException must not be caught as ZxvException");
            }
        });

        // Once seen, the SDK marks the context dead and fails fast in managed code.
        Assert.Throws<ZxvFatalException>(() => cap.Demand(CapabilityRights.LedgerRead));
        Assert.Throws<ZxvFatalException>(() => _p.CreateBudgets(cap));
        Assert.False(_p.Ledger.TryVerify(out _));
    }

    [Fact]
    public void Fatal_policy_defaults_to_throw()
    {
        // FailFast ends the process (Environment.FailFast), so it cannot run in-process.
        Assert.Equal(FatalPolicy.Throw, ZxvLibrary.FatalPolicy);
    }

    [Fact]
    public void Dependency_injection_registers_a_narrow_capability()
    {
        var services = new ServiceCollection();
        services.AddZxv(o => o.Currencies.Add("NGN"));
        services.AddZxvCapability(CapabilityRights.LedgerRead | CapabilityRights.Budget);
        using ServiceProvider sp = services.BuildServiceProvider();
        Capability cap = sp.GetRequiredService<Capability>();
        Assert.Same(cap, sp.GetRequiredService<Capability>());
        Assert.Equal(CapabilityRights.LedgerRead | CapabilityRights.Budget, cap.Rights);
        using Budgets budgets = sp.GetRequiredService<ZxvPlatform>().CreateBudgets(cap);
        Assert.Equal(Rational.Of(5), budgets.Create("b", BudgetResource.Energy, 5).Remaining);
    }
}
