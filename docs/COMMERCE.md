<!--
Copyright (c) 2024-2026 Michael Laurence Curzi and 36N9 Genetics, LLC
SPDX-License-Identifier: Apache-2.0
-->

# Commerce on ZXV: a guide for buyers, sellers and institutions

ZXV has a marketplace built in. Anyone can open a shop, and anyone can buy from one. There is no central marketplace server. Shops and listings are signed by their owner's key, so they can travel over the peer-to-peer network, a LAN or the internet, and any node can check that they are genuine.

The code is in `kernel/src/market`. This guide covers what it does for each kind of user, and where its limits are.

## What you can sell

| Kind | How it is delivered | When the money is released |
|---|---|---|
| Physical goods | The seller ships and records a carrier proof | When the buyer confirms receipt, or after the release timer (default 3 days, set per listing within 1 to 30 days). If there is no verified carrier proof, the timer is doubled. |
| Digital goods | By content ID (CID). The delivered CID must match the CID in the listing exactly. | On buyer confirmation, or by timer |
| Services | The seller marks the job done | On buyer confirmation, or by timer |
| Bookings | A time slot with a fixed capacity | After the slot, on confirmation or by timer |
| Subscriptions | Access starts when payment is made | At the end of the period. The buyer can open a dispute at any point during the period. |

You can price in VFV, the platform's internal unit (rail numeric 555, 2 decimal places), or in any ISO 4217 currency the operator has registered, such as USD, EUR, JPY (0 decimals) or BHD (3 decimals). Every amount is a whole number of the currency's smallest unit (cents, yen, fils), so no rounding drift is possible. Each order uses one currency. If a cart holds items from several shops or in several currencies, it becomes one order per shop and currency.

## For buyers

1. **Add items to a cart and check out.** Stock is reserved when you check out. You then have one hour to pay; if you don't, the order lapses and the stock is released.
2. **Your money goes into escrow, not to the seller.** It stays there until one of three things happens:
   - you confirm receipt;
   - the seller shows proof of delivery and the release timer runs out without a dispute;
   - a mediator rules on a dispute.
3. **If the seller never delivers,** you get a full refund automatically. For physical goods this happens 7 days after payment by default. For bookings it happens one day after the slot ends.
4. **Cancelling:**
   - Before you pay, you can cancel freely.
   - After you pay but before anything ships, you can still cancel and get a full refund.
   - For bookings only, cancelling inside the seller's stated cutoff costs the flat cancellation fee shown on the listing. That fee can never be more than the price.
   - If the seller cancels, you never pay a fee.
5. **Returns.** If a listing has a return window, you can return some or all of the units within it. You get back exactly what you paid for those units, tax included, to the smallest unit. If the seller does not act within 7 days of your return being shipped, the refund happens anyway when the money is still in escrow. If the money has already been released and the seller fails to fund the refund, the failure goes on the seller's record.
6. **Disputes.** You or the seller can open a dispute while the money is still in escrow. Both sides can submit evidence by CID. The case is decided by one of two mediators:
   - **A chosen arbiter.** The shop names one when it opens, and you accept them by placing the order.
   - **A community panel.** You can choose this at checkout instead. The operator picks the jurors. The buyer and seller can never sit on the panel. The ruling is the median of the jurors' votes, so one extreme juror cannot swing it.

   Rulings are signed. No timer decides a dispute: a panel gives a ruling at its deadline only if a majority has voted.
7. **Reviews.** You can leave one review per completed order, and only you, the buyer, can leave it. You cannot review an order that was refunded before it completed. Each review's weight comes from the operator's sybil-resistance hook (for example the independence weighting in `kernel/src/social`), so a crowd of sock puppets counts for little.
8. **Subscriptions have no traps.**
   - Auto-renew is **off** until you turn it on.
   - When you turn it on, you set how many renewals you allow, and the price is locked at what you agreed to.
   - You get a reminder at least 3 days before each renewal.
   - If the price goes up, the subscription does not renew until you agree again.
   - Cancelling is one step, at any time, with no fee.

## For people who let an assistant shop for them

You can give an AI assistant a **mandate**. It sets a limit per order, a total limit, the categories the assistant may buy from, and an expiry date. Within those limits the assistant can fill a cart and prepare orders.

**It still cannot buy anything without you.** Every order it builds waits for your **confirmation token**: your own signature over that exact order, including every item, price, tax line, the seller, and a one-time number. Some things the token guarantees:

- A token for one order cannot confirm a different one.
- A token cannot be used twice.
- The assistant cannot make a token itself.
- An order you don't confirm within a day lapses.
- You can revoke a mandate at any time.

## For sellers

1. **Open a shop.** You sign the shop details (name, region, optional arbiter, how quickly you ship) with your key. To update the shop, you sign a new version with a higher version number. An old version cannot be replayed.
2. **List items.** Each listing is signed the same way and is tied to your shop's current signed record. For each listing you set:
   - stock;
   - shipping, as a flat charge per order line;
   - tax class;
   - return window;
   - release timer;
   - for digital goods, the content CID;
   - for bookings, the slots.

   You can withdraw a listing or restock it at any time.
3. **Get paid.** When escrow releases, you receive the sale amount minus the **φ% tithe**, plus the tax you collected if the operator has not set itself up to remit tax for you.

   The tithe is exactly `floor(a · φ / 100)`, about 1.618%, where `a` is your net revenue on the sale, excluding tax. It is taken **once per sale**. Some consequences:
   - A sale refunded before release pays no tithe.
   - If you refund after release, the commons returns the matching share of the tithe, so the tithe always equals the tithe on what you actually kept.
   - There are no other platform fees in this module.
4. **Being found.** Search is ranked by the Interaction Surplus Framework (`kernel/src/concord`). Shops whose interests *complement* the buyer's rank highest; near-copies of what the buyer already has, and direct opposites, rank lowest.
   - Review quality also counts, but a new shop starts at a neutral score worth two reviews, so it is not buried.
   - No one shop may take more than two places on a page.
   - **There is no way to pay for placement.** The ranking has no input for ad spend.

   If a buyer and a seller have a mutual concord divide, neither can see the other's shop.
5. **Taxes.** The operator enters the rates for each region and tax class: VAT, GST or sales tax, prices with or without tax included, whether shipping is taxed, and how to round. The market only does the arithmetic. If no rate exists for a sale, the order is flagged `tax_unconfigured` and no tax is added. **This software gives no tax advice.** Ask a professional what you owe.

## For institutions (B2B)

- **Purchase orders.** The buyer organisation drafts a PO with agreed unit prices and payment terms (net days, up to 365), then signs it. The seller then accepts or rejects it. Either side can see the state at every step: draft, submitted, accepted or rejected, invoiced, closed. The buyer can cancel until the seller accepts.
- **Invoices.** When the seller ships, an invoice is issued with the tax computed. Invoice numbers are unique per shop.
- **No interest, ever.** An invoice can carry only **flat, disclosed fees**:
  - an optional flat service fee;
  - an optional flat late fee.

  Both are fixed in the PO before the seller accepts it, and the operator caps both. The late fee applies **once**, when the due date passes, and never grows: an invoice 400 days late owes the same as one a day late. If payment was started before the due date, no late fee applies at all. There is no rate field anywhere in the code, so interest cannot be configured.
- **Bank payment (ISO 20022).** For a fiat invoice, the market hands a payment-initiation request (amount, currency, minor units, end-to-end ID `ZXV-INV-<number>`, remittance `/ZXV/INV/<number>`) to the operator's **pain.001** builder. That builder is `pay_iso` in `kernel/src/pay`, which owns the schema and the CBPR+ rules. When payment arrives, the invoice settles for the exact amount due. The seller remits the tithe once, on revenue excluding tax.
- **VFV invoices** never go into an ISO 20022 currency field. They settle inside the platform.
- **Gatekeeping (optional).** The operator can attach a check at checkout, such as an opt-in KYC tier from `kernel/src/ident` or a sanctions screen. With no check attached, the market is open to all.

## How the money stays honest

These properties are tested in `kernel/src/market/test_market.c`, which runs 3,239 checks:

- **Every state change is in one table.** The test compares the table, edge by edge, with an independent list. It drives all 22 transitions through the API, and tries every disallowed action in each state to show it is refused without changing anything.
- **Escrow conservation.** In every order, what came in equals what went out plus what is still held. Nothing is held once an order is finished. An independent mock ledger checks this after every operation, including 3,000 randomised steps with settlement failures injected.
- **Refund exactness.** The test covers 144 combinations of tax rate, rounding mode, tax-inclusive or tax-exclusive pricing, and before or after release. In each, refunding units in batches returns exactly the line total and exactly the tax charged.
- **Tithe exactness.** The tithe always equals `floor(a·φ/100)`. This is checked against an independent 128-bit integer square-root reference.
- **Atomicity.** Every money movement is a single instruction to the payment layer. If the payment layer refuses it, nothing changes.

## Honest limits

- This module is the commerce state machine. **It does not move money itself.** Money moves through the operator's settlement hook (the `pay` ledger, `cardnet` cards, or a bank), which must apply each instruction all-or-nothing.
- Proof of delivery for physical goods is only as good as the carrier attestation the operator plugs in.
- Disputes are decided by people. The code does not judge who is right.
- Capacities are fixed. One node's market holds up to 32 shops, 128 listings, 128 orders, 256 reviews, 32 purchase orders and 32 invoices.
- Partial invoice payments are not supported yet.
- Consumer-protection law, marketplace-facilitator tax rules, licensing and sanctions all vary by country. The operator and their counsel are responsible for compliance.
