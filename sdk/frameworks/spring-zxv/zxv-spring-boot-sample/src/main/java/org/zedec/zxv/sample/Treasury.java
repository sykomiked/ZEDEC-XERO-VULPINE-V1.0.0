// Copyright (c) 2024-2026 Michael Laurence Curzi and 36N9 Genetics, LLC
// SPDX-License-Identifier: Apache-2.0
package org.zedec.zxv.sample;

import org.springframework.beans.factory.annotation.Qualifier;
import org.springframework.stereotype.Service;
import org.zedec.zxv.Capability;
import org.zedec.zxv.ledger.Account;
import org.zedec.zxv.ledger.AccountKind;
import org.zedec.zxv.ledger.Balance;
import org.zedec.zxv.ledger.Ledger;

/**
 * Posts and reads through two narrow capabilities from
 * {@code application.yml}: {@code teller} may post, {@code auditor} may only
 * read. Neither can sign, open models or touch budgets.
 */
@Service
public class Treasury {
    private final Ledger teller;
    private final Ledger auditor;
    private final Account bank;
    private final Account customer;

    /**
     * Opens an issuer account and one customer account in NGN.
     *
     * @param teller  LEDGER_READ and LEDGER_POST
     * @param auditor LEDGER_READ only
     */
    public Treasury(@Qualifier("teller") Capability teller, @Qualifier("auditor") Capability auditor) {
        this.teller = Ledger.of(teller);
        this.auditor = Ledger.of(auditor);
        this.bank = this.teller.openAccount("bank", "NGN", AccountKind.ISSUER);
        this.customer = this.teller.openAccount("ada", "NGN", AccountKind.HOLDER);
    }

    /**
     * Pays the customer from the issuer.
     *
     * @param amountMinor kobo (1/100 NGN), exact
     * @param reference   end-to-end reference
     * @return the journal sequence number
     */
    public long payCustomer(long amountMinor, String reference) {
        return teller.post(bank, customer, amountMinor, "NGN", reference);
    }

    /**
     * Refunds the issuer from the customer, through the read-only auditor
     * capability: always refused with CapabilityDeniedException. It exists to
     * show capability checks over HTTP.
     *
     * @param amountMinor kobo
     * @param reference   end-to-end reference
     * @return never returns normally
     */
    public long refundAsAuditor(long amountMinor, String reference) {
        return auditor.post(customer, bank, amountMinor, "NGN", reference);
    }

    /**
     * The customer's balance, read with the auditor capability.
     *
     * @return debits, credits and equity in kobo
     */
    public Balance customerBalance() {
        return auditor.balance(customer);
    }
}
