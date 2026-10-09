// Copyright (c) 2024-2026 Michael Laurence Curzi and 36N9 Genetics, LLC
// SPDX-License-Identifier: Apache-2.0

using System;
using Zxv.Ledger;

namespace Zxv.Sdk.Tests;

/// <summary>A platform with the PAPSS-region currencies used across the tests.</summary>
internal static class TestPlatform
{
    internal static readonly string[] Currencies = { "NGN", "GHS", "KES", "XOF", "ZAR", "EGP" };

    internal static ZxvPlatform Create(TimeProvider? time = null, Payments.IMxTransport? transport = null)
    {
        var options = new ZxvOptions();
        foreach (string c in Currencies)
        {
            options.Currencies.Add(c);
        }

        options.PrivateUnits.Add(new PrivateUnitOptions { Code = "VFV", MinorUnits = 2 });
        return new ZxvPlatform(Microsoft.Extensions.Options.Options.Create(options), null, transport, time);
    }

    /// <summary>Opens an issuer and a funded holder.</summary>
    internal static (Account Issuer, Account Holder) Funded(ZxvPlatform p, string ccy, long minor, string name = "Holder")
    {
        Account issuer = p.Ledger.OpenAccount("Settlement " + ccy, ccy, AccountKind.Issuer);
        Account holder = p.Ledger.OpenAccount(name, ccy);
        p.Ledger.Post(new Transfer(issuer.Id, holder.Id, Money.FromMinorUnits(minor, p.Currencies.GetEnabled(ccy)), "FUND-" + Guid.NewGuid().ToString("N")[..20]));
        return (issuer, holder);
    }
}
