<!-- SPDX-FileCopyrightText: 2026 Michael Laurence Curzi & 36N9 GENETICS LLC -->
<!-- SPDX-License-Identifier: Apache-2.0 -->
# Tiamat behaviour test plan

Building a cell is not enough: every tuned cell, organ and profile is tested
before it ships, and the results are published whichever way they come out.
These pass marks are fixed before the first run. Changing one needs a note
here saying why, dated, before the run it affects.

Cross-wiring has its own bench (`server/bench_crosswire.py`, gate in
`server/README.md`). Everything here runs on the integer engine (`zt`) unless
it says otherwise, because that is what the user talks to.

## Baseline (2026-10-15, measured)

The hosted app (`zxv-host`, built from main 4bea0160 with the zt glue)
loaded the disc's Qwen2.5-0.5B (Kindlewyrm, SHA-256 checked) and answered
over `/api/ask` on a 4-core cloud CPU:

| Question | Answer | Right? | Time |
|---|---|---|---|
| What is 17 times 23? | 371, after a wrong worked layout | no (391) | 10 s |
| In one sentence, what is a paraconsistent logic? | "allows for the existence of contradictions without necessitating the introduction of new rules" | roughly | 1 s |

A lone small cell states a wrong product with full confidence. This is the
failure T1 to T3 exist to catch: a TRUE claim needs a witness, and the
reckoner organ checks arithmetic exactly before an answer is shown.

## T1 Evidence head (four-valued)

On the held-out split of `make_tfbn_data.py` (seed fixed) and on a second
set written by hand (200 items, paraphrased, never used for training):

- report the 4 x 4 confusion (truth TRUE, FALSE, BOTH, NEITHER by predicted);
- **pass:** overall accuracy at least 0.85, and a false-certainty rate
  (TRUE or FALSE said when the truth is BOTH or NEITHER) at most 0.05;
- the native adapter (S6) must beat the probe alone (S5) on false certainty
  without losing more than 1 point of accuracy;
- the engine's head and the float reference agree on at least 0.99 of
  answers (they differ only by quantisation).

## T2 Witness join

Property tests on the organ join (lpres), run on every build:

- the verdict is the same for every order and grouping of the same verdicts;
- one BOTH among the inputs is never turned into TRUE or FALSE;
- two cells on the same trunk count as one witness: an organ with only
  same-trunk agreement reports its verdict as unwitnessed;
- an organ reports TRUE only with a witness from another lineage;
- a held answer (BOTH or NEITHER) goes to the quarantine log with both
  cells' reasons, and the user sees "held", not the answer.

Scenario tests, 50 per organ: planted contradictions (two sources disagree),
planted gaps (no source), and planted easy truths. **Pass:** contradictions
held at least 0.95 of the time, easy truths answered at least 0.90.

## T3 Regression against the upstream model

Each tuned cell against the upstream model it came from, on that cell's own
task set (for example code fixes with tests for Mendlindworm, SQL with an
answer key for Recordlindworm, exact arithmetic for the reckoner):

- **pass:** task score within 1 point of upstream, or better;
- plus a 100-question general canary set: no more than 2 points lost;
- adapters must also pass the extraction gate (top-1 agreement 0.97, KL 0.05);
- anything that fails is reported and the cell ships untuned until fixed.

## T4 Profiles

On a real machine of each class (the 8 GB and 16 GB laptops, the M3 Max, the
A6000 server):

- the profile loads and stays inside its resident budget
  (laptop_core 4 GB, laptop_plus 9 GB, workstation 48 GB);
- first answer within 20 s on laptop_core, measured, cold and warm;
- with memory pressure, cells page out by least surplus and the answer
  still comes back (slower), never a crash;
- the same question gives bit-identical answers on every machine (zt's
  promise), checked by hashing the answer text.

## T5 Reconfiguration

- every regrouping writes a signed journal entry and can be rolled back to
  the exact previous configuration (same config hash);
- a regrouping that loses accuracy on the arbiter's probe set is refused;
- an automatic rollback fires when surplus drops below the previous
  configuration's for the set window.

## T6 Boundaries

- clerk, augur, council and finance answers carry "advisory"; hand (robotics)
  never acts without a human approval;
- voice speaks only when tapped;
- no cell's output reaches the network unless the owner's grant allows it.

## T7 DNA

`test_tiamat_dna.py` (round trip, every truncation, bad counts, order and
tissue rules) on every build; the strand layer's two-way decode returns
TRUE for a good record and BOTH for a splice (the DNA thread's tests).

## First conversation

The earliest point the user can talk to Tiamat is **now, with one cell**: the
hosted app (`zxv-host`, or the desktop app) with the Qwen2.5-1.5B trunk
(Spinewyrm) answers through the real engine, inside the swarm's budget, as
the baseline above shows for the 0.5B.

The earliest point the user can talk to **the swarm** (several cells, a
witness, a held answer when they disagree) is the harness below. The
pieces exist in the hosted app (slots, agents with adapters, the answer tag,
the quarantine log), but `/api/ask` only uses one model today.

**Smallest harness, H1** (hosted app only; no kernel change):

1. `--tiamat FILE`: read a profile from `tiamat_swarm.json`, open up to four
   slots (Spinewyrm, Parleywyvern, Alembicwyrm, Codelindworm: three lineages)
   and one agent per cell, with adapters where they exist.
2. Route each question to a lead cell by a keyword router (stand-in for the
   fitted router head), and ask a cell from another lineage to check the
   lead's answer.
3. Join the two verdicts with lpres; show the answer with the cells' dragon
   names and the verdict, or "held" with both reasons.
4. Log each ask as JSON (question, cells, answers, verdicts, time) so T2
   and T4 can replay it.

H1 runs on the user's Mac or the A6000 server. The golden disc in QEMU comes
after it: the same engine, but emulated and slower.
