<!-- Copyright (c) 2024-2026 Michael Laurence Curzi and 36N9 Genetics, LLC -->
<!-- SPDX-License-Identifier: Apache-2.0 -->

# V-Shelf: an exact inventory ledger for retail

V-Shelf is a C11 library that keeps stock per SKU and per location. It
uses whole units for counted items and exact fractions for measured ones,
so 3/4 kg stays 3/4 kg. Every movement is a signed, chained journal
record. The API is [`include/zvshelf.h`](include/zvshelf.h).

## Movements

| Call | Effect | Refused when |
|---|---|---|
| `zvs_receive(ref, sku, loc, qty)` | stock in | the reference was used before (`ZIND_E_REPLAY`) |
| `zvs_sell(ref, sku, loc, qty)` | stock out | stock would go negative (`ZIND_E_NEGATIVE`) |
| `zvs_transfer(ref, sku, from, to, qty)` | moves stock between locations | the source would go negative |
| `zvs_shrink(ref, sku, loc, qty, reason)` | theft, damage, expiry or other | stock would go negative |

All four calls are also refused in these cases:

- the position is held for a variance (`ZIND_E_HELD`);
- a fractional quantity is given for an item counted in whole units
  (`ZVS_E_UNIT`);
- the exact result would not fit in `int64/int64` (`ZXV_E_RANGE`).

A refused call changes nothing and records nothing. Every reference is
used at most once per store, so a till that re-sends a sale is told
`ZIND_E_REPLAY` and the stock is not touched twice.

## Counts and variances

`zvs_count(ref, sku, loc, counted, &variance)` compares a cycle count with
the book.

- **Equal:** the position is confirmed (TRUE), and a signed record says so.
- **Different:** `ZVS_E_VARIANCE`. The variance is `counted - book`,
  exactly. It is written to the quarantine log, and the position is held
  as **BOTH**: no movement in or out until a registered approver decides.
  The approver signs `zvs_variance_message` under the context
  `"v-shelf/variance"`. That message binds the SKU, the location, the hash
  of the variance record and the decision:
  - `ZVS_ACCEPT_COUNT` posts an exact adjustment, and the book becomes the count;
  - `ZVS_KEEP_BOOK` releases the hold so the shelf can be recounted.

  `zvs_variance_resolve` applies the decision, and the store's capability
  needs `ATTEST`. A forged decision is refused and logged.

## Checking the books

- `zvs_flows` gives lifetime received, sold, shrunk and adjusted per SKU,
  and `received - sold - shrunk + adjusted == total` always holds.
- `zvs_reconcile` verifies the whole signed journal with the store's key
  and replays it from empty. Every balance and every hold must equal the
  live state exactly, and no balance may go negative along the way.

## Example

```c
zvs_store_create(cap, till_key, qlog, &s);
zvs_location_add(s, "floor", &floor);
zvs_item_add(s, "APPLES", ZVS_UNIT_MEASURE, "kg");
zind_qty_parse("12.5", &q);
zvs_receive(s, "PO-1002", "APPLES", floor, q);
zvs_sell(s, "POS-2", "APPLES", floor, (zind_qty){3, 4});   /* 3/4 kg */
zvs_on_hand(s, "APPLES", floor, &q);                       /* 47/4 kg exactly */
zvs_sell(s, "POS-2", "APPLES", floor, (zind_qty){3, 4});   /* ZIND_E_REPLAY */
```

`make demo` runs [`examples/pos_demo.c`](examples/pos_demo.c). It plays
a store day:

1. a delivery, a transfer, and sales by the unit and by weight;
2. a re-sent sale, a sale below zero and half a can, each refused;
3. an evening count that is off by one, then held;
4. the manager's signed decision, and a journal replay.

## Tests

```sh
make -C sdk/libzxv all
make -C sdk/industry/v-shelf test     # 831 checks, fuzz smoke, demo, no-float check
make -C sdk/industry/v-shelf SANITIZE=1 OUT=build/san test
make -C sdk/industry/v-shelf fuzz FUZZ_ITERS=20000
```

`test_shelf` covers these areas:

- setup refusals;
- whole and fractional units;
- negative stock and duplicate references;
- overflow, including lifetime flow totals at `INT64_MAX`;
- transfers and shrinkage;
- variances held for approvers, with decisions that are forged, for the
  wrong decision, or made without `ATTEST`;
- conservation of flows and the journal replay;
- tampered journals.

`fuzz_shelf` runs random movements, counts and decisions, including
fractions with prime denominators that overflow quickly, against an
independent model. It reconciles the journal every 200 operations.

## Limits

- **The kit records what it is told. It cannot see shelves.** A count is
  only as good as the person counting, and a sale that never reaches the
  till is invisible. Shrinkage shows up only as a variance at the next
  count.
- Quantities are `int64/int64` rationals. A movement whose exact result
  does not fit is refused rather than rounded. Because lifetime flow
  totals are exact too, a SKU that receives near `INT64_MAX` in total
  stops accepting receipts (`ZXV_E_RANGE`).
- No costing, valuation, pricing, tax, lots or expiry dates. It tracks
  quantities only. Integrations with point-of-sale systems are public-API
  integrations, not partnerships.
- Up to 16 locations, 4,096 SKUs and 262,144 references per store, all in
  memory. The store is not thread-safe. Persist `zvs_records` for
  durability.
