// Copyright (c) 2024-2026 Michael Laurence Curzi and 36N9 Genetics, LLC
// SPDX-License-Identifier: Apache-2.0

namespace Zxv.Currencies;

/// <summary>
/// The three Vino ledger rails and their platform-internal numeric codes.
/// </summary>
/// <remarks>
/// None of these numerics is an active ISO 4217 currency: 846 and 888 are
/// unassigned and 810 is the withdrawn code of the old Russian ruble (RUR).
/// They appear only in ledger rail balances and proprietary fields, never in an
/// ISO 20022 <c>Ccy</c> attribute.
/// </remarks>
#pragma warning disable CA1008 // No zero value: the values are the rail numerics themselves.
public enum VinoRail
#pragma warning restore CA1008
{
    /// <summary>Asset / backing rail: what an account has received.</summary>
    Debit = 846,

    /// <summary>Claim / liability rail: what an account has sent or owes.</summary>
    Credit = 810,

    /// <summary>Equity rail: debit minus credit, the spendable position.</summary>
    Equity = 888,
}
