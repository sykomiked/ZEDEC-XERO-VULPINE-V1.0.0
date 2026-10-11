// Copyright (c) 2024-2026 Michael Laurence Curzi and 36N9 Genetics, LLC
// SPDX-License-Identifier: Apache-2.0

using System;

namespace Zxv.Currencies;

/// <summary>Properties of a currency as recorded in ISO 4217 or the platform configuration.</summary>
[Flags]
public enum CurrencyAttributes
{
    /// <summary>No flag.</summary>
    None = 0,

    /// <summary>An ISO 4217 fund code (e.g. CLF, USN).</summary>
    Fund = 0x01,

    /// <summary>ISO 4217 lists the minor unit as N.A. (metals, XDR, XTS, XXX).</summary>
    MinorUnitNotApplicable = 0x02,

    /// <summary>Not the currency of any ISO 3166 country (e.g. XDR).</summary>
    NoCountry = 0x04,

    /// <summary>Usable for a payment amount.</summary>
    Payable = 0x08,

    /// <summary>A platform-private unit (e.g. VFV). Never valid in an ISO 20022 <c>Ccy</c> attribute.</summary>
    Private = 0x10,
}

/// <summary>A currency or platform unit.</summary>
/// <param name="Code">ISO 4217 alpha-3 code (e.g. <c>NGN</c>), or the private unit code.</param>
/// <param name="Numeric">ISO 4217 numeric code (e.g. 566), or <see langword="null"/> for private units.</param>
/// <param name="MinorUnits">Decimal places, or <see langword="null"/> where ISO 4217 says N.A.</param>
/// <param name="Name">English name from ISO 4217, or <see langword="null"/> when unknown.</param>
/// <param name="Attributes">Currency attributes.</param>
public sealed record Currency(string Code, int? Numeric, byte? MinorUnits, string? Name, CurrencyAttributes Attributes)
{
    /// <summary>Gets a value indicating whether this is an ISO 4217 currency (as opposed to a private unit).</summary>
    public bool IsIso4217 => (Attributes & CurrencyAttributes.Private) == 0;

    /// <summary>Gets a value indicating whether the currency may be used for a payment amount.</summary>
    public bool IsPayable => (Attributes & CurrencyAttributes.Payable) != 0 || (Attributes & CurrencyAttributes.Private) != 0;

    /// <summary>Gets a value indicating whether the code may appear in an ISO 20022 <c>Ccy</c> attribute.</summary>
    public bool IsWireCurrency => IsIso4217 && (Attributes & CurrencyAttributes.Payable) != 0;

    internal byte RequireMinorUnits() =>
        MinorUnits ?? throw new InvalidOperationException($"{Code} has no minor unit in ISO 4217 and cannot carry an amount.");
}
