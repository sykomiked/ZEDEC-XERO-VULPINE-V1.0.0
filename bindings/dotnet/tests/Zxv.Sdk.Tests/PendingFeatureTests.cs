// Copyright (c) 2024-2026 Michael Laurence Curzi and 36N9 Genetics, LLC
// SPDX-License-Identifier: Apache-2.0

using System;
using Zxv.Cards;
using Zxv.Conformance;
using Zxv.Interop;
using Zxv.Ledger;

namespace Zxv.Sdk.Tests;

/// <summary>
/// Features whose kernel modules are still in development must fail cleanly
/// with <see cref="ZxvFeatureNotAvailableException"/>, never crash. When the
/// native library lights a feature up, these tests flip to the live path.
/// </summary>
public sealed class PendingFeatureTests : IDisposable
{
    private readonly ZxvPlatform _p = TestPlatform.Create();

    public void Dispose() => _p.Dispose();

    [Fact]
    public void Netting()
    {
        if (_p.Supports(ZxvFeatures.Netting))
        {
            return;
        }

        var ex = Assert.Throws<ZxvFeatureNotAvailableException>(() => _p.Netting.OpenCycle("CYCLE-1", "NGN"));
        Assert.Equal(ZxvStatus.NotImplemented, ex.Status);
        Assert.Contains("cbank", ex.Detail, StringComparison.Ordinal);
    }

    [Fact]
    public void Card_lifecycle()
    {
        if (_p.Supports(ZxvFeatures.Cards))
        {
            return;
        }

        Account a = _p.Ledger.OpenAccount("Card holder", "NGN");
        Money limit = Money.FromMinorUnits(100_000, _p.Currencies.GetEnabled("NGN"));
        Assert.Throws<ZxvFeatureNotAvailableException>(() => _p.Cards.Issue(CardNetwork.Dragon, a.Id, limit));
        Assert.Throws<ZxvFeatureNotAvailableException>(() => _p.Cards.Reissue(1));
        Assert.Throws<ZxvFeatureNotAvailableException>(() => _p.Cards.Authorize(1, limit));
    }

    [Fact]
    public void Conformance()
    {
        if (_p.Supports(ZxvFeatures.Conformance))
        {
            return;
        }

        Assert.Throws<ZxvFeatureNotAvailableException>(() => _p.Conformance.Run(VssCheck.Balance));
        Assert.Throws<ZxvFeatureNotAvailableException>(() => _p.Conformance.GetLevel());
    }

    [Theory]
    [InlineData(CardNetwork.Dragon, "79927398713", true)]
    [InlineData(CardNetwork.Dragon, "79927398710", false)]
    [InlineData(CardNetwork.Phoenix, "5724", true)]
    [InlineData(CardNetwork.Phoenix, "5727", false)]
    [InlineData(CardNetwork.Thunderbird, "2363", true)]
    [InlineData(CardNetwork.Thunderbird, "2364", false)]
    public void Check_digits(CardNetwork network, string pan, bool valid)
    {
        Assert.True(_p.Supports(ZxvFeatures.CardCheckDigits));
        Assert.Equal(valid, CardService.IsCheckDigitValid(network, pan));
    }

    [Fact]
    public void Check_digit_input_is_validated() =>
        Assert.Equal(ZxvStatus.InvalidArgument, Assert.Throws<ZxvException>(() => CardService.IsCheckDigitValid(CardNetwork.Dragon, "4111-1111")).Status);
}
