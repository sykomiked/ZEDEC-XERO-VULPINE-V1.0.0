<!-- Copyright (c) 2024-2026 Michael Laurence Curzi and 36N9 Genetics, LLC -->
<!-- SPDX-License-Identifier: Apache-2.0 -->

# Check stock levels against a cycle count

Stock per site and item, kept as an M (MUMPS) global and described by a
MYRA schema (`stock.myra`): each item's description, units on hand, unit
and reorder point, and each lot with its expiry date.

Two sources describe the same shelves: the stock system and a physical
cycle count. Both are decoded exactly, compared node by node and totalled
per site, in whole units. Nothing is rounded.

| What happens | Verdict |
|---|---|
| system and count agree | TRUE, with `total-on-hand` and `on-hand-by-site` |
| they disagree on an item or lot | BOTH: **held for a stock controller**; the total leaves it out and says so |
| a quantity is negative, or a line breaks the schema | FALSE: refused with the file, line and column |

## Run it

```sh
zxv new warehouse-stock
cd warehouse-stock
zxv run
```

As shipped both sources agree: 519 units on hand, NORTH 184 and SOUTH
335.

To see a hold, change the cooking oil's `64` to `61` in `cycle-count.txt`
(the item line and its lot). To see a refusal, set a quantity in
`stock-system.txt` to `-5`, or write an item number as text
(`^STK("NORTH","rice")`).

## Options on the step

`sources=` the files to compare, `nonneg=` fields that may not go below
zero, `sum=` and `by=` the total and its grouping, `reviewer=` who a hold
is for. Use `data=FILE` instead of `sources=` to decode one file.

## Limits

A check of one snapshot, not a stock system: no locations within a site,
no movements, no reorders placed. The reorder point is shown, not acted
on. Sample data.
