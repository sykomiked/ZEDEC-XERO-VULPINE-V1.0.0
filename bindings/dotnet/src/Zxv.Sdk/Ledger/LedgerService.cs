// Copyright (c) 2024-2026 Michael Laurence Curzi and 36N9 Genetics, LLC
// SPDX-License-Identifier: Apache-2.0

using System;
using System.Collections.Generic;
using System.Threading;
using Microsoft.Extensions.Logging;
using Zxv.Currencies;
using Zxv.Interop;

namespace Zxv.Ledger;

/// <summary>
/// The platform ledger: accounts and balanced postings over the kernel triple
/// ledger (financial, provenance and externality axes). Obtain it from
/// <see cref="ZxvPlatform.Ledger"/>. Thread-safe; every posting is atomic.
/// </summary>
/// <remarks>
/// Calls are synchronous because they are in-process and complete in
/// microseconds; there is no I/O to await. Batch methods accept a
/// <see cref="CancellationToken"/> that is honoured between postings.
/// </remarks>
public sealed class LedgerService
{
    private readonly ZxvContextHandle _ctx;
    private readonly CurrencyService _currencies;
    private readonly ILogger _logger;

    internal LedgerService(ZxvContextHandle ctx, CurrencyService currencies, ILogger logger)
    {
        _ctx = ctx;
        _currencies = currencies;
        _logger = logger;
    }

    /// <summary>Opens an account in an enabled currency.</summary>
    /// <param name="name">Display name (UTF-8, at most 63 bytes).</param>
    /// <param name="currency">An enabled currency or private unit code.</param>
    /// <param name="kind">Holder (default) or issuer.</param>
    /// <returns>The new account.</returns>
    public Account OpenAccount(string name, string currency, AccountKind kind = AccountKind.Holder)
    {
        Guard.Text(name, nameof(name));
        Guard.Text(currency, nameof(currency));
        int rc = NativeMethods.LedgerOpenAccount(_ctx, name, currency, (int)kind, out uint id);
        NativeCall.Check(rc, "Ledger.OpenAccount");
        Log.AccountOpened(_logger, id, currency, kind);
        return new Account(new AccountId(id), name, currency, kind);
    }

    /// <summary>Gets an account.</summary>
    /// <param name="id">The account id.</param>
    /// <returns>The account.</returns>
    public unsafe Account GetAccount(AccountId id)
    {
        uint v = id.Value;
        string ccy = NativeCall.ReadText((byte* b, nuint c, out nuint l) => NativeMethods.LedgerAccountCcy(_ctx, v, b, c, out l), "Ledger.GetAccount");
        string name = NativeCall.ReadText((byte* b, nuint c, out nuint l) => NativeMethods.LedgerAccountName(_ctx, v, b, c, out l), "Ledger.GetAccount");
        NativeCall.Check(NativeMethods.LedgerAccountKind(_ctx, v, out int kind), "Ledger.GetAccount");
        return new Account(id, name, ccy, (AccountKind)kind);
    }

    /// <summary>Lists all accounts in id order.</summary>
    /// <returns>The accounts.</returns>
    public IReadOnlyList<Account> ListAccounts()
    {
        NativeCall.Check(NativeMethods.LedgerAccountCount(_ctx, out uint count), "Ledger.ListAccounts");
        var list = new List<Account>((int)count);
        for (uint i = 0; i < count; i++)
        {
            list.Add(GetAccount(new AccountId(i)));
        }

        return list;
    }

    /// <summary>
    /// Posts a balanced transfer. The native core validates funds, currency,
    /// reference uniqueness and capacity before writing anything.
    /// </summary>
    /// <param name="transfer">The transfer.</param>
    /// <returns>The posting receipt.</returns>
    /// <exception cref="ZxvException">
    /// <see cref="ZxvStatus.InsufficientFunds"/>, <see cref="ZxvStatus.InvalidCurrency"/> (including any
    /// cross-currency attempt: there is no implicit FX), <see cref="ZxvStatus.Duplicate"/> reference, and others.
    /// </exception>
    public PostingReceipt Post(Transfer transfer)
    {
        ArgumentNullException.ThrowIfNull(transfer);
        Guard.Text(transfer.Reference, nameof(transfer));
        Guard.Text(transfer.Amount.Currency, nameof(transfer));
        int rc = NativeMethods.LedgerPost(
            _ctx, transfer.From.Value, transfer.To.Value, transfer.Amount.MinorUnits, transfer.Amount.Currency, transfer.Reference, out ulong entryId);
        if (rc != (int)ZxvStatus.Ok)
        {
            ZxvException ex = NativeCall.Create(rc, "Ledger.Post");
            Log.PostRefused(_logger, transfer.Reference, ex.Code, ex.Detail);
            throw ex;
        }

        Log.Posted(_logger, entryId, transfer.Reference, transfer.Amount.MinorUnits, transfer.Amount.Currency);
        return new PostingReceipt(entryId, transfer);
    }

    /// <summary>Posts several transfers in order. Each posting is atomic; the batch is not.</summary>
    /// <param name="transfers">The transfers.</param>
    /// <param name="cancellationToken">Checked before each posting.</param>
    /// <returns>Receipts for the postings made.</returns>
    public IReadOnlyList<PostingReceipt> PostMany(IEnumerable<Transfer> transfers, CancellationToken cancellationToken = default)
    {
        ArgumentNullException.ThrowIfNull(transfers);
        var receipts = new List<PostingReceipt>();
        foreach (Transfer t in transfers)
        {
            cancellationToken.ThrowIfCancellationRequested();
            receipts.Add(Post(t));
        }

        return receipts;
    }

    /// <summary>Gets the three rail balances of an account.</summary>
    /// <param name="id">The account id.</param>
    /// <returns>DEBIT 846, CREDIT 810 and EQUITY 888 balances.</returns>
    public RailBalances GetBalances(AccountId id)
    {
        Account account = GetAccount(id);
        NativeCall.Check(NativeMethods.LedgerBalance(_ctx, id.Value, out long d, out long c, out long e), "Ledger.GetBalances");
        byte scale = ScaleOf(account.Currency);
        return new RailBalances(new Money(d, account.Currency, scale), new Money(c, account.Currency, scale), new Money(e, account.Currency, scale));
    }

    /// <summary>Gets the spendable position (EQUITY rail) of an account.</summary>
    /// <param name="id">The account id.</param>
    /// <returns>The equity balance.</returns>
    public Money GetAvailable(AccountId id) => GetBalances(id).Equity;

    /// <summary>Gets the number of journal lines of an account.</summary>
    /// <param name="id">The account id.</param>
    /// <returns>The line count.</returns>
    public int GetEntryCount(AccountId id)
    {
        NativeCall.Check(NativeMethods.LedgerEntryCount(_ctx, id.Value, out uint n), "Ledger.GetEntryCount");
        return checked((int)n);
    }

    /// <summary>Reads journal lines of an account, oldest first.</summary>
    /// <param name="id">The account id.</param>
    /// <param name="start">Zero-based index of the first line.</param>
    /// <param name="count">Maximum lines to return; <see langword="null"/> for all remaining.</param>
    /// <returns>The lines.</returns>
    public unsafe IReadOnlyList<LedgerEntry> GetEntries(AccountId id, int start = 0, int? count = null)
    {
        ArgumentOutOfRangeException.ThrowIfNegative(start);
        Account account = GetAccount(id);
        byte scale = ScaleOf(account.Currency);
        int total = GetEntryCount(id);
        int end = count is int c ? Math.Min(total, start + Math.Max(0, c)) : total;
        var list = new List<LedgerEntry>(Math.Max(0, end - start));
        for (int i = start; i < end; i++)
        {
            uint index = (uint)i;
            ulong entryId = 0;
            long amount = 0;
            uint cp = 0;
            string reference = NativeCall.ReadText(
                (byte* b, nuint cap, out nuint l) => NativeMethods.LedgerEntryAt(_ctx, id.Value, index, out entryId, out amount, out cp, b, cap, out l),
                "Ledger.GetEntries");
            list.Add(new LedgerEntry(entryId, new Money(amount, account.Currency, scale), new AccountId(cp), reference));
        }

        return list;
    }

    /// <summary>
    /// Verifies the ledger invariants: per currency the DEBIT and CREDIT rails
    /// balance, no holder is negative, and the exact minor-unit shadow equals the
    /// kernel triple-ledger financial axis.
    /// </summary>
    /// <exception cref="ZxvException">An invariant failed (<see cref="ZxvStatus.InvariantViolated"/>).</exception>
    public void Verify() => NativeCall.Check(NativeMethods.LedgerCheck(_ctx), "Ledger.Verify");

    /// <summary>Returns whether the ledger invariants hold, without throwing.</summary>
    /// <param name="detail">The failure detail, when they do not.</param>
    /// <returns><see langword="true"/> when every invariant holds.</returns>
    public bool TryVerify(out string detail)
    {
        int rc = NativeMethods.LedgerCheck(_ctx);
        detail = rc == (int)ZxvStatus.Ok ? string.Empty : NativeText.LastError();
        return rc == (int)ZxvStatus.Ok;
    }

    private byte ScaleOf(string currency) => _currencies.GetEnabled(currency).MinorUnits ?? 0;
}
