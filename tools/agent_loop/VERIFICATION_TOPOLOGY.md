# The verification topology — four tiers, negative space, certificates, formal proof

A 100:1 ratio is only worth having if the tests close the system rather
than bloat it. The harness therefore has four tiers, and every tier
targets an invariant no other tier can reach. This page is the H100's
standing instruction for each.

## Tier 1 — Static invariants (compile-time topology)

**Target**: zero-`malloc`, struct alignment, cache-line fit (64 B).

- Every translation unit compiles with `-std=c11 -ffreestanding -nostdlib
  -fno-builtin -Wall -Wextra -Werror`.
- `_Static_assert` pins the sizes and offsets the ABI depends on
  (`sizeof(struct msg)`, `offsetof(...)`, `_Alignof(...)`).
- The negative-space audit proves the *absence*:
  `sh tools/agent_loop/negative_space.sh <bin-dir>` fails if any linked
  binary carries an undefined `malloc`, `free`, `calloc`, `realloc`,
  `sbrk`, `brk`, `mmap`, `dlopen` or `pthread_create`.

**Acceptance**: the build compiles with zero warnings; the audit prints
`[OK] no forbidden symbol survived`.

## Tier 2 — Dual-plane parity (human UI vs AI IPC)

**Target**: 1:1 state synchronisation across `.zxv_audit_manifest`.

Every app exports, in one table, the human binding (the hotkey, the
label, the render hook) and the AI binding (the opcode, the schema, the
execute hook). The test:

1. reads the `.zxv_audit_manifest` section from the ELF;
2. fails if any op has a null in either plane (a dead button or a blind
   spot);
3. drives the op through the AI plane and asserts the human framebuffer
   reflects it, then the reverse.

**Acceptance**: zero nulls in the manifest; both directions observed.

## Tier 3 — The four-valued state space (non-explosive transitions)

**Target**: no unhandled state; a paradox isolates locally.

- `tools/agent_loop/mk_truth_tables.py` writes the complete operator
  tables (conjoin, disjoin, negation) and the per-app sweeps.
- The tables are filled from `kernel/src/lpres/lpres.c`'s algorithm
  (min/max on the chain `F < N < B < T`), never by hand.
- An SMT pass (Z3/CVC5) encodes the transition function
  `T(state, input)` and proves:

  ```
  forall S in States, forall I in Inputs:
      T(S, I) in ValidStates union {QUARANTINE}
  ```

  i.e. no input can force the machine into an undefined or explosive
  state. The four-valued layer is where a contradiction is *held*, so
  the solver's job is to prove the hold is total.

**Acceptance**: the tables are total (every ordered pair has an entry);
the solver returns `unsat` for "an input reaches an undefined state".

## Tier 4 — Dynamic stress and fuzzing (concurrency and bus pressure)

**Target**: lock-free ring underflow/overflow, zero-copy IPC drops.

- QEMU headless injection drives the boot and the idle loop.
- LibFuzzer (or the repo's `tests/harness/mutator/`) feeds malformed
  input to the parsers: the manifest grammar, the pillar tokenizers, the
  network frames.
- The bounded rings are asserted: `head - tail >= capacity` is
  backpressure, never a drop; a drop is a test failure.

**Acceptance**: no panic, no drop, the quarantine handler catches every
malformed input.

## Negative space — proving what is NOT present

| Invariant | The proof |
|---|---|
| **Heap vacuum** | `llvm-nm -u` on every binary: no dynamic-alloc symbol |
| **Non-interference** | after an app's slice, the scratch and IPC regions not marked dirty are zeroed (a post-execution sweep) |
| **Dead-code elimination** | `--gc-sections --print-gc-sections` diffed against the baseline symbol envelope: any survivor is unverified negative space |
| **No unreachable fallthrough** | the paraconsistent layer's undefined state collapses into the quarantine handler, never into `ud2`/traps |

## Certificates — cryptographic proof of verification

Every compiled artifact carries a `.zxv_cert` section
(`tools/agent_loop/mk_cert.py`):

```
magic[8] "ZXVCERT\0" | build_hash[32] | proof_digest[32] |
ratio u32 | invariants u32 | signature[4627]  (ML-DSA-87)
```

- `build_hash` = SHA3-256 of the compiler environment + the binary bytes.
- `proof_digest` = the Merkle root of the proof traces, so the
  certificate binds every proof without carrying it.
- `signature` = ML-DSA-87 over the header, the same family the kernel's
  identity and the bearers use — one cryptographic anchor, not two.

The kernel's module loader reads `.zxv_cert` at boot and rejects an
unsigned or mismatched module **before execution**.

**Acceptance**: every packaged binary has a `.zxv_cert`; the loader
rejects a deliberately corrupted one.

## Formal proofs — from empirical tests to tautology

- **The AOT SMT pass**: the H100 translates each freestanding function's
  pre- and post-conditions into first-order constraints and asks the
  solver for satisfiability of the *negation*. `unsat` proves no input
  causes an overflow, an out-of-bounds access, or a division by zero.
- **Translation validation**: the H100 emits the specification beside the
  code (`.v`/`.lean`), and the key invariants — the lock-free rings, the
  four-valued tables — are checked against Coq/Lean 4.
- **The omni-tautology criterion**, formally:

  ```
  forall S in States, forall I in Inputs:
      T(S, I) in ValidStates union {QUARANTINE}
  ```

**Acceptance**: the SMT proofs and the formal specifications ship in
`zxv_polyglot_testbench` (Volume 3), so a third party re-verifies without
the H100.

## The order the H100 works in

1. Tier 1 + the negative-space audit (they gate the build).
2. The certificates (they bind what passed).
3. Tier 2 (the parity manifest) once the apps load.
4. Tier 3 (the tables + the SMT pass) at the four values first, then N.
5. Tier 4 (the fuzzers) once the boot is stable.
6. The formal specifications last, against the invariants the earlier
   tiers proved empirically.

Each tier's acceptance is a hard gate: a tier that does not pass blocks
the next, and a certificate is issued only for an artifact that passed
all four.
