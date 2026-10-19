<!-- Copyright (c) 2024-2026 Michael Laurence Curzi and 36N9 Genetics, LLC -->
<!-- SPDX-License-Identifier: Apache-2.0 -->

# Naval ports: pegs held together by a Sicilian account

**Advisory design, pending counsel.** Code: [`kernel/src/navport`](../kernel/src/navport/navport.h).
Tests: `make -C kernel test-navport` (unit tests, random sessions, and the
stress simulation below). Integer only. V-Bills and ports belong to the
INSTITUTION profile.

## The idea, in the owner's words and in the code

| Owner's word | What it is in `navport` |
|---|---|
| floating vouchers as glue | Each member issues vouchers in its own unit (a CBDC, fiat, commodity or compute voucher). Every unit has one peg to the V-Bill, so every cross rate goes through the V-Bill. |
| socket | Two or more members accept each other's vouchers all cycle long. Flows go both ways and mostly cancel. At each close only the net is settled, in V-Bills, and the cycle starts again. That is the perpetual oscillation. |
| naval port | The set of members, their pegs and the shared account. The port holds a peg between K units without K(K-1)/2 bilateral pegs. |
| nostro / vostro | `hold[i][j]` is what member i holds of j's vouchers: i's nostro at j and j's vostro for i. Both sides report their books at every close. |
| Sicilian account | One pooled V-Bill account per port. Each member owns a share S_i. It is the only thing that settles nets, so it **is** the reserve. |
| decentralised | No operator. Closes are signed by a quorum of members (default two thirds plus one). Peg resets need the issuing member's signature and a quorum. Admission needs a quorum. A member can leave by its own signature. |
| paraconsistent | Two four-valued checks. Peg health comes from price evidence (TRUE, FALSE, BOTH, NEITHER). Each nostro/vostro pair is reconciled against two witnesses. A conflict holds that one item and never blocks or corrupts the rest. |

```
  member A (unit TCBDC)          member B (unit TFIAT)          member C (unit TGOLD)
  nostro at B, C                 nostro at A, C                 nostro at A, B
  vostro for B, C                vostro for A, C                vostro for A, B
          \                              |                              /
           \______ net at close, in V-Bills (sum is exactly zero) _____/
                                         |
                    Sicilian account: S_A + S_B + S_C V-Bills
```

## What a peg really needs

A peg holds exactly as long as the issuer of the weak unit can pay what
holders bring back at the peg. In a port that payment is V-Bills from the
issuer's share, so:

1. **Gross flows need no reserve. Net flows do.** Balanced traffic cancels at
   close. That cancellation is the real "compression": in the calm run below,
   15x more value moved than was settled. It is netting, measured. It never
   multiplies value.
2. **The share is the hard limit.** A conversion out of unit f is refused if
   f's net debit would exceed S_f (`NAVP_E_LIMIT`). So every close can be
   paid, and the port never lends to anyone.
3. **A persistent one-way flow drains the weak member's share.** When the
   share runs out, the port refuses further outflow. That refusal **is** the
   de-peg. Holders of the weak unit can still use it inside its own system,
   but they cannot leave through the port at the old rate.
4. **No contagion and no bail-out.** Other members' shares are never spent
   to defend a weak member. They end up holding more V-Bills, received as
   settlement. A peg reset (devaluation) needs the weak member's own
   signature plus a quorum, and applies only after open positions settle at
   the old peg.
5. **Evidence lags the drain.** Health is judged once per cycle, so one bad
   cycle can empty what was left. The simulation shows this (S2). Shorter
   cycles, or a coverage-based stale cap, shrink that window.

On peg health, SUSPEND mode (the default) stops outflow once evidence says
the unit is off peg (FALSE). That keeps 29% of the reserve in S2. DEFEND
mode keeps converting at the peg until the share is gone (S3 ends at 0.28%).
Defending a peg only buys time. It never fixes the cause.

## What backs a V-Bill (checked against the code)

The premise that V-Bills are "backed by every commodity ever traded" is not
what the code does. A V-Bill is backed by what is actually registered:

- the Vino reserve, which includes auction proceeds (`kernel/src/vauction`
  A3 floor: R * floor_den >= floor_num * backing * N);
- custodian-signed warehouse receipts with a per-custodian haircut, valued
  through signed median price feeds (`vauction`: up to 8 custodians, 32
  receipts, 4 feeds);
- attested capacity minted under a jurisdiction policy that starts empty
  (`kernel/src/mint`: e-WHR, compute, kWh, with two haircuts).

Diversified backing does lower the risk that one asset's crash breaks the
floor. It does not add liquidity on its own: a receipt for grain in a
warehouse cannot pay a net debit until someone buys it. A port settles in
V-Bills, so what matters at close is how many V-Bills each member has
locked, not the breadth of the basket behind them.

## Pipe yield (off by default)

The owner's follow-up: more V-Bills locked in the loop, at higher
compression, should earn a larger "pipe yield" from the V-Bill dividend, and
the 0.08889% assurance fee applies to every pipe transaction.

What is built:

- **Fee:** `pay_assure_fee` (0.08889%, floor) is taken first on every
  conversion, and recorded per member in its own unit and in V-Bill value.
  It flows to the existing 50/25/15/10 split outside this module.
- **Pipe yield:** `navp_yield` splits an amount the caller has already taken
  from the realised 25% V-Bill dividend pool. Each member's weight is
  locked V-Bills plus an optional weight on fees it actually paid. The
  split is exact (largest remainders). It never creates V-Bills, and there
  is no rate.
- **Compression earns nothing by itself.** Two members can trade back and
  forth to inflate gross volume and so the compression ratio. In S6 that
  wash trading lifted compression from 15.2x to 17.2x and cost the two
  traders about 5,200 V in fees. It earned them about 900 V of extra yield.
  The other member gained about 400 V without doing anything. Fees are the
  only brake on this kind of gaming, so any reward based on volume must stay
  below the fee.
- **More V-Bills in the pipe raise capacity, not gross volume.** In S4 the
  same run starts against 10x the reserves. The first refusal comes at cycle
  24 instead of 11, and the share never runs dry. Gross volume is limited
  only by netting.
- **Macro and micro.** A port share is an institution-level position. The
  users whose V-Bills a member locks see their part in their own Sicily
  Account (`kernel/src/sicily`, S1 view) on the member's ledger. This module
  keeps no per-user records.

**Securities risk.** Paying holders a return for locking value is the same
risk raised for GPU farming: it looks like an investment contract. The
switch (`yield_enabled`) is off by default and stays off until counsel
says otherwise. Nothing here promises a return.

## Legal limits (pending counsel)

- Test units only. There is no protocol fiat on-ramp or off-ramp. Each
  member's unit is a claim its own custodian attests.
- Issuing redeemable vouchers, running a peg between currencies, and holding
  V-Bills for users are regulated activities (money transmission, e-money,
  securities, and for central banks, monetary law). Formats are not
  licences.
- No interest and no lending. A member that cannot pay its net debit is
  stopped, not financed.

## Open decisions for the owner

1. Default cycle length and stale cap. A shorter cycle shrinks the window in
   S2 where one cycle drains the rest of a reserve.
2. SUSPEND or DEFEND as the default on off-peg evidence. SUSPEND is built in.
3. Whether a silent member's positions may settle on one witness. That is
   built (S5: the port keeps running), on the grounds that the positions came
   from the silent member's own signed conversions.
4. Pipe yield weights, and whether it is ever switched on (counsel).
5. Price evidence: in production these are signed oracle quotes. Here the
   caller counts them.

## Stress simulation

Generated by `kernel/src/navport/navport_sim.c` (deterministic; re-run with
`make -C kernel test-navport`, which writes `$ZXV_TMP/navport_sim.md`).
Synthetic flows and a rule-of-thumb oracle. It shows mechanics. It does not
predict any real unit.

### Naval port stress simulation (advisory, pending counsel)

Three members, pegs TCBDC 150/1, TFIAT 1/1, TGOLD 7/3 (minor units per V-Bill).
300 random conversions per cycle of 1..5000 V each, quorum 2 of 3, fee 0.08889%.
Oracle rule: on-peg while a share is >= 25% of its start, off-peg below 35%
(so 25-35% is BOTH). Pipe yield pool = 25% of the cycle's realised fees.

#### S1 Calm: balanced flows

| | |
|---|---|
| cycles | 60 |
| starting share per member | 1000000 V |
| mode on FALSE | SUSPEND |
| conversions done / refused | 18000 / 0 |
| refused: LIMIT / DEPEG / HELD / STALE | 0 / 0 / 0 / 0 |
| first refusal at cycle | -1 |
| cycles with a BOTH / FALSE member | 0 / 0 |
| closes held / closes with carried pairs | 0 / 0 |
| gross settled value / net moved | 44787212 / 2948401 V |
| compression (gross / net) | 15.19x |
| TCBDC share start -> end (lowest) | 1000000 -> 947608 (86.60%) |
| TFIAT share start -> end (lowest) | 1000000 -> 962995 (68.02%) |
| TGOLD share start -> end (lowest) | 1000000 -> 1089397 (87.22%) |
| TCBDC fees paid / pipe yield received | 13402 / 3153 V |
| TFIAT fees paid / pipe yield received | 10265 / 2713 V |
| TGOLD fees paid / pipe yield received | 12020 / 3034 V |

#### S2 Run on TFIAT, SUSPEND

| | |
|---|---|
| cycles | 60 |
| starting share per member | 1000000 V |
| mode on FALSE | SUSPEND |
| conversions done / refused | 9278 / 8722 |
| refused: LIMIT / DEPEG / HELD / STALE | 197 / 6565 / 1960 / 0 |
| first refusal at cycle | 11 |
| cycles with a BOTH / FALSE member | 9 / 30 |
| closes held / closes with carried pairs | 0 / 0 |
| gross settled value / net moved | 22961733 / 8731818 V |
| compression (gross / net) | 2.62x |
| TCBDC share start -> end (lowest) | 1000000 -> 1085904 (86.60%) |
| TFIAT share start -> end (lowest) | 1000000 -> 290900 (0.00%) |
| TGOLD share start -> end (lowest) | 1000000 -> 1623196 (99.54%) |
| TCBDC fees paid / pipe yield received | 6738 / 1689 V |
| TFIAT fees paid / pipe yield received | 5565 / 641 V |
| TGOLD fees paid / pipe yield received | 5854 / 2187 V |

#### S3 Run on TFIAT, DEFEND

| | |
|---|---|
| cycles | 60 |
| starting share per member | 1000000 V |
| mode on FALSE | DEFEND |
| conversions done / refused | 10613 / 7387 |
| refused: LIMIT / DEPEG / HELD / STALE | 7387 / 0 / 0 / 0 |
| first refusal at cycle | 11 |
| cycles with a BOTH / FALSE member | 0 / 48 |
| closes held / closes with carried pairs | 0 / 0 |
| gross settled value / net moved | 23249802 / 2212583 V |
| compression (gross / net) | 10.50x |
| TCBDC share start -> end (lowest) | 1000000 -> 1304941 (86.60%) |
| TFIAT share start -> end (lowest) | 1000000 -> 2831 (0.00%) |
| TGOLD share start -> end (lowest) | 1000000 -> 1692228 (99.54%) |
| TCBDC fees paid / pipe yield received | 6738 / 1775 V |
| TFIAT fees paid / pipe yield received | 5308 / 505 V |
| TGOLD fees paid / pipe yield received | 5854 / 2175 V |

#### S4 Same run, 10x the reserves

| | |
|---|---|
| cycles | 60 |
| starting share per member | 10000000 V |
| mode on FALSE | SUSPEND |
| conversions done / refused | 11650 / 6350 |
| refused: LIMIT / DEPEG / HELD / STALE | 0 / 0 / 6350 / 0 |
| first refusal at cycle | 24 |
| cycles with a BOTH / FALSE member | 29 / 0 |
| closes held / closes with carried pairs | 0 / 0 |
| gross settled value / net moved | 28994518 / 12883355 V |
| compression (gross / net) | 2.25x |
| TCBDC share start -> end (lowest) | 10000000 -> 13080362 (98.66%) |
| TFIAT share start -> end (lowest) | 10000000 -> 3258115 (30.60%) |
| TGOLD share start -> end (lowest) | 10000000 -> 13661523 (99.95%) |
| TCBDC fees paid / pipe yield received | 6738 / 2195 V |
| TFIAT fees paid / pipe yield received | 9837 / 1142 V |
| TGOLD fees paid / pipe yield received | 5854 / 2249 V |

#### S5 TGOLD silent for cycles 20-29

| | |
|---|---|
| cycles | 60 |
| starting share per member | 1000000 V |
| mode on FALSE | SUSPEND |
| conversions done / refused | 18000 / 0 |
| refused: LIMIT / DEPEG / HELD / STALE | 0 / 0 / 0 / 0 |
| first refusal at cycle | -1 |
| cycles with a BOTH / FALSE member | 0 / 0 |
| closes held / closes with carried pairs | 0 / 0 |
| gross settled value / net moved | 44787212 / 2948401 V |
| compression (gross / net) | 15.19x |
| TCBDC share start -> end (lowest) | 1000000 -> 947608 (86.60%) |
| TFIAT share start -> end (lowest) | 1000000 -> 962995 (68.02%) |
| TGOLD share start -> end (lowest) | 1000000 -> 1089397 (87.22%) |
| TCBDC fees paid / pipe yield received | 13402 / 3153 V |
| TFIAT fees paid / pipe yield received | 10265 / 2713 V |
| TGOLD fees paid / pipe yield received | 12020 / 3034 V |

#### S6 Wash trading A<->C, yield by locked V-Bills only

| | |
|---|---|
| cycles | 60 |
| starting share per member | 1000000 V |
| mode on FALSE | SUSPEND |
| conversions done / refused | 18000 / 0 |
| refused: LIMIT / DEPEG / HELD / STALE | 0 / 0 / 0 / 0 |
| first refusal at cycle | -1 |
| cycles with a BOTH / FALSE member | 0 / 0 |
| closes held / closes with carried pairs | 0 / 0 |
| gross settled value / net moved | 50790212 / 2948251 V |
| compression (gross / net) | 17.22x |
| TCBDC share start -> end (lowest) | 1000000 -> 956608 (86.68%) |
| TFIAT share start -> end (lowest) | 1000000 -> 962995 (68.02%) |
| TGOLD share start -> end (lowest) | 1000000 -> 1080397 (86.89%) |
| TCBDC fees paid / pipe yield received | 16060 / 3627 V |
| TFIAT fees paid / pipe yield received | 10265 / 3113 V |
| TGOLD fees paid / pipe yield received | 14592 / 3470 V |

#### S7 Wash trading A<->C, fee-weighted yield (10 units per fee V)

| | |
|---|---|
| cycles | 60 |
| starting share per member | 1000000 V |
| mode on FALSE | SUSPEND |
| conversions done / refused | 18000 / 0 |
| refused: LIMIT / DEPEG / HELD / STALE | 0 / 0 / 0 / 0 |
| first refusal at cycle | -1 |
| cycles with a BOTH / FALSE member | 0 / 0 |
| closes held / closes with carried pairs | 0 / 0 |
| gross settled value / net moved | 50790212 / 2948251 V |
| compression (gross / net) | 17.22x |
| TCBDC share start -> end (lowest) | 1000000 -> 956608 (86.68%) |
| TFIAT share start -> end (lowest) | 1000000 -> 962995 (68.02%) |
| TGOLD share start -> end (lowest) | 1000000 -> 1080397 (86.89%) |
| TCBDC fees paid / pipe yield received | 16060 / 3628 V |
| TFIAT fees paid / pipe yield received | 10265 / 3113 V |
| TGOLD fees paid / pipe yield received | 14592 / 3469 V |

invariant violations: 0
