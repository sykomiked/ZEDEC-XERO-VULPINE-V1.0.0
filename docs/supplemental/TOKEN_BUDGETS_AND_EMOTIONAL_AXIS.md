<!-- Copyright (c) 2024-2026 Michael Laurence Curzi and 36N9 Genetics, LLC -->
<!-- SPDX-License-Identifier: Apache-2.0 -->

# Why the swarm has a token budget, Fibonacci weights and an emotional axis

The reference for these rules is [`docs/SWARM_ECONOMY.md`](../SWARM_ECONOMY.md).
That document says what the rules are. This one says why they were chosen,
and what they do and do not claim.

## For the general reader

ZXV runs several AI models at once on one machine: a companion you talk to
and a set of specialists behind it. Every model wants to generate text, and
generating text costs time, memory and power. If nothing limits them, one busy
model can slow everything else down, and the machine gets hot and sluggish.

So the swarm works like a household with a fixed weekly budget. Each cycle it
has a fixed number of tokens (pieces of text) it may generate in total. That
number is measured from what the machine can really do. The budget is then
divided:

- **By position.** Models nearer the top (the companion) get a larger share
  than deep background workers. The shares follow the Fibonacci numbers
  (5 : 3 : 2 : 1 for four levels), so each level down gets a bit less.
- **By mood.** Part of the budget is set aside and handed out by an
  "emotion" label, shown as an emoji such as 🤔 curious or 😟 worry, with an
  intensity from 0 to 5. A model flagged 😟 at high intensity gets more
  tokens and more self-checks, for example.
- **By a fair market.** Part of the budget is sold to models that bid for it
  with credit they earned by doing useful work. Caps stop any one model from
  winning everything, and a guaranteed floor stops any model being starved.

Unused tokens expire at the end of the cycle, so nobody can stockpile them.
The point of all this is simple: the swarm stays fast because the total is
fixed, and it stays fair because the division follows written rules that
another model checks.

**About the word "emotion".** In this code an emotion is a labelled control
signal: an emoji code point plus a number. It changes how many tokens a model
gets and how those tokens are spent. It is not a claim that the software feels
anything.

## For the academic reader

The design sits in three established areas.

1. **Compute budgeting for inference.** Fixing tokens per cycle `T` and
   splitting it by rule is a form of compute-constrained scheduling, close in
   spirit to adaptive-computation and early-exit methods. The rules are
   integer-exact (largest remainder, ties to the lower index), so the same
   inputs give the same allotments on every target.
2. **Mixture-of-experts routing.** The companion routes requests to
   specialists (SWARM_ECONOMY.md section 11). The budget plays the role of an
   MoE capacity factor: it bounds how much work any expert can absorb per
   step. Unlike learned gating, the split here is a fixed, auditable rule plus
   a market, not a trained router.
3. **Market-based resource allocation.** The open market is a
   proportional-share auction: each bidder receives market tokens in
   proportion to its bid, and the price is total bids divided by tokens
   (`kernel/src/swarm/swarm_market.h`, rule M3). This is the mechanism of
   Spawn (Waldspurger et al., 1992) and of later proportional-share CPU
   schedulers; it clears in one step, so no Walrasian tâtonnement
   iteration is needed. Anti-monopoly caps (M4, M7) and a commons floor (M2)
   are added for fairness; they trade some allocative efficiency for
   starvation-freedom. We have not analysed the incentive properties
   (truthfulness, equilibrium existence) formally.

**Why Fibonacci.** Level capacities `F(d+2)` and weights `F(L−d+1)` were
chosen because consecutive Fibonacci ratios give a smooth, geometric-like
decay (tending to the golden ratio), every ratio is a small integer ratio, and
the same numbers reappear in the denominations and caps (8/21, 13/21). This
is a design choice for simplicity and exactness. It is not a proof of
optimality, and no experiment in the repository compares it with other weight
schedules.

**Why an imaginary axis.** Each allotment is written `re + i·im`. The real
part comes from the level rule and the market; the imaginary part comes from
emotional charge. The complex notation records that the two are computed
independently (orthogonal inputs) and are kept separate for display and
ledger posting. The model may spend `re + im`; no complex arithmetic beyond
that bookkeeping is involved.

## What actually exists

| Piece | Where | What it does |
|---|---|---|
| Budget rules R1-R6 | `kernel/src/swarm/swarm_budget.h`, `swarm_budget.c` | Fibonacci capacity and weights, exact integer split, no carry-over, a consume call that never grants past the allotment. |
| Emotional economy E1-E5 | `kernel/src/swarm/swarm_emotion.h`, `swarm_emotion.c` | Eight emoji (😐 😌 😄 🤔 🥰 😟 😠 😢), intensity 0-5 mapped to charge 0, 1, 2, 3, 5, 8. The swarm mood moves `charge/21` of `T` (at most 8/21) to the imaginary pool, which is split by each model's charge. If no model has charge, it folds back to the real axis. |
| Behaviour profiles | same files; exposed as `zxv_swarm_emotion_profile` in `sdk/libzxv/src/zxv_econ.c` | Per emoji: valence, sampling temperature ×1000, exploration weight, self-check passes, priority. |
| Market M1-M10 | `kernel/src/swarm/swarm_market.h`, `swarm_market.c` | Commons floor 8/21, open market 13/21, market cap and wealth cap 8/21, conserved money, Crown capital that can never be traded or bid. |
| Nine forms of capital | `kernel/src/zcapital/zcap_forms.h` | The canonical forms the market aliases. |
| Witness, reserve | `kernel/src/swarm/swarm_ledger.*`, `swarm_reserve.*` | A different model recomputes each split and records a trit; a fractal reserve of other models' emotional charge. |
| Denominations | `kernel/src/swarm/swarm_denom.*`, [`docs/TOKEN_DENOMINATIONS.md`](../TOKEN_DENOMINATIONS.md) | Fibonacci-faced token tiers with exact wrap and unwrap. |
| Tests | `make -C kernel test-swarm` (see `kernel/Makefile`) | Host tests for the budget, the economy, cycles, the Hackronomicon shorthand and denominations. |

The Enochian module (`kernel/src/swarm/swarm_enochian.*`) is the swarm's
internal protocol language. It does not take part in token allocation.

## Limits

- **No performance measurements here.** The budget is designed to keep the
  swarm responsive and fair. This repository does not contain a benchmark
  showing that the Fibonacci split, the emotional axis or the market improves
  latency, quality or fairness over a simpler split.
- **Where emotions come from is open.** `swarm_emotion.h` says that setting
  moods and feelings is the caller's business. The module turns labels into
  tokens; it does not detect emotion.
- **Profiles may not reach the sampler yet.** The behaviour profiles
  (temperature, self-check passes) are defined and exposed through libzxv.
  We did not confirm that every inference path reads them.
- **The wiring is partial.** SWARM_ECONOMY.md marks sections 9-11 (posting to
  `kernel/src/finance/triple_ledger.h`, per-machine sizing, growth and
  shrinking) as the plan, not built.
- **The emotional axis is a modelling choice.** Calling it "imaginary" is a
  notation for an independent input, not a physical or psychological claim.
