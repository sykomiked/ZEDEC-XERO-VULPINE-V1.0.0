# Gridlock resolution for payment queues

Module: `kernel/src/gridlock/` (`gridlock.h`, `gridlock.c`, `test_gridlock.c`, `bench_gridlock.c`),
prefix `gl_`. Sibling of `kernel/src/qnet/` (cycle-cancellation netting, docs/QUANTUM_NETTING.md).
That module removes obligations. This one decides which queued payments can settle **now**, in
gross, with the money that is actually there.

**Verdict in one line:** in FIFO mode the standard removal algorithm is already exactly optimal,
and we prove it. In free mode the removal algorithm left 31 to 44 % of the settleable value
unsettled on average, and the exact solver recovers it with a proof up to about 50 to 100
payments. At 256 payments it returns a better answer plus a proven bound. That bound is tight
when liquidity is plentiful and loose when liquidity is scarce. Nothing here is quantum.

## The problem

Participants `p` (up to 64) have a balance `b_p >= 0` and an optional interest-free credit cap
`c_p >= 0`. Queued payments `i` (up to 256) have a sender, a receiver, an amount > 0, a priority
class (0 = most urgent, up to 3) and a FIFO position `seq`. Queue order is ascending `seq`, with
ties broken by index. The solver chooses a set `S` to settle at once such that, for every `p`:

    b_p - out_p(S) + in_p(S) >= -c_p

The objective is to maximise the settled value. Option `GL_OBJ_LEX_PRIORITY` instead maximises
class 0 value first, then class 1, and so on. There are two modes:

- **free:** any subset may settle.
- **FIFO:** each sender's settled payments must be a prefix of its queue, which is what RTGS
  queues usually require.

The textbook gridlock is A->B, B->C, C->A, each for 100, with zero balances. None of the three
settles alone, but all three settle together.

## Four-valued payment status

Each payment carries a status with the same numbers as `lpres_state_t`: NEITHER = 0, TRUE = 1
(queued), FALSE = 2 (cancelled) and BOTH = 3. The enum is defined locally because `lpres.h` pulls
in other headers. Reports from different sources are combined with `gl_report_status`, a
knowledge-order join (bitwise OR):

- **TRUE:** the payment is eligible to settle.
- **FALSE:** the payment is dropped.
- **BOTH** (for example, one source says queued and another says cancelled): the payment is
  **held**. It is never settled and never treated as cancelled, and it is reported as
  `GL_D_HELD_BOTH`.
- **NEITHER** (no evidence): the payment is held and reported as `GL_D_HELD_NEITHER`.

In FIFO mode a held payment also blocks the payments behind it in the same sender's queue
(`GL_D_BLOCKED_FIFO`), because settling them would jump the queue on a guess. A cancelled
payment does not block anything.

## Solvers

1. **`gl_heuristic`** is the classical baseline. It starts with every eligible payment. While
   some participant is negative, it takes the most negative one and removes one of its
   outgoing payments:
   - `GL_RM_LAST` removes the last payment in that participant's queue. FIFO mode always uses
     this rule.
   - `GL_RM_LARGEST` removes the largest payment.
   - In lexicographic free mode, the removal picks from the lowest-priority class first.
   - No payment is ever re-added.

   This is our own documented version of the removal family described in the payments
   literature and in public descriptions of RTGS gridlock algorithms. A given live system's
   exact rules may differ. Because the removal order inside each sender is fixed, the result
   does not depend on which negative participant is processed first. A scratch search of
   200,000 small instances found no case where it did.
2. **`gl_solve`** is the exact solver.
   - **FIFO mode, no search.** Feasible prefix vectors are closed under componentwise max:
     if participant `p` pays the same and receives at least as much, it stays feasible. So
     there is a greatest feasible vector. It contains every other feasible set, so it is
     optimal for both objectives. The removal algorithm never goes below it: a participant
     that is negative at `k >= k*` must have `k*_p < k_p`. So the algorithm stops exactly at
     the optimum, in polynomial time. The proof is in `gridlock.c`, and the tests check it
     against brute force on 420 random instances.
   - **Free mode, branch and bound.** The problem is NP-hard: one sender paying many receivers
     is subset-sum.
     - *Incumbent:* the better of both removal rules, each followed by a greedy re-add.
     - *Branching:* include, then exclude, per payment. Payments are taken by class, then
       amount descending.
     - *Dominance:* a sender that can pay all its remaining payments out of money certain to
       arrive pays them all.
     - *Bounds (no LP):* `M_p` is the most `p` can pay out. It is at most what `p` still has
       queued, and at most `b + c + decided incoming + sum over undecided incoming payments of
       min(a_i, M_sender - sender's decided out)`. The rule is iterated (3 rounds per node,
       256 at the root), and the bound is `value <= sum_p M_p`. A node where some `M_p` falls
       below what `p` already pays is infeasible. The lexicographic objective refines this per
       class.
     - *Node budget:* within the budget the result is `GL_OPTIMAL`. When the budget runs out,
       the result is `GL_FEASIBLE`, the best set found, plus a **proven upper bound**: the
       maximum node bound over all unexplored subtrees, and the root bound. That pair is the
       gap certificate.
3. **`gl_brute_force`** enumerates all subsets, or all prefix vectors in FIFO mode, for up to 20
   eligible payments. It is used for tests and audits.

Every result passes `gl_verify` before it is returned:

- every participant ends at or above `-credit`;
- the sum of balances is unchanged exactly (conservation);
- FIFO order holds where required;
- no held, cancelled or blocked payment settles;
- the settled value equals the sum of the chosen amounts.

`gl_check` bounds the grand total of balances, credits and amounts within int64 once. Every
later sum stays below that total. The module code uses integers only, does no allocation and
calls no libc. It uses a caller-owned workspace of about 15 KiB.

## Measured results

`bench_gridlock` was built with gcc 13 `-O2` and run on one core of an Intel Xeon at 2.10 GHz.
The node budget was 2,000,000. The queues are fixed-seed random queues with amounts from 1 to
1000. Each participant's balance is 0 %, 10 % or 40 % of the value it has queued. Gap % is
`(best - x) / best`, and the bound gap is `(bound - best) / bound`. The table shows seed 1; the
benchmark prints seed 2 as well, and the summary covers all 42 runs.

Free mode, total value:

| parts | pays | liq % | heur LAST | gap % | heur LARGEST | gap % | exact/best | upper bound | bound gap % | status | nodes | heur ms | exact ms |
|---|---|---|---|---|---|---|---|---|---|---|---|---|---|
| 10 | 20 | 10 | 0 | 100.00 | 49 | 0.00 | 49 | 49 | 0.00 | optimal | 25 | 0.005 | 0.0 |
| 10 | 20 | 40 | 3770 | 25.73 | 2800 | 44.84 | 5076 | 5076 | 0.00 | optimal | 45 | 0.004 | 0.0 |
| 10 | 50 | 0 | 0 | 0.00 | 0 | 0.00 | 0 | 0 | 0.00 | optimal | 334477 | 0.017 | 82.5 |
| 10 | 50 | 10 | 10718 | 45.28 | 9256 | 52.74 | 19587 | 19587 | 0.00 | optimal | 2284 | 0.012 | 0.5 |
| 10 | 50 | 40 | 24150 | 2.84 | 21008 | 15.48 | 24857 | 24857 | 0.00 | optimal | 244 | 0.006 | 0.0 |
| 32 | 50 | 10 | 398 | 76.56 | 459 | 72.97 | 1698 | 1698 | 0.00 | optimal | 255 | 0.018 | 0.1 |
| 32 | 50 | 40 | 11861 | 14.25 | 12307 | 11.03 | 13832 | 13832 | 0.00 | optimal | 3025 | 0.014 | 0.9 |
| 32 | 100 | 0 | 0 | 0.00 | 0 | 0.00 | 0 | 0 | 0.00 | optimal | 806622 | 0.049 | 419.9 |
| 32 | 100 | 10 | 266 | 99.02 | 1944 | 92.87 | 27278 | 36908 | 26.09 | best+bound | 2000001 | 0.031 | 694.0 |
| 32 | 100 | 40 | 33144 | 20.84 | 35159 | 16.03 | 41869 | 41869 | 0.00 | optimal | 175730 | 0.014 | 48.8 |
| 64 | 100 | 10 | 73 | 90.56 | 773 | 0.00 | 773 | 773 | 0.00 | optimal | 135 | 0.034 | 0.1 |
| 64 | 100 | 40 | 11707 | 36.44 | 13789 | 25.13 | 18418 | 18418 | 0.00 | optimal | 661678 | 0.028 | 242.8 |
| 32 | 256 | 0 | 0 | 0.00 | 0 | 0.00 | 0 | 96207 | 100.00 | best+bound | 2000001 | 0.139 | 1873.4 |
| 32 | 256 | 10 | 49294 | 31.51 | 56228 | 21.87 | 71968 | 103520 | 30.48 | best+bound | 2000001 | 0.082 | 1134.3 |
| 32 | 256 | 40 | 113092 | 3.78 | 113103 | 3.77 | 117536 | 118344 | 0.68 | best+bound | 2000001 | 0.018 | 638.9 |
| 64 | 256 | 0 | 0 | 0.00 | 0 | 0.00 | 0 | 93161 | 100.00 | best+bound | 2000001 | 0.168 | 2079.0 |
| 64 | 256 | 10 | 6163 | 76.55 | 19866 | 24.42 | 26283 | 102830 | 74.44 | best+bound | 2000001 | 0.169 | 2000.1 |
| 64 | 256 | 40 | 112351 | 2.75 | 108612 | 5.99 | 115529 | 123310 | 6.31 | best+bound | 2000001 | 0.028 | 1266.5 |

Three more seed-1 rows, (10, 20, 0 %), (32, 50, 0 %) and (64, 100, 0 %), were all 0 and proven
optimal in at most 60 nodes.

Summary over all 42 free-mode runs:

- 28 were proven optimal.
- The LAST heuristic was below the exact or best answer in 27 runs.
- On the 18 proven-optimal runs with a non-zero optimum, the mean gap was 43.8 % for LAST and
  31.3 % for LARGEST.

In FIFO mode the heuristic and `gl_solve` were identical in all 21 runs, as the proof requires.
Each took at most 0.5 ms. Compared with the free-mode best, FIFO settled 0 to 99 % less (for
example 33144 vs 41869, and 266 vs 27278).

**Honest verdict.**

- **FIFO mode:** there is no algorithmic edge to sell. The industry removal algorithm is
  already optimal. What we add is the proof, the independent audit (`gl_verify`) and the
  four-valued holds.
- **Free mode (or any rule set that allows queue bypass):** the exact solver settles
  substantially more than the removal heuristics on these synthetic queues. It proves
  optimality at small and medium sizes in milliseconds to under a second. At 256 payments it
  still improves on the heuristics, but it can only bound the remaining gap. The bound is
  within 1 to 6 % at 40 % liquidity, 30 to 74 % at 10 %, and uninformative at 0 % liquidity.
  There, every run found nothing and nothing could be proven. Settling anything with zero
  liquidity needs an exact circulation, and the bound cannot rule that out.
- **No quantum comparison was made.** Quantum-annealing papers pitch this integer program.
  Any such claim should be measured against an exact classical solver like this one. We make
  no speedup claim either way.

## Honest limits

- The queues are synthetic and random, not real RTGS data. Timings depend on the machine.
- The free mode is exponential in the worst case. Large, liquidity-starved queues end in
  "best + bound", not a proof.
- The bound is combinatorial. There is no LP relaxation or cutting planes; an LP or MIP solver
  would give tighter bounds at more cost.
- One currency per instance. There are no partial payments, no collateral or pricing for
  credit, and no persistence. Payments must already be authenticated; this module is
  arithmetic, not consensus.
- Whether a system may bypass FIFO is a rulebook question. This module only measures what
  bypassing would settle.

## Not built yet

- No wiring into `cbank`, `settle` or any ledger path, and no ISO 20022 import.
- No combination with `qnet`. Netting obligations first and then resolving gridlock on what is
  left is the obvious next step.
- No multi-currency queues, no partial settlement of single payments, and no incremental
  re-solve when one payment arrives.

## Tests and benchmark

From `kernel/`:

```
gcc -std=c11 -O1 -g -Wall -Werror -Wextra -Wdouble-promotion -Wfloat-conversion \
    -fsanitize=address,undefined -fno-sanitize-recover=all -DTEST_HOST -Iinclude \
    src/gridlock/test_gridlock.c src/gridlock/gridlock.c -o $(ZXV_TMP)/test_gridlock -lm \
    && $(ZXV_TMP)/test_gridlock
gcc -std=c11 -O2 -Wall -Wextra -Iinclude src/gridlock/bench_gridlock.c src/gridlock/gridlock.c \
    -o $(ZXV_TMP)/bench_gridlock && $(ZXV_TMP)/bench_gridlock
```

The tests cover:

- **The textbook gridlock:** all three payments settle, and no single payment or pair does.
- **Constructed heuristic failures:**
  - LAST settles 9 where the optimum is 10;
  - LARGEST settles 9 where the optimum is 10;
  - a cycle trap where both rules settle 0 where the optimum is 10, and FIFO is 0.
- **Ordering:** lexicographic versus value objectives, FIFO order by `seq` with index
  tie-breaks.
- **Four-valued status:** the BOTH, NEITHER and FALSE handling, and FIFO blocking.
- **Exact boundaries:** a balance of exactly 0 afterwards, and exactly `-credit`.
- **Overflow:** int64 boundaries.
- **Independent brute force:** a test-side enumeration over 600 random tiny instances in all
  modes, objectives and rules, plus 240 medium instances against `gl_brute_force`. These cover:
  - exact agreement;
  - proven-bound validity under node budgets from 1 to about 200;
  - the FIFO theorem, heuristic = optimum;
  - FIFO never beating free mode.
- **Determinism:** golden node counts.
- **Larger runs:** 24 runs with up to 256 payments.
- **Every error path.**
