<!-- Copyright (c) 2024-2026 Michael Laurence Curzi and 36N9 Genetics, LLC -->
<!-- SPDX-License-Identifier: Apache-2.0 -->
# Share a relief fund exactly

A fund of $2,500,000.00 goes to five districts. Each district first gets a
floor of $50,000.00, and what is left is shared by population. Shares like
that rarely come out in whole cents, and rounding each one on its own can
leave the books a few cents over or under the fund.

`relief.tb` (Trebago) does it exactly:

1. `share`: each district's exact share as a fraction of a cent, for
   example `31117660000000/289067`. Nothing is rounded.
2. `cents`: whole cents by the largest-remainder method. Every district
   gets the whole cents of its share; the few cents left over go one each
   to the largest leftover fractions (a tie goes to the district listed
   first).
3. `total`: the sum of `cents`. It equals the fund to the cent, every time.

## Run it

```sh
zxv new relief-allocation
cd relief-allocation
zxv run
```

The verdict is TRUE and `total` is `250000000`, the fund in cents.

## Things to try

- **A refusal.** Set every `pop` to `0`. There is nobody to share by, and
  the step is refused at the division in line 16, with the column.
  Writing a population as `1.5` is refused too: populations, the fund and
  the floor are whole counts (the `counts=` option).
- **A hold.** Raise `floor` to `60000000`. Five floors of $600,000 need
  more than the fund. That is not an arithmetic error but a shortfall, so
  the step is **held** (BOTH) with `rest` shown below zero: a person
  decides whether to lower the floor or add to the fund. `total` then
  shows what the floors alone would cost.
- Leave out `pop` and the step is NEITHER: it waits for the data.

## Limits

- Amounts are int64 cents and each share must be below 10^9 cents
  ($10 million). A larger share is refused, never rounded. Split the
  fund, or change the whole-cents lines in `relief.tb`.
- Largest remainder is one fair rounding rule, not the only one. If your
  rules say otherwise, the exact `share` is there to apply them to.
- It computes an allocation; it does not pay anyone. Eligibility,
  approval and payment live elsewhere.
