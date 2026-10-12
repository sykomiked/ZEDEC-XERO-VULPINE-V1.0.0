<!-- Copyright (c) 2024-2026 Michael Laurence Curzi and 36N9 Genetics, LLC -->
<!-- SPDX-License-Identifier: Apache-2.0 -->

# ZXV academic document series

These papers describe the ZXV architecture for specialists. Each is written
for one field and can be read on its own. For plain-language explanations,
start with [`docs/supplemental/`](../supplemental/README.md).

## The papers

| Paper | Written for | Main questions |
|---|---|---|
| [MATHEMATICS.md](MATHEMATICS.md) | Mathematicians | How the kernel computes with exact rationals and integers; the lattices, finite fields and orders it uses; what is proven and what is only tested |
| [PARACONSISTENT_LOGIC.md](PARACONSISTENT_LOGIC.md) | Logicians, philosophers of logic | The four-valued logic as implemented, how it compares with Belnap–Dunn, LP and K3, and how contradictions are held without explosion |
| [TESTING_METHODOLOGY.md](TESTING_METHODOLOGY.md) | Software-engineering and verification researchers | Why a paraconsistent, integer-only kernel needed new kinds of tests, and what those tests are |
| [PHYSICS_AND_QUANTUM_FRAMING.md](PHYSICS_AND_QUANTUM_FRAMING.md) | Physicists, quantum-information researchers | What "quantum-inspired" means here, what is claimed and, explicitly, what is not |
| [ECONOMICS_AND_CENTRAL_BANKING.md](ECONOMICS_AND_CENTRAL_BANKING.md) | Economists, monetary economists, central bankers | The internal economy's mechanism design, ledger invariants and monetary architecture, and its legal status |
| [COMPUTER_SCIENCE_AND_SYSTEMS.md](COMPUTER_SCIENCE_AND_SYSTEMS.md) | Computer scientists, systems researchers, programmers | The kernel, ABI, layering, runtime and how to read and build the source |

## What ZXV is, in one paragraph

ZXV is an experimental operating system and runtime for a local swarm of
small AI models. It runs on ordinary classical computers. Its kernel uses only
integer and exact rational arithmetic, so results are designed to be
bit-identical across CPU architectures. It reasons with four truth values
(true, false, both, neither) so that conflicting answers can be held and
resolved instead of crashing the system. It uses post-quantum cryptography
(the NIST ML-KEM, ML-DSA and SLH-DSA standards) and stays compatible with
conventional software through a stable C ABI and SDKs in many languages.

## On the word "quantum"

ZXV does not turn a classical computer into a quantum computer, and it claims
no quantum speedup. It borrows a few quantum-inspired ideas, such as states
that are more than true or false, and combines them with cryptography designed
to resist quantum attackers and with compatibility with conventional
computing. The physics paper sets out exactly where the analogy holds and
where it stops.

## Conventions used in every paper

- **Status labels.** Every claim is tagged with one of four labels:
  - **Measured**: a number from a recorded experiment, with its report.
  - **Tested**: a host or CI test checks it.
  - **Implemented, not evaluated**: the code exists but nothing measures it.
  - **Design intent**: planned or specified, not built.
- **Evidence.** Each claim cites a path in this repository. Numbers are copied
  from reports, never estimated.
- **References.** Papers cite published work that the authors could verify.
  Where a design choice has no supporting literature, the paper says it is a
  design choice.
- **Disagreements.** Where two repository documents disagree, the paper says
  so rather than choosing silently.
- **Formal models.** The Lean 4 and TLA+ files in `proofs/` are hand-written
  specifications of parts of the C code. They are checked by machine, but no
  refinement proof links them to the C source or the binary, as seL4's proofs
  do. They are evidence about the design, not a verification of the kernel.
  Wherever a paper says "Lean" or "TLC", read it with this limit.

## Citing

See [`CITATION.cff`](../../CITATION.cff) at the repository root. The
cross-field summary of claims is
[`docs/supplemental/CLAIMS_AND_EVIDENCE.md`](../supplemental/CLAIMS_AND_EVIDENCE.md).
