// Copyright (c) 2024-2026 Michael Laurence Curzi and 36N9 Genetics, LLC
// SPDX-License-Identifier: Apache-2.0

using System;
using System.Linq;
using Zxv.Currencies;
using Zxv.Interop;

namespace Zxv.Sdk.Tests;

public sealed class PlatformAndCurrencyTests : IDisposable
{
    private readonly ZxvPlatform _p = TestPlatform.Create();

    public void Dispose() => _p.Dispose();

    [Fact]
    public void Abi_and_features_are_reported()
    {
        Assert.Equal(1, _p.AbiVersion.Major);
        Assert.True(_p.Supports(ZxvFeatures.Ledger | ZxvFeatures.Pacs008 | ZxvFeatures.Camt053 | ZxvFeatures.Iso4217));
        Assert.False(_p.Supports(ZxvFeatures.Netting));
    }

    [Theory]
    [InlineData("NGN", 566, 2, "Naira")]
    [InlineData("GHS", 936, 2, "Ghana Cedi")]
    [InlineData("KES", 404, 2, "Kenyan Shilling")]
    [InlineData("XOF", 952, 0, "CFA Franc BCEAO")]
    [InlineData("XAF", 950, 0, "CFA Franc BEAC")]
    [InlineData("ZAR", 710, 2, "Rand")]
    [InlineData("EGP", 818, 2, "Egyptian Pound")]
    [InlineData("TND", 788, 3, "Tunisian Dinar")]
    [InlineData("RWF", 646, 0, "Rwanda Franc")]
    public void Iso4217_lookup_by_alpha(string code, int numeric, byte minor, string name)
    {
        Currency c = _p.Currencies.GetIso(code);
        Assert.Equal(numeric, c.Numeric);
        Assert.Equal(minor, c.MinorUnits);
        Assert.Equal(name, c.Name);
        Assert.True(c.IsWireCurrency);
        Assert.Equal(code, _p.Currencies.GetIsoByNumeric(numeric).Code);
    }

    [Fact]
    public void Rail_numerics_are_not_currencies()
    {
        Assert.Equal(846, CurrencyService.GetRailNumeric(VinoRail.Debit));
        Assert.Equal(810, CurrencyService.GetRailNumeric(VinoRail.Credit));
        Assert.Equal(888, CurrencyService.GetRailNumeric(VinoRail.Equity));
        foreach (int n in new[] { 846, 810, 888 })
        {
            ZxvException ex = Assert.Throws<ZxvException>(() => _p.Currencies.GetIsoByNumeric(n));
            Assert.Equal(ZxvStatus.NotFound, ex.Status);
        }

        Assert.Contains("RUR", _p.Currencies.GetCaveat(810), StringComparison.Ordinal);
        Assert.Equal(string.Empty, _p.Currencies.GetCaveat(566));
    }

    [Fact]
    public void Private_unit_is_not_iso()
    {
        Assert.False(_p.Currencies.TryGetIso("VFV", out _));
        Currency vfv = _p.Currencies.GetEnabled("VFV");
        Assert.False(vfv.IsIso4217);
        Assert.False(vfv.IsWireCurrency);
        Assert.Null(vfv.Numeric);
        ZxvException ex = Assert.Throws<ZxvException>(() => _p.Currencies.RegisterPrivateUnit("NGN", 2));
        Assert.Equal(ZxvStatus.InvalidCurrency, ex.Status);
    }

    [Fact]
    public void Iso_table_is_complete_for_african_union_settlement()
    {
        var codes = _p.Currencies.ListIso().Select(c => c.Code).ToHashSet(StringComparer.Ordinal);
        Assert.True(codes.Count > 150);
        foreach (string c in new[] { "NGN", "GHS", "KES", "XOF", "XAF", "ZAR", "EGP", "MAD", "ETB", "UGX", "TZS", "GMD", "GNF", "SLE", "LRD", "ZMW", "MWK", "DZD", "AOA", "MZN", "CDF", "SDG", "SOS", "BWP", "NAD", "LSL", "SZL", "MUR", "SCR", "CVE", "STN", "DJF", "ERN", "KMF", "MGA", "BIF", "SSP", "LYD", "TND", "MRU", "ZWG" })
        {
            Assert.Contains(c, codes);
        }

        Assert.Matches(@"^\d{4}-\d{2}-\d{2}$", _p.Currencies.Iso4217PublicationDate);
    }

    [Theory]
    [InlineData("GH", true)]
    [InlineData("NG", true)]
    [InlineData("MA", true)]
    [InlineData("FR", false)]
    public void African_union_membership(string a2, bool member) =>
        Assert.Equal(member, _p.Currencies.IsAfricanUnionMember(a2));

    [Fact]
    public void Currency_configuration()
    {
        Assert.Equal(TestPlatform.Currencies.Length + 1, _p.Currencies.ListEnabled().Count);
        Assert.True(_p.Currencies.IsEnabled("XOF"));
        Assert.False(_p.Currencies.IsEnabled("USD"));
        Assert.Equal(ZxvStatus.InvalidCurrency, Assert.Throws<ZxvException>(() => _p.Currencies.Enable("XXX")).Status);
        Assert.Equal(ZxvStatus.InvalidCurrency, Assert.Throws<ZxvException>(() => _p.Currencies.Enable("NGN", 3)).Status);
        Assert.Equal(ZxvStatus.InvalidCurrency, Assert.Throws<ZxvException>(() => _p.Currencies.Enable("ngn")).Status);
        Assert.Equal((byte)2, _p.Currencies.Enable("USD").MinorUnits);
    }

    [Fact]
    public void Disposed_platform_refuses_calls()
    {
        ZxvPlatform p = TestPlatform.Create();
        p.Dispose();
        p.Dispose(); // idempotent
        Assert.Throws<ObjectDisposedException>(() => p.Ledger.OpenAccount("x", "NGN"));
    }

    [Fact]
    public void Exceptions_carry_stable_code_and_detail()
    {
        ZxvException ex = Assert.Throws<ZxvException>(() => _p.Currencies.GetIso("QQQ"));
        Assert.Equal("ZXV_E_NOT_FOUND", ex.Code);
        Assert.Equal("Currencies.GetIso", ex.Operation);
        Assert.Contains("QQQ", ex.Detail, StringComparison.Ordinal);
    }

    [Fact]
    public void Embedded_nul_is_rejected_before_native_code() =>
        Assert.Throws<ArgumentException>(() => _p.Currencies.GetIso("NG\0N"));
}
