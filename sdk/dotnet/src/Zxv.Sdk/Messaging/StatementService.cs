// Copyright (c) 2024-2026 Michael Laurence Curzi and 36N9 Genetics, LLC
// SPDX-License-Identifier: Apache-2.0

using System;
using System.Collections.Generic;
using System.Linq;
using Zxv.Ledger;
using Zxv.Payments;

namespace Zxv.Messaging;

/// <summary>Which part of an account journal a statement page covers.</summary>
public sealed record StatementRequest
{
    /// <summary>Gets the zero-based index of the first journal line. Default 0.</summary>
    public int Start { get; init; }

    /// <summary>Gets the maximum number of lines (1 to 32). Default 32.</summary>
    public int MaxEntries { get; init; } = MessageService.MaxStatementEntries;

    /// <summary>Gets the MsgId; generated when <see langword="null"/>.</summary>
    public string? MessageId { get; init; }
}

/// <summary>One page of an account statement, reconciled and serialized as camt.053.</summary>
/// <param name="Account">The account.</param>
/// <param name="Opening">Balance before the first line of the page.</param>
/// <param name="Closing">Balance after the last line of the page.</param>
/// <param name="Entries">The journal lines on this page.</param>
/// <param name="NextStart">Index for the next page, or <see langword="null"/> at the end of the journal.</param>
/// <param name="Document">The camt.053 document.</param>
public sealed record AccountStatement(Account Account, Money Opening, Money Closing, IReadOnlyList<LedgerEntry> Entries, int? NextStart, MxDocument Document);

/// <summary>
/// Produces camt.053 statements from the ledger journal. Obtain it from
/// <see cref="ZxvPlatform.Statements"/>.
/// </summary>
public sealed class StatementService
{
    private readonly LedgerService _ledger;
    private readonly MessageService _messages;
    private readonly PaymentService _payments;
    private readonly IdGenerator _ids;
    private readonly TimeProvider _time;

    internal StatementService(LedgerService ledger, MessageService messages, PaymentService payments, IdGenerator ids, TimeProvider time)
    {
        _ledger = ledger;
        _messages = messages;
        _payments = payments;
        _ids = ids;
        _time = time;
    }

    /// <summary>Builds one statement page for an account.</summary>
    /// <param name="account">The account.</param>
    /// <param name="request">Paging; defaults to the first 32 lines.</param>
    /// <returns>The page.</returns>
    /// <remarks>
    /// The opening balance is recomputed from the journal so that every page
    /// reconciles exactly (OPBD + booked entries = CLBD), which the native
    /// builder enforces. Statements are in ISO 4217 currencies only; a private
    /// unit (VFV) account is refused with <see cref="Interop.ZxvStatus.InvalidCurrency"/>.
    /// </remarks>
    public AccountStatement BuildStatement(AccountId account, StatementRequest? request = null) => BuildCore(null, account, request);

    /// <summary>
    /// Builds one statement page for an account through <paramref name="capability"/>
    /// (needs <see cref="CapabilityRights.LedgerRead"/>; ABI 1.2).
    /// </summary>
    /// <param name="capability">A capability of this platform.</param>
    /// <param name="account">The account.</param>
    /// <param name="request">Paging; defaults to the first 32 lines.</param>
    /// <returns>The page.</returns>
    /// <exception cref="CapabilityDeniedException">The capability cannot read.</exception>
    /// <exception cref="ArgumentException">The capability belongs to a different platform.</exception>
    public AccountStatement BuildStatement(Capability capability, AccountId account, StatementRequest? request = null)
    {
        ArgumentNullException.ThrowIfNull(capability);
        return BuildCore(capability, account, request);
    }

    private AccountStatement BuildCore(Capability? capability, AccountId account, StatementRequest? request)
    {
        request ??= new StatementRequest();
        ArgumentOutOfRangeException.ThrowIfNegative(request.Start);
        ArgumentOutOfRangeException.ThrowIfLessThan(request.MaxEntries, 1);
        ArgumentOutOfRangeException.ThrowIfGreaterThan(request.MaxEntries, MessageService.MaxStatementEntries);
        CapabilityLedger? scoped = capability is null ? null : _ledger.WithCapability(capability);

        Account acct = scoped?.GetAccount(account) ?? _ledger.GetAccountUnchecked(account);
        IReadOnlyList<LedgerEntry> before = scoped?.GetEntries(account, 0, request.Start) ?? _ledger.GetEntriesUnchecked(account, 0, request.Start);
        IReadOnlyList<LedgerEntry> page = scoped?.GetEntries(account, request.Start, request.MaxEntries) ?? _ledger.GetEntriesUnchecked(account, request.Start, request.MaxEntries);
        Money zero = (scoped?.GetBalances(account) ?? _ledger.GetBalances(account)).Equity with { MinorUnits = 0 };
        Money opening = before.Aggregate(zero, (sum, e) => sum + e.Amount);
        Money closing = page.Aggregate(opening, (sum, e) => sum + e.Amount);
        int total = scoped?.GetEntryCount(account) ?? _ledger.GetEntryCountUnchecked(account);
        int next = request.Start + page.Count;

        MxDocument doc = _messages.BuildCamt053Core(capability, new Camt053Message(
            request.MessageId ?? _ids.NextMessageId(),
            _time.GetUtcNow(),
            _payments.ToIdentifier(account),
            opening,
            closing,
            page.Select(e => new StatementEntry(e.Amount)).ToList()));

        return new AccountStatement(acct, opening, closing, page, next < total ? next : null, doc);
    }
}
