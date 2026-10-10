// Copyright (c) 2024-2026 Michael Laurence Curzi and 36N9 Genetics, LLC
// SPDX-License-Identifier: Apache-2.0

using System;
using System.Linq;
using System.Xml.Linq;
using Zxv.Interop;
using Zxv.Messaging;

namespace Zxv.Sdk.Tests;

public sealed class MessagingTests : IDisposable
{
    private static readonly DateTimeOffset At = new(2026, 10, 9, 13, 0, 0, TimeSpan.FromHours(1));
    private readonly ZxvPlatform _p = TestPlatform.Create();

    public void Dispose() => _p.Dispose();

    private static Pacs008Message Pacs(Money amount, string debtor = "Ada & Sons <Lagos>") => new(
        "MSG-0001", At, new PartyAccount(debtor, "NG0001"), new PartyAccount("Kwame Mensah", "GH0002"), amount, "E2E-0001");

    [Fact]
    public void Pacs008_is_well_formed_with_exact_amount()
    {
        MxDocument doc = _p.Messages.BuildPacs008(Pacs(Money.FromMinorUnits(250_050, _p.Currencies.GetEnabled("NGN"))));
        Assert.Equal(MxMessageKind.Pacs008, doc.Kind);
        XDocument x = XDocument.Parse(doc.Xml);
        Assert.Contains("pacs.008.001.09", x.Root!.Name.NamespaceName, StringComparison.Ordinal);
        XElement amt = x.Descendants().First(e => e.Name.LocalName == "IntrBkSttlmAmt");
        Assert.Equal("NGN", amt.Attribute("Ccy")!.Value);
        Assert.Equal("2500.50", amt.Value);
        Assert.Equal("2026-10-09T12:00:00Z", x.Descendants().First(e => e.Name.LocalName == "CreDtTm").Value);
        Assert.Equal("Ada & Sons <Lagos>", x.Descendants().First(e => e.Name.LocalName == "Nm").Value);
        Assert.DoesNotContain("555", doc.Xml, StringComparison.Ordinal);
        Assert.DoesNotContain("777", doc.Xml, StringComparison.Ordinal);
    }

    [Fact]
    public void Zero_decimal_currency()
    {
        MxDocument doc = _p.Messages.BuildPacs008(Pacs(Money.FromMinorUnits(5000, _p.Currencies.GetEnabled("XOF"))));
        Assert.Contains("Ccy=\"XOF\">5000<", doc.Xml, StringComparison.Ordinal);
    }

    [Fact]
    public void Private_unit_never_goes_on_the_wire()
    {
        Money vfv = Money.FromMinorUnits(100, _p.Currencies.GetEnabled("VFV"));
        Assert.Equal(ZxvStatus.InvalidCurrency, Assert.Throws<ZxvException>(() => _p.Messages.BuildPacs008(Pacs(vfv))).Status);
    }

    [Fact]
    public void Scale_must_match_iso4217()
    {
        var wrong = new Money(250_050, "NGN", 3);
        Assert.Equal(ZxvStatus.InvalidCurrency, Assert.Throws<ZxvException>(() => _p.Messages.BuildPacs008(Pacs(wrong))).Status);
    }

    [Fact]
    public void Field_limits_are_enforced()
    {
        Money m = Money.FromMinorUnits(1, _p.Currencies.GetEnabled("NGN"));
        Assert.Equal(ZxvStatus.TooLong, Assert.Throws<ZxvException>(() => _p.Messages.BuildPacs008(Pacs(m) with { MessageId = new string('M', 36) })).Status);
        Assert.Equal(ZxvStatus.TooLong, Assert.Throws<ZxvException>(() => _p.Messages.BuildPacs008(Pacs(m, new string('n', 71)))).Status);
        Assert.Equal(ZxvStatus.AmountOutOfRange, Assert.Throws<ZxvException>(() => _p.Messages.BuildPacs008(Pacs(m with { MinorUnits = 0 }))).Status);
        Assert.Throws<ArgumentException>(() => _p.Messages.BuildPacs008(Pacs(m) with { EndToEndId = string.Empty }));
    }

    [Fact]
    public void Camt053_must_reconcile()
    {
        var ghs = _p.Currencies.GetEnabled("GHS");
        Money M(long v) => Money.FromMinorUnits(v, ghs);
        var entries = new[] { new StatementEntry(M(10_000)), new StatementEntry(M(-2_550)), new StatementEntry(M(999), EntryStatus.Pending) };
        var bad = new Camt053Message("STMT-1", At, "GH0002", M(0), M(10_000), entries);
        Assert.Equal(ZxvStatus.InvariantViolated, Assert.Throws<ZxvException>(() => _p.Messages.BuildCamt053(bad)).Status);

        MxDocument doc = _p.Messages.BuildCamt053(bad with { Closing = M(7_450) });
        XDocument x = XDocument.Parse(doc.Xml);
        Assert.Contains("camt.053.001.08", x.Root!.Name.NamespaceName, StringComparison.Ordinal);
        Assert.Equal(3, x.Descendants().Count(e => e.Name.LocalName == "Ntry"));
        Assert.Contains("74.50", doc.Xml, StringComparison.Ordinal);
    }

    [Fact]
    public void Camt053_page_size_is_bounded()
    {
        var ngn = _p.Currencies.GetEnabled("NGN");
        var entries = Enumerable.Range(0, 33).Select(_ => new StatementEntry(Money.FromMinorUnits(1, ngn))).ToList();
        Assert.Throws<ArgumentException>(() => _p.Messages.BuildCamt053(new Camt053Message("S", At, "A", Money.Zero(ngn), Money.FromMinorUnits(33, ngn), entries)));
    }

    [Fact]
    public void Pending_message_kinds_report_unsupported()
    {
        Assert.True(_p.Messages.IsSupported(MxMessageKind.Pacs008));
        Assert.True(_p.Messages.IsSupported(MxMessageKind.Camt053));
        Assert.False(_p.Messages.IsSupported(MxMessageKind.Pacs002));
        Assert.False(_p.Messages.IsSupported(MxMessageKind.Camt056));
    }
}
