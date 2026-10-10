// Copyright (c) 2024-2026 Michael Laurence Curzi and 36N9 Genetics, LLC
// SPDX-License-Identifier: Apache-2.0

using System;
using System.Globalization;
using Zxv.Currencies;

namespace Zxv.Sdk.Tests;

public class MoneyTests
{
    private static readonly Currency Ngn = new("NGN", 566, 2, "Naira", CurrencyAttributes.Payable);
    private static readonly Currency Xof = new("XOF", 952, 0, "CFA Franc BCEAO", CurrencyAttributes.Payable);
    private static readonly Currency Bhd = new("BHD", 48, 3, "Bahraini Dinar", CurrencyAttributes.Payable);

    [Theory]
    [InlineData(2500.50, 250050)]
    [InlineData(0.01, 1)]
    [InlineData(-12.30, -1230)]
    [InlineData(0, 0)]
    public void FromDecimal_is_exact(double major, long minor)
    {
        Money m = Money.FromDecimal((decimal)major, Ngn);
        Assert.Equal(minor, m.MinorUnits);
        Assert.Equal((byte)2, m.Scale);
        Assert.Equal((decimal)major, m.ToDecimal());
    }

    [Fact]
    public void FromDecimal_never_rounds()
    {
        Assert.Throws<ArgumentException>(() => Money.FromDecimal(10.005m, Ngn));
        Assert.Throws<ArgumentException>(() => Money.FromDecimal(1.5m, Xof));
        Assert.Equal(10_005, Money.FromDecimal(10.005m, Bhd).MinorUnits);
    }

    [Fact]
    public void FromDecimal_enforces_exact_range()
    {
        Assert.Throws<ArgumentOutOfRangeException>(() => Money.FromDecimal(90_071_992_547_409.92m, Ngn));
        Assert.Equal(Money.MaxMinorUnits, Money.FromDecimal(90_071_992_547_409.91m, Ngn).MinorUnits);
    }

    [Fact]
    public void ToString_is_invariant_and_keeps_scale()
    {
        Assert.Equal("NGN 2500.50", Money.FromMinorUnits(250050, Ngn).ToString());
        Assert.Equal("XOF 5000", Money.FromMinorUnits(5000, Xof).ToString());
        Assert.Equal("BHD 1.005", Money.FromMinorUnits(1005, Bhd).ToString());
        Assert.Equal("NGN 1,234,567.89", Money.FromMinorUnits(123456789, Ngn).ToString(CultureInfo.InvariantCulture));
    }

    [Fact]
    public void Arithmetic_requires_same_currency()
    {
        Money a = Money.FromMinorUnits(100, Ngn);
        Assert.Equal(300, (a + a + a).MinorUnits);
        Assert.Equal(-100, (-a).MinorUnits);
        Assert.Throws<InvalidOperationException>(() => a + Money.FromMinorUnits(1, Xof));
    }

    [Fact]
    public void Currency_without_minor_unit_cannot_carry_amount()
    {
        var xau = new Currency("XAU", 959, null, "Gold", CurrencyAttributes.MinorUnitNotApplicable);
        Assert.Throws<InvalidOperationException>(() => Money.Zero(xau));
    }
}
