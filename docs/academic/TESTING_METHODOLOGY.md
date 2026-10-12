<!-- Copyright (c) 2024-2026 Michael Laurence Curzi and 36N9 Genetics, LLC -->
<!-- SPDX-License-Identifier: Apache-2.0 -->

# Testing a paraconsistent, integer-only kernel: why new test suites were needed

*A technical report for researchers in software testing and verification.*

## Abstract

ZXV is a freestanding C11 code base with no floating point in its kernel paths. Some of its
decisions are made in a four-valued logic (TRUE, FALSE, BOTH, NEITHER), and it is required
to keep contradictory states without crashing or acting on them. This report explains why
the usual unit test, which compares an output with one boolean expected value, is not
enough for such a system, and describes the test machinery in the repository. The
machinery has seven parts: exhaustive four-valued oracles rebuilt independently from the
stated rules; tests that a permitted contradiction stays BOTH while a hard invariant fails
closed; order-independence tests that permute the arrival of inputs to a paradox graph;
bit-identity tests across CPUs and kernel sets; a mutation harness whose CI gate requires
tests to kill mutants; sanitizer, fuzzing and coverage campaigns; and hand-written TLA+ and
Lean models, some of which are required to produce a counterexample. For each claim we
give the file that supports it and say whether it is measured, tested, only implemented,
or design intent. Known weaknesses are listed: the lpres connectives depart from Belnap's
lattice, some golden vectors come from the code itself, the formal models are not linked
to the C code by any refinement proof, and the stated 13:1 test-to-code target is far
from the measured 0.36:1.

## 1 Introduction and scope

A conventional test is a predicate $O(i) = [\,P(i) = e(i)\,]$ with an expected value
$e(i)$ that is usually boolean or scalar. Two features of ZXV break this pattern.

1. **Results are four-valued.** The attestation layer `kernel/src/lpres/lpres.h` defines
   `lpres_state_t` with `LPRES_STATE_NEITHER`, `_TRUE`, `_FALSE` and `_BOTH`. A test that
   checks only "true or not true" cannot tell BOTH (contradictory evidence) from NEITHER
   (no evidence). The two must lead to different actions.
2. **Contradiction is a normal state, not an error.** The system must hold BOTH
   indefinitely, route it (wait, escalate, quarantine), and still keep hard invariants such
   as double-entry conservation. So the test suite has to check two kinds of property at
   the same time: *tolerance* properties (a contradiction is kept and does not spread) and
   *safety* properties (a broken invariant is refused and rolled back).

This report covers the test design for the four-valued layer (`lpres`, `lpres_hex`), the
paradox engine (`kernel/src/paradox/`), and the general quality machinery they rely on.
Plain-language background is in
[MULTI_VALUED_LOGIC.md](../supplemental/MULTI_VALUED_LOGIC.md) and
[INTEGER_ONLY_KERNEL.md](../supplemental/INTEGER_ONLY_KERNEL.md). Project-wide claims and
their evidence are in [CLAIMS_AND_EVIDENCE.md](../supplemental/CLAIMS_AND_EVIDENCE.md).

**On quantum terminology.** Some source comments use quantum vocabulary: for example,
`TRIT_GLUT_NEUTRAL` in `kernel/include/m5_types.h` is commented as a "balanced
superposition". ZXV does not turn a classical computer into a quantum computer and claims
no quantum speedup. It runs on classical hardware. BOTH is a discrete label for
contradictory evidence. It is not a complex amplitude, and there is no interference.
"Post-quantum" in this report means only the NIST-standardised algorithms (FIPS 203
ML-KEM, FIPS 204 ML-DSA, FIPS 205 SLH-DSA), which are tested against published vectors
(§8).

## 2 Definitions

**Definition 2.1 (value set).** $\mathbf{4} = \{t, f, b, n\}$. In
`lpres_state_t` these are TRUE = 1, FALSE = 2, BOTH = 3 and NEITHER = 0. Evidence fixes
the state: `lpres_add_evidence` sets $b$ when both the positive and the negative count
are non-zero, $t$ or $f$ when only one is, and $n$ when neither is (`kernel/src/lpres/lpres.c`).

**Definition 2.2 (connectives as implemented).** $\neg$ swaps $t$ and $f$ and leaves $b$
and $n$ fixed (`lpres_negate`). `lpres_conjoin` returns $f$ if either side is $f$, treats
$t$ as the identity, and otherwise returns $n$ if either side is $n$. `lpres_disjoin` is
the dual. Reading these as lattice operations gives the **chain** $f < n < b < t$, with
$\wedge = \min$ and $\vee = \max$. Belnap's logic FOUR instead uses the truth order, in
which $b$ and $n$ are incomparable and $b \wedge n = f$, $b \vee n = t$ (Belnap 1977).
The source comment in `lpres.c` says "Belnap's 4-valued logic", but the code does not
implement Belnap's tables. Because $n < b$ while $\neg n = n < b = \neg b$, negation does
not reverse the order on $\{b, n\}$, and De Morgan's law fails exactly for that pair:
$\neg(b \wedge n) = n$ but $\neg b \vee \neg n = b$. The test suite records this rather
than hiding it (§3).

**Definition 2.3 (64-state tag).** `lpx_t` in `kernel/src/lpres/lpres_hex.h` is a 6-bit
value made of three 2-bit fields: BASE (the lpres state), POLARITY (entropic, neutral,
FORBIDDEN, negentropic) and PHASE (a Gray-coded lifecycle). The function
`lpx_action(t)` maps each tag to one of PROCEED, WAIT, REFUSE, ESCALATE or ROLLBACK.

**Definition 2.4 (tolerance and safety properties).** A *tolerance property* says that a
contradictory input is preserved and contained, for example: BOTH never gives actuator
authority, and a neutral hold never settles early. A *safety property* says that a hard
invariant cannot be broken, for example: FORBIDDEN is absorbing, and $A = L + E$ holds
after every pass. A four-valued test suite needs oracles for both kinds.

**Definition 2.5 (mutation score).** For the mutants $M$ of a target that compile,
$\text{score} = (\text{killed} + \text{timeout}) / (\text{killed} + \text{timeout} +
\text{survived})$ (`tests/MUTATION_REPORT.md`).

## 3 Four-valued oracles

**Exhaustive enumeration with an independent rule oracle.** `kernel/src/lpres/test_lpres_hex.c`
does not compare `lpx_action` with values copied from its own table. It restates the
rules of header section X2 as a separate function, `oracle(base, pol)`, and checks all
64 tags against it. It also checks the following:

- exactly 16 tags roll back (the FORBIDDEN ones);
- the phase never changes the action;
- `lpx_and` and `lpx_or` follow `lpres_conjoin` and `lpres_disjoin` on the base for all
  $64^2 = 4096$ pairs, take the worst polarity, and take the least advanced phase;
- `lpx_not` is an involution that keeps polarity and phase;
- `lpx_polarity` matches the margin rule $|p_1 - n_1|$ versus $|p_0 - n_0|$ for every
  count in $[0,5]^4$, and evidence alone never produces FORBIDDEN.

The output space is small and fully enumerated, so the test can be complete instead of
sampled. This is the main methodological difference from boolean testing. The oracle
covers every value of the domain, not just one expected bit.

**Recording a known deviation instead of masking it.** The De Morgan check in
`test_combine` exempts exactly the pair $\{b, n\}$. The comment says that lpres's tables
break the law for that pair, and that this is deliberate, because Belnap's tables would let a
contradiction plus a gap authorise. In effect the test states a design decision in executable
form. If someone changes the tables, the exemption is the line that has to change.

**Separating BOTH from NEITHER at the gate.** `tests/host/test_lpres.c` checks that
`lpres_implies_actuator_authority` is false for $b$, $f$ and $n$, that
`lpres_safety_gate_clear` is blocked by a BOTH and also by a NEITHER attestation, and
that revoking the contradictory attestation clears the gate.

The same file also fixes one behaviour a reviewer may question: an *empty* registry
passes the gate (`"empty registry: gate clear"`). This is vacuous truth over zero
attestations. The bring-up comment in `lpres.c` says that "nothing attested must not
read as everything permitted", but the bring-up function checks
`lpres_count_contradictions`, not the gate. The two statements are not in direct
conflict, but the test pins the vacuous case as correct, and whether it should be is a
design question.

**Two encodings coexist.** A separate, older presence lattice, `lpres_core.h`
(`trit_t`, with `TRIT_GLUT` aliases), is tested by `kernel/src/lpres/test_lpres.c`. That
test asserts that an unattested resource reads as `TRIT_FALSE`. In `lpres.h` the
unattested state is NEITHER. The two APIs declare different capabilities (`lpres.c`,
DECLARATION block), and each has its own tests. Researchers comparing results should not
treat the two as one logic.

## 4 Holding contradiction while failing closed

The tests check the two property classes of Definition 2.4 in the same runs.

| Property | Class | Test (function) | File |
|---|---|---|---|
| Conflicting evidence gives BOTH, and further positive evidence leaves it BOTH | tolerance | `add_evidence_test` | `tests/host/test_lpres.c` |
| A converging dispute waits; a stuck or diverging one escalates to a person | tolerance | `test_actions` | `kernel/src/lpres/test_lpres_hex.c` |
| Neutral hold keeps every branch and never settles early | tolerance | `test_neutral` | `kernel/src/paradox/test_paradox.c` |
| Special (unclassified) records stay byte-identical after review | tolerance | `test_special` | same |
| FORBIDDEN cannot be left by any `lpx_set`, step, `lpx_not`, `lpx_and` or `lpx_or` | safety | `test_forbidden` | `test_lpres_hex.c` |
| Forbidden overrides every kind and every operator | safety | `test_forbidden` | `test_paradox.c` |
| Quarantine log: any altered field, re-hashed forgery, truncation or altered head is detected; a full log refuses | safety | `test_qlog` | `test_lpres_hex.c` |
| $A = L + E$ after every pass of 300 seeded rounds with injected one-sided postings; every parameter stays in its bounds | safety | `test_conservation` | `test_paradox.c` |

`test_conservation` also requires that containment, apply, proposal and rollback paths
all ran, guarding against a vacuous pass.

In the complex-paradox graph, the requirement that a contradiction should not spread is
tested as *bounded taint*. In the three-peer scenario of `test_determinism`, the injected
node must end `PDX_NS_CONTAINED` and its dependant `PDX_NS_TAINTED`, while unrelated nodes
end `PDX_NS_APPLIED` or `PDX_NS_HELD`. This is a specific expected *vector* of states,
not a single pass/fail value.

## 5 Order independence

Peers receive the same evidence in different orders, so a resolver whose result
depended on arrival order would let them disagree. `paradox.h` removes the dependence:

- nodes are sorted by `id` (the hash of the signed input) before each pass;
- several updates for one node in one batch merge by "worst wins" (forbidden >
  destructive > special > neutral > constructive > none; for tension, the larger
  magnitude, then the larger direction).

A merge that is commutative, associative and idempotent (a join on a finite order) makes
the result of a batch independent of permutation. The *argument* is ours. The repository
tests the property empirically:

- **1,000 seeds** (`test_determinism`): each scenario has 6 to 24 nodes, up to 4
  dependencies each, and three update batches. It runs once in the given order and once
  with the nodes and every batch shuffled by a Fisher-Yates permutation. The node states,
  the pass chain (SHA-256), the log head and the final outcome must be identical. The
  scenarios must reach every node state except OPEN, and both endings (RESOLVED and
  ESCALATE_TO_OWNER), so the property is not checked only on trivial graphs.
- **Sensitivity and peers.** A one-bit change in one input must change the chain (else a
  constant chain would pass). Three peers that classify the same evidence with
  `pdx_classify` in different orders must agree on states, chain and log head.

This is randomised testing, not a proof: 1,000 samples from a space that grows as $n!$.

## 6 Determinism and bit identity

Integer-only arithmetic makes exact equality a usable oracle across CPUs. Floating-point
code needs a tolerance $\varepsilon$ for the same comparison. The repository has four
layers of such checks.

- **Kernel equivalence.** `kernel/src/tensor/test_zt_simd.c` checks every SIMD kernel set
  (AVX2, AVX-512, NEON) and the thread pool against the portable C reference, bit for
  bit. The checks include extreme values and every tail length, and whole forward passes
  of three fixture models. `verify-all` also cross-compiles the test for aarch64 and runs
  it under `qemu-aarch64` when the tools are present.
- **Real model.** `bench/results/2026-10-10-qwen2.5-1.5b/REPORT.md` §1.2 and §1.5
  report the same hash over every logit for the C, AVX2 and AVX-512 kernels on x86_64
  and the C and NEON kernels on arm64. The arm64 run used qemu emulation, not arm64
  hardware (CLAIMS_AND_EVIDENCE.md §1).
- **No hidden state.** `tests/03_metamorphic/meta.h` runs each metamorphic relation
  forward, again, and in reverse order. It requires the same FNV-1a digest of all outputs
  each time, so a module that keeps static state between calls is caught.
- **Cross-endian known answers.** The `kat-cross` target (`build_system/kat_cross.sh`)
  runs the PQ known-answer tests on aarch64, riscv64, armhf, i686 and big-endian s390x
  under qemu-user.

## 7 Mutation testing: tests must kill mutants

Coverage shows that code ran. It does not show that a test would notice if the code were
wrong (Inozemtseva and Holmes 2014). ZXV therefore uses mutation analysis
(DeMillo et al. 1978; Jia and Harman 2011).

**Harness.** `tests/harness/mutator/mutate.py` is deterministic. It masks comments,
literals and preprocessor lines, then applies one-token operators: `rel` (`< <=`, `> >=`, `== !=`), `arith` (operator swaps),
`const` ($N \to N \pm 1$), `delchk` (delete a guard), `ret` (negate a literal return
code) and `cond` (a condition becomes 1 or 0). Each mutant runs in a private copy of the tree. `--selftest` checks the scanner. Targets
and test commands are listed in `tests/harness/mutator/targets.json`.

**Interaction with known bugs.** The tiered harness `tests/01_root_axioms/tier.h` provides
`CHECK_KNOWN(id, …)`. It reports a known, documented bug as `[XFAIL]`, and an entry that
no check exercises as `[STALE]`, which fails the run. With `TIER_STRICT=1`, an `[XPASS]`
also fails. The mutation harness sets `TIER_STRICT=1`, so a mutant that accidentally
"fixes" a known bug counts as killed. The boundary generators (`tier_bounds_u64`,
`tier_bounds_i64`) supply $\{-1, 0, 1, M-1, M, M+1\}$ for every parameter.

**Measured results.**

- Over ten targets, the *before* score (module tests only) was 50.6% (1436 of 2839) and
  the *after* score (with the tiered tests) was 89.4% (2538 of 2839), on the same mutant
  set (`tests/MUTATION_REPORT.md`). Every remaining survivor is listed with a reason:
  equivalent, or reachable only with fault injection or a signing key.
- For the four-valued layer, `lpres_hex` scores 184 of 211 (87.2%). Its 27 survivors are
  classified as equivalent (`targets.json`, `baseline_note`), and
  `docs/SYSTEM_REFERENCE.md` gives the same 87.2%.
- `paradox` and `paradox_graph` are mutation targets; no score for them is recorded.

**CI gate.** The `mutation-subset` job in `.github/workflows/ci.yml` runs
`mutate.py --ci --shard I/3`. It fails if any gated target drops below its baseline:
rmag_core 0.9555, fs_malloc 0.8333, settle 0.8883, zxv_engine 0.8567, lpres_hex 0.8720,
mk_pay 0.9153, vhub 0.9493, swarm_denom 0.9893 and zxvd_rpc 0.7400, the last run in
`zxvd.yml`. Rules for which jobs run when are in [docs/CI.md](../CI.md).

**The 13:1 target.** The owner's stated goal is a test-to-code ratio of 13:1, *counted
only for tests that fail when the code breaks, as shown by the mutation gate*
(`docs/ARCHITECTURE.md` §15, "How the ratio is counted"). This is **design intent**. The
measured value in the repository is **0.36:1**, "Measured on c971139"
(`docs/GAP_AUDIT.md` row B7; also `docs/ARCHITECTURE.md` §1 and §15). Neither document
says whether that measurement applied the mutation-killing filter.

## 8 Further machinery

**Sanitizers.** `tests/harness/tiers.py --mode san` builds every tiered suite with ASan,
LeakSanitizer and UBSan (`-fno-sanitize-recover=all`); `TIER_UB_KNOWN` records known
undefined behaviour without executing it under a sanitizer. `test-lpres-hex` and
`test-paradox` are always built with ASan and UBSan, and the paradox target also builds
freestanding for aarch64 (`-mgeneral-regs-only`) and i386, failing if any external
symbol other than `lpx_*`, `pdx_*` or `sha256*` appears. CI runs `sanitize-asan-ubsan`, `sanitize-integer` and `sanitize-msan`
  (`fuzz/FUZZ_REPORT.md`, "Sanitizer jobs"). There is no TSan job, because nothing in
  `kernel/src` creates threads.

**Fuzzing.** `fuzz/FUZZ_REPORT.md` (campaign of 2026-10-10) reports $10^7$ executions
each for four parser harnesses, 300 s runs for nine more, 222,417,084 ledger operations
each followed by a full invariant check, and seven findings (F1 to F7), each fixed with a
regression input. The report also states a limitation of its property oracle: the harness counted a
refused payment as correct, so it could not see a fee-rounding refusal that the libzxv
pipeline test later found. No fuzz harness has `lpres_hex` or the paradox engine as its
target. `lpres.c` and `lpres_core.c` are linked into some economy harnesses only as
dependencies (`fuzz/Makefile`).

**Coverage.** `docs/COVERAGE.md` (clang 18.1.3, 2026-10-10) measures source-based line,
branch and MC/DC coverage over `verify-all` plus a fuzz-corpus replay: 87.0% line,
67.9% branch, 88.5% function and 32.2% MC/DC over all linked modules, and for `lpres`
89.2% line, 75.2% branch and 48.8% MC/DC over 41 conditions. The paradox engine was
added after that measurement (commit 27132b5) and is not in the table. Of the 36 modules
linked by no test, `mm` has a mutation score of 91.4% in
  `tests/MUTATION_REPORT.md` through the separately run tiered test `t1_alloc`. The
  coverage run does not include the tiered suites, which may explain the difference.

**Golden vectors.** `kernel/src/hwemu/hwe_vectors.h` is generated by `gen_hwe_vectors.c`.
`kernel/tools/check_hwe_vectors.py` checks the SHA-256, SHA-3/SHAKE, HMAC and gcd vectors
against Python's `hashlib`, `hmac` and `math.gcd`, which are independent oracles. The
same script says that the `LOGIC64` (four-valued, 64-lane) and tensor vectors "are
defined by the open C reference itself and are not re-derived". For the logic lanes they
are therefore regression vectors, not independent oracles.

**Post-quantum known answers.** `kernel/src/pqsec/test_pq_kat.c` (ML-DSA-65,
SLH-DSA-SHAKE-128s) and `test_pq_matrix.c` (ML-KEM-1024, ML-DSA-87, SLH-DSA-SHAKE-256s)
check NIST ACVP vectors, including modified-signature rejection cases; HQC-5 uses its
official v5.0.0 KATs and X25519 uses RFC 7748. `test_pq_kat.c` notes that the stand-in it
replaced passed its own round-trip test while being forgeable: self-consistency is not
correctness.

**Formal models.** `proofs/` contains TLA+ models of the ledger, checked by TLC over
stated small bounds, and Lean 4 transcriptions of integer recurrences and rational
arithmetic. `proofs/check.sh` fails if an *expected counterexample* is not found:
`MC_ledger_replay_wrap.cfg` and `MC_triple_ledger_trial.cfg` must fail. This is an
executable oracle for a known-false property. `proofs/README.md` says the models are
"written by hand" with "no refinement proof linking them to the C source or the binary".
No model covers `lpres` or the paradox engine.

**ROM-based stress harness.** `build_system/game_master.py` boots the kernel in QEMU with
each ROM image attached as a virtio-blk device. It drives a seeded input storm (the seed
comes from a SHA-256 of the ROM path) and logs every `[FAULT]` with the symbolised
function. It also runs each image byte-complemented, which the harness calls the
"negative space". The rationale is in
[WHY_GAME_ROM_ENGINES.md](../supplemental/WHY_GAME_ROM_ENGINES.md). No ROMs are committed,
the harness is not in CI, and no results of a campaign are recorded in the repository.

## 9 Status of claims

| Claim | Status | Evidence |
|---|---|---|
| All 64 tags' actions match an independently written rule oracle; FORBIDDEN is absorbing | Tested (host and CI, `verify-all`) | `kernel/src/lpres/test_lpres_hex.c`; `kernel/Makefile` `test-lpres-hex` |
| Combination laws hold for all 4096 tag pairs; De Morgan fails only for $\{b,n\}$ | Tested | `test_lpres_hex.c` `test_combine` |
| lpres connectives implement Belnap's FOUR | Not the case; source comment and code disagree (§2) | `kernel/src/lpres/lpres.c` |
| BOTH and NEITHER block the safety gate and actuator authority | Tested | `tests/host/test_lpres.c` |
| Paradox graph results are invariant under permuted arrival (1,000 seeds, 3 peers) | Tested (randomised, not proved) | `kernel/src/paradox/test_paradox.c` `test_determinism` |
| $A = L + E$ holds after every pass under injected attacks | Tested (toy ledger, 300 rounds) | `test_paradox.c` `test_conservation` |
| test_lpres_hex 76 checks and test_paradox 246 checks pass | Tested (local run while writing this report; 76 also in `docs/SYSTEM_REFERENCE.md`) | `make -C kernel test-lpres-hex test-paradox` |
| SIMD kernels equal the C reference bit for bit | Tested in CI | `kernel/src/tensor/test_zt_simd.c` |
| Same logit hash across x86_64 kernels and arm64 (emulated) | Measured | `bench/results/2026-10-10-qwen2.5-1.5b/REPORT.md` §1.2, §1.5 |
| Mutation score rose from 50.6% to 89.4% over ten targets | Measured | `tests/MUTATION_REPORT.md` |
| lpres_hex mutation score 87.2% (184/211), gated in CI | Measured; gate tested in CI | `tests/harness/mutator/targets.json` |
| Paradox engine mutation score | Implemented, not evaluated (target listed, no score recorded) | `targets.json` |
| Line 87.0%, branch 67.9%, MC/DC 32.2% | Measured | `docs/COVERAGE.md` |
| Parsers fuzzed to $10^7$; 222M ledger operations clean | Measured | `fuzz/FUZZ_REPORT.md` |
| PQ algorithms match NIST ACVP vectors, also on big-endian s390x | Tested in CI | `kernel/src/pqsec/test_pq_kat.c`, `test_pq_matrix.c`; `kat-cross` |
| Ledger invariants hold in TLA+ models; required counterexamples are found | Tested in CI (models only, small bounds) | `proofs/README.md`, `proofs/check.sh` |
| hwe hash and gcd vectors agree with independent implementations | Tested | `kernel/tools/check_hwe_vectors.py` |
| ROM stress harness finds kernel faults | Implemented, not evaluated | `build_system/game_master.py` |
| 13:1 mutation-killing test-to-code ratio | Design intent (measured 0.36:1) | `docs/ARCHITECTURE.md` §15; `docs/GAP_AUDIT.md` B7 |

## 10 Limitations and threats to validity

1. **Oracle provenance.** Exhaustive four-valued tests are only as good as the rules they
   restate. The independent `oracle()` in `test_lpres_hex.c` is written by the same
   project from the same header. The lpres base tables are tested *against themselves*:
   `lpx_and` is compared with `lpres_conjoin`, not with an external reference. The
   difference from Belnap's FOUR (§2) shows that "four-valued" does not fix a single
   semantics.
2. **Golden vectors from the implementation.** The LOGIC64 and tensor vectors in
   `hwe_vectors.h` detect regressions, not errors that were present when they were
   generated.
3. **Sampling, not proof.** Order independence is checked on 1,000 seeded permutations,
   and conservation on 300 seeded rounds of a toy ledger, not on `pay_ledger`. No formal
   model covers `lpres` or `paradox`.
4. **Equivalent mutants are classified by hand.** Survivor reasons in
   `MUTATION_REPORT.md` and `targets.json` are human judgements. The `zxv_engine` score
   differs by machine: 334/377 measured locally against 323/377 on the CI runner, because
   some mutants read uninitialised stack. A mutation score depends on the platform as well
   as the tests.
5. **Documents differ in time.** `docs/AUDIT_REPORT.md` reports 3,984 passing checks, and
   `docs/ARCHITECTURE.md` reports 4,515. `tests/MUTATION_REPORT.md` says the gate "has not
   yet been executed on GitHub from this branch", while `ARCHITECTURE.md` reports it green
   on `94fbd0d`. These are snapshots from different commits.
6. **Coverage scope.** The coverage run excludes the tiered suites and predates the
   paradox engine. Its MC/DC figure is low, and the document itself says it implies no
   compliance claim.
7. **Formal models are not refined.** TLC results hold only for the bounds in each
   `.cfg` file.

## 11 Relation to prior work

The four-valued setting follows Dunn (1976) and Belnap (1977); bilattices (Ginsberg 1988)
and the logic of paradox (Priest 1979) are related. ZXV's contribution is engineering
(action tables and audit logs on top of such values); it claims no new logic. The oracle
problem (Weyuker 1982; Barr et al. 2015) is addressed here by exhaustive enumeration of
small finite domains. The forward/again/reverse relation in `meta.h` and the permutation
tests are metamorphic relations (Chen et al. 1998; Segura et al. 2016). Mutation analysis
follows DeMillo et al. (1978), with operators close to the sufficient set of Offutt et
al. (1996); Jia and Harman (2011) and Papadakis et al. (2019) survey the field. The seeded
scenarios resemble QuickCheck (Claessen and Hughes 2000). ASan follows Serebryany et al.
(2012), MC/DC follows Chilenski and Miller (1994), TLA+ follows Lamport (2002) and Lean 4
follows de Moura and Ullrich (2021). Unlike seL4 (Klein et al. 2009), there is no
refinement proof to the C code.

## References

- Barr, E. T., Harman, M., McMinn, P., Shahbaz, M., Yoo, S. (2015). The oracle problem in software testing: a survey. *IEEE Transactions on Software Engineering* 41(5), 507–525.
- Belnap, N. D. (1977). A useful four-valued logic. In J. M. Dunn, G. Epstein (eds.), *Modern Uses of Multiple-Valued Logic*, Reidel, 5–37.
- Chen, T. Y., Cheung, S. C., Yiu, S. M. (1998). Metamorphic testing: a new approach for generating next test cases. Technical Report HKUST-CS98-01, Hong Kong University of Science and Technology.
- Chilenski, J. J., Miller, S. P. (1994). Applicability of modified condition/decision coverage to software testing. *Software Engineering Journal* 9(5), 193–200.
- Claessen, K., Hughes, J. (2000). QuickCheck: a lightweight tool for random testing of Haskell programs. *Proc. ICFP 2000*, ACM.
- de Moura, L., Ullrich, S. (2021). The Lean 4 theorem prover and programming language. *Proc. CADE-28*, LNCS 12699, Springer.
- DeMillo, R. A., Lipton, R. J., Sayward, F. G. (1978). Hints on test data selection: help for the practicing programmer. *Computer* 11(4), 34–41.
- Dunn, J. M. (1976). Intuitive semantics for first-degree entailment and "coupled trees". *Philosophical Studies* 29, 149–168.
- Ginsberg, M. L. (1988). Multivalued logics: a uniform approach to reasoning in artificial intelligence. *Computational Intelligence* 4(3), 265–316.
- Inozemtseva, L., Holmes, R. (2014). Coverage is not strongly correlated with test suite effectiveness. *Proc. ICSE 2014*, ACM.
- Jia, Y., Harman, M. (2011). An analysis and survey of the development of mutation testing. *IEEE Transactions on Software Engineering* 37(5), 649–678.
- Klein, G., et al. (2009). seL4: formal verification of an OS kernel. *Proc. SOSP 2009*, ACM.
- Lamport, L. (2002). *Specifying Systems: The TLA+ Language and Tools for Hardware and Software Engineers*. Addison-Wesley.
- NIST (2024). FIPS 203 (ML-KEM), FIPS 204 (ML-DSA), FIPS 205 (SLH-DSA). National Institute of Standards and Technology.
- Offutt, A. J., Lee, A., Rothermel, G., Untch, R. H., Zapf, C. (1996). An experimental determination of sufficient mutant operators. *ACM Transactions on Software Engineering and Methodology* 5(2), 99–118.
- Papadakis, M., Kintis, M., Zhang, J., Jia, Y., Le Traon, Y., Harman, M. (2019). Mutation testing advances: an analysis and survey. *Advances in Computers* 112, 275–378.
- Priest, G. (1979). The logic of paradox. *Journal of Philosophical Logic* 8, 219–241.
- Segura, S., Fraser, G., Sanchez, A. B., Ruiz-Cortés, A. (2016). A survey on metamorphic testing. *IEEE Transactions on Software Engineering* 42(9), 805–824.
- Serebryany, K., Bruening, D., Potapenko, A., Vyukov, D. (2012). AddressSanitizer: a fast address sanity checker. *Proc. USENIX ATC 2012*.
- Weyuker, E. J. (1982). On testing non-testable programs. *The Computer Journal* 25(4), 465–470.
