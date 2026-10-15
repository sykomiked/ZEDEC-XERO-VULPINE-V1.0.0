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
test-yield` and by `verify-all`. It is a CI mutation target (181 of 186 valid
mutants killed; the 5 survivors are equivalent and listed in
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

## What yield is not

- **Not a reward, yet (Y6).** Yield changes no allotment, price or priority.
  Paying models by yield would be an economy rule, and that is the owner's
  decision (`swarm_budget.h`, `swarm_market.h`).
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
