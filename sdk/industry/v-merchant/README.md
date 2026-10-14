<!-- Copyright (c) 2024-2026 Michael Laurence Curzi and 36N9 Genetics, LLC -->
<!-- SPDX-License-Identifier: Apache-2.0 -->

# V-Merchant: the merchant side of offline payments

V-Merchant is a C11 library for the till: the merchant side of
point-of-sale and agent payments on the libzxv ledger. It covers these
tasks:

- the merchant issues invoices;
- the till checks a customer's signed payment proof offline;
- back online, the till settles the day's proofs in one batch;
- the day closes with an exact report whose digest is signed.

The API is [`include/zvmerch.h`](include/zvmerch.h).

**Out of scope: the customer's wallet and voucher redemption UX.** These
belong to the Vino Embed kit. This kit only verifies what a wallet has
signed. It holds no customer keys and shows no customer screens.

## Lifecycle

| Invoice state | Phase | Call | Money |
|---|---|---|---|
| open | Potential | `zvm_invoice_create(id, ccy, amount, issued_at, expires_at)` | - |
| accepted | Emerging | `zvm_accept` (offline) | - (the proof is queued) |
| settled | Manifest | `zvm_settle` (online) | payer -> merchant (net) and the four fee accounts |
| open again | Potential | `zvm_settle`, when the payer could not pay | - (the proof bounced) |
| void | - | `zvm_invoice_void` while open | - |

The fee is the library's assurance fee. The kit calls
`zxv_fee_assure_carry` (one carry per currency) and `zxv_fee_split` and
computes neither itself.

## The payment proof

The customer's device signs `zvm_pay_bytes`. It is pure, so a wallet
builds the same bytes itself:

```
"zvm/pay/v1" || str merchant || str invoice || str ccy || u64le amount
             || u32le payer || u64le nonce || u64le time
```

An Ed25519 key signs the bytes as they are. An ML-DSA-87 key signs them
under the context `"v-merchant/pay"`. The proof travels by QR, NFC,
Bluetooth or a file in a strict wire form:

```
"ZVMP" || u8 1 || the signed fields || u8 alg || u16le sig length || sig
```

`zvm_proof_decode` refuses (`ZXV_E_FORMAT`) any wrong length, id,
algorithm or signature length. Decoding checks nothing cryptographic.

`zvm_accept` checks the proof offline against its invoice and the till's
list of payer keys. It refuses these:

- a wrong merchant, invoice, currency or amount (`ZVM_E_MISMATCH`);
- a time outside the invoice's validity (`ZVM_E_EXPIRED`);
- a payer or key type that is not on the list (`ZVM_E_PAYER`);
- a signature that does not verify (`ZXV_E_VERIFY`, quarantined);
- a payer nonce that was already used, or an invoice that is already paid
  (`ZIND_E_REPLAY`).

`zvm_payer_add` can check a card number's check digit (`zvm_card_check`,
`ZVM_E_CARD`). It keeps only the last four digits.

## Settlement and duplicates

A proof's identity is SHA3-256 over (payer account, payer nonce). That
identity is also the ledger reference of the payment. A proof that
another till on the same ledger has already settled is therefore refused
by the ledger itself. `zvm_settle` marks it DUPLICATE, voids the invoice
and quarantines the proof.

The ledger checks funds before it checks references. So when a duplicate
payer is also short of funds, the refusal arrives as "funds". The kit
looks the reference up in the journal before it decides between a bounce
and a duplicate. A payer who simply cannot pay bounces, and the invoice
opens again.

## End of day

`zvm_eod_report` gives exact per-currency lines in major units:

```
EOD STALL-42 day 20261012
KES settled 2 gross 1650.5 fee 1.46 net 1649.04 bounced 1 duplicates 0
KES fee reserve-floor 0.75 vbill-pool 0.36 infrastructure 0.21 regenerative 0.14
```

`zvm_eod_close` signs the report's SHA3-256 and the totals into a record,
then starts the day's totals again from zero. It is refused in two cases:

- while proofs are still queued (`ZVM_E_PENDING`);
- when the day is not after the last closed day (`ZXV_E_STATE`).

## Example

`make demo` runs [`examples/till_demo.c`](examples/till_demo.c), a market
stall's day with the till offline:

1. Three customers pay by signing on their phones.
2. The same proof is offered twice and refused.
3. A forged proof is refused.
4. A close before settling is refused as pending.
5. Back online, the batch settles: one payment bounces and its invoice
   reopens.
6. The day closes with the report above.

## Tests

```sh
make -C sdk/libzxv all
make -C sdk/industry/v-merchant test     # 1,066 checks, fuzz smoke, demo, no-float check
make -C sdk/industry/v-merchant SANITIZE=1 OUT=build/san test
make -C sdk/industry/v-merchant fuzz FUZZ_ITERS=20000
```

`test_merchant` covers these areas:

- the wire form: round trips, every truncation, malformed fields;
- card check digits on all three networks;
- offline acceptance with both algorithms: wrong merchant, amount,
  currency, time, payer or key type; forged and flipped signatures;
  replayed nonces; paid and voided invoices;
- batch settlement: settled, bounced and reopened, and a proof settled by
  another till refused as a duplicate, including when the payer is short;
- fees against `zxv_fee_*`, including the carry;
- the exact end-of-day report in three currencies, and day closing;
- the signed record chain.

`fuzz_merchant` runs two tills of one merchant on one ledger against a
model. It uses shared invoice ids, reused nonces, hostile proofs, the same
proof on both tills, drained payers, voids, day closes, and mutated wire
bytes. After each operation it checks these invariants:

- every payer, the merchant and all eight fee accounts hold the model's
  balances, so a (payer, nonce) moves money at most once;
- each till's day totals equal the model's;
- a decoded proof re-encodes to the same bytes.

## Limits

- **No cash in, no cash out, no fiat on-ramp or off-ramp.** Money moves
  only between ledger accounts that already hold it. This kit is not a
  payment processor, an acquirer or a money transmitter.
- **No certification claims.** This kit is not certified under EMV, PCI
  DSS or any card scheme's rules. The card check is a check digit only. It
  proves nothing about a card's validity, and the kit stores no full card
  number.
- **Offline acceptance is a risk the merchant takes.** A valid proof says
  the payer signed, not that the payer can pay. That is only known at
  settlement, when the proof may bounce.
- **Time comes from devices.** The proof time is the payer device's
  clock, and invoice validity is what the caller sets.
- Out of scope: the customer's wallet and voucher redemption UX (the Vino
  Embed kit).
- "vbill-pool" is the label of the library's fee bucket 1
  (`ZXV_FEE_VBILL_DIVIDEND`). It is a fee bucket, not a payment to anyone
  holding V-Bills, and V-Bills are not an investment.
- The till is in memory (up to 8 currencies, 4,096 payers, 65,536
  invoices) and is not thread-safe. Persist `zvm_records` for durability.
