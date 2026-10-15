<!--
Copyright (c) 2024-2026 Michael Laurence Curzi and 36N9 Genetics, LLC
SPDX-License-Identifier: Apache-2.0
-->

# Token yield: verified work per token

The owner's words (2026-10-15):

> "the big industry bottleneck is tokens are not all equal in how efficiency
> is determined and as far as output. There's no metric currently in the
> industry that I know of that gauges token efficiency. So if we have less
> token generation, we need to make each token go further and be way more
> accurate."

The usual numbers are speed (tokens per second) and price (per million
tokens). Both count every token the same, so a model that needs three tries
looks as good as one that is right the first time. A few benchmarks report
the cost of a solved task, but no common number says how much checked work a
token buys. Token yield is that number for ZXV.

Code: `kernel/src/swarm/swarm_yield.h` (rules Y1-Y6) and `swarm_yield.c`.
Tests: `kernel/src/swarm/test_swarm_yield.c`, run by `make -C kernel
test-yield` and by `verify-all`. It is a CI mutation target (266 of 273 valid
mutants killed; the 7 survivors are equivalent and listed in
`tests/harness/mutator/targets.json`).

## The three numbers

| Number | What it is | Unit |
|---|---|---|
| **Yield** | Verified work per 1,000 tokens. Every token spent on a task counts, retries and revision passes included; only output that passes the quality gate counts as work | work-milli per 1,000 tokens |
| **Effective rate** | Yield times speed: verified work per second | work-milli per second |
| **Accuracy per token** | How close each token's prediction is to an exact float64 reference (KL divergence) | measured by `bench/tensor` |

"Verified" means the gate in `swarm_quality.h` (Q1): an output passes only if
its breadth `r` times the share of its claims actually checked `l` (from
tests, sources and witnesses, never self-estimates) is at least 1.8. A
passing task earns `r × l` of work (×1000, so at least 1800). A task that
never passes earns nothing, and all its tokens count as wasted.

Two shares come with it: the share of tasks right on the **first try**, and
the share of tokens **wasted** on tasks that never passed.

## The owner's point, in numbers

Model A makes 300 tokens a second but needs three tries, 1,200 tokens a task.
Model B makes 150 tokens a second and is right the first time, 400 tokens a
task. Both final outputs score `r` 2.0 and `l` 1.0 (2000 work-milli).

| | Speed | Tokens a task | Yield (per 1,000 tokens) | Effective rate (per second) |
|---|---|---|---|---|
| A | 300/s | 1,200 | 1,666 | 500 |
| B | 150/s | 400 | 5,000 | 750 |

B does half again as much checked work a second at half the speed. A has to
reach 450 tokens a second just to tie. These are the test's known answers
(`test_retries_lose`).

## Accuracy per token, measured

Each ZXV token is already more exact than the usual engine's. On Qwen2.5-1.5B
Q4_K_M, against a float64 reference of the same weights, the KL
divergence of the next-token distribution was 0.000008 for the ZXV integer
engine and 0.0073 for llama.cpp, about 900 times closer (2026-10-11, native
Q4_K/Q6_K rows, `bench/tensor`). The next step is to report that number next
to yield in `zt_bench`, so one run shows both.

## Charted like a market price (Y7)

The owner (2026-10-15): "Token yield can be charted like a stock market
chart ... think of it like the dividend yield or the bond yield."

Each epoch is one candle, like a trading day:

| Candle part | Token yield |
|---|---|
| Open | the epoch's first task's yield |
| High / low | its best and worst task (a task that never passes is a yield of 0) |
| Close | the epoch's overall yield: all its verified work over all its tokens |
| Volume | the tokens spent |

The close always lies between the low and the high (an overall ratio is
never outside the range of its parts; the test checks 2,000 random epochs).
An epoch with no tasks is a flat candle at the last close with no volume, like
a day with no trades. `swarm_yield_chart_t` keeps the last 144 candles
(F(12)), newest first through `swarm_yield_chart_bar`. Drawing them is the
UI's job; the data is all here.

## Tied to Vino vouchers through pools that already exist (Y8)

The owner also asked whether token yield could set the yield on Vino
floating vouchers. Letting it set a voucher's rate directly would pay out
value no fee or attested capacity backs, which breaks backed-only minting
(`mint.h` M4-M6) and comes close to interest (no usury). So the link works
through money that already exists:

- The assurance fee's 15% node-bounty pool (and, once V-Bills exist, the 25%
  V-Bill dividend pool, `pay_assure.h` B1) can be shared by each model's
  verified work: `swarm_yield_weights(SWARM_YIELD_LINK_WORK, ...)` gives the
  weights, and the pool's own capped split (`pay_commons_split`, no
  monopoly) shares it. Under the budget's fixed allotments, verified work is
  yield times tokens spent, so the more each token buys, the bigger the
  share.
- It never mints and never sets a voucher's rate. Every unit paid out came
  in as a fee.
- **Off by default** (`SWARM_YIELD_LINK_OFF`): every model weighs 1, an even
  split. Nothing pays out the node-bounty pool yet, so for now the link is a
  hook, checked end to end against `pay_commons_split` in `test-yield`.

Financial use stays advisory, and a fee-funded payout tied to performance
may be regulated in some jurisdictions; it needs the same legal review as the
rest of the economy before real money moves.

## What yield is not

- **Not a reward by itself (Y6).** Yield changes no allotment, price or
  priority. Its one link to money is Y8, off by default; turning it on is the
  owner's decision.
- **Not a denomination.** Token denominations (`TOKEN_DENOMINATIONS.md`)
  change how tokens are counted and settled. Yield measures what the tokens
  bought.
- **Only as good as `l`.** Yield trusts the verified share, so `l` must come
  from tests, sources and witnesses (Q1), never from the model's own
  estimate.

## Exactness

All arithmetic is integer. Products are carried in 128 bits (192 for the
rate comparison) without `__int128`, so 32-bit builds work, nothing rounds
before the final division, and comparisons between models are exact. The
test checks 200,000 random cases against the host's 128-bit arithmetic.
