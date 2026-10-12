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

    /// <summary>Exact rationals (ABI 1.1).</summary>
    Rational = 0x0000_2000,

    /// <summary>Four-valued logic and attestations (ABI 1.1).</summary>
    FourValuedLogic = 0x0000_4000,

    /// <summary>Capability handles (ABI 1.1).</summary>
    Capabilities = 0x0000_8000,

    /// <summary>Rational resource budgets, rmag (ABI 1.1).</summary>
    Budgets = 0x0001_0000,

    /// <summary>Phase coordinator ticks (ABI 1.1).</summary>
    Phase = 0x0002_0000,

    /// <summary>UBH-168 harmonic wire frames (ABI 1.1).</summary>
    Wire = 0x0004_0000,

    /// <summary>Fibonacci and emotional swarm token economy (ABI 1.1).</summary>
    Swarm = 0x0008_0000,

    /// <summary>SHA3-256, SHAKE256 and Keccak-256 (ABI 1.1).</summary>
    Hash = 0x0010_0000,

    /// <summary>FIPS 203 ML-KEM-1024 (ABI 1.1).</summary>
    MlKem1024 = 0x0020_0000,

    /// <summary>FIPS 204 ML-DSA-87 (ABI 1.1).</summary>
    MlDsa87 = 0x0040_0000,

    /// <summary>RFC 8032 Ed25519 (ABI 1.1).</summary>
    Ed25519 = 0x0080_0000,

    /// <summary>Integer GGUF inference engine (ABI 1.1).</summary>
    Tensor = 0x0100_0000,

    /// <summary>LoRA adapters. Declared but not built yet: never set today (ABI 1.1).</summary>
    Lora = 0x0200_0000,

    /// <summary>EVM state-channel encoding (ABI 1.1).</summary>
    Evm = 0x0400_0000,

    /// <summary>The 64-state hexagram tag and quarantine log (ABI 1.3).</summary>
    Hex = 0x0800_0000,

    /// <summary>The hardware emulation matrix, ZHP-1 profiles and hardware updates (ABI 1.4).</summary>
    Hw = 0x1000_0000,
}
