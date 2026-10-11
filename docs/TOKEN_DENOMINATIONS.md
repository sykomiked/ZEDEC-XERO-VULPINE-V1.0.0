# Token denominations and octane

The swarm's AI tokens come in denominations, the way money comes in coins and
notes. A model can hold its per-cycle allotment as plain tokens, as a few dense
(compressed) tokens, or as super tokens. Each wallet picks its own **octane**,
like the grades at a fuel pump, and can step up to racing fuel or back down at
any time without changing what it holds.

Code: `kernel/src/swarm/swarm_denom.h` (rules D1-D9) and `swarm_denom.c`.
Tests: `kernel/src/swarm/test_swarm_denom.c`, run by `make -C kernel test-denom`
and by `verify-all`. Benchmark: `tools/bench/bench_swarm_denom.c`.

## The tiers

Tier *k* has the face value F(k+2), the Fibonacci ladder already used by the
budget's level rule and by Vino coins:

| tier | 0 | 1 | 2 | 3 | 4 | 5 | 6 | 7 | 8 | 9 | 10 | ... | 23 | ... | 63 |
|---|---|---|---|---|---|---|---|---|---|---|---|---|---|---|---|
| face | 1 | 2 | 3 | 5 | 8 | 13 | 21 | 34 | 55 | 89 | 144 | ... | 75025 | ... | F(65) |

Tier 0 is the plain, uncompressed token. Tier 10 (144) and up are **super tokens**.

Fibonacci faces were picked because wrapping and unwrapping are then single
exact steps: one tier-k token and one tier-(k+1) token fuse into one
tier-(k+2) token, and any token splits back into the two below it. Every amount
also has exactly one shortest form (its Zeckendorf form), so two wallets with
the same value and octane always hold the same pieces.

## The pump grades

| grade | octane (densest tier) | densest face |
|---|---|---|
| PLAIN | 0 | 1 |
| REGULAR | 4 | 8 |
| MIDGRADE | 9 | 89 |
| PREMIUM | 14 | 987 |
| RACING | 23 | 75025 |
| MAX | 63 | 17,167,680,177,565 |

These grade points are defaults; any octane from 0 to 63 is allowed.

## The guarantees

- **Lossless.** Wrap, unwrap, spend, transfer and octane changes keep the total
  value exact, in integer arithmetic. Anything that would overflow fails and
  changes nothing. A 4,000-step random test checks that the total across four
  wallets never moves.
- **No premium.** A dense token is worth exactly its face in plain tokens.
  Compression earns no interest, costs no fee, and buys no extra allotment or
  budget priority. It is a denomination, not leverage.
- **Each system picks its own density.** A payment arrives at the receiver's
  octane, whatever octane the payer used. Change comes back at the payer's own.
- **Lockstep with the budget.** `swarm_denom_payout` pays each model's
  allotment into its wallet, `swarm_denom_consume` spends from the budget and
  the wallet together (and refuses if they disagree), and wallets expire with
  the cycle like unused tokens do (R6).
- **Wire code.** Amounts travel as Fibonacci codes, which are self-delimiting.

## What density buys, measured

Measured with `tools/bench/bench_swarm_denom.c` (gcc -O2, x86-64 cloud
container). "Pieces" is how many token objects the wallet holds. "Piece-list
bits" is the wallet's pieces sent as Fibonacci-coded (tier, count) pairs.
"Amount-only bits" is the plain amount as one Fibonacci code.

| amount | grade | pieces | piece-list bits | amount-only bits |
|---|---|---|---|---|
| 100 | PLAIN | 100 | 16 | 11 |
| 100 | REGULAR | 14 | 27 | 11 |
| 100 | MIDGRADE | 3 | 26 | 11 |
| 100 | PREMIUM | 3 | 26 | 11 |
| 100 | RACING | 3 | 26 | 11 |
| 100 | MAX | 3 | 26 | 11 |
| 1000 | PLAIN | 1000 | 21 | 16 |
| 1000 | REGULAR | 125 | 19 | 16 |
| 1000 | MIDGRADE | 12 | 22 | 16 |
| 1000 | PREMIUM | 2 | 21 | 16 |
| 1000 | RACING | 2 | 21 | 16 |
| 1000 | MAX | 2 | 21 | 16 |
| 4096 | PLAIN | 4096 | 23 | 18 |
| 4096 | REGULAR | 512 | 22 | 18 |
| 4096 | MIDGRADE | 47 | 25 | 18 |
| 4096 | PREMIUM | 7 | 36 | 18 |
| 4096 | RACING | 6 | 47 | 18 |
| 4096 | MAX | 6 | 47 | 18 |
| 100000 | PLAIN | 100000 | 30 | 25 |
| 100000 | REGULAR | 12500 | 29 | 25 |
| 100000 | MIDGRADE | 1127 | 51 | 25 |
| 100000 | PREMIUM | 106 | 54 | 25 |
| 100000 | RACING | 9 | 68 | 25 |
| 100000 | MAX | 9 | 68 | 25 |
| 1000000 | PLAIN | 1000000 | 35 | 30 |
| 1000000 | REGULAR | 125000 | 34 | 30 |
| 1000000 | MIDGRADE | 11239 | 56 | 30 |
| 1000000 | PREMIUM | 1017 | 54 | 30 |
| 1000000 | RACING | 17 | 48 | 30 |
| 1000000 | MAX | 5 | 46 | 30 |
| 1000000000 | PLAIN | 1000000000 | 49 | 44 |
| 1000000000 | REGULAR | 125000000 | 48 | 44 |
| 1000000000 | MIDGRADE | 11235956 | 51 | 44 |
| 1000000000 | PREMIUM | 1013175 | 69 | 44 |
| 1000000000 | RACING | 13335 | 81 | 44 |
| 1000000000 | MAX | 14 | 108 | 44 |

| grade | ns per transfer (1,000,000 tokens, 1e6 transfers) |
|---|---|
| PLAIN | 242.8 |
| REGULAR | 186.4 |
| MIDGRADE | 207.8 |
| PREMIUM | 269.6 |
| RACING | 334.1 |
| MAX | 365.6 |

What this shows:

1. **Density cuts the piece count, by a lot.** A million tokens are a million
   pieces at PLAIN, 17 at RACING and 5 at MAX. This is the real gain, wherever
   tokens are handled one piece at a time: per-piece receipts, witnesses,
   signatures or ledger rows. With one ML-DSA signature per piece, for example,
   1,000,000 plain tokens would need a million signatures and RACING needs 17.
2. **Density does not shrink the bits.** Sending the amount alone is always the
   smallest form (30 bits for a million). Sending the piece list costs the same
   or more at higher octanes, because the pieces must be named. So for the
   wire, send amounts, not piece lists.
3. **Density does not speed up the arithmetic.** A transfer takes a few hundred
   nanoseconds at every grade and gets slightly slower at higher octanes,
   because there are more tiers to walk. Timings vary run to run by about 20%.
4. **Density does not make a model think faster** or give it more tokens.
   Throughput of a model is set by its allotment (`swarm_budget.h`) and its
   hardware, never by the denomination it holds.

## Mutation score

`swarm_denom` is a CI mutation target (`tests/harness/mutator/targets.json`):
465 of 470 valid mutants are killed (98.9%). The 5 survivors are equivalent
mutants, listed in the config's baseline note.

## Open question carried over

`docs/AUDIT_REPORT.md` still asks whether every **VFV coin** must be a
Fibonacci rung (`vino_mint` accepts any value). Swarm tokens are Fibonacci-only
by construction here; the VFV question is separate and still needs the owner's
answer.
