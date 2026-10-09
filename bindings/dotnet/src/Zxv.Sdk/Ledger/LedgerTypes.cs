// Copyright (c) 2024-2026 Michael Laurence Curzi and 36N9 Genetics, LLC
// SPDX-License-Identifier: Apache-2.0

using System.Globalization;

namespace Zxv.Ledger;

/// <summary>Identifies a ledger account within one <see cref="ZxvPlatform"/>.</summary>
/// <param name="Value">The native account number.</param>
public readonly record struct AccountId(uint Value)
{
    /// <summary>Returns the account number as invariant text.</summary>
    /// <returns>The account number.</returns>
    public override string ToString() => Value.ToString(CultureInfo.InvariantCulture);
}

/// <summary>What an account may hold.</summary>
public enum AccountKind
{
    /// <summary>A holder account. Its equity can never go below zero: no debt for holders.</summary>
    Holder = 0,

    /// <summary>
    /// An issuer account (issuance, settlement or nostro mirror). It may carry a
    /// CREDIT (810) balance: value it has put into circulation on the platform.
    /// </summary>
    Issuer = 1,
}

/// <summary>A ledger account.</summary>
/// <param name="Id">The account id.</param>
/// <param name="Name">Display name (UTF-8, at most 63 bytes).</param>
/// <param name="Currency">Currency or private unit code.</param>
/// <param name="Kind">Holder or issuer.</param>
public sealed record Account(AccountId Id, string Name, string Currency, AccountKind Kind);

/// <summary>
/// The three Vino rail balances of an account. <see cref="Equity"/> is the
/// spendable position and always equals <see cref="Debit"/> minus <see cref="Credit"/>.
/// </summary>
/// <param name="Debit">DEBIT rail (846): total received.</param>
/// <param name="Credit">CREDIT rail (810): total sent.</param>
/// <param name="Equity">EQUITY rail (888): debit minus credit.</param>
public sealed record RailBalances(Money Debit, Money Credit, Money Equity);

/// <summary>A request to move value between two accounts in the same currency.</summary>
/// <param name="From">Paying account.</param>
/// <param name="To">Receiving account.</param>
/// <param name="Amount">Positive amount in the accounts' currency.</param>
/// <param name="Reference">Unique reference (at most 35 bytes), normally the ISO 20022 EndToEndId.</param>
public sealed record Transfer(AccountId From, AccountId To, Money Amount, string Reference);

/// <summary>Result of a successful posting.</summary>
/// <param name="EntryId">Triple-ledger entry id of the posting.</param>
/// <param name="Transfer">The transfer that was posted.</param>
public sealed record PostingReceipt(ulong EntryId, Transfer Transfer);

/// <summary>One line of an account's journal.</summary>
/// <param name="EntryId">Triple-ledger entry id (shared by both sides of a posting).</param>
/// <param name="Amount">Signed amount: positive received, negative sent.</param>
/// <param name="Counterparty">The other account of the posting.</param>
/// <param name="Reference">The posting reference.</param>
public sealed record LedgerEntry(ulong EntryId, Money Amount, AccountId Counterparty, string Reference);
