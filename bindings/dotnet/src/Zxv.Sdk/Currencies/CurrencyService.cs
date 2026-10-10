// Copyright (c) 2024-2026 Michael Laurence Curzi and 36N9 Genetics, LLC
// SPDX-License-Identifier: Apache-2.0

using System;
using System.Collections.Generic;
using System.Diagnostics.CodeAnalysis;
using Zxv.Interop;

namespace Zxv.Currencies;

/// <summary>
/// ISO 4217 reference data and the per-platform currency configuration.
/// Obtain it from <see cref="ZxvPlatform.Currencies"/>. Thread-safe.
/// </summary>
[SuppressMessage("Performance", "CA1822:Mark members as static", Justification = "Reference data is reached through the platform instance by design, so callers never touch the native library before a platform exists.")]
public sealed class CurrencyService
{
    private const byte MinorNotApplicable = 255;
    private readonly ZxvContextHandle _ctx;

    internal CurrencyService(ZxvContextHandle ctx)
    {
        _ctx = ctx;
    }

    /// <summary>Gets the publication date of the ISO 4217 list compiled into the native library.</summary>
    /// <exception cref="ZxvFeatureNotAvailableException">The library was built without the ISO 4217 table.</exception>
    public unsafe string Iso4217PublicationDate =>
        NativeCall.ReadText((byte* b, nuint c, out nuint l) => NativeMethods.Iso4217Published(b, c, out l), "Currencies.Iso4217PublicationDate");

    /// <summary>Looks up an ISO 4217 currency by alpha-3 code.</summary>
    /// <param name="code">Alpha-3 code, e.g. <c>NGN</c>, <c>GHS</c>, <c>XOF</c>.</param>
    /// <returns>The currency.</returns>
    /// <exception cref="ZxvException">Not an ISO 4217 code (<see cref="ZxvStatus.NotFound"/>).</exception>
    public Currency GetIso(string code)
    {
        Guard.Text(code, nameof(code));
        int rc = NativeMethods.Iso4217ByAlpha(code, out ushort numeric, out byte minor, out uint flags);
        NativeCall.Check(rc, "Currencies.GetIso");
        return new Currency(code, numeric, minor == MinorNotApplicable ? null : minor, TryName(code), (CurrencyAttributes)flags);
    }

    /// <summary>Tries to look up an ISO 4217 currency by alpha-3 code.</summary>
    /// <param name="code">Alpha-3 code.</param>
    /// <param name="currency">The currency, when found.</param>
    /// <returns><see langword="true"/> if <paramref name="code"/> is in ISO 4217.</returns>
    public bool TryGetIso(string code, [NotNullWhen(true)] out Currency? currency)
    {
        currency = null;
        if (string.IsNullOrEmpty(code) || code.Contains('\0', StringComparison.Ordinal))
        {
            return false;
        }

        int rc = NativeMethods.Iso4217ByAlpha(code, out ushort numeric, out byte minor, out uint flags);
        if (rc == (int)ZxvStatus.NotFound)
        {
            return false;
        }

        NativeCall.Check(rc, "Currencies.TryGetIso");
        currency = new Currency(code, numeric, minor == MinorNotApplicable ? null : minor, TryName(code), (CurrencyAttributes)flags);
        return true;
    }

    /// <summary>Looks up an ISO 4217 currency by numeric code (e.g. 566 for NGN).</summary>
    /// <param name="numeric">ISO 4217 numeric code.</param>
    /// <returns>The currency.</returns>
    /// <exception cref="ZxvException">Not an active ISO 4217 numeric (including the rail numerics 555/777/888).</exception>
    public unsafe Currency GetIsoByNumeric(int numeric)
    {
        ArgumentOutOfRangeException.ThrowIfNegative(numeric);
        ArgumentOutOfRangeException.ThrowIfGreaterThan(numeric, 999);
        byte minor = 0;
        uint flags = 0;
        string code = NativeCall.ReadText(
            (byte* b, nuint c, out nuint l) => NativeMethods.Iso4217ByNumeric((ushort)numeric, b, c, out l, out minor, out flags),
            "Currencies.GetIsoByNumeric");
        return new Currency(code, numeric, minor == MinorNotApplicable ? null : minor, TryName(code), (CurrencyAttributes)flags);
    }

    /// <summary>Lists every currency in the compiled ISO 4217 table, sorted by code.</summary>
    /// <returns>The currencies.</returns>
    public unsafe IReadOnlyList<Currency> ListIso()
    {
        NativeCall.Check(NativeMethods.Iso4217Count(out uint count), "Currencies.ListIso");
        var list = new List<Currency>((int)count);
        for (uint i = 0; i < count; i++)
        {
            uint index = i;
            string code = NativeCall.ReadText((byte* b, nuint c, out nuint l) => NativeMethods.Iso4217At(index, b, c, out l), "Currencies.ListIso");
            list.Add(GetIso(code));
        }

        return list;
    }

    /// <summary>
    /// Returns the interop caveat for a numeric code, e.g. that 777 is the
    /// Vino CREDIT rail and not an ISO 4217 currency; empty when there is none.
    /// </summary>
    /// <param name="numeric">A numeric currency or rail code.</param>
    /// <returns>Human-readable caveat text.</returns>
    public unsafe string GetCaveat(int numeric)
    {
        ArgumentOutOfRangeException.ThrowIfNegative(numeric);
        ArgumentOutOfRangeException.ThrowIfGreaterThan(numeric, 999);
        return NativeCall.ReadText((byte* b, nuint c, out nuint l) => NativeMethods.CcyCaveat((ushort)numeric, b, c, out l), "Currencies.GetCaveat");
    }

    /// <summary>Returns whether an ISO 3166 alpha-2 country is an African Union member.</summary>
    /// <param name="countryAlpha2">Country code, e.g. <c>GH</c>.</param>
    /// <returns><see langword="true"/> for AU members.</returns>
    /// <remarks>Membership is as of the date compiled into the native table; suspension status is not recorded.</remarks>
    public bool IsAfricanUnionMember(string countryAlpha2)
    {
        Guard.Text(countryAlpha2, nameof(countryAlpha2));
        NativeCall.Check(NativeMethods.AuIsMember(countryAlpha2, out int member), "Currencies.IsAfricanUnionMember");
        return member != 0;
    }

    /// <summary>Gets the platform-internal numeric code of a Vino rail (555, 777 or 888).</summary>
    /// <param name="rail">The rail.</param>
    /// <returns>The numeric code reported by the native core.</returns>
    public static int GetRailNumeric(VinoRail rail)
    {
        int index = rail switch
        {
            VinoRail.Debit => 0,
            VinoRail.Credit => 1,
            VinoRail.Equity => 2,
            _ => throw new ArgumentOutOfRangeException(nameof(rail), rail, "Unknown rail."),
        };
        NativeCall.Check(NativeMethods.RailNumeric(index, out ushort numeric), "Currencies.GetRailNumeric");
        return numeric;
    }

    /// <summary>
    /// Gets the platform jurisdiction code a Vino rail carries: "NCR" (New
    /// California Republic) for DEBIT 555, "NRE" (Neo Roman Empire) for
    /// CREDIT 777 and "PNS" (Principality of New Sicily) for EQUITY 888.
    /// These are not ISO 3166 country codes: they travel only as
    /// <c>/ZXV/...</c> remittance data, never in a <c>Ctry</c> field.
    /// </summary>
    /// <param name="rail">The rail.</param>
    /// <returns>The three-letter platform jurisdiction code.</returns>
    public static string GetRailJurisdiction(VinoRail rail) => rail switch
    {
        VinoRail.Debit => "NCR",
        VinoRail.Credit => "NRE",
        VinoRail.Equity => "PNS",
        _ => throw new ArgumentOutOfRangeException(nameof(rail), rail, "Unknown rail."),
    };

    /// <summary>Enables an ISO 4217 currency for ledger accounts on this platform.</summary>
    /// <param name="code">Alpha-3 code.</param>
    /// <returns>The enabled currency.</returns>
    /// <exception cref="ZxvException">Not ISO 4217, or not payable (e.g. XXX, XAU).</exception>
    public Currency Enable(string code)
    {
        Guard.Text(code, nameof(code));
        NativeCall.Check(NativeMethods.CcyEnable(_ctx, code), "Currencies.Enable");
        return GetEnabled(code);
    }

    /// <summary>
    /// Enables an ISO 4217 currency, asserting its minor units. Required on
    /// native builds without the ISO 4217 table; elsewhere a mismatch is refused.
    /// </summary>
    /// <param name="code">Alpha-3 code.</param>
    /// <param name="minorUnits">Expected decimal places.</param>
    /// <returns>The enabled currency.</returns>
    public Currency Enable(string code, byte minorUnits)
    {
        Guard.Text(code, nameof(code));
        NativeCall.Check(NativeMethods.CcyEnableWithMinor(_ctx, code, minorUnits), "Currencies.Enable");
        return GetEnabled(code);
    }

    /// <summary>
    /// Registers a platform-private unit such as <c>VFV</c>. It can be held on
    /// ledger accounts but is refused as an ISO 20022 message currency.
    /// </summary>
    /// <param name="code">3 to 8 upper-case letters and digits, not an ISO 4217 code.</param>
    /// <param name="minorUnits">Decimal places (0 to 8).</param>
    /// <returns>The registered unit.</returns>
    public Currency RegisterPrivateUnit(string code, byte minorUnits)
    {
        Guard.Text(code, nameof(code));
        NativeCall.Check(NativeMethods.CcyRegisterPrivate(_ctx, code, minorUnits), "Currencies.RegisterPrivateUnit");
        return GetEnabled(code);
    }

    /// <summary>Gets an enabled currency or private unit.</summary>
    /// <param name="code">The code.</param>
    /// <returns>The currency as configured.</returns>
    /// <exception cref="ZxvException">Not enabled (<see cref="ZxvStatus.NotFound"/>).</exception>
    public Currency GetEnabled(string code)
    {
        Guard.Text(code, nameof(code));
        NativeCall.Check(NativeMethods.CcyGet(_ctx, code, out byte minor, out uint flags), "Currencies.GetEnabled");
        var cflags = (CurrencyAttributes)flags;
        if ((cflags & CurrencyAttributes.Private) != 0)
        {
            return new Currency(code, null, minor, null, cflags);
        }

        int? numeric = null;
        if (NativeMethods.Iso4217ByAlpha(code, out ushort num, out _, out uint isoFlags) == (int)ZxvStatus.Ok)
        {
            numeric = num;
            cflags |= (CurrencyAttributes)isoFlags;
        }

        return new Currency(code, numeric, minor, TryName(code), cflags);
    }

    /// <summary>Returns whether a currency or private unit is enabled on this platform.</summary>
    /// <param name="code">The code.</param>
    /// <returns><see langword="true"/> if enabled.</returns>
    public bool IsEnabled(string code)
    {
        if (string.IsNullOrEmpty(code) || code.Contains('\0', StringComparison.Ordinal))
        {
            return false;
        }

        return NativeMethods.CcyGet(_ctx, code, out _, out _) == (int)ZxvStatus.Ok;
    }

    /// <summary>Lists the enabled currencies and private units, in the order they were enabled.</summary>
    /// <returns>The enabled currencies.</returns>
    public unsafe IReadOnlyList<Currency> ListEnabled()
    {
        NativeCall.Check(NativeMethods.CcyEnabledCount(_ctx, out uint count), "Currencies.ListEnabled");
        var list = new List<Currency>((int)count);
        for (uint i = 0; i < count; i++)
        {
            uint index = i;
            string code = NativeCall.ReadText((byte* b, nuint c, out nuint l) => NativeMethods.CcyEnabledAt(_ctx, index, b, c, out l), "Currencies.ListEnabled");
            list.Add(GetEnabled(code));
        }

        return list;
    }

    private static unsafe string? TryName(string code)
    {
        return NativeText.TryRead((byte* b, nuint c, out nuint l) => NativeMethods.Iso4217Name(code, b, c, out l), out string name) == (int)ZxvStatus.Ok
            ? name
            : null;
    }
}
