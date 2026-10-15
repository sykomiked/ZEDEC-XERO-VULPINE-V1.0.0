<!-- Copyright (c) 2024-2026 Michael Laurence Curzi and 36N9 Genetics, LLC -->
<!-- SPDX-License-Identifier: Apache-2.0 -->
# The high-torque coding engine

ZXV's coding agent writes code the deterministic side of the system must
accept before it runs. This page is its design: what it optimizes, how its
work is graded, and the order it must follow. The rules a coding session in
this repository follows are the short form in `CLAUDE.md`.

State: **design**. The parts it is built from exist (named below); the
engine as one component does not yet.

## The engine metaphor

| Engine | Coding engine | Measured as |
|---|---|---|
| Horsepower | tokens per second | raw output rate; deliberately not the target |
| Torque | work settled per token | changes that pass their checks on the first pass |
| RPM | tokens per cycle (TPC) | the per-cycle token allotment of the swarm budget (`kernel/src/swarm`, `docs/supplemental/TOKEN_BUDGETS_AND_EMOTIONAL_AXIS.md`) |
| Octane | the denomination a task is paid in | `kernel/src/swarm/swarm_denom.h` grades (below) |
| Compression ratio | functional change per emitted character | no padding, no restated context, no placeholder code |
| Cylinders | execution lanes | the strata of `build_system/strata.txt` |
| Firing order | the fixed order of a change | check, reserve, compute, mutate, commit |
| Knock | a write before its check | refused (see firing order) |

The target is torque, not horsepower: a short answer that settles beats a
long one that needs another turn.

## Where it sits

The coding engine is an **advisor**, like the Pit Boss's advisor model
(`docs/PIT_BOSS.md`): it proposes, it never commits. Its output is a
candidate change that crosses the same boundary as any foreign input:

1. built and checked in the Cage (`docs/CAGE.md`): compile, `verify-all`,
   the layer check, the format check;
2. accepted only on TRUE from that verdict (`lpres`, below); BOTH or
   NEITHER returns the change to the engine with the verdict attached;
3. committed through the normal signed path (`docs/REGISTRAR.md`).

A model's sampling is not deterministic; the checks are. "First-pass
settlement" means the engine's proposal passes them unchanged, not that
the engine's word is trusted.

## Octane: the kernel's scale

Token denominations already exist (`docs/TOKEN_DENOMINATIONS.md`): tier *k*
has the Fibonacci face F(k+2), an amount's canonical form is its Zeckendorf
form, and a wallet's **octane** is the densest tier it may hold. The grades
are canonical (owner decision 2026-10-14) and the coding work is graded on
them:

| Grade (`swarm_denom.h`) | Densest face | Coding work paid at this grade | Bar it must clear |
|---|---|---|---|
| PLAIN (0) | 1 | identifiers, delimiters, formatting, strings, comments | compiles; clang-format clean |
| REGULAR (4) | 8 | control flow, four-valued state handling, ring index arithmetic | every branch reachable and tested; no undefined state |
| MIDGRADE (9) | 89 | exact arithmetic and invariants, bounded buffers; IPC, authenticated sockets, stratum boundaries, journal formats | integer or rational only; overflow checked; boundary tests; layer check |
| PREMIUM (14) and up | 987+ | settlement opcodes: state roots, journal roll-forward, update and recovery paths, signature checks | crash-at-every-write or equivalent test; mutation shows the test bites |

The faces the original proposal used (1-3, 5-8, 13-21, 34-55, 89-144+)
fall inside these grades in the same order; only the grade names follow
the kernel.

## Four-valued verdicts

A check's outcome is an `lpres_state_t` (`kernel/src/lpres/lpres.h`):

| Verdict | Meaning for a proposed change | Engine's next step |
|---|---|---|
| TRUE | every check passed | commit through the signed path |
| FALSE | a check failed | fix the cause; never retry unchanged |
| BOTH | the inputs conflict (two specs, two tests disagree) | stop; emit the conflict, ask the owner |
| NEITHER | a required parameter or input is missing | stop; name the missing item |

Verdicts combine with `lpres_conjoin`, which is min on the chain
F < N < B < T, not Belnap's lattice: a mix of BOTH and NEITHER never yields
TRUE, so a contradiction plus a gap can never authorize a commit.

## Firing order

Every change the engine writes follows the same order, and code that
mutates before it checks is rejected in review and by tests:

1. check invariants and bounds (four-valued verdict);
2. reserve capacity: a bounded ring with `head - tail >= capacity` as
   backpressure, never unbounded allocation on a real-time path;
3. compute, in integers or `kernel/src/rational`, never IEEE-754 for exact
   values;
4. mutate state;
5. commit: journal first, then the pointer to it (the order
   `kernel/src/loader/abupdate.c` uses and its power-cut test proves).

## Lanes

The strata are the cylinders: each change keeps pure logic, transport,
trust, settlement and adapters in their own modules
(`build_system/strata.txt`, S0 base to S8 products), and an include may not
reach a higher stratum (`build_system/verify_layers.sh`). Foreign code runs
in the Cage; proposals from models stay advisory until verified.

## What is missing

- One component that takes a proposal, runs the Cage checks and returns
  the four-valued verdict (today the checks exist as `make` targets and
  scripts, run by people and CI).
- Charging the engine's work against the swarm budget in denominations,
  per grade.
- A measurement of torque (changes accepted on the first pass, per token).
