<!-- Copyright (c) 2024-2026 Michael Laurence Curzi and 36N9 Genetics, LLC -->
<!-- SPDX-License-Identifier: Apache-2.0 -->

# Why ZXV uses more than true and false

Most software treats every check as yes or no. Parts of ZXV record four basic
states instead (true, false, both, neither), add a 6-bit tag on top, and
route contradictions through a "paradox engine" rather than raising an error.
This note explains why, and states what is built.

## For the general reader

ZXV is designed to run a swarm of AI agents and peer machines. They will
disagree. One peer says a payment arrived; another says it did not. One model
run says a file is valid; a later check says it is not. A system that only
knows true and false has to pick one side straight away or crash.

ZXV keeps the disagreement as a fact of its own. "Both" means there is
evidence for and against; "neither" means there is no evidence yet. Either
one means "do not act yet". The system then looks at which way the evidence
is moving and decides what to do: wait, ask a person, or roll the work back.
Only a clean "true" is allowed to drive a device or move money.

## For the academic reader

The four base values follow the Belnap-Dunn four-valued setting (Dunn 1976;
Belnap 1977): T, F, B (both, a glut) and N (neither, a gap). Negation swaps T
and F and fixes B and N. As in Priest's LP, a glut is a legitimate value, so
holding A and not-A does not make every proposition hold.

In ZXV, non-explosion is enforced operationally, not proved. There is no
inference engine that could derive arbitrary conclusions. A B or N state is a
value attached to a unit of work, and an action table maps it to WAIT or
ESCALATE. The safety gate (`lpres_safety_gate_clear`) passes only when every
live attestation is certain, and `lpres_implies_actuator_authority` returns
true for T alone.

The `lpres_hex` tag is a 6-bit product: base (4 values) x polarity (4 values:
negentropic, neutral, entropic, forbidden) x lifecycle phase (4 values on a
Gray cycle), so 64 states. The action is one lookup in a 64-entry constant
table, and the tests rebuild the table from its stated rules. Forbidden is
absorbing: no update removes it. Combining tags uses lpres's own base
operators, the worst polarity and the least advanced phase.

The paradox engine works one level up. It never decides whether a claim is
true. It classifies a B or N item into four kinds (constructive, destructive,
neutral, special) by evidence trend and a registry of known signatures, then
runs a fixed operator for that kind, each writing to a SHA-256 hash-chained
log. Linked paradoxes form a dependency graph of up to 64 nodes, resolved in a
fixed priority order over nodes sorted by input hash, so the result does not
depend on arrival order. This places it closer to belief revision and
contradiction-tolerant bookkeeping than to a paraconsistent proof system.

**A deviation worth knowing.** In Belnap's FOUR, B AND N = F and B OR N = T.
`lpres_conjoin` returns N and `lpres_disjoin` returns B for that pair
(`kernel/src/lpres/lpres.c`). One consequence: De Morgan's law fails for that
pair (NOT(B AND N) = N, but NOT B OR NOT N = B). The code does not say whether
this is intended. Anyone relying on the lattice laws should check it.

## What actually exists

| Piece | Where | What it does |
|---|---|---|
| Four-valued attestations | `kernel/src/lpres/lpres.h`, `lpres.c` | T/F/Both/Neither states, evidence counts, NOT/AND/OR, registry and safety gate. Linked into the arm64 kernel image. |
| Presence table | `kernel/src/lpres/lpres_core.h` | Per-resource presence as a `trit_t` (`kernel/include/m5_types.h`: false, true and three "glut" variants). Linked into every kernel image. |
| 64-state tag | `kernel/src/lpres/lpres_hex.h`, `lpres_hex.c` | Rules X1 to X6: tag layout, action table, one-step phase moves, absorbing forbidden, combining, hash-chained quarantine log. |
| Paradox engine | `kernel/src/paradox/paradox.h`, `paradox.c`, `paradox_graph.c`; `docs/PARADOX_ENGINE.md` | Four kinds, logged operators, complex-paradox graph. |
| Tests | `kernel/src/lpres/test_lpres_hex.c`, `kernel/src/paradox/test_paradox.c` | Run under ASan/UBSan by `make -C kernel test-lpres-hex` and `test-paradox`; paradox also compiled freestanding for aarch64 (`-mgeneral-regs-only`) and i386. The paradox doc reports 1,000 shuffled scenarios and three simulated peers. |
| Users of the tag | `kernel/arch/hosted/zxv_answer_tag.c`, `kernel/src/aipi/aipi.c`, `kernel/src/vhub/vhub.c` | Tags each model answer (forbidden if its logits saturated), quarantines content whose hash fails, and acts on `lpx_action`. |

Related: the tensor engine reads two integer evidence fields into six swarm
truth states (true, false, glut, neutral, paradox, unknown); see
`kernel/src/tensor/zt.h`, rule T14.

## Limits

- **Not in a bootable image yet.** `lpres_hex.c` and the paradox engine are
  host-tested and checked to build freestanding, but no
  `build_system/Makefile.*` links them into a kernel image. The hosted
  runtime and some modules use them.
- **A tag reports a check; it is not the check.** Forbidden means a bounds
  check, sanitizer or ledger invariant (A = L + E) actually fired. Policy
  refusals can be labelled with a tag but are not detected by this logic.
- **Detection is only what is listed:** step limit, budget, repeated state,
  the caller's invariant callback and the signature registry.
- **Contradictions are held, not resolved.** Under conservation only one of
  two conflicting spends can settle. Settling relies on new evidence, a
  margin rule, or the owner.
- **Signing is the caller's job.** The engine produces hashes; signing log
  heads and sharing signatures with peers is designed to happen elsewhere.
- **Several overlapping value sets** exist (`lpres_state_t`, `trit_t`, the T14
  coil states). They are not one system, and a reader should check which one a module uses.
- The "K3" in `lpres.h`'s title appears to be a component label from the
  project's specs ("K3_LPRES"); the logic it implements has four values, not
  the three of Kleene's K3.
