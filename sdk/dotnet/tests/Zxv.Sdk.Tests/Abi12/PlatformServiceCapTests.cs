// Copyright (c) 2024-2026 Michael Laurence Curzi and 36N9 Genetics, LLC
// SPDX-License-Identifier: Apache-2.0

using System;
using System.Collections.Generic;
using System.IO;
using System.Linq;
using System.Runtime.CompilerServices;
using System.Text.RegularExpressions;
using System.Threading.Tasks;
using Zxv.Interop;
using Zxv.Ledger;
using Zxv.Messaging;
using Zxv.Payments;

namespace Zxv.Sdk.Tests.Abi12;

/// <summary>
/// ABI 1.2: the platform services call only the <c>*_cap</c> forms, and the
/// services that took no capability gained overloads that take one.
/// </summary>
public sealed class PlatformServiceCapTests : IDisposable
{
    private readonly ZxvPlatform _p = TestPlatform.Create();

    public void Dispose() => _p.Dispose();

    private (Capability Root, Account Ada, Account Kwame) Seed()
    {
        Capability root = _p.CreateRootCapability(CapabilityRights.LedgerRead | CapabilityRights.LedgerPost);
        CapabilityLedger ledger = _p.Ledger.WithCapability(root);
        Account bank = ledger.OpenAccount("Bank", "NGN", AccountKind.Issuer);
        Account ada = ledger.OpenAccount("Ada", "NGN");
        Account kwame = ledger.OpenAccount("Kwame", "NGN");
        ledger.Post(new Transfer(bank.Id, ada.Id, Money.FromMinorUnits(10_000, _p.Currencies.GetEnabled("NGN")), "SEED-1"));
        return (root, ada, kwame);
    }

    [Fact]
    public async Task Payments_through_a_teller_capability()
    {
        (Capability root, Account ada, Account kwame) = Seed();
        using (root)
        {
            using Capability teller = root.Attenuate(CapabilityRights.LedgerRead | CapabilityRights.LedgerPost);
            Money amount = Money.FromMinorUnits(2_500, _p.Currencies.GetEnabled("NGN"));

            PaymentReceipt r = await _p.Payments.SubmitAsync(teller, new PaymentRequest(ada.Id, kwame.Id, amount));

            Assert.Equal(2_500, _p.Ledger.GetAvailable(kwame.Id).MinorUnits);
            Assert.Contains("<Nm>Ada</Nm>", r.Message.Xml, StringComparison.Ordinal);
        }
    }

    [Fact]
    public async Task Payments_through_a_read_only_capability_book_nothing()
    {
        (Capability root, Account ada, Account kwame) = Seed();
        using (root)
        {
            using Capability auditor = root.Attenuate(CapabilityRights.LedgerRead);
            Money amount = Money.FromMinorUnits(2_500, _p.Currencies.GetEnabled("NGN"));

            var ex = await Assert.ThrowsAsync<CapabilityDeniedException>(() => _p.Payments.SubmitAsync(auditor, new PaymentRequest(ada.Id, kwame.Id, amount)));

            Assert.Equal(ZxvStatus.CapabilityDenied, ex.Status);
            Assert.Equal(0, _p.Ledger.GetAvailable(kwame.Id).MinorUnits);
            Assert.Equal(10_000, _p.Ledger.GetAvailable(ada.Id).MinorUnits);
        }
    }

    [Fact]
    public async Task Payments_refuse_a_capability_of_another_platform()
    {
        (Capability root, Account ada, Account kwame) = Seed();
        root.Dispose();
        using ZxvPlatform other = TestPlatform.Create();
        using Capability foreign = other.CreateRootCapability();
        Money amount = Money.FromMinorUnits(1, _p.Currencies.GetEnabled("NGN"));

        await Assert.ThrowsAsync<ArgumentException>(() => _p.Payments.SubmitAsync(foreign, new PaymentRequest(ada.Id, kwame.Id, amount)));
        await Assert.ThrowsAsync<ArgumentNullException>(() => _p.Payments.SubmitAsync(null!, new PaymentRequest(ada.Id, kwame.Id, amount)));
    }

    [Fact]
    public void Statements_need_LedgerRead()
    {
        (Capability root, Account ada, _) = Seed();
        using (root)
        {
            using Capability read = root.Attenuate(CapabilityRights.LedgerRead);
            using Capability post = root.Attenuate(CapabilityRights.LedgerPost);

            AccountStatement s = _p.Statements.BuildStatement(read, ada.Id);
            AccountStatement same = _p.Statements.BuildStatement(ada.Id, new StatementRequest { MessageId = s.Document.MessageId });

            Assert.Equal(10_000, s.Closing.MinorUnits);
            Assert.Equal(s.Closing, same.Closing);
            Assert.Single(s.Entries);
            Assert.Throws<CapabilityDeniedException>(() => _p.Statements.BuildStatement(post, ada.Id));
            Assert.Throws<ArgumentNullException>(() => _p.Statements.BuildStatement(null!, ada.Id));
        }
    }

    [Fact]
    public void The_platform_capability_is_independent_of_the_callers()
    {
        // Revoking every capability the program holds does not stop the
        // members that take none: they present the platform's own capability,
        // which nothing outside the platform can reach.
        (Capability root, Account ada, Account kwame) = Seed();
        root.Revoke();
        Assert.Throws<CapabilityDeniedException>(() => _p.Ledger.WithCapability(root).GetBalances(ada.Id));

        _p.Ledger.Post(new Transfer(ada.Id, kwame.Id, Money.FromMinorUnits(1, _p.Currencies.GetEnabled("NGN")), "AFTER-REVOKE"));

        Assert.Equal(1, _p.Ledger.GetAvailable(kwame.Id).MinorUnits);
        Assert.True(_p.Messages.IsSupported(MxMessageKind.Pacs008));
        root.Dispose();
    }

    [Fact]
    public void IsSupported_is_false_on_a_halted_context()
    {
        Assert.True(_p.Messages.IsSupported(MxMessageKind.Camt053));
        Assert.False(_p.Messages.IsSupported(MxMessageKind.Pacs004));
        _p.Halt("test");
        Assert.False(_p.Messages.IsSupported(MxMessageKind.Camt053));
    }

    [Fact]
    public void Configuration_enables_currencies_through_the_cap_forms()
    {
        // ZxvOptions are applied with zxv_ccy_enable_cap and friends; the
        // observable result is unchanged.
        Assert.Equal(TestPlatform.Currencies.Append("VFV"), _p.Currencies.ListEnabled().Select(c => c.Code));
        Assert.False(_p.Currencies.GetEnabled("VFV").IsIso4217);
    }

    [Fact]
    public void The_sdk_calls_no_deprecated_context_form()
    {
        // ABI.md R10 / "Changes in 1.2": these zxv_ctx forms take no
        // capability. The bindings stay in Zxv.Native (1.x never breaks),
        // but no Zxv.Sdk code path may call them.
        string[] deprecated =
        {
            "CcyEnable", "CcyEnableWithMinor", "CcyRegisterPrivate", "LedgerOpenAccount", "LedgerPost", "LedgerBalance",
            "LedgerCheck", "LedgerAccountCcy", "LedgerAccountName", "LedgerAccountKind", "LedgerAccountCount",
            "LedgerEntryCount", "LedgerEntryAt", "MsgCreate", "RmagCreate", "PhaseCreate", "LpresCreate", "SwarmCreate",
        };
        var call = new Regex(@"NativeMethods\.(\w+)\(", RegexOptions.CultureInvariant);
        var offenders = new List<string>();
        int calls = 0;
        foreach (string file in Directory.EnumerateFiles(SdkSource(), "*.cs", SearchOption.AllDirectories))
        {
            if (file.Contains(Path.DirectorySeparatorChar + "obj" + Path.DirectorySeparatorChar, StringComparison.Ordinal))
            {
                continue;
            }

            foreach (Match m in call.Matches(File.ReadAllText(file)))
            {
                calls++;
                if (deprecated.Contains(m.Groups[1].Value, StringComparer.Ordinal))
                {
                    offenders.Add(Path.GetFileName(file) + ": " + m.Groups[1].Value);
                }
            }
        }

        Assert.True(calls > 100, "the scan found the SDK sources");
        Assert.Empty(offenders);
    }

    private static string SdkSource([CallerFilePath] string here = "") =>
        Path.GetFullPath(Path.Combine(Path.GetDirectoryName(here)!, "..", "..", "..", "src", "Zxv.Sdk"));
}
