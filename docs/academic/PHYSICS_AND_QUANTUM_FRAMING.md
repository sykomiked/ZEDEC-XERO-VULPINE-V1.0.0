<!-- Copyright (c) 2024-2026 Michael Laurence Curzi and 36N9 Genetics, LLC -->
<!-- SPDX-License-Identifier: Apache-2.0 -->

# Quantum-inspired, not quantum: what ZXV does and does not claim

*A technical note for physicists and quantum-information researchers.*

## Abstract

ZEDEC-XERO-VULPINE (ZXV) is an operating system and integer-only AI runtime
that runs on ordinary classical processors. Its source tree uses vocabulary
from quantum mechanics and field theory: "superposition", "Hamiltonian",
"collapse", "qubit", "entanglement", "zero-point energy", "wormhole",
"holographic", "field", "coil". This note reads each such construct against
its source code and classifies it as (a) a classical algorithm that has a
real, checkable definition, (b) a numeric toy model of a physical formula,
or (c) a metaphor. We find no component that stores or evolves a
multi-qubit state, and none that could give a quantum speedup. The only
complex-amplitude state vectors in the tree are independent single-qubit
states in a host-only model, which a classical computer simulates in linear
time. Two parts are substantive in a physics-adjacent sense: integer
signal processing on the harmonic wire (frequency-division multiplexing with
Goertzel detection), and post-quantum cryptography (NIST FIPS 203/204/205)
checked against official known-answer vectors. We also report one defect: in
the integer kernel build, the reduced Planck constant in
`kernel/src/quantum/quantum_device.h` rounds to zero.

## 1 Introduction and scope

### 1.1 Non-claims, stated first

ZXV makes none of the following claims, and no statement in this repository
should be read as making them:

1. **No qubits.** ZXV runs on classical CPUs (arm64, x86_64, riscv64,
   riscv32, arm32; `docs/supplemental/CLAIMS_AND_EVIDENCE.md` §3). It
   controls no quantum hardware, and nothing in it is a physical two-level
   system.
2. **No quantum speedup.** No algorithm in the tree is claimed to beat the
   best known classical algorithm for any problem by exploiting quantum
   effects.
3. **No entanglement.** No data structure holds a non-product state of two
   or more subsystems (§4.3).
4. **No violation of known complexity results.** A classical machine
   simulating a general $n$-qubit circuit by state vector stores $2^n$
   complex amplitudes. No polynomial-time classical simulation of general
   quantum circuits is known, and finding one would collapse BQP into BPP,
   which is widely doubted though unproven (Bernstein and Vazirani 1997;
   Nielsen and Chuang 2010). Efficient special cases exist and are well
   understood. Stabiliser (Clifford) circuits are simulable in polynomial
   time by the Gottesman–Knill theorem (Gottesman 1998), with an
   $O(n^2)$-per-measurement tableau method (Aaronson and Gottesman 2004).
   Adding $t$ non-Clifford $T$ gates gives a cost exponential in $t$ but
   polynomial in $n$; Bravyi and Gosset (2016) report scaling of about
   $2^{0.23\,t}$ for their stabiliser-rank method. Circuits with low
   entanglement can be simulated by matrix-product methods (Vidal 2003).
   ZXV does not claim to improve any of these bounds.

### 1.2 What ZXV does claim in this area

ZXV is a classical system that borrows a few *features* associated with
quantum computing as design ideas: multi-valued states that hold
conflicting evidence, weighted alternatives that are kept open until a
decision is forced, and an explicit "measurement" step that commits one
alternative. It combines these with post-quantum cryptography and with
ordinary compatibility (it boots under emulation on five architectures and
ships a stable C ABI, `sdk/libzxv/`). The project's own architecture table
lists an "exact float-free circuit simulator" as planned (📋) and requires it
to be a "labelled classical simulator" (`docs/ARCHITECTURE.md` §9). No such
simulator exists in the tree at the time of writing.

### 1.3 Method

We searched the kernel for `qubit`, `superpos` and `amplitude`
(case-insensitive) and read each matching module's header and source. Each
statement below cites the file it rests on. Plain-language companions are
[`MULTI_VALUED_LOGIC.md`](../supplemental/MULTI_VALUED_LOGIC.md),
[`HARMONIC_WIRE_EXPLAINED.md`](../supplemental/HARMONIC_WIRE_EXPLAINED.md) and
[`CLAIMS_AND_EVIDENCE.md`](../supplemental/CLAIMS_AND_EVIDENCE.md).

## 2 Definitions

**Definition 1 (pure quantum state).** An $n$-qubit pure state is a unit
vector $|\psi\rangle = \sum_{x\in\{0,1\}^n} \alpha_x |x\rangle$ with
$\alpha_x \in \mathbb{C}$ and $\sum_x |\alpha_x|^2 = 1$. Evolution is by
unitary maps; measurement in the computational basis gives $x$ with
probability $|\alpha_x|^2$ (the Born rule).

**Definition 2 (interference).** If two indistinguishable paths reach the
same outcome with amplitudes $\alpha_1, \alpha_2$, the outcome has
probability
$$|\alpha_1+\alpha_2|^2 = |\alpha_1|^2 + |\alpha_2|^2 + 2\,\mathrm{Re}(\alpha_1\overline{\alpha_2}).$$
The cross term can be negative. That is what lets probabilities cancel.

**Definition 3 (classical probabilistic state).** A distribution over $k$
alternatives is a point of the simplex
$\Delta_{k-1} = \{p \in \mathbb{R}_{\ge 0}^k : \sum_i p_i = 1\}$. Paths
combine by adding non-negative weights, so a cross term never arises and
nothing cancels.

**Definition 4 (product state).** An $n$-qubit state is a product state if
$|\psi\rangle = |\phi_1\rangle\otimes\cdots\otimes|\phi_n\rangle$. Product
states need $2n$ amplitudes, not $2^n$, and any sequence of single-qubit
gates and measurements on them is classically simulable in $O(n)$ time per
layer.

**Definition 5 (Belnap–Dunn value).** An element of
$\mathbf{4} = \{\mathsf{T},\mathsf{F},\mathsf{B},\mathsf{N}\}$, equivalently a
pair of bits (told-true, told-false) (Dunn 1976; Belnap 1977). $\mathsf{B}$
records evidence for and against; $\mathsf{N}$ records no evidence.

## 3 The four-valued logic is an analogy to superposition, not an instance

The four-valued attestation in `kernel/src/lpres/lpres.h` keeps a state
(`LPRES_STATE_BOTH` and others) plus two counters,
`positive_evidence` and `negative_evidence` (type `lpres_attestation`). A
$\mathsf{B}$ value can look like "true and false at once", and the project
uses that resemblance as a design metaphor. It differs from a quantum
superposition in three precise ways:

1. **It is not a vector.** $\mathbf{4}$ is a finite lattice (Definition 5).
   It has no linear structure, no inner product and no basis change. There
   is nothing to which a unitary could apply.
2. **Its weights are counts and ratios, not amplitudes.** Where the tree
   attaches weights to alternatives, they are non-negative and live on a
   simplex (Definition 3). The evidence counters above are unsigned
   integers. The exact rational type `rat_t` in
   `kernel/src/rational/rational.h` is used for weights such as the
   row-stochastic mixing matrices of `kernel/src/mixmat/mixmat.h`, whose
   header states that each weight is $P_i/W$, "ratios of integer counts".
   Such weights are probabilities or proportions. Two of them never cancel,
   so there is no interference in the sense of Definition 2.
3. **"Collapse" is a policy decision.** A $\mathsf{B}$ or $\mathsf{N}$ value
   maps to WAIT or ESCALATE in an action table, and only $\mathsf{T}$ grants
   authority to act (`lpres_implies_actuator_authority`; see
   [`MULTI_VALUED_LOGIC.md`](../supplemental/MULTI_VALUED_LOGIC.md)). Nothing
   is sampled, and the outcome is deterministic.

The logic is best placed with paraconsistent and belief-revision formalisms
(Belnap 1977; Priest 1979). The academic logic note in this series covers it
in full.

## 4 Modules that use quantum vocabulary

### 4.1 `superpos`: a weighted ranking of build configurations

`kernel/src/superpos/superpos.h` describes a "Superposition Coordinator"
with a state
$|\Psi_\text{build}\rangle = \sum_i c_i |\phi_i\rangle\otimes|\psi_i\rangle$
and a "Hamiltonian" $H_\text{deps}+H_\text{opt}+H_\text{target}$. Each
alternative (`sp_eigenstate_t`) stores an integer pair `amplitude_re`,
`amplitude_im` (scale `SP_AMPLITUDE_SCALE` = 1000). What the code does
(`kernel/src/superpos/superpos.c`):

- `superpos_hamiltonian_energy` sums integer penalties (100000 for a missing
  dependency, a doubled cost for a mismatched target). It is a scalar cost
  function. No operator is diagonalised.
- `superpos_hamiltonian_evolve` replaces `amplitude_re` with the average of
  itself and $1000\cdot 100/E$ (capped), or divides both parts by 10 when a
  dependency is missing. This map is not unitary and does not preserve the
  norm. Amplitudes of different alternatives are never added together.
- `superpos_compute_probabilities` sets each weight to
  $(\mathrm{re}^2+\mathrm{im}^2)$ normalised to permille. Because no two
  $c_i$ are ever summed, this is an ordinary normalised score, and the
  imaginary part only adds to it.
- `superpos_measure` picks the highest-weight alternative that is
  dependency-satisfied and safety-verified, and "collapses" when there is
  one viable candidate or the best weight is at least 500 permille. It is a
  deterministic argmax with a threshold.
- `superpos_interference` returns CONSTRUCTIVE when two verified
  alternatives share a target architecture and DESTRUCTIVE otherwise. It is
  a comparison of enum values.
- `superpos_tick` adds `SP_PHASE_CLOCK_MS` (10) to a counter. Nothing reads
  a clock.

In short, `superpos` is a ranking of candidate builds with quantum names.
`docs/SYSTEM_REFERENCE.md` (entry "superpos") says the same: "The 'Hilbert
space' and quantum language describes ordinary bookkeeping. Untested." The
module is linked into all five kernel images (`build_system/Makefile.*`) and
appears in the list of modules with no test in `docs/COVERAGE.md`.

### 4.2 `quantum`: a numeric model with physics names

`kernel/src/quantum/quantum_device.h` declares Casimir cavities, a
"zero-point energy extractor", a "wormhole throat", an "exotic matter
generator", a "harmonic renderer" and a "second quantization" field. Its
header now says: "WHAT THIS IS: a numeric model with physics-inspired names,
nothing more. It drives no hardware, extracts no energy and moves nothing
through any 'wormhole'." Reading the source confirms this:

- `quantum_casimir_compute` evaluates the ideal-plate Casimir pressure
  $F/A = \pi^2\hbar c/(240\,a^4)$ and energy density
  $\pi^2\hbar c/(720\,a^4)$ (Casimir 1948). These are textbook formulas,
  correctly transcribed, and checked to $10^{-9}$ in the host test.
- `quantum_zpe_extract` adds $\eta\,\rho\,\Delta t$ to a running total. The
  total is a number, not energy delivered anywhere. No accepted physics
  permits cyclic net work from the static Casimir vacuum energy, and the
  module does not model one. This also matches the project's stated stance
  that energy is a hard budget that cannot be bought past
  (`DESIGN_PHILOSOPHY.md` §1).
- `quantum_wormhole_open` and `quantum_wormhole_transit` compare a
  "stability" number to 0.5, log a payload id in a ring buffer and lower the
  stability by 0.1. The names recall Morris–Thorne traversable wormholes
  (Morris and Thorne 1988), which need matter violating the classical energy
  conditions. No gravitational physics is computed.
- `quantum_field_create_particle` and `_annihilate_particle` increment or
  decrement an occupation number $n_k$ and store $\sqrt{n_k}$. This is
  bookkeeping of Fock-state labels, not field operators acting on a state
  space.
- The enum lists `QDEV_ENTANGLEMENT_BUS`, `QDEV_QUBIT_ARRAY`,
  `QDEV_QUBIT_ANNEALER` and `QDEV_PHOTONIC_LATTICE`, but
  `quantum_system_t` has no storage for any of them; they exist only as
  names. `system_entanglement` is set to zero by `quantum_system_init` and
  never written again.

**A defect in the kernel build.** Under `TEST_HOST`, `surplus_real_t` is
`double`. In kernel images it is Q32.32 fixed point
(`kernel/src/surplus/surplus.h`), whose smallest step is
$2^{-32}\approx 2.3\times10^{-10}$. `Q_PLANCK_REDUCED` is
`SR_FROM_FLOAT(1.054571817e-34)`, which truncates to 0. We confirmed this by
evaluating the same expression in a small C program (result 0; $c$ and
$\pi^2$ are representable). So in every kernel image, the Casimir force,
energy density and vacuum energy are identically zero, and only the
host-double build computes the formulas. The test
(`kernel/src/quantum/test_quantum_device.c`) is built with `TEST_HOST`
only (`kernel/Makefile`), so it cannot catch this. The test file states that
it does "NOT assert anything about physical realizability".

### 4.3 `epu`: genuine single-qubit state vectors, host only

`kernel/src/epu/epu_device.h` (the "Emotional Processing Unit") opens with
"THIS IS A MODEL. IT IS NOT A DEVICE DRIVER" and lists its limits. It is the
one place where Definition 1 is implemented as written: each of
`EPU_NUM_QUBITS` (144) records holds two complex amplitudes in `double`
(`a_re, a_im, b_re, b_im`), `epu_qubit_apply_gate` applies the unitaries
$I, X, Y, Z, H, S, T$ and a "golden phase" $\mathrm{diag}(1, e^{2\pi i/\varphi})$,
and `epu_qubit_measure` samples by the Born rule from a seeded SplitMix64
generator and collapses the state.

The model has no multi-qubit gate. Limitation L2 in the header states that
`epu_qubit_entangle` "does NOT build a 4-amplitude two-qubit state"; it sets
a flag so that measuring one partner forces the other to the same outcome,
which reproduces the classical correlation of $|00\rangle+|11\rangle$ in one
basis "and NOTHING ELSE: no Bell-inequality violation" (Bell 1964). The
state is therefore always a product state (Definition 4) plus a shared
classical outcome. It is simulable in $O(n)$, and the presence of $T$ gates
does not change that, because the Gottesman–Knill and Bravyi–Gosset bounds
concern entangling circuits. Limitation L15 states that the module uses
`double` throughout and so cannot be linked into the integer-only kernel
images; it is a host-tested library (`make verify-all`, `test_epu`). It is
far simpler than classical toy theories that reproduce selected quantum
features (e.g. Spekkens 2007) and makes no foundational claim.

## 5 Real signal processing: the harmonic wire

The harmonic wire (`kernel/src/harmonic/`) has tones with real frequencies,
but they are classical signals. The trunk bank (`zt_trunk_bank.h`)
synthesises 16-bit integer PCM at 8000 samples per second, places ten
carriers (111, 222, …, 999 Hz and 1111 Hz) in a frequency-division
multiplex with $\pi/4$-DQPSK, one symbol per 320-sample block, and detects
them with the Goertzel algorithm (Goertzel 1958) under a 4-term
Blackman–Harris window (Harris 1978). The throughput is about 50 bit/s per
trunk and 3.4 s per frame on one trunk
([`HARMONIC_WIRE_EXPLAINED.md`](../supplemental/HARMONIC_WIRE_EXPLAINED.md)).
Nothing connects these sample streams to a speaker, microphone or radio. The
"I/Q lanes" inside a 21-octet frame are a two-channel vector comparison with
no carrier. The physics here is that of sampled sinusoids, with the usual
leakage and intermodulation concerns. The supplemental page records that
nine carriers are harmonics of 111 Hz, which makes the composite sensitive to
nonlinearity.

## 6 Post-quantum cryptography: classical algorithms against a quantum adversary

Shor's algorithm would break RSA and elliptic-curve discrete logarithms on a
large fault-tolerant quantum computer (Shor 1997). Data recorded today could
be decrypted later, so migration has to begin before such machines exist
(Mosca 2018). ZXV's response is classical: it implements algorithms believed
to resist quantum attack and runs them on classical CPUs.

- **ML-KEM** (FIPS 203): `kernel/src/mlkem/` (ML-KEM-768, with
  `mlkem768_kat_vectors.h` taken from the NIST ACVP server, with recorded
  SHA-256 digests of the source JSON) and `kernel/src/pqsec/mlkem1024/`.
- **ML-DSA** (FIPS 204): `kernel/src/pqsec/mldsa/`, vendored from the
  pq-crystals reference at a pinned commit (`README.zxv`).
- **SLH-DSA** (FIPS 205): `kernel/src/pqsec/slhdsa/`, vendored from
  `pq-code-package/slhdsa-c` at a pinned commit.
- **HQC-5** and **X25519** complete the hybrid "MATRIX" level
  (`kernel/src/pqsec/pq_matrix.h`; `docs/PQ_MATRIX.md`). HQC is selected by
  NIST but is not yet a final FIPS standard.

Each is tested against official known-answer vectors, including cross-builds
on five other CPUs with one big-endian (`kernel/Makefile` target
`kat-cross`). `docs/PQ_MATRIX.md` §4 states that the implementations are
reference grade and not hardened against every side channel or fault
injection. The word "quantum" in "post-quantum" describes the threat model,
not the computer the code runs on.

## 7 Other physics-flavoured names

| Name | Where | What it is | Reading |
|---|---|---|---|
| Phase-tick | `kernel/src/phase_coord/`; `docs/supplemental/GLOSSARY.md` | Logical time issued by a coordinator, not wall-clock time | Metaphor; closest prior concept is a logical clock (Lamport 1978) |
| Coils, fields | `kernel/src/tensor/zt.h` rules T7–T11 | A memory layout of nested shells with Fibonacci sizes; "field" names an access order | Metaphor. `CLAIMS_AND_EVIDENCE.md` §1 reports that lossless coil and holographic coding "changed nothing" on a real model |
| Holographic files | `kernel/src/holographic/holo.h` | Five file formats with magic numbers and an add/XOR "interference" combine | Metaphor; `docs/SYSTEM_REFERENCE.md` says "the holography is metaphor". 6.3% line coverage (`docs/COVERAGE.md`) |
| Holographic shading | `kernel/src/emu/holo.h`, `holo.c` | Per-pixel integer tint: push channels toward their complement, and shift near pixels warm, far pixels cool | Real perceptual cue (chromostereopsis), not holography. No wavefront is recorded or reconstructed |
| UBH-168 | `kernel/src/ubh/ubh.h` | A 168-bit (21-octet) framed envelope viewed as octets, septets or sextets | Data framing; "harmonization" has no physical meaning |
| Casimir, ZPE, wormhole | `kernel/src/quantum/` | §4.2 | Numeric model; no physical effect |
| Qubit buffer | `kernel/src/epu/` | §4.3 | Product-state simulator, host only |
| Non-local karma events | `kernel/src/dharma/karma.h` (`NON_LOCAL`) | Two events coupled by a fixed correlation rule, looked up anywhere in the set | Metaphor for Bell/EPR correlation; a classical table lookup. The comment said "genuine quantum non-locality" until 2026-10-12 |

The comment in `kernel/src/emu/holo.h` used to say chromostereopsis gives
"genuine holographic depth on a 2D display, no glasses". That was
overstated, and on 2026-10-12 the comment was corrected to "a perceived
depth cue" with a note that "holographic" is a name, not a claim. Chromostereopsis is a real depth illusion from chromatic
aberration of the eye, but its strength and even its sign vary between
observers and viewing conditions (Thompson, May and Stone 1993). It gives
a depth cue, not holographic depth. The function `holo_selfcheck`, called at
boot in `kernel/arch/arm64/kernel_main_arm64.c`, checks only the colour
arithmetic (near goes warm, far goes cool, grey stays stable, no overflow). It
does not test perception.

## 8 Status of claims

| Claim | Status | Evidence |
|---|---|---|
| ZXV runs on classical CPUs and boots under emulation on five architectures | Tested (CI) | `docs/supplemental/CLAIMS_AND_EVIDENCE.md` §3; `build_system/ci_boot_check.sh` |
| No module stores a multi-qubit (non-product) state | Implemented, not evaluated (code inspection) | `kernel/src/epu/epu_device.h` L2; `kernel/src/quantum/quantum_device.h` |
| `superpos` amplitudes are never summed across alternatives; it ranks builds | Implemented, not evaluated (no test) | `kernel/src/superpos/superpos.c`; `docs/SYSTEM_REFERENCE.md`; `docs/COVERAGE.md` |
| Casimir formulas are transcribed correctly (host build) | Tested (host) | `kernel/src/quantum/test_quantum_device.c` |
| $\hbar$ rounds to 0 in Q32.32 kernel builds, so Casimir outputs are zero there | Measured (by the authors of this note, re-evaluating the constant) | `kernel/src/quantum/quantum_device.h`; `kernel/src/surplus/surplus.h` |
| EPU single-qubit gates are unitary and measurement follows the Born rule with a seeded PRNG | Tested (host) | `kernel/src/epu/test_epu_device.c`; `kernel/Makefile` (`test_epu`) |
| Four-valued values carry integer evidence counts, not amplitudes | Tested (host) | `kernel/src/lpres/lpres.h`; `kernel/src/lpres/test_lpres.c` |
| Mixing weights are exact non-negative rationals summing to 1 | Implemented, not evaluated in this note | `kernel/src/mixmat/mixmat.h`; `kernel/src/rational/test_rational.c` |
| Harmonic wire: 8 kHz integer PCM, ten-trunk FDM, Goertzel detection | Tested (host, bare i386, bare big-endian PowerPC) | `kernel/src/harmonic/zt_trunk_bank.h`; `docs/supplemental/HARMONIC_WIRE_EXPLAINED.md` |
| ML-KEM, ML-DSA, SLH-DSA, HQC match official KATs | Tested (CI) | `kernel/src/pqsec/test_pq_kat.c`, `test_pq_matrix.c`; `kernel/src/mlkem/test_mlkem_kat.c`; `docs/PQ_MATRIX.md` |
| PQ handshake costs about 13 ms at MATRIX level (one core, `-O2`) | Measured | `docs/PQ_MATRIX.md` §3 |
| Chromostereopsis shading produces near-warm / far-cool colour shifts | Tested (boot self-check, arm64) | `kernel/src/emu/holo.c`; `kernel/arch/arm64/kernel_main_arm64.c` |
| Chromostereopsis gives a perceived depth cue (the source once said "genuine holographic depth"; corrected 2026-10-12) | Not evaluated: no perception test exists (§7) | `kernel/src/emu/holo.h` |
| An exact float-free, labelled classical circuit simulator | Design intent | `docs/ARCHITECTURE.md` §9 |

## 9 Limitations and threats to validity

- **Coverage of the search.** We searched for `qubit`, `superpos` and
  `amplitude`. Modules that use quantum metaphors under other words (for
  example `kernel/src/hdcm/`, `kernel/src/reality/`, `kernel/src/shimmer/`,
  which matched the search but are not analysed here) may contain further
  claims. Our conclusion that no multi-qubit state exists rests on reading
  the modules above plus the absence of $2^n$-sized amplitude arrays in the
  matches; it is not a proof over the whole tree.
- **Moving target.** Header comments such as "WHAT THIS IS" were added
  during an audit (`docs/SYSTEM_REFERENCE.md`). Later commits may change the
  modules. Cite the commit you read.
- **The $\hbar$ underflow** was found by evaluating the constant expression,
  not by running a kernel image. We did not check every downstream user of
  `quantum_casimir_force` in the images; the module has no includers outside
  its directory (`docs/SYSTEM_REFERENCE.md`).
- **Cryptographic assurance** is limited to functional agreement with KATs
  and a valgrind branch check on two parameter sets; that check "is not a
  proof of constant time" (`kernel/Makefile`, quoted in
  `CLAIMS_AND_EVIDENCE.md` §2). Post-quantum security of these schemes is a
  conjecture about hard problems, as for all public-key cryptography.
- **Perception claims** about chromostereopsis are outside the code's
  tests and outside the authors' measurements.

## 10 Relation to prior work

The boundary between classically simulable and hard quantum computation is
set by stabiliser theory (Gottesman 1998; Aaronson and Gottesman 2004), its
extension by stabiliser rank (Bravyi and Gosset 2016) and entanglement-based
tensor methods (Vidal 2003). Every quantum-named construct in ZXV falls well
inside the simulable region, most of them trivially, since they are not
quantum states at all. "Quantum-inspired" in this repository means
borrowing a vocabulary and some design ideas (keep alternatives open, commit
by an explicit measurement), much as earlier classical work borrowed
quantum ideas for heuristics; it does not mean the dequantisation results of
the quantum-algorithms literature, which ZXV does not engage with. The
four-valued logic follows Belnap and Dunn, and paraconsistency in the sense
of Priest. The post-quantum layer follows the NIST standards and the
migration argument of Mosca (2018).

## References

- Aaronson, S. and Gottesman, D. (2004). Improved simulation of stabilizer circuits. *Physical Review A* 70, 052328.
- Bell, J. S. (1964). On the Einstein Podolsky Rosen paradox. *Physics* 1(3), 195–200.
- Belnap, N. D. (1977). A useful four-valued logic. In J. M. Dunn and G. Epstein (eds.), *Modern Uses of Multiple-Valued Logic*, 5–37. Reidel.
- Bernstein, E. and Vazirani, U. (1997). Quantum complexity theory. *SIAM Journal on Computing* 26(5), 1411–1473.
- Bravyi, S. and Gosset, D. (2016). Improved classical simulation of quantum circuits dominated by Clifford gates. *Physical Review Letters* 116, 250501.
- Casimir, H. B. G. (1948). On the attraction between two perfectly conducting plates. *Proceedings of the Koninklijke Nederlandse Akademie van Wetenschappen* 51, 793–795.
- Dunn, J. M. (1976). Intuitive semantics for first-degree entailments and "coupled trees". *Philosophical Studies* 29, 149–168.
- Goertzel, G. (1958). An algorithm for the evaluation of finite trigonometric series. *The American Mathematical Monthly* 65(1), 34–35.
- Gottesman, D. (1998). The Heisenberg representation of quantum computers. In *Group22: Proceedings of the XXII International Colloquium on Group Theoretical Methods in Physics*, 32–43. International Press. (arXiv:quant-ph/9807006.)
- Harris, F. J. (1978). On the use of windows for harmonic analysis with the discrete Fourier transform. *Proceedings of the IEEE* 66(1), 51–83.
- Lamport, L. (1978). Time, clocks, and the ordering of events in a distributed system. *Communications of the ACM* 21(7), 558–565.
- Morris, M. S. and Thorne, K. S. (1988). Wormholes in spacetime and their use for interstellar travel: a tool for teaching general relativity. *American Journal of Physics* 56(5), 395–412.
- Mosca, M. (2018). Cybersecurity in an era with quantum computers: will we be ready? *IEEE Security & Privacy* 16(5), 38–41.
- National Institute of Standards and Technology (2024). *FIPS 203: Module-Lattice-Based Key-Encapsulation Mechanism Standard*; *FIPS 204: Module-Lattice-Based Digital Signature Standard*; *FIPS 205: Stateless Hash-Based Digital Signature Standard*.
- Nielsen, M. A. and Chuang, I. L. (2010). *Quantum Computation and Quantum Information*, 10th anniversary edition. Cambridge University Press.
- Priest, G. (1979). The logic of paradox. *Journal of Philosophical Logic* 8(1), 219–241.
- Shor, P. W. (1997). Polynomial-time algorithms for prime factorization and discrete logarithms on a quantum computer. *SIAM Journal on Computing* 26(5), 1484–1509.
- Spekkens, R. W. (2007). Evidence for the epistemic view of quantum states: a toy theory. *Physical Review A* 75, 032110.
- Thompson, P., May, K. and Stone, R. (1993). Chromostereopsis: a multicomponent depth effect? *Displays* 14(4), 227–234.
- Vidal, G. (2003). Efficient classical simulation of slightly entangled quantum computations. *Physical Review Letters* 91, 147902.
