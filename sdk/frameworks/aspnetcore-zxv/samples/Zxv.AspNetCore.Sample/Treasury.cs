// Copyright (c) 2024-2026 Michael Laurence Curzi and 36N9 Genetics, LLC
// SPDX-License-Identifier: Apache-2.0

using Microsoft.Extensions.DependencyInjection;
using Zxv.Currencies;
using Zxv.Ledger;

namespace Zxv.AspNetCore.Sample;

/// <summary>
/// Posts through the "teller" capability (LedgerRead, LedgerPost) and reads
/// through the "auditor" capability (LedgerRead only). Neither can sign,
/// open models or touch budgets.
/// </summary>
public sealed class Treasury
{
    private readonly CapabilityLedger _teller;
    private readonly CapabilityLedger _auditor;
    private readonly Currency _ngn;
    private readonly Account _bank;
    private readonly Account _customer;

    /// <summary>Opens an issuer account and one customer account in NGN.</summary>
    /// <param name="zxv">The platform.</param>
    /// <param name="teller">May post.</param>
    /// <param name="auditor">May only read.</param>
    public Treasury(ZxvPlatform zxv, [FromKeyedServices("teller")] Capability teller, [FromKeyedServices("auditor")] Capability auditor)
    {
        System.ArgumentNullException.ThrowIfNull(zxv);
        _teller = zxv.Ledger.WithCapability(teller);
        _auditor = zxv.Ledger.WithCapability(auditor);
        _ngn = zxv.Currencies.GetEnabled("NGN");
        _bank = _teller.OpenAccount("bank", "NGN", AccountKind.Issuer);
        _customer = _teller.OpenAccount("ada", "NGN", AccountKind.Holder);
    }

    /// <summary>Pays the customer from the issuer.</summary>
    /// <param name="amountMinor">Kobo.</param>
    /// <param name="reference">End-to-end reference.</param>
    /// <returns>The journal entry id.</returns>
    public ulong PayCustomer(long amountMinor, string reference) =>
        _teller.Post(new Transfer(_bank.Id, _customer.Id, Money.FromMinorUnits(amountMinor, _ngn), reference)).EntryId;

    /// <summary>Tries to refund through the read-only auditor capability: always CapabilityDeniedException (403).</summary>
    /// <param name="amountMinor">Kobo.</param>
    /// <param name="reference">End-to-end reference.</param>
    /// <returns>Never returns normally.</returns>
    public ulong RefundAsAuditor(long amountMinor, string reference) =>
        _auditor.Post(new Transfer(_customer.Id, _bank.Id, Money.FromMinorUnits(amountMinor, _ngn), reference)).EntryId;

    /// <summary>The customer's balance, read with the auditor capability.</summary>
    /// <returns>The balance in kobo.</returns>
    public BalanceView CustomerBalance()
    {
        RailBalances b = _auditor.GetBalances(_customer.Id);
        return new BalanceView(b.Debit.MinorUnits, b.Credit.MinorUnits, b.Equity.MinorUnits);
    }
}
