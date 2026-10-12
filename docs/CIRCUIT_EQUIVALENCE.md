# Exact circuit equivalence checker (classical)

`kernel/src/qeq/` decides whether two small quantum circuits implement the
same unitary, **exactly**. It is a **classical tool** built on the exact
simulator in `kernel/src/qsim/` (see QUANTUM_SIMULATOR.md). It runs on an
ordinary CPU, talks to no quantum hardware and gives no quantum speedup. Its
cost is exponential in the number of qubits.

Who it is for: people who write circuit optimisers, compilers and SDKs, and
labs and courses that need to show that an optimised or compiled circuit
still implements the same unitary as the original. A floating-point
simulator can only say "the two matrices are within 1e-6". This checker says
"equal" or "not equal, here is the entry", with no tolerance.

## Roadmap steps it builds on (section 6.6d, track Q)

| Step | Relation |
|---|---|
| Q.1 | Uses the exact simulator (`qs_*`) as its engine; qsim is read, not changed. |
| Q.2 | Adds a new kind of check: circuit against circuit, not only circuit against a known state. |

Not built yet: QPU adapters (Q.4), the hybrid scheduler (Q.5), ancilla and
qubit-permutation handling (see limits).

## What it checks

Input: two `qs_circuit_t` on the same `n` qubits (`n <= QS_MAX_QUBITS` = 10),
built in C or parsed from OpenQASM 2.0 (subset below).

- **Strict** (`QEQ_STRICT`): U1 == U2 entry by entry.
- **Up to global phase** (`QEQ_UP_TO_PHASE`): U1 == z^j U2 for one j, the same
  for every column, with z = e^(2 pi i/N). j is found from the first nonzero
  entry of column 0 and then required, exactly, in every column.
- **Methods**: `QEQ_DIRECT` runs C1 and C2 on every basis state |x> and
  compares the two columns. `QEQ_MITER` runs C1 and then `adjoint(C2)` (from
  `qs_circuit_adjoint`) on |x> and checks the result is z^j |x>. The miter
  needs one state instead of two; it does the same number of gate
  applications, and in the benchmark it was **not** faster (see below).
- **Counterexample** on a mismatch: input index `cex_in`, output index
  `cex_out` and both exact amplitudes (element + power of sqrt 2). DIRECT
  reports U1[out,in] and U2[out,in]; MITER reports (C2^dag C1)[out,in] and the
  value expected there (z^j or 0).

### Verdict (four-valued)

Same numbers as `lpres_state_t` (NEITHER=0, TRUE=1, FALSE=2, BOTH=3), defined
locally so the module stays freestanding.

| Verdict | Meaning |
|---|---|
| TRUE | Equivalent in the requested mode, proven for all 2^n columns. |
| FALSE | Not equivalent; the counterexample is filled in. |
| NEITHER | Could not decide; `why` says why: too many qubits, a malformed or unrepresentable gate (with circuit and gate index), an overflow inside the exact arithmetic, or the gate-application budget ran out. The checker never guesses. |
| BOTH | Not used. "Equal only up to a global phase" is TRUE in phase mode with `phase_exp != 0` and the flag `global_phase_only = 1`. |

A FALSE found before the budget runs out is still FALSE (one exact
counterexample is a proof).

## Algorithms

- **Exact comparison.** qsim keeps every state over the smallest possible
  sqrt(2)^k, so equal columns have identical integers. Two single amplitudes
  with different k are compared by multiplying the one with the smaller k by
  sqrt 2 = z^(N/8) - z^(3N/8) until the k agree; every int64 step is
  overflow-checked. This matters only for locating the counterexample row.
- **Why "no z^j" proves FALSE.** If U1 = c U2 with both unitary, c = U1[r,0] /
  U2[r,0] lies in Q(z_N), and c^(2^n) = det U1 / det U2 is a root of unity,
  because every gate in the set has a root-of-unity determinant. So c is a
  root of unity in Q(z_N), and for N = 8 or 16 those are exactly the powers of
  z. Trying j = 0..N-1 is therefore complete.
- **Ring choice.** N = 8 unless some phase gate needs pi/8 (m = 16), then
  N = 16. The caller may force N; forcing 8 on a circuit with a pi/8 phase
  gives NEITHER (`QEQ_WHY_GATE`, `qs_rc = QS_ERR_PHASE`).
- **Budget.** `max_gate_apps` caps the gate applications; a column is
  started only if its full cost fits.

## OpenQASM 2.0 subset

Accepted: `OPENQASM 2.0;`, `include "qelib1.inc";`, exactly one `qreg`,
`creg` and `barrier` (ignored), `//` comments, and the gates
`id h x y z s sdg t tdg cx cz swap ccx u1 p cu1 cp`. Angles must be an exact
multiple of pi/8 written as `0`, `pi`, `pi/4`, `-pi/2`, `3*pi/8`, `2*pi/16`.
Everything else is refused with a code, line and column, never approximated:
`pi/3`, `pi/16`, `0.785398`, `theta`, `pi*3/8`, unknown gates (`rx`),
register index out of range, unknown or second register, repeated qubits,
wrong arity, register broadcast (`h q;`), `measure`, `reset`, `if`, `gate`,
`opaque`, `U`, `CX`, more than 1024 gates, more than 10 qubits.
`qeq_qasm_write` prints a circuit back in the same subset (phases normalised
to [0, 2 pi)); write-then-parse returns the same gates.

## Measured (bench_qeq.c)

One run, Intel Xeon @ 2.10 GHz (4 vCPU), gcc 13.3 `-O2`, wall-clock ms.
Random Clifford+T circuits C (H X Y Z S Sdg T Tdg CNOT CZ SWAP CCX); the
"rewrite" is C with CNOT -> H CZ H, SWAP -> 3 CNOT, S -> T T, Z -> S S and
planted G G^-1 pairs (equivalent); the "mutant" is the rewrite with one gate
replaced. Float check: double precision, column by column, strict,
tolerance 1e-6.

Equivalent pairs (every column must be checked):

| n | gates C / rewrite | exact DIRECT | exact MITER | float | largest float deviation |
|---|---|---|---|---|---|
| 2 | 500 / 891 | 0.27 | 0.29 | 0.12 | 6.6e-15 |
| 3 | 500 / 954 | 0.83 | 0.88 | 0.36 | 9.8e-15 |
| 4 | 500 / 923 | 2.61 | 2.90 | 1.10 | 5.5e-15 |
| 5 | 500 / 923 | 10.9 | 10.1 | 3.4 | 4.6e-15 |
| 6 | 10 / 21 | 1.28 | 1.22 | 0.26 | 7.4e-16 |
| 6 | 500 / 907 | 38.0 | 39.7 | 12.7 | 3.4e-15 |
| 7 | 500 / 927 | 237 | 227 | 48 | 3.6e-15 |
| 8 | 100 / 192 | 128 | 144 | 38 | 6.6e-16 |
| 8 | 500 / 909 | 597 | 656 | 190 | 2.5e-15 |
| 9 | 500 / 900 | 2720 | 2678 | 740 | 1.6e-15 |
| 10 | 10 / 13 | 151 | 213 | 80 | 1.2e-16 |
| 10 | 100 / 154 | 1747 | 2131 | 608 | 2.3e-16 |
| 10 | 500 / 923 | 10181 | 11474 | 3393 | 1.1e-15 |

Mutants: 35 of 36 were FALSE (exact, both methods, and float); each took at
most 10.7 ms, because the first differing column stops the check. One mutant
(n = 4, 500 gates) was TRUE for both exact methods and for the float check:
the replaced gate left the unitary unchanged.

Summary of all 72 cases (n = 2..10, sizes 10, 50, 100, 500):

- Exact verdicts: 37 TRUE, 35 FALSE, **0 NEITHER** (no overflow at these
  sizes).
- **Float and exact verdicts differed in 0 of 72 cases.** On these random
  circuits a tolerance of 1e-6 gives the right answer; the largest float
  error on an equivalent pair was 9.9e-15. We do not claim the float check is
  wrong in practice for circuits like these. What the exact check adds is a
  proof (no tolerance to choose, no "close enough"), an exact counterexample,
  an exact global phase, and an honest NEITHER where floats would silently
  round.
- Cost: the exact check was about 3x slower than the simple float check.
  The miter was not faster than the direct method.

Run: `gcc -std=c11 -O2 -DTEST_HOST -Iinclude src/qeq/bench_qeq.c
src/qeq/qeq.c src/qsim/qsim.c -o bench_qeq -lm && ./bench_qeq` (from
`kernel/`, about 45 s).

## Honest limits

- Exponential: 2^n columns, each a full exact simulation, so total work is
  about 4^n x gates. n = 10 with 500 gates took about 10 s; n > 10 is NEITHER.
- Gate set: Clifford+T plus pi/8 phases only. Arbitrary rotation angles are
  not representable (NEITHER, or a parser refusal); approximate synthesis is
  out of scope.
- Coefficients grow with T-type gates. An adversarial (H T)^300 on one qubit
  overflows int64 and gives NEITHER (`QEQ_WHY_OVERFLOW`), never a guess.
- Same qubit order, no ancillas, no measurement or classical control, no
  search over qubit permutations (a compiler that relabels qubits needs the
  caller to undo the relabelling first).
- Float vs exact: in our measurements the float check never disagreed.
  The value here is certainty and diagnostics, not a different answer on
  typical circuits.
- Freestanding C11, integer only, no allocation, no libc. The caller owns a
  `qeq_work_t` of about 140 KiB.

## Tests

From `kernel/`:

    gcc -std=c11 -O1 -g -Wall -Werror -Wextra -Wdouble-promotion -Wfloat-conversion \
      -fsanitize=address,undefined -fno-sanitize-recover=all -DTEST_HOST -Iinclude \
      src/qeq/test_qeq.c src/qeq/qeq.c src/qsim/qsim.c -o /tmp/test_qeq -lm && /tmp/test_qeq

Covers: HH, T^8, T^4 = Z, CNOT = H CZ H, SWAP = 3 CNOT, the 7-T Toffoli vs
CCX, a 3-qubit QFT against its decomposed form and against the textbook
column values, exact counterexamples with exact amplitudes (including rows
with different sqrt 2 powers), global phase (Z X Z X = -I, X Y = iZ),
DIRECT/MITER agreement, every NEITHER reason and argument error, parser round
trips and every refusal with its line, 120 random circuits against their
peephole-optimised rewrite (all TRUE) and 240 single-gate mutation verdicts
checked against an independent double-precision simulation (all agree).
