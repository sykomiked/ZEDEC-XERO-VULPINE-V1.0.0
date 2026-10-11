// Copyright (c) 2024-2026 Michael Laurence Curzi and 36N9 Genetics, LLC
// SPDX-License-Identifier: Apache-2.0

namespace Zxv.AspNetCore.Sample;

/// <summary>A quote request: JSON strings such as "12.75" or "1/3". A JSON float is refused with 400.</summary>
/// <param name="Price">Unit price.</param>
/// <param name="Quantity">Quantity.</param>
public sealed record QuoteRequest(Rational Price, Rational Quantity);

/// <summary>A quote.</summary>
/// <param name="Total">The exact result.</param>
public sealed record QuoteResponse(Rational Total);

/// <summary>A payment in integer minor units (kobo).</summary>
/// <param name="AmountMinor">The amount.</param>
/// <param name="Reference">The end-to-end reference.</param>
public sealed record Payment(long AmountMinor, string Reference);

/// <summary>A posted payment.</summary>
/// <param name="EntryId">The journal entry id.</param>
public sealed record Posted(ulong EntryId);

/// <summary>A balance in minor units.</summary>
/// <param name="Debit">Debits.</param>
/// <param name="Credit">Credits.</param>
/// <param name="Equity">Equity.</param>
public sealed record BalanceView(long Debit, long Credit, long Equity);
