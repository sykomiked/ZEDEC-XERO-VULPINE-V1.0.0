# QUBO Workbench (classical, with optimality certificates)

Code: `kernel/src/qubo/` (`qubo.h`, `qubo.c`, `test_qubo.c`, `bench_qubo.c`), prefix `qubo_`.

## What it is

People write optimisation problems as QUBOs (minimise `E(x) = offset + sum_{i<=j} Q[i][j] x_i x_j`
over binary `x`) or as Ising models (spins `s_i` in {-1,+1}) so they can send them to quantum
annealers. This module solves the same problems on an ordinary CPU and is honest about how good
the answer is. Every solver returns:

- the best assignment `x` it found and its exact integer energy,
- a **proven lower bound** on the true minimum,
- the gap (energy minus bound) and a status: `OPTIMAL` (gap 0, proven), `GAP` (branch and bound
  ran out of budget; the optimum lies between the bound and the energy) or `HEURISTIC_ONLY`
  (a heuristic answer whose bound is only the cheap root bound).

It is a classical workbench. Nothing in it is quantum, it claims no quantum
speedup, and the benchmark below compares only classical methods with each other. It is related
to roadmap step Q.6 ("quantum-inspired optimiser, reported only if it measurably wins") and uses
the same rule: the annealer is reported as winning only where it measurably wins. It does not.

## Data model

- `qubo_t`: up to `QUBO_MAX_VARS` = 64 binary variables, `int64` upper-triangular `Q`, `int64`
  offset. Assignments are a `uint64_t` bitmask.
- `qubo_check` refuses lower-triangle or out-of-range entries and requires
  `sum |Q| + |offset| <= 2^60`. With that bound every energy, flip delta and energy difference
  computed inside is a sum of distinct terms and cannot overflow; larger problems are refused
  with `QUBO_ERR_OVERFLOW`, never wrapped.
- `qubo_ising_t`: `h[i]`, `J[i][j]` (i < j), offset; spin bit set means `s_i = +1`.
- Conversions are exact integers. Ising to QUBO (`s = 2x - 1`) keeps the energy exactly. QUBO to
  Ising needs quarters (`x = (s + 1)/2`), so `qubo_to_ising` returns a model whose energy is
  exactly **4 x** the QUBO energy, offset included. Each conversion is overflow-checked.

## Text format and the four-valued check

```
# comment
n 3              # size, 1..64, must come before entries
offset -7        # optional constant
0 0 -1           # i j value  (i == j: linear term; "1 0" and "0 1" are the same pair)
1 2 -4
```

The parser tags its input with a state numerically identical to `lpres_state_t`
(NEITHER=0, TRUE=1, FALSE=2, BOTH=3; redefined locally so no other headers are pulled in):

| Tag | When | Result |
|---|---|---|
| TRUE | well formed, consistent (an exact repeat of a coefficient is accepted once) | `QUBO_OK` |
| FALSE | syntax error, index out of range, integer overflow, abs-sum too large | refused, line reported |
| BOTH | the same coefficient (or `n`, or `offset`) given twice with different values | refused with `QUBO_ERR_CONFLICT` unless the caller explicitly picks `QUBO_MERGE_SUM` |
| NEITHER | no size line, or an entry before it | refused with `QUBO_ERR_NO_SIZE` |

Contradictory input is never resolved by guessing which value was meant. Summing duplicates is
the usual QUBO-file convention, so it is offered, but only as an explicit caller choice.

## Algorithms

1. **Brute force** (`qubo_brute_force`): Gray-code enumeration with O(n) incremental field
   updates, n <= 32. Refused up front (`QUBO_ERR_BUDGET`) if `2^n - 1` steps exceed the caller's
   budget, so it is exact or nothing.
2. **Branch and bound** (`qubo_bnb`): depth-first, explicit stack (at most n + 1 entries).
   At each node it applies first-order persistencies (fix `x_i = 0` if
   `lin_i + sum_j min(0, Q_ij) >= 0`, fix `x_i = 1` if `lin_i + sum_j max(0, Q_ij) <= 0`; each keeps
   at least one optimum) and then the row bound
   `const + sum_i min(0, lin_i + sum_{j > i free} min(0, Q_ij))`. Branching order: descending row
   weight. It can be warm-started with a heuristic answer. When the node budget runs out, the
   proven bound is the minimum over the incumbent and every open node's parent bound (and at
   least the root bound): a valid optimality-gap certificate.
3. **Parallel tempering** (`qubo_anneal`): up to 16 replicas, geometric ladder `T_{r+1} = 3/4 T_r`,
   temperatures in 1/256 energy units, Metropolis via a 256-entry Q24 `exp(-x/16)` table (copied
   from `qnet`, renamed) and a per-replica threshold table (no division in the inner loop),
   adjacent replica swaps after each sweep. Default hottest temperature: the largest possible
   single-flip change.
4. **Local search** (`qubo_local_search`): steepest descent from random starts, with restarts and a
   total flip cap. This is the standard classical baseline.

All randomness is a caller-seeded xorshift64; same seed, same answer (checked by golden-value
tests). `qubo_verify` re-checks any result's energy, gap and status.

## Measured benchmark

`bench_qubo.c`, host gcc 13 `-O2`, one core of an Intel Xeon @ 2.10 GHz, CPU time by `clock()`.
Budgets fixed for all instances: PT 16 replicas x 1000 sweeps; LS 4000 restarts; B&B 300,000 nodes,
warm-started with the better heuristic answer. Brute force cross-checks n <= 24. Families: `dense`
(all pairs, values in [-100, 100]), `sparse` (pair probability 4/n), `maxcut` (G(n, p = min(1/2, 6/n)),
unit weights, energy = minus the cut), `planted` (frustration-free pair penalties around a hidden
assignment, known optimum exactly 0). Seed 0 of each cell:

| family | n | PT energy | LS energy | B&B energy | proven bound | B&B status | brute force | PT ms | LS ms | B&B ms |
|---|---|---|---|---|---|---|---|---|---|---|
| dense | 16 | -693 | -693 | -693 | -693 | OPTIMAL | -693 | 14.5 | 8.4 | 0.4 |
| dense | 24 | -2332 | -2332 | -2332 | -2332 | OPTIMAL | -2332 | 22.2 | 13.0 | 4.4 |
| dense | 32 | -3044 | -3044 | -3044 | -3044 | OPTIMAL | - | 28.1 | 20.7 | 531.9 |
| dense | 48 | -5414 | -5414 | -5414 | -28739 | GAP | - | 51.0 | 45.1 | 1980.6 |
| dense | 64 | -9390 | -9390 | -9390 | -52215 | GAP | - | 73.7 | 63.0 | 3204.0 |
| sparse | 16 | -571 | -571 | -571 | -571 | OPTIMAL | -571 | 10.7 | 6.6 | 0.0 |
| sparse | 32 | -1463 | -1463 | -1463 | -1463 | OPTIMAL | - | 25.9 | 21.4 | 0.1 |
| sparse | 64 | -2653 | -2653 | -2653 | -2653 | OPTIMAL | - | 65.9 | 62.3 | 0.4 |
| maxcut | 16 | -35 | -35 | -35 | -35 | OPTIMAL | -35 | 9.9 | 6.0 | 0.4 |
| maxcut | 32 | -72 | -72 | -72 | -72 | OPTIMAL | - | 24.1 | 18.9 | 18.8 |
| maxcut | 48 | -105 | -105 | -105 | -256 | GAP | - | 42.7 | 37.9 | 2064.2 |
| maxcut | 64 | -152 | -152 | -152 | -396 | GAP | - | 68.3 | 59.4 | 3173.1 |
| planted | 32 | 0 | 0 | 0 | 0 | OPTIMAL | - | 21.7 | 20.4 | 2.2 |
| planted | 48 | 0 | 0 | 0 | 0 | OPTIMAL | - | 39.1 | 40.1 | 96.5 |
| planted | 64 | 0 | 0 | 0 | 0 | OPTIMAL | - | 63.7 | 88.7 | 1255.4 |

Over 3 seeds per cell (60 instances): B&B proved optimality in all 15 sparse, all 15 planted,
dense up to n = 32 (9/9) and max-cut up to n = 32 (9/9) plus 1 of 3 at n = 48; it ended with a GAP
on all dense n = 48, 64 and on 5 of 6 max-cut n = 48, 64. PT and LS each reached the reference
energy (the proven optimum, or the best energy known when the status is GAP) on 60/60.

An earlier run of this benchmark found a real flaw: with temperatures in whole energy units the
coldest replica could not go below T = 1, which is too hot for max-cut (deltas of 1 or 2), and PT
missed the optimum on 0/3 max-cut instances at n = 32, 48, 64. Temperatures are now in 1/256
units; the table above is after that fix.

## Honest verdict

- **Heuristics:** at n <= 64 on these families, plain restarted local search reaches the same
  answers as parallel tempering in similar time (faster on most rows). The annealer does **not** measurably win
  here, so it is not reported as better. Larger or harder instances could differ; that is not
  measured.
- **Certificates are the actual product.** For sparse and planted problems up to 64 variables,
  and for dense and max-cut problems up to about 32, the workbench returns a proven optimum in
  milliseconds to about a second. Beyond that the bound is weak: on dense n = 48 the proven bound
  (-28739) is more than 5x below the best energy (-5414), so the certificate says little. A stronger
  bound (roof duality via max-flow, or an SDP/Lagrangian bound) is the obvious next step.
- No claim is made about any quantum device; nothing here was compared with one. The numbers
  are about these measured classical runs only.

## Honest limits

- NP-hard problem: exact solvers are exponential in the worst case; 64 variables maximum.
- The replica-swap test uses the nominal 3/4 ladder ratio, not the floored integer temperatures.
- `qubo_verify` checks a result's energy and status but cannot re-prove the bound; the tests do
  that against brute force.
- Synthetic instances; timings are single-core and host-dependent.

## Not built yet

Roof-duality / max-flow bound; more than 64 variables (sparse storage); reading vendor file
formats (e.g. BQM JSON) beyond the plain text format; a userland CLI; sending the same problem to a
real QPU through the Q.4 adapter for side-by-side comparison.

## Tests

From `kernel/`:

```
gcc -std=c11 -O1 -g -Wall -Werror -Wextra -Wdouble-promotion -Wfloat-conversion \
    -fsanitize=address,undefined -fno-sanitize-recover=all -DTEST_HOST -Iinclude \
    src/qubo/test_qubo.c src/qubo/qubo.c -o $(ZXV_TMP)/test_qubo -lm && $(ZXV_TMP)/test_qubo
gcc -std=c11 -O2 -Wall -Wextra -Iinclude src/qubo/bench_qubo.c src/qubo/qubo.c -o /tmp/bench_qubo
```

The test prints `295 checks passed, 0 failed`. It covers hand-computed problems, Ising/QUBO round
trips over every assignment (n <= 10), B&B == Gray-code brute force == a slow reference on 600
random instances (n <= 14, 5 families, fixed seeds) plus 40 more at n = 15..18 against brute force, gap-certificate validity on 200 budget-starved runs (bound <=
true optimum, and better than the root bound when GAP), heuristics never below the optimum and LS
ending in a 1-flip local minimum, determinism and golden trajectories, every parser tag and error,
overflow refusal, and every error path.
