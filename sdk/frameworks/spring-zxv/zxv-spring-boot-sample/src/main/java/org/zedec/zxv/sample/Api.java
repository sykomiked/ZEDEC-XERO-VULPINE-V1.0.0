// Copyright (c) 2024-2026 Michael Laurence Curzi and 36N9 Genetics, LLC
// SPDX-License-Identifier: Apache-2.0
package org.zedec.zxv.sample;

import org.springframework.web.bind.annotation.GetMapping;
import org.springframework.web.bind.annotation.PostMapping;
import org.springframework.web.bind.annotation.RequestBody;
import org.springframework.web.bind.annotation.RequestParam;
import org.springframework.web.bind.annotation.RestController;
import org.zedec.zxv.Rational;
import org.zedec.zxv.ledger.Balance;

/** The sample's endpoints. Every number is exact: Rational strings or integer minor units. */
@RestController
public class Api {
    private final Treasury treasury;

    /**
     * Creates the controller.
     *
     * @param treasury the ledger service
     */
    public Api(Treasury treasury) {
        this.treasury = treasury;
    }

    /**
     * A quote request. {@code price} and {@code quantity} are JSON strings
     * such as {@code "12.75"} or {@code "1/3"}; a JSON float is refused with 400.
     *
     * @param price    unit price
     * @param quantity quantity
     */
    public record QuoteRequest(Rational price, Rational quantity) {}

    /**
     * A quote.
     *
     * @param total price times quantity, exact
     */
    public record QuoteResponse(Rational total) {}

    /**
     * A payment.
     *
     * @param amountMinor integer minor units (kobo)
     * @param reference   end-to-end reference
     */
    public record Payment(long amountMinor, String reference) {}

    /**
     * A posted payment.
     *
     * @param sequence journal sequence number
     */
    public record Posted(long sequence) {}

    /**
     * {@code price * quantity}, exactly.
     *
     * @param q the request
     * @return the total
     */
    @PostMapping("/quote")
    public QuoteResponse quote(@RequestBody QuoteRequest q) {
        return new QuoteResponse(q.price().multiply(q.quantity()));
    }

    /**
     * {@code a + b} from query parameters ({@code ?a=0.1&b=0.2} is {@code "0.3"}).
     *
     * @param a first term
     * @param b second term
     * @return the sum
     */
    @GetMapping("/sum")
    public QuoteResponse sum(@RequestParam Rational a, @RequestParam Rational b) {
        return new QuoteResponse(a.add(b));
    }

    /**
     * Pays the customer through the teller capability.
     *
     * @param p the payment
     * @return the journal sequence
     */
    @PostMapping("/payments")
    public Posted pay(@RequestBody Payment p) {
        return new Posted(treasury.payCustomer(p.amountMinor(), p.reference()));
    }

    /**
     * Tries to post through the read-only auditor capability: 403.
     *
     * @param p the payment
     * @return never returns normally
     */
    @PostMapping("/refunds")
    public Posted refund(@RequestBody Payment p) {
        return new Posted(treasury.refundAsAuditor(p.amountMinor(), p.reference()));
    }

    /**
     * The customer's balance.
     *
     * @return the balance in kobo
     */
    @GetMapping("/balance")
    public Balance balance() {
        return treasury.customerBalance();
    }
}
