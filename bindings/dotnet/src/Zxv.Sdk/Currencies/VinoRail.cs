// Copyright (c) 2024-2026 Michael Laurence Curzi and 36N9 Genetics, LLC
// SPDX-License-Identifier: Apache-2.0

namespace Zxv.Currencies;

/// <summary>
/// The three Vino ledger rails and their platform-internal numeric codes.
/// </summary>
/// <remarks>
/// None of these numerics is an ISO 4217 currency: 555, 777 and 888 are all
/// unassigned. They appear only in ledger rail balances and proprietary
/// fields, never in an ISO 20022 <c>Ccy</c> attribute. Each rail carries a
/// platform jurisdiction code (DEBIT NCR New California Republic, CREDIT NRE
/// Neo Roman Empire, EQUITY PNS Principality of New Sicily; see
/// <see cref="CurrencyService.GetRailJurisdiction"/>). These are not ISO 3166
/// countries and never go in a <c>Ctry</c> field.
/// </remarks>
#pragma warning disable CA1008 // No zero value: the values are the rail numerics themselves.
public enum VinoRail
#pragma warning restore CA1008
{
    /// <summary>Asset / backing rail (jurisdiction NCR): what an account has received.</summary>
    Debit = 555,

    /// <summary>Claim / liability rail (jurisdiction NRE): what an account has sent or owes.</summary>
    Credit = 777,

    /// <summary>Equity rail (jurisdiction PNS): debit minus credit, the spendable position.</summary>
    Equity = 888,
}
