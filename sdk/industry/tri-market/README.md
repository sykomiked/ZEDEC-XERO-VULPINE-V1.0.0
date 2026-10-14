<!-- Copyright (c) 2024-2026 Michael Laurence Curzi and 36N9 Genetics, LLC -->
<!-- SPDX-License-Identifier: Apache-2.0 -->

# Tri-Market: e-commerce escrow with signed confirmations and rulings

Tri-Market is a C11 library that holds a shop order's payment in escrow on
the libzxv ledger. The money is released to the seller only when the
buyer confirms delivery by signing on their own device, with Ed25519 or
ML-DSA-87. A dispute freezes the order as BOTH until a registered
arbiter signs an exact split. An order nobody confirms is refunded after
its deadline. The API is [`include/ztmarket.h`](include/ztmarket.h).

The fee is the library's assurance fee. The kit calls
`zxv_fee_assure_carry` (one carry per seller) and `zxv_fee_split` and
computes neither itself.

## Lifecycle

| State | Phase | Call | Money |
|---|---|---|---|
| escrow | Potential | `ztm_order_create` / `ztm_order_create_quoted` | buyer -> escrow |
| shipped | Emerging | `ztm_ship(order, shipment_ref, time)` | in escrow |
| released | Manifest | `ztm_confirm` with the buyer's signature | escrow -> seller (net) and the four fee accounts |
| disputed | BOTH, frozen | `ztm_dispute(order, party, reason)` | stays in escrow |
| ruled | Manifest | `ztm_rule` with an arbiter's signature | escrow -> seller part (less fee) and buyer part |
| refunded | Dissolving | `ztm_expire(order, now)` after the deadline | escrow -> buyer, in full |

Money leaves escrow once. The kit refuses these:

- a second confirmation, or a replayed one (`ZIND_E_REPLAY`);
- a confirmation made for another order;
- a confirmation dated after the deadline (`ZTM_E_EXPIRED`);
- a ruling on an order that is not disputed;
- a refund before the deadline (`ZTM_E_EARLY`) or on a disputed order (`ZIND_E_HELD`);
- a refund after release.

## The signed messages

The buyer signs `ztm_confirm_bytes`. It is pure, so a buyer app holding
the order hash builds the same bytes itself:

```
"ztm/confirm/v1" || u8 len || order id || order hash[32] || u64le price || u64le time
```

The order hash is the hash of the order's signed CREATE record
(`ztm_order_hash`), so a confirmation cannot be moved to another order
or another desk. An order id is never reused on a desk. An Ed25519 key signs the
bytes as they are. An ML-DSA-87 key signs them under the context
`"tri-market/confirm"`.

An arbiter signs `ztm_ruling_message` under the context
`"tri-market/ruling"`:

```
"ztm/ruling/v1" || u8 len || order id || dispute record hash[32] || u64le to_seller || u64le to_buyer
```

The parts must add up to the price exactly (`ZTM_E_SPLIT`). The fee is
charged on the seller part only. The kit refuses these and logs the
forgeries to the quarantine log:

- a forged ruling (`ZXV_E_VERIFY`);
- a ruling from a key that is not an arbiter (`ZTM_E_ARBITER`);
- a ruling made without `ATTEST` on the desk cap.

## Cross-currency quotes

With a `zxv_vhub` attached (`ztm_hub_attach`), an order can be listed in
another hub currency. `ztm_order_create_quoted` asks the hub
(`zxv_vhub_quote`) what the list price is worth in the desk currency
through the V-Bill. The quote must be exact (`ZIND_E_INEXACT` otherwise).
The kit pins it in the signed order record: list slot, list price,
V-Bills, and both peg epochs. The buyer pays, and the seller is paid, in
the desk currency. The kit converts nothing itself.

## Example

```c
ztm_desk_create(cap, "EUR", desk_key, qlog, &d);
ztm_order_create(d, "SHOP-1001", buyer, seller, 189900, ZTM_ALG_ED25519,
                 buyer_pk, 32, now + 14 * 86400, now, &o);
ztm_ship(d, o, "TRACK-0001", now + 3600);
/* on the buyer's phone: */
ztm_confirm_bytes("SHOP-1001", order_hash, 189900, now + 86400, msg, sizeof msg, &len);
zxv_ed25519_sign(phone_cap, buyer_sk, msg, len, sig);
/* at the desk: */
ztm_confirm(d, o, now + 86400, sig, 64);   /* ZXV_OK: seller paid, fee split */
ztm_confirm(d, o, now + 86400, sig, 64);   /* ZIND_E_REPLAY */
```

`make demo` runs [`examples/shop_demo.c`](examples/shop_demo.c). It shows
four orders:

- one confirmed, with the fee split and the replay refused;
- one disputed and ruled 70/30, after an uneven ruling was refused;
- one refunded after its deadline, after an early refund was refused;
- one listed in another hub currency, where an inexact price is refused
  and an exact one is escrowed.

## Tests

```sh
make -C sdk/libzxv all
make -C sdk/industry/tri-market test     # 1,593 checks, fuzz smoke, demo, no-float check
make -C sdk/industry/tri-market SANITIZE=1 OUT=build/san test
make -C sdk/industry/tri-market fuzz FUZZ_ITERS=20000
```

`test_market` covers these areas:

- creation refusals and both signature algorithms;
- fees checked against `zxv_fee_assure_carry` and `zxv_fee_split`,
  including the per-seller carry;
- confirmations that are replayed, doubled, for another order, late or forged;
- disputes by either party;
- rulings that are uneven, forged, replayed, from a non-arbiter or
  without `ATTEST`;
- timeouts at and after the deadline;
- quoted orders: exact, inexact, without a hub, and unpegged;
- out-of-range prices and ruling parts (negative, zero, `INT64_MAX`);
- conservation of money and the signed record chain.

`fuzz_market` runs random orders with hostile confirmations and rulings
against a model. After each operation it checks these invariants:

- escrow on the ledger equals what the desk owes;
- buyer, seller and fee balances match the model;
- money is conserved;
- an order is disputed exactly when its tag is BOTH.

## Limits

- **Not a payment processor or money transmitter.** Money moves only
  between accounts on the libzxv ledger. No fiat enters or leaves through
  this kit: there is no on-ramp and no off-ramp.
- **Time comes from callers and devices.** Deadlines and confirmation
  times are what the caller passes and what the buyer's device signed.
  Use a trusted time source if that matters.
- **A signature proves a key, not a delivery.** A buyer who confirms
  without receiving the goods has still confirmed. Disputes are decided
  by people.
- The hub's minor units for the desk currency's slot cannot be read
  through ABI 1.5. The operator states them in `ztm_hub_attach`.
- One currency per desk, in minor units (`int64`). Partial shipments and
  partial refunds outside a ruling are not modelled.
- The library names fee bucket 1 `ZXV_FEE_VBILL_DIVIDEND`. This kit labels
  it "V-Bill pool". It is a fee bucket, not a payment to anyone holding
  V-Bills, and V-Bills are not an investment.
- The desk is in memory (up to 65,536 orders) and is not thread-safe.
  Persist `ztm_records` for durability.
