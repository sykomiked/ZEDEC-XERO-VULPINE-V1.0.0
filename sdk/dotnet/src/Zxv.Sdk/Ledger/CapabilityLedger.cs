// Copyright (c) 2024-2026 Michael Laurence Curzi and 36N9 Genetics, LLC
// SPDX-License-Identifier: Apache-2.0

using System;
using System.Collections.Generic;
using System.Threading;
using Zxv.Interop;

namespace Zxv.Ledger;

/// <summary>
/// The ledger seen through a <see cref="Capability"/>: the same semantics as
/// <see cref="LedgerService"/>, but every call presents the capability and the
/// native library checks it (posting and opening accounts need
/// <see cref="CapabilityRights.LedgerPost"/>; balances, accounts and journals
/// need <see cref="CapabilityRights.LedgerRead"/>; ABI 1.2). Obtain it with
/// <see cref="LedgerService.WithCapability(Capability)"/> and hand it to code
/// that should hold no more than that capability. Thread-safe. Like the
/// platform it belongs to, it throws <see cref="ObjectDisposedException"/>
/// once the platform is disposed.
/// </summary>
public sealed class CapabilityLedger
{
    private readonly LedgerService _ledger;
    private readonly Capability _cap;

    internal CapabilityLedger(LedgerService ledger, Capability capability)
    {
        _ledger = ledger;
        _cap = capability;
    }

    /// <summary>Gets the capability presented on every call.</summary>
    public Capability Capability => _cap;

    /// <summary>Posts a balanced transfer (see <see cref="LedgerService.Post(Transfer)"/>).</summary>
    /// <param name="transfer">The transfer.</param>
    /// <returns>The posting receipt.</returns>
    /// <exception cref="CapabilityDeniedException">The capability cannot post.</exception>
    /// <exception cref="InsufficientFundsException">A holder would go below zero.</exception>
    public PostingReceipt Post(Transfer transfer)
    {
        ArgumentNullException.ThrowIfNull(transfer);
        Guard.Text(transfer.Reference, nameof(transfer));
        Guard.Text(transfer.Amount.Currency, nameof(transfer));
        Enter("Ledger.Post");
        int rc = NativeMethods.LedgerPostCap(_cap.Handle, transfer.From.Value, transfer.To.Value, transfer.Amount.MinorUnits, transfer.Amount.Currency, transfer.Reference, out ulong entryId);
        _cap.Check(rc, "Ledger.Post");
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

    /// <summary>Opens an account in an enabled currency. Needs <see cref="CapabilityRights.LedgerPost"/>.</summary>
    /// <param name="name">Display name (UTF-8, at most 63 bytes).</param>
    /// <param name="currency">An enabled currency or private unit code.</param>
    /// <param name="kind">Holder (default) or issuer.</param>
    /// <returns>The new account.</returns>
    /// <exception cref="CapabilityDeniedException">The capability cannot post.</exception>
    public Account OpenAccount(string name, string currency, AccountKind kind = AccountKind.Holder)
    {
        Guard.Text(name, nameof(name));
        Guard.Text(currency, nameof(currency));
        Enter("Ledger.OpenAccount");
        _cap.Check(NativeMethods.LedgerOpenAccountCap(_cap.Handle, name, currency, (int)kind, out uint id), "Ledger.OpenAccount");
        return new Account(new AccountId(id), name, currency, kind);
    }

    /// <summary>Gets an account. Needs <see cref="CapabilityRights.LedgerRead"/>.</summary>
    /// <param name="id">The account id.</param>
    /// <returns>The account.</returns>
    /// <exception cref="CapabilityDeniedException">The capability cannot read.</exception>
    public unsafe Account GetAccount(AccountId id)
    {
        uint v = id.Value;
        Enter("Ledger.GetAccount");
        string ccy = ReadText((byte* b, nuint c, out nuint l) => NativeMethods.LedgerAccountCcyCap(_cap.Handle, v, b, c, out l), "Ledger.GetAccount");
        string name = ReadText((byte* b, nuint c, out nuint l) => NativeMethods.LedgerAccountNameCap(_cap.Handle, v, b, c, out l), "Ledger.GetAccount");
        _cap.Check(NativeMethods.LedgerAccountKindCap(_cap.Handle, v, out int kind), "Ledger.GetAccount");
        return new Account(id, name, ccy, (AccountKind)kind);
    }

    /// <summary>Lists all accounts in id order. Needs <see cref="CapabilityRights.LedgerRead"/>.</summary>
    /// <returns>The accounts.</returns>
    public IReadOnlyList<Account> ListAccounts()
    {
        Enter("Ledger.ListAccounts");
        _cap.Check(NativeMethods.LedgerAccountCountCap(_cap.Handle, out uint count), "Ledger.ListAccounts");
        var list = new List<Account>((int)count);
        for (uint i = 0; i < count; i++)
        {
            list.Add(GetAccount(new AccountId(i)));
        }

        return list;
    }

    /// <summary>Gets the number of journal lines of an account. Needs <see cref="CapabilityRights.LedgerRead"/>.</summary>
    /// <param name="id">The account id.</param>
    /// <returns>The line count.</returns>
    public int GetEntryCount(AccountId id)
    {
        Enter("Ledger.GetEntryCount");
        _cap.Check(NativeMethods.LedgerEntryCountCap(_cap.Handle, id.Value, out uint n), "Ledger.GetEntryCount");
        return checked((int)n);
    }

    /// <summary>Reads journal lines of an account, oldest first. Needs <see cref="CapabilityRights.LedgerRead"/>.</summary>
    /// <param name="id">The account id.</param>
    /// <param name="start">Zero-based index of the first line.</param>
    /// <param name="count">Maximum lines to return; <see langword="null"/> for all remaining.</param>
    /// <returns>The lines.</returns>
    public unsafe IReadOnlyList<LedgerEntry> GetEntries(AccountId id, int start = 0, int? count = null)
    {
        ArgumentOutOfRangeException.ThrowIfNegative(start);
        Account account = GetAccount(id);
        byte scale = _ledger.ScaleOf(account.Currency);
        int total = GetEntryCount(id);
        int end = count is int c ? Math.Min(total, start + Math.Max(0, c)) : total;
        var list = new List<LedgerEntry>(Math.Max(0, end - start));
        for (int i = start; i < end; i++)
        {
            uint index = (uint)i;
            ulong entryId = 0;
            long amount = 0;
            uint cp = 0;
            string reference = ReadText(
                (byte* b, nuint cap, out nuint l) => NativeMethods.LedgerEntryAtCap(_cap.Handle, id.Value, index, out entryId, out amount, out cp, b, cap, out l),
                "Ledger.GetEntries");
            list.Add(new LedgerEntry(entryId, new Money(amount, account.Currency, scale), new AccountId(cp), reference));
        }

        return list;
    }

    /// <summary>Gets the spendable position (EQUITY rail) of an account. Needs <see cref="CapabilityRights.LedgerRead"/>.</summary>
    /// <param name="id">The account id.</param>
    /// <returns>The equity balance.</returns>
    public Money GetAvailable(AccountId id) => GetBalances(id).Equity;

    /// <summary>Gets the three rail balances of an account.</summary>
    /// <param name="id">The account.</param>
    /// <returns>The balances.</returns>
    public RailBalances GetBalances(AccountId id)
    {
        Enter("Ledger.GetBalances");
        _cap.Check(NativeMethods.LedgerBalanceCap(_cap.Handle, id.Value, out long d, out long c, out long e), "Ledger.GetBalances");
        Account account = GetAccount(id);
        byte scale = _ledger.ScaleOf(account.Currency);
        return new RailBalances(new Money(d, account.Currency, scale), new Money(c, account.Currency, scale), new Money(e, account.Currency, scale));
    }

    /// <summary>
    /// Verifies the ledger invariants. A failure halts the context: it raises
    /// <see cref="ZxvFatalException"/> and every later call fails the same way.
    /// </summary>
    public void Verify()
    {
        Enter("Ledger.Verify");
        _cap.Check(NativeMethods.LedgerCheckCap(_cap.Handle), "Ledger.Verify");
    }

    /// <summary>Fails fast once the platform is disposed, or the capability is disposed or its context known halted.</summary>
    private void Enter(string operation)
    {
        _ledger.ThrowIfDisposed();
        _cap.Enter(operation);
    }

    private string ReadText(NativeTextCall read, string operation)
    {
        int rc = NativeText.TryRead(read, out string text);
        _cap.Check(rc, operation);
        return text;
    }
}
