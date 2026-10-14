# Exact quantum circuit simulator (classical)

`kernel/src/qsim/` is a **classical simulator** of small quantum circuits. It
runs on an ordinary CPU, stores every amplitude of the state, and computes
the result exactly, with no floating point. It is not a quantum computer, it
talks to no quantum hardware, and it gives no quantum speedup. Its cost grows
exponentially with the number of qubits. It is meant for small circuits,
testing and teaching, and as an exact reference for the later roadmap steps.

## Roadmap steps covered (section 6.6d, track Q)

| Step | What is built here |
|---|---|
| Q.1 | Exact circuit simulator: amplitudes in an exact algebraic ring, no rounding drift. |
| Q.2 | Checks against known circuits (Bell, GHZ, QFT on 2-4 qubits) and against a double-precision reference. |

Q.6 netting optimiser: see docs/QUANTUM_NETTING.md.

## Data model

- Amplitudes live in the cyclotomic ring Z[z] with z = e^(2*pi*i/N) and N
  chosen at `qs_init`: N = 8 or N = 16. One element is N/2 `int64`
  coefficients in the basis 1, z, ..., z^(N/2-1), using z^(N/2) = -1.
- The whole state shares one denominator sqrt(2)^k: amplitude x is
  `amp[x] / sqrt(2)^k`.
- N = 8 covers the Clifford+T gate set. N = 16 adds the pi/8 phase, so the
  controlled 2*pi/16 phase in the 4-qubit QFT is exact.
- `qs_state_t` is a fixed-size, caller-owned struct: `QS_MAX_QUBITS` = 10,
  so 1024 amplitudes of 8 coefficients (64 KiB). A circuit (`qs_circuit_t`)
  is a fixed array of up to 1024 gate records.
- Basis index bit q is qubit q (qubit 0 is the least significant bit).

## Gates

I, X, Y, Z, H, S, Sdg, T, Tdg, PHASE(k/m) = diag(1, e^(2*pi*i*k/m)) with m in
{1, 2, 4, 8, 16} dividing N, CNOT, CZ, SWAP, CCX (Toffoli) and controlled
PHASE(k/m). A phase whose order m does not divide N is refused with
`QS_ERR_PHASE` (for example the pi/8 phase on an N = 8 state).
`qs_run_circuit` runs a circuit and `qs_circuit_adjoint` builds its exact
inverse (gates reversed; S and T swap with their daggers; phase k becomes
-k mod m).

## Algorithms

- **Ring arithmetic.** Add, subtract and multiply with every step checked by
  `__builtin_*_overflow`. Multiplying by z^e is a coefficient shift with a
  sign flip on wrap-around, so phase gates never grow the numbers.
- **H and the sqrt(2) reduction.** H maps a pair (a0, a1) to (a0 + a1,
  a0 - a1) and raises k by one. Then, while k > 0 and every amplitude is
  divisible by sqrt(2), every amplitude is divided by sqrt(2) and k drops.
  sqrt(2) = z8 - z8^3 in the ring, x is divisible by sqrt(2) exactly when
  x*sqrt(2) has only even coefficients, and then x/sqrt(2) = x*sqrt(2)/2.
  Because k is always minimal, equal states have identical representations
  and `qs_state_equal` is an exact equality test.
- **Probabilities.** |a|^2 = a * conj(a) is a real ring element.
  `qs_prob_basis` and `qs_prob_qubit` return a fraction num/den of two real
  elements (both over 2^k, which cancels); den is the norm squared. For a
  normalised state den is exactly the integer 2^k, and `qs_check_unit`
  proves that the total probability is exactly 1.
- **Exact sign test.** A real element of Z[z8] is a + b*sqrt(2); its sign
  comes from the signs of a and b, and from comparing a^2 with 2*b^2 when
  they differ. A real element of Z[z16] is A + alpha*B with A, B in
  Z[sqrt(2)] and alpha = 2cos(pi/8) = sqrt(2 + sqrt(2)), so its sign follows
  from the sign of A^2 - (2 + sqrt(2))*B^2 in Z[sqrt(2)]. The squares are
  computed in fixed 640-bit integers. `qs_prob_cmp` uses this to compare a
  probability with any rational p/q exactly.
- **Measurement.** `qs_measure` collapses one qubit to an outcome the caller
  chooses, without renormalising; all later probabilities are relative to
  the kept norm squared, exactly. An outcome with probability exactly 0 is
  refused and the state is left unchanged. `qs_renormalise` rescales exactly
  when the norm squared is a power of two (set k to its exponent) and
  refuses otherwise.
- **Sampling.** `qs_sample` and `qs_measure_sample` take a 32-bit number u
  from the caller's seeded PRNG and return the inverse cumulative
  distribution at the dyadic rational u/2^32, using the exact comparison.
  The outcome for a given u is exactly defined and reproducible. Over
  uniform u each outcome's frequency is within 2^-32 of its exact
  probability; a finite random number cannot do better when the
  probabilities are irrational, such as (2 + sqrt(2))/4.

## Measured results (test_qsim)

- 500 random circuits (1-6 qubits, 10-160 gates; 100 of them T-heavy) match
  the double-precision reference to within 2.0e-14. The largest denominator
  exponent seen was k = 14 and the largest coefficient magnitude 30. Every
  circuit followed by its adjoint returned exactly to |0...0>.
- The coefficient bound |c| <= sqrt(2)^k holds on every gate of every random
  circuit. It follows from every Galois conjugate of the state being a unit
  vector.
- A single-qubit (H T)^m sequence is the worst case found. It reaches
  k = 63 at m = 124, where probability queries start returning
  `QS_ERR_OVERFLOW`. A gate overflows at m = 252 (last good k = 126), which
  marks the state broken.

## Honest limits

- A classical simulator: memory and time grow as 2^n in the number of
  qubits, and the number size grows with the T-count. There is no quantum
  speedup and no claim of one.
- Only gates whose matrix entries lie in Z[z]/sqrt(2)^k are offered.
  Arbitrary rotation angles are not representable; approximate synthesis
  into Clifford+T is not done here.
- Overflow is detected, never wrapped. A gate that overflows marks the state
  unusable (`QS_ERR_BROKEN` from every later call until it is
  re-initialised). Probability queries need k <= 62.
- No noise model and no decoherence: ideal unitary evolution only.
- Module code is freestanding C11: integer only, no allocation, no libc.

## Not built yet (later steps)

Q.3 (QEC decoder helper with Both/Neither tags), Q.4 (QPU adapters for
vendor cloud APIs), Q.5 (hybrid scheduler). Nothing here submits work to real
quantum hardware.

## Running the tests

From `kernel/`:

```
gcc -std=c11 -O1 -g -Wall -Werror -Wextra -Wdouble-promotion -Wfloat-conversion \
  -fsanitize=address,undefined -fno-sanitize-recover=all -DTEST_HOST \
  -Iinclude src/qsim/test_qsim.c src/qsim/qsim.c -o $(ZXV_TMP)/test_qsim -lm
$(ZXV_TMP)/test_qsim
```

Freestanding check (must print no undefined symbols, or only `qs_` ones):

```
for t in aarch64-none-elf i386-none-elf; do fl=; [ $t = aarch64-none-elf ] && fl=-mgeneral-regs-only; \
  clang --target=$t -ffreestanding $fl -std=c11 -O2 -Wall -Werror -Wextra -Wdouble-promotion \
  -Wfloat-conversion -Iinclude -c src/qsim/qsim.c -o /tmp/qsim.o && nm -u /tmp/qsim.o; done
```
