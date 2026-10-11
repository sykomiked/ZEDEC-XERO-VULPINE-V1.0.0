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
    private readonly ContextRef _context;
    private readonly CurrencyService _currencies;
    private readonly ILogger _logger;
    private readonly Capability _service;
    private readonly CapabilityLedger _own;

    /// <summary>Initializes the service.</summary>
    /// <param name="context">The platform context.</param>
    /// <param name="currencies">The platform currencies.</param>
    /// <param name="service">
    /// The platform's own capability (never exposed). The members that take no
    /// capability go through it, so every native call is an ABI 1.2 <c>*_cap</c> form.
    /// </param>
    /// <param name="logger">The logger.</param>
    internal LedgerService(ContextRef context, CurrencyService currencies, Capability service, ILogger logger)
    {
        _context = context;
        _currencies = currencies;
        _service = service;
        _logger = logger;
        _own = new CapabilityLedger(this, service);
    }

    /// <summary>Gets the ledger seen through the platform's own capability; throws once the platform is disposed.</summary>
    private CapabilityLedger Own
    {
        get
        {
            _context.ThrowIfDisposed();
            return _own;
        }
    }

    /// <summary>Opens an account in an enabled currency, without a capability.</summary>
    /// <param name="name">Display name (UTF-8, at most 63 bytes).</param>
    /// <param name="currency">An enabled currency or private unit code.</param>
    /// <param name="kind">Holder (default) or issuer.</param>
    /// <returns>The new account.</returns>
    [Obsolete("Deprecated since ABI 1.2 (no capability): use WithCapability(cap).OpenAccount, which needs LedgerPost.")]
    public Account OpenAccount(string name, string currency, AccountKind kind = AccountKind.Holder)
    {
        Account account = Own.OpenAccount(name, currency, kind);
        Log.AccountOpened(_logger, account.Id.Value, currency, kind);
        return account;
    }

    /// <summary>Gets an account, without a capability.</summary>
    /// <param name="id">The account id.</param>
    /// <returns>The account.</returns>
    [Obsolete("Deprecated since ABI 1.2 (no capability): use WithCapability(cap).GetAccount, which needs LedgerRead.")]
    public Account GetAccount(AccountId id) => GetAccountUnchecked(id);

    /// <summary>Reads an account through the platform's own capability (payments, statements).</summary>
    internal Account GetAccountUnchecked(AccountId id) => Own.GetAccount(id);

    /// <summary>Lists all accounts in id order, without a capability.</summary>
    /// <returns>The accounts.</returns>
    [Obsolete("Deprecated since ABI 1.2 (no capability): use WithCapability(cap).ListAccounts, which needs LedgerRead.")]
    public IReadOnlyList<Account> ListAccounts() => Own.ListAccounts();

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
    /// <remarks>
    /// This member presents the platform's own capability; code that should
    /// hold less posts through <see cref="WithCapability(Capability)"/>.
    /// </remarks>
    public PostingReceipt Post(Transfer transfer)
    {
        CapabilityLedger own = Own;
        PostingReceipt receipt;
        try
        {
            receipt = own.Post(transfer);
        }
        catch (ZxvException zx)
        {
            Log.PostRefused(_logger, transfer.Reference, zx.Code, zx.Detail);
            throw;
        }

        Log.Posted(_logger, receipt.EntryId, transfer.Reference, transfer.Amount.MinorUnits, transfer.Amount.Currency);
        return receipt;
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
    /// <returns>DEBIT 555, CREDIT 777 and EQUITY 888 balances.</returns>
    public RailBalances GetBalances(AccountId id) => Own.GetBalances(id);

    /// <summary>Gets the spendable position (EQUITY rail) of an account.</summary>
    /// <param name="id">The account id.</param>
    /// <returns>The equity balance.</returns>
    public Money GetAvailable(AccountId id) => GetBalances(id).Equity;

    /// <summary>Gets the number of journal lines of an account, without a capability.</summary>
    /// <param name="id">The account id.</param>
    /// <returns>The line count.</returns>
    [Obsolete("Deprecated since ABI 1.2 (no capability): use WithCapability(cap).GetEntryCount, which needs LedgerRead.")]
    public int GetEntryCount(AccountId id) => GetEntryCountUnchecked(id);

    /// <summary>Counts journal lines through the platform's own capability (statements).</summary>
    internal int GetEntryCountUnchecked(AccountId id) => Own.GetEntryCount(id);

    /// <summary>Reads journal lines of an account, oldest first, without a capability.</summary>
    /// <param name="id">The account id.</param>
    /// <param name="start">Zero-based index of the first line.</param>
    /// <param name="count">Maximum lines to return; <see langword="null"/> for all remaining.</param>
    /// <returns>The lines.</returns>
    [Obsolete("Deprecated since ABI 1.2 (no capability): use WithCapability(cap).GetEntries, which needs LedgerRead.")]
    public IReadOnlyList<LedgerEntry> GetEntries(AccountId id, int start = 0, int? count = null) => GetEntriesUnchecked(id, start, count);

    /// <summary>Reads journal lines through the platform's own capability (statements).</summary>
    internal IReadOnlyList<LedgerEntry> GetEntriesUnchecked(AccountId id, int start = 0, int? count = null) => Own.GetEntries(id, start, count);

    /// <summary>
    /// Verifies the ledger invariants: per currency the DEBIT and CREDIT rails
    /// balance, no holder is negative, and the exact minor-unit shadow equals the
    /// kernel triple-ledger financial axis.
    /// </summary>
    /// <exception cref="ZxvFatalException">An invariant failed (<see cref="ZxvStatus.InvariantViolated"/>): the context is now halted.</exception>
    public void Verify() => Own.Verify();

    /// <summary>Returns whether the ledger invariants hold, without throwing.</summary>
    /// <param name="detail">The failure detail, when they do not.</param>
    /// <returns><see langword="true"/> when every invariant holds.</returns>
    public bool TryVerify(out string detail)
    {
        _context.ThrowIfDisposed();
        int rc = NativeMethods.LedgerCheckCap(_service.Handle);
        detail = rc == (int)ZxvStatus.Ok ? string.Empty : NativeText.LastError();
        return rc == (int)ZxvStatus.Ok;
    }

    /// <summary>
    /// Returns the ledger seen through <paramref name="capability"/>: every
    /// call presents it and the native library checks it.
    /// </summary>
    /// <param name="capability">A capability of the same platform.</param>
    /// <returns>The capability-checked ledger.</returns>
    public CapabilityLedger WithCapability(Capability capability)
    {
        ArgumentNullException.ThrowIfNull(capability);
        _context.ThrowIfDisposed();
        if (!ReferenceEquals(capability.Context, _context))
        {
            throw new ArgumentException("The capability belongs to a different platform.", nameof(capability));
        }

        return new CapabilityLedger(this, capability);
    }

    /// <summary>Throws <see cref="ObjectDisposedException"/> once the platform is disposed.</summary>
    internal void ThrowIfDisposed() => _context.ThrowIfDisposed();

    internal byte ScaleOf(string currency) => _currencies.GetEnabled(currency).MinorUnits ?? 0;
}
