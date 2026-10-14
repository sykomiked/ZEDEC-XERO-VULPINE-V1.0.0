# Quantum-inspired netting (Q.6), benchmarked against exact cycle cancellation

Module: `kernel/src/qnet/` (`qnet.h`, `qnet.c`, `test_qnet.c`, `bench_qnet.c`), prefix `qn_`.

## Roadmap step

Section 6.6d, track Q:

> | Q.6 | Quantum-inspired netting optimiser, benchmarked against exact cycle cancellation | Already queued; reported only if it measurably wins |

**Verdict in one line:** the annealer does not measurably win. The exact solver eliminates the
most gross by construction and was best or tied in every measured case, so the system should use
`qn_exact_optimum`. Greedy stays as the fast fallback. The annealer stays in the tree as a measured
research baseline only and is not wired into any settlement path.

Nothing in this module is quantum. "Quantum-inspired" is the usual name for this family of replica
heuristics. This one is classical parallel tempering running on an ordinary CPU.

## What problem it solves

Members owe each other integer amounts (minor units). Edge `e` means `from[e]` owes `to[e]` the
amount `cap[e] > 0`. Cycle cancellation picks a **circulation** `f`:

- `0 <= f[e] <= cap[e]` on every edge;
- at every member, the flow in equals the flow out.

It then reduces every obligation to `cap[e] - f[e]`. Because `f` is a circulation, **every
member's net position stays exactly the same**, and no new counterparty pair is created:
obligations only shrink. The goal is to maximise the **eliminated gross**, `sum f[e]`.

This is different from `abacus.h` (`smaug_clear`), which rewrites the book into new payer/payee
pairs. That gives larger savings, but it creates relationships the members never had. `qnet` only
ever removes debt.

## Data model

- `qn_graph_t`: up to `QN_MAX_NODES` = 64 members and `QN_MAX_EDGES` = 512 obligations, with
  int64 amounts. Parallel edges are allowed. Self-loops and amounts <= 0 are refused.
  `qn_graph_check` also refuses a graph whose total gross does not fit in int64, so every partial
  sum the solvers form cannot overflow.
- `qn_result_t`: the flow per edge, `eliminated`, a cycle/move count and an iteration count.
- `qn_workspace_t`: caller-owned scratch memory (about 150 KiB). There is no allocation and no
  libc. The module code uses integers only and builds freestanding for aarch64 and i386 (its own
  64-bit division, so there is no `__udivdi3`).

## The three solvers

1. **`qn_greedy_cycles`** (baseline). A DFS walks the edges in index order over positive
   remaining amounts, finds a cycle and cancels its minimum. It repeats until the remainder is
   acyclic. Finished DFS nodes stay finished, because removing capacity cannot create a cycle.
   Each cancellation zeroes an edge, so there are at most `m` cycles. The result is maximal but
   not maximum.
2. **`qn_exact_optimum`** (optimum). This is a min-cost circulation with cost -1 per unit on every
   edge. It runs on the residual graph: a forward arc costs -1 where `f < cap`, and a backward arc
   costs +1 where `f > 0`. It uses **minimum-mean cycle cancelling**. Karp's walk-length
   Bellman-Ford recurrence finds the cycle with the most negative mean, the solver cancels it by
   its bottleneck, and it repeats until no negative cycle remains. Minimum-mean cancelling is
   polynomial (Goldberg and Tarjan). An iteration guard (`QN_EXACT_MAX_ITERS`) returns
   `QN_ERR_LIMIT`, never a partial answer.
3. **`qn_anneal`** (the "quantum-inspired" heuristic). This is classical parallel tempering
   (replica exchange) with up to 8 replicas.
   - It first enumerates the simple cycles of length <= `max_cycle_len` (smallest member as root,
     at most 4096 cycles, with a per-root work budget).
   - A move pushes flow along one listed cycle, or removes flow from it, by the maximum possible
     amount or by a random amount.
   - Gains are always accepted. A loss is accepted with probability `exp(-x)`, where `x` is
     `loss * beta / max_amount`. The probability comes from a Q24 integer table of `exp(-i/16)`
     compared against 24 random bits (`qn_metropolis_accept`).
   - Inverse temperatures form a ladder `2^r` and ramp up by as much as 64x over the sweeps.
     Neighbouring replicas exchange states under the standard replica-exchange rule.
   - It returns the best state seen. Optional greedy polish then cancels any cycles that are
     left.
   - The seeded xorshift64 PRNG is passed in, and the run is deterministic for a given seed.

Every solver checks its own output with `qn_verify` (bounds, conservation, eliminated = sum f)
before it returns `QN_OK`. `qn_exact_optimum` also re-checks its answer with `qn_check_optimal`.
That function is the **optimality certificate**: plain Bellman-Ford proving that the residual
graph has no negative cycle, which is the LP optimality condition for a min-cost circulation.

## Measured results

`bench_qnet` was built with gcc 13 `-O2` and run on one core of an Intel Xeon at 2.10 GHz.

- Times are CPU milliseconds per run from `clock()`. They varied by roughly ±30 % between two runs;
  the eliminated amounts were identical across runs.
- Anneal uses `qn_anneal_defaults`: 8 replicas, 200 sweeps, 64 moves per sweep per replica (102,400
  moves), cycles up to length 6, polish on, seed 0xC0FFEE.
- "anneal-np" is the same run without the greedy polish.
- Gap % is `(exact - x) / exact`.

| case | n | m | exact | greedy | gap % | anneal | gap % | anneal-np | gap % | greedy ms | exact ms | anneal ms | anneal-np ms |
|---|---|---|---|---|---|---|---|---|---|---|---|---|---|
| random cap1000 s1      |  8 |  16 |     3446 |     2885 | 16.28 |     3446 |  0.00 |     3446 |  0.00 |   0.001 |   0.006 |   13.61 |   12.70 |
| random cap1000 s2      |  8 |  16 |     4493 |     4357 |  3.03 |     4493 |  0.00 |     4493 |  0.00 |   0.001 |   0.005 |   13.00 |   12.53 |
| random cap1000 s1      |  8 |  32 |     2371 |     1935 | 18.39 |     2371 |  0.00 |     2371 |  0.00 |   0.002 |   0.008 |   11.41 |   15.33 |
| random cap1000 s2      |  8 |  32 |    13053 |    11918 |  8.70 |    12668 |  2.95 |    12638 |  3.18 |   0.003 |   0.027 |   14.77 |   14.31 |
| random cap1000 s1      | 16 |  32 |     5179 |     4924 |  4.92 |     5179 |  0.00 |     5179 |  0.00 |   0.003 |   0.038 |   13.49 |   14.00 |
| random cap1000 s2      | 16 |  32 |     4592 |     4348 |  5.31 |     4592 |  0.00 |     4592 |  0.00 |   0.003 |   0.017 |   17.13 |   17.07 |
| random cap1000 s1      | 16 |  96 |    27585 |    24517 | 11.12 |    23847 | 13.55 |    19694 | 28.61 |   0.009 |   0.328 |   13.57 |   14.05 |
| random cap1000 s2      | 16 |  96 |    31686 |    28057 | 11.45 |    29874 |  5.72 |    24653 | 22.20 |   0.010 |   0.381 |   15.46 |   14.02 |
| random cap1000 s1      | 32 |  64 |     7777 |     5956 | 23.42 |     6824 | 12.25 |     5858 | 24.68 |   0.007 |   0.159 |   11.51 |   11.74 |
| random cap1000 s2      | 32 |  64 |     7050 |     6015 | 14.68 |     5779 | 18.03 |     5779 | 18.03 |   0.007 |   0.097 |   12.91 |   12.75 |
| random cap1000 s1      | 32 | 256 |    90515 |    77071 | 14.85 |    78664 | 13.09 |    49301 | 45.53 |   0.039 |   3.797 |   13.03 |   12.55 |
| random cap1000 s2      | 32 | 256 |    97601 |    89395 |  8.41 |    87696 | 10.15 |    49822 | 48.95 |   0.045 |   4.236 |   11.64 |   11.52 |
| random cap1000 s1      | 64 | 128 |    16972 |    15348 |  9.57 |    14964 | 11.83 |    11987 | 29.37 |   0.030 |   1.054 |   16.57 |   17.79 |
| random cap1000 s2      | 64 | 128 |    10931 |     8377 | 23.36 |    10258 |  6.16 |     7930 | 27.45 |   0.027 |   0.507 |   10.64 |   10.82 |
| random cap1000 s1      | 64 | 512 |   186170 |   156300 | 16.04 |   160595 | 13.74 |    77632 | 58.30 |   0.184 |  32.958 |   12.56 |   14.44 |
| random cap1000 s2      | 64 | 512 |   177216 |   151675 | 14.41 |   154748 | 12.68 |    82056 | 53.70 |   0.173 |  40.866 |   15.14 |   14.07 |
| random cap9 s1         | 16 |  96 |      337 |      292 | 13.35 |      316 |  6.23 |      293 | 13.06 |   0.009 |   0.252 |    8.50 |    7.93 |
| random cap9 s2         | 16 |  96 |      309 |      266 | 13.92 |      282 |  8.74 |      264 | 14.56 |   0.008 |   0.241 |    8.37 |    7.92 |
| random cap9 s1         | 64 | 512 |     1979 |     1722 | 12.99 |     1707 | 13.74 |      809 | 59.12 |   0.131 |  17.023 |    7.15 |    7.33 |
| random cap9 s2         | 64 | 512 |     1940 |     1661 | 14.38 |     1631 | 15.93 |      950 | 51.03 |   0.185 |  23.262 |   10.50 |    9.85 |
| ring 32                | 32 |  32 |       32 |       32 |  0.00 |       32 |  0.00 |        0 | 100.00 |   0.006 |   0.020 |    0.01 |    0.01 |
| circle K6              |  6 |  30 |    11349 |    10279 |  9.43 |    11251 |  0.86 |    11251 |  0.86 |   0.002 |   0.018 |   14.23 |   14.25 |
| 8 circles K5           | 40 | 160 |    57981 |    53454 |  7.81 |    55712 |  3.91 |    55632 |  4.05 |   0.027 |   2.705 |   14.03 |   11.86 |
| supply 4x8 +8 back     | 32 |  80 |     6506 |     4692 | 27.88 |     5524 | 15.09 |     5524 | 15.09 |   0.008 |   0.083 |   11.97 |   12.09 |
| supply 4x16 +32 back   | 64 | 176 |    23278 |    18044 | 22.48 |    18124 | 22.14 |    18124 | 22.14 |   0.035 |   2.366 |   10.93 |   11.86 |
| 10 greedy traps        | 60 |  80 |       60 |       40 | 33.33 |       58 |  3.33 |       58 |  3.33 |   0.017 |   0.396 |    7.74 |    8.48 |

Summary over the 26 cases:

- **Exact is never beaten.** This is proved, not just observed: the certificate passed on every
  run.
- **Anneal vs greedy:** anneal eliminated more in 19 cases, tied in 1 and eliminated less in 6.
  The mean gap to exact is 8.1 % for anneal and 13.8 % for greedy.
- **Anneal vs exact:** anneal matched exact in only 6 cases, all of them tiny (8 to 16 members).
- **Anneal without polish** is much worse on the dense graphs (45 to 59 % gap). On a 32-member
  ring it finds nothing, because the only cycle is longer than the cycle list allows. On large
  graphs most of the anneal's result comes from the greedy polish.
- **Time:**
  - Greedy takes 0.001 to 0.19 ms.
  - Exact takes 0.005 to 41 ms; the slowest cases are the full 64 x 512 graphs.
  - Anneal takes about 7 to 17 ms. Its fixed move budget sets the time, not the graph.
  - On every graph up to 32 x 256 and 40 x 160, exact was faster than anneal and better.
  - Only on the 64 x 512 graphs was anneal faster, by about 2 to 3 times. There it still left 12
    to 16 % of the eliminable gross on the table.

**Verdict.** On the roadmap rule ("reported only if it measurably wins"), the quantum-inspired
optimiser does **not** win.

- It cannot exceed the exact optimum on eliminated gross: exact is optimal and runs in polynomial
  time.
- It beats greedy on most graphs (19 of 26), but at tens to thousands of times greedy's run time.
- On small and medium graphs it is slower than exact.

**Recommendation:**

- Use `qn_exact_optimum` for netting.
- Use `qn_greedy_cycles` only as a fallback if exact returns `QN_ERR_LIMIT` or a hard time budget
  is exceeded.
- Do not use `qn_anneal` in production. It is kept for comparison, and to re-run this benchmark if
  graph sizes ever grow past what the exact solver handles in time.

## Honest limits

- The graphs are synthetic: fixed-seed random graphs, rings, complete "circles", a layered supply
  chain with back edges, and repeated greedy traps. None of them are real ledgers.
- Timings depend on the machine.
- The annealer's parameters were not tuned per case. A larger move budget might close more of
  the gap, but it can never exceed exact.
- The annealer only moves flow along the enumerated short cycles. Its list is capped (4096
  cycles, with per-root budgets), so dense graphs see only part of their short cycles.
- One currency per graph. There is no persistence, and obligations must already be authenticated:
  this module is arithmetic, not consensus.
- `QN_ERR_LIMIT` (the exact iteration guard) is not reached by any test.

## Not built yet

- No wiring into `abacus`, `cbank` or any settlement path.
- No multi-currency netting and no per-member limits (for example, "do not cancel more than X
  with this counterparty").
- No incremental re-netting when one obligation changes.
- No QPU (Q.4) or hybrid-scheduler (Q.5) path. Given the measurements above, there is no case
  for sending this problem to quantum hardware.

## Tests and benchmark

From `kernel/`:

```
gcc -std=c11 -O1 -g -Wall -Werror -Wextra -Wdouble-promotion -Wfloat-conversion \
    -fsanitize=address,undefined -fno-sanitize-recover=all -DTEST_HOST -Iinclude \
    src/qnet/test_qnet.c src/qnet/qnet.c -o $(ZXV_TMP)/test_qnet -lm && $(ZXV_TMP)/test_qnet
gcc -std=c11 -O2 -Wall -Wextra -Iinclude src/qnet/bench_qnet.c src/qnet/qnet.c \
    -o $(ZXV_TMP)/bench_qnet && $(ZXV_TMP)/bench_qnet
```

The tests cover:

- **Hand-computed optima:** a triangle (9), parallel edges (6), an acyclic graph (0), and a greedy
  trap where greedy gives 4 and exact gives 6, with the certificate rejecting greedy's flow.
- **Brute force:** every integer circulation of 300 random graphs with up to 4 members, 7 edges and
  amounts 1 to 3. Exact equals the brute-force maximum, and the certificate accepts exactly the
  maximum circulations.
- **Randomized properties:** feasibility, unchanged nets, exact >= greedy, exact >= anneal, and
  anneal deterministic per seed.
- **Statistics and regression:** int64 overflow boundaries, the Metropolis acceptance rates
  (e^-1, e^-2), and golden regression values for the annealer.
- **Every error path.**
