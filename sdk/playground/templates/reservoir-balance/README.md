<!-- Copyright (c) 2024-2026 Michael Laurence Curzi and 36N9 Genetics, LLC -->
<!-- SPDX-License-Identifier: Apache-2.0 -->
# Project a week of reservoir storage

A reservoir starts the week at 420 ML. Each day the river brings water in,
an environmental release goes out, and the town draws its demand.
`reservoir.tb` (Trebago) gives the storage at the end of every day as an
exact running sum (`scan`), with no drift from rounding:

| Output | Meaning |
|---|---|
| `storage` | ML in the reservoir at the end of each day |
| `headroom` | ML of room left before it spills (`capacity - storage`) |
| `short` | demand that could not be met that day |
| `shortfall` | the week's total unmet demand |

The step is **advisory**: the result is a projection for a person to
review. The Playground does not open a valve or change a release.

## Run it

```sh
zxv new reservoir-balance
cd reservoir-balance
zxv run
```

As shipped, storage falls from 385 ML to 118 ML but never goes below
empty or above full: the verdict is TRUE, marked as a proposal that a
person must approve.

## Things to try

- **A hold.** Raise Thursday to Sunday's `demand` to `110`. Storage goes
  below zero on Saturday and Sunday. A negative storage is a computed
  shortage, not an arithmetic error, so the step is **held** (BOTH) and
  every short day is named for a person to decide: restrictions, a
  smaller release, or water from elsewhere. A spill (`headroom` below
  zero, from a large `inflow`) is held the same way.
- **A refusal.** Write an inflow as `2.25e1`. Exponents name binary
  approximations, so the value is refused; so is a mistyped program,
  with its line and column.
- Leave out `demand` and the step is NEITHER: it waits for the data.

## Limits

- A mass balance only: no evaporation, seepage or forecast uncertainty
  unless you put them in the inputs. Storage below zero is reported as
  such; the reservoir does not physically go negative, the plan fails.
- Designed to support a planner's review, not to operate a dam. Any real
  operating decision stays with qualified people and their procedures.
