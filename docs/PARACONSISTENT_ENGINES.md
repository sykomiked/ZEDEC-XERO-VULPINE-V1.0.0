<!-- Copyright (c) 2024-2026 Michael Laurence Curzi and 36N9 Genetics, LLC -->
<!-- SPDX-License-Identifier: Apache-2.0 -->

# Paraconsistent decision engines

Four engines in one module, `kernel/src/pce/`: probability, tactics,
strategy and financial analysis. They share one idea. When the evidence
disagrees, an ordinary system picks one side, averages, or throws the
disagreement away. These engines keep it. Every answer is one of the four
truth values (TRUE, FALSE, BOTH, NEITHER) with the 64-state hexagram tag on
top (base, trend, phase), and what happens next is the existing one-table
rule `lpx_action` (`kernel/src/lpres/lpres_hex.h`):

| Answer | What happens |
|---|---|
| TRUE | The finding stands. Advisory work still goes to a person (below). |
| FALSE | Refused, nothing to undo. |
| BOTH | The disagreement is kept with both sides and its size. Converging: wait. Steady or diverging: a person decides. |
| NEITHER | Not enough to say. Nothing acts on an unknown. |
| forbidden | A broken invariant (a balance sheet where assets are not liabilities plus equity): refused and logged. |

Every number is an exact integer or an exact rational (`rational.h`), never
a float, and an arithmetic overflow is refused, never wrapped or rounded.
Every run is deterministic and can append one record to the paradox engine's
hash-chained log (`pdx_log_t`) holding a SHA-256 of its inputs and of its
outputs, so a rerun can be checked digest for digest.

## What each engine does that a normal OS can't

### Probability (`pce_prob.h`)

- **Four-valued probability.** A belief about a claim is a mass over TRUE,
  FALSE, BOTH and NEITHER in exact rationals summing to exactly 1. Mass on
  BOTH is measured conflict; mass on NEITHER is measured ignorance. One
  number cannot tell "half, because the sources conflict" from "half,
  because nobody knows". This can.
- **Combining sources without erasing conflict.** Two independent sources
  are joined on the knowledge order, which is the bitwise OR of lpres's
  codes, so TRUE from one and FALSE from the other lands on BOTH. Dempster's
  rule, which most evidence systems use, divides the conflict away. The
  tests run the classic case both ways: two sources 99% sure of opposite
  answers. Dempster reports a calm 50/50. The join reports 98.02% outright
  contradiction, and the verdict is BOTH.
- **Truth operations on uncertain claims** (AND, OR, NOT of independent
  masses) use lpres's own tables, so there is one source of truth for the
  logic. The tests confirm De Morgan holds except for exactly the
  BOTH-with-NEITHER mass, the documented consequence of the owner's
  2026-10-12 choice of min/max tables (a contradiction plus a gap never
  grants authority).
- **Exact Bayes, rates, expectations.** Evidence that every hypothesis calls
  impossible is a conflict (BOTH: the data contradicts the model), never a
  division by zero. Two models of the same thing are compared by exact
  total variation: within tolerance TRUE, beyond it BOTH, both kept.

### Tactics (`pce_tac.h`)

- Alpha-beta game-tree search over any two-sided game described by three
  callbacks (moves, apply, score): games, routing or scheduling against bad
  luck, a business move against a competitor's reply.
- **Several evaluators, no silent winner.** The search runs once per
  evaluator and once with the robust leaf (the worst any evaluator gives the
  side choosing). All evaluators pick the same move: TRUE. They differ:
  BOTH, each evaluator's move kept and the robust move offered as the safe
  choice. Budget spent or no move: NEITHER, nothing offered.
- **Trend with depth.** The run is repeated one ply shallower; agreement
  growing with depth reads as converging (wait), shrinking as diverging
  (a person decides).
- The tests check alpha-beta against a plain minimax on 400 random trees for
  every evaluator and the robust leaf, move for move.

### Strategy (`pce_strat.h`)

- Options against scenarios with integer payoffs, where each scenario's
  likelihood is an exact interval (what the probability engine's readings
  give), so ignorance is carried, not guessed.
- **Three rules vote**: the best lower expected payoff over every
  distribution the evidence allows, the best worst case, and the least
  regret. They agree: TRUE. They split: BOTH, with each rule's pick and
  every option's bounds, worst case and regret, so a person sees the
  trade-off instead of one rule's answer presented as the only one.
  Interval dominance reports which options are still in contention.
- **Negotiation range.** Several estimates of each side's walk-away point: a
  deal range under every estimate with one price range satisfying all of
  them is TRUE (with that range), under none FALSE, anything between BOTH.
- The tests check the greedy lower and upper expectations against a brute
  force over the corners of the evidence on 500 random cases.

### Financial analysis (`pce_fin.h`): advisory analytics only

- **Reconciling sources.** The same figure from several statements: equal is
  TRUE; different is BOTH and kept as an exact range. A GAAP against IFRS
  difference is an allowed disagreement (settled by a reconciliation rule,
  so it waits); a difference under one standard goes to a person.
- **The accounting identity is a wall.** A statement where assets are not
  liabilities plus equity is quarantined from every figure and logged on its
  own with a forbidden tag.
- **Ratios as exact ranges** (current ratio, debt to equity, net margin,
  interest cover). A denominator range that touches zero is NEITHER, never
  infinity.
- **Present value under disputed rates.** Exact NPV at each analyst's rate:
  positive at every rate TRUE, negative at every rate FALSE, otherwise BOTH:
  whose rate decides. Too large to keep exactly is NEITHER.
- **Discrete risk from two models.** Expected loss and value at risk; two
  models agreeing TRUE, differing BOTH, one model NEITHER (unwitnessed, the
  same convention as the app model's witnessed call).

## Hard limits, enforced in the API

- **Purpose.** Tactics and strategy runs name a purpose: game, logistics,
  scheduling, business, negotiation or mapping. There is no other purpose to
  name, and weapons targeting is not one. An option the caller's policy
  prohibits (`PCE_OPT_PROHIBITED`) is refused: never ranked, never a regret
  baseline. This is a refusal by policy that the API makes explicit, not
  something the logic detects.
- **Advisory.** Work flagged as touching life-safety, a device or money is
  advisory: a TRUE that would proceed becomes ESCALATE, a person approves.
  Every financial finding is advisory.
- **Analytics only.** The finance engine takes read-only inputs. No output
  is a balance, an order or a trade, and nothing in it is investment advice.
- **Freestanding.** C11, no libc, no allocation, no float, no memcpy: the
  only external symbols are the engines' own and those of lpres, lpres_hex,
  paradox, rational and SHA-256 (checked by `make test-pce`).

## As blocks

`pce_entry.h` exposes the engines as entries with contracts: named integer
vectors in, exact numbers or truth values (lpres's codes) out, the shape of
a block entry in the app model (`docs/APP_MODEL.md`). Entries: `prob.join`,
`prob.verdict`, `prob.bayes`, `strat.decide`, `strat.leader`, `strat.zopa`,
`fin.npv`, `fin.npv_sign`. Tactics stays a C and SDK interface, since a game
is three callbacks rather than a vector.

`kernel/src/apps/zxapp_pce.c` is the app model's `c` runtime for them, and
three blocks ship in `kernel/src/apps/blocks/`: `pce_prob`, `pce_strat` and
`pce_fin`. Each `logic c` line names its engine entry inline, so an app
uses them like any other block:

```
zxapp 1
id planner
use pce_prob 1
use pce_strat 1
```

and calls `pce_prob.join`, `pce_strat.decide` and so on. The block's
contract is checked first, then the engine's own shapes. The runtime is
freestanding (no libc, no allocation). A second implementation of the
probability entries in KRON, so `zxapp_call_witnessed` checks C against
KRON, is the next step.

## Tests

`make -C kernel test-pce` (part of verify-all): about 9,600 checks under
ASan and UBSan (the engines, golden digests, and the blocks composed into
an app), then the freestanding build for aarch64 and i386.
