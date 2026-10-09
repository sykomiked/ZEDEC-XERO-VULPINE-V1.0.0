// Copyright (c) 2024-2026 Michael Laurence Curzi and 36N9 Genetics, LLC
// SPDX-License-Identifier: Apache-2.0

using System;

namespace Zxv.Interop;

/// <summary>
/// Feature groups that may be live in a given libzxv build (<c>ZXV_FEAT_*</c>).
/// Modules still in development in the kernel are present in the ABI as stubs;
/// check these flags instead of catching <see cref="ZxvStatus.NotImplemented"/>.
/// </summary>
[Flags]
#pragma warning disable CA1028, CA1711 // uint matches the C ABI; "Features" is the domain name.
public enum ZxvFeatures : uint
#pragma warning restore CA1028, CA1711
{
    /// <summary>No feature.</summary>
    None = 0,

    /// <summary>Triple-ledger accounts and postings.</summary>
    Ledger = 0x0000_0001,

    /// <summary>pacs.008 FI-to-FI customer credit transfer.</summary>
    Pacs008 = 0x0000_0002,

    /// <summary>camt.053 bank-to-customer statement.</summary>
    Camt053 = 0x0000_0004,

    /// <summary>Full ISO 4217 currency table.</summary>
    Iso4217 = 0x0000_0008,

    /// <summary>pacs.002 payment status report.</summary>
    Pacs002 = 0x0000_0010,

    /// <summary>pacs.004 payment return.</summary>
    Pacs004 = 0x0000_0020,

    /// <summary>pacs.009 financial institution credit transfer.</summary>
    Pacs009 = 0x0000_0040,

    /// <summary>camt.056 FI-to-FI payment cancellation request.</summary>
    Camt056 = 0x0000_0080,

    /// <summary>camt.029 resolution of investigation.</summary>
    Camt029 = 0x0000_0100,

    /// <summary>Multilateral netting cycles.</summary>
    Netting = 0x0000_0200,

    /// <summary>Card issue, reissue and authorization.</summary>
    Cards = 0x0000_0400,

    /// <summary>PAN check digits (Luhn, Damm, Verhoeff).</summary>
    CardCheckDigits = 0x0000_0800,

    /// <summary>VSS conformance checks.</summary>
    Conformance = 0x0000_1000,
}
