<!-- Copyright (c) 2024-2026 Michael Laurence Curzi and 36N9 Genetics, LLC -->
<!-- SPDX-License-Identifier: Apache-2.0 -->

# Paraconsistent reasoning in the ZXV kernel

*A technical report for logicians and philosophers of logic.*

## Abstract

The ZXV kernel records the status of units of work (attestations, ledger postings, model answers, hardware images) in a four-valued set $\mathbf{V} = \{T, F, B, N\}$ ("true", "false", "both", "neither") rather than in $\{T, F\}$. This report reconstructs, from the C source, the truth tables the kernel actually implements, characterises the resulting algebra (a four-element chain with an involution that is not order-reversing), and compares it with Belnap–Dunn FOUR, Priest's LP and strong Kleene K3. The implemented conjunction and disjunction agree with FOUR everywhere except on the pair $\{B, N\}$, where one De Morgan law fails. We then describe the 6-bit tag that extends each value with an evidence-trend "polarity" and a lifecycle "phase", the 64-entry action table that maps tags to actions, and a "paradox engine" that classifies and routes $B$/$N$ items over a dependency graph with an order-independent, hash-chained resolution procedure. Explosion is avoided operationally: there is no inference engine, and the only use of a value is to select an action, of which only $T$ ever authorises. No soundness or completeness proof exists in the repository; we state what is tested and what is not.

## 1 Introduction and scope

The kernel is freestanding, integer-only C11 (see [`../supplemental/INTEGER_ONLY_KERNEL.md`](../supplemental/INTEGER_ONLY_KERNEL.md)). A plain-language companion to this report is [`../supplemental/MULTI_VALUED_LOGIC.md`](../supplemental/MULTI_VALUED_LOGIC.md); the engineering description of the paradox engine is [`../PARADOX_ENGINE.md`](../PARADOX_ENGINE.md). This report covers:

* `kernel/src/lpres/lpres.h`, `lpres.c`: the four-valued attestation registry (type `lpres_state_t`) and its connectives;
* `kernel/src/lpres/lpres_hex.h`, `lpres_hex.c`: the 64-state tag `lpx_t`;
* `kernel/include/m5_types.h` and `kernel/src/lpres/lpres_core.*`: the separate presence type `trit_t` and its "glut" values;
* `kernel/src/paradox/paradox.h`, `paradox.c`, `paradox_graph.c`: the paradox engine.

It does not cover the six-valued truth sets of the swarm and wire layers (`swarm_hk_truth_t` in `kernel/src/swarm/swarm_hk.h`, `CT_*` in `kernel/src/cotier/ct_reconcile.h`, `zt_truth_state_t` in `kernel/src/harmonic/zt_harmonic_logic.h`) beyond Section 6.

*Note on vocabulary.* Some source comments describe glut values as "superposition". This is a metaphor. ZXV runs on classical hardware, does not make a classical computer a quantum computer and claims no quantum speedup. A glut is an ordinary enumerated value; nothing here involves amplitudes or interference.

## 2 The four values and their evidential reading

**Definition 2.1.** `lpres_state_t` (`lpres.h`) has four enumerators: `LPRES_STATE_NEITHER = 0`, `LPRES_STATE_TRUE = 1`, `LPRES_STATE_FALSE = 2`, `LPRES_STATE_BOTH = 3`. We write them $N, T, F, B$.

Each attestation record (`lpres_attestation_t`) carries two counters, `positive_evidence` and `negative_evidence`. `lpres_add_evidence` increments one counter and recomputes the state as

$$
v(p, n) = \begin{cases} B & p > 0 \wedge n > 0 \\ T & p > 0 \wedge n = 0 \\ F & p = 0 \wedge n > 0 \\ N & p = 0 \wedge n = 0 \end{cases}
$$

This is exactly Dunn's reading of the four values as the subsets of $\{\text{told true}, \text{told false}\}$ (Dunn 1976; Belnap 1977), with a counter standing in for each "told". Three implementation facts matter for the semantics:

1. Counters only grow, so under `lpres_add_evidence` alone a record that has reached $B$ stays $B$. Leaving $B$ requires `lpres_revoke` (the record is then ignored by every query) or `lpres_update_state`, which overwrites the state without touching the counters. The state and the counters can therefore disagree after an override; nothing re-checks them.
2. `lpres_attest` seeds the counters from the declared initial state (one positive and/or one negative unit).
3. Out-of-range enumerator values are mapped to $N$ by `lpres_negate` and by `lpx_base_from_lpres` ("never to TRUE", `lpres_hex.h`).

## 3 The connectives as implemented

### 3.1 Truth tables

The tables below are derived by reading `lpres_negate`, `lpres_conjoin` and `lpres_disjoin` in `kernel/src/lpres/lpres.c` and were re-computed by enumerating all 16 argument pairs; every entry is also asserted in `tests/host/test_lpres.c` (`negate_test`, `conjoin_test`, `disjoin_test`).

| $a$ | $\neg a$ |
|---|---|
| $T$ | $F$ |
| $B$ | $B$ |
| $N$ | $N$ |
| $F$ | $T$ |

| $\wedge$ | $T$ | $B$ | $N$ | $F$ |
|---|---|---|---|---|
| $T$ | $T$ | $B$ | $N$ | $F$ |
| $B$ | $B$ | $B$ | **$N$** | $F$ |
| $N$ | $N$ | **$N$** | $N$ | $F$ |
| $F$ | $F$ | $F$ | $F$ | $F$ |

| $\vee$ | $T$ | $B$ | $N$ | $F$ |
|---|---|---|---|---|
| $T$ | $T$ | $T$ | $T$ | $T$ |
| $B$ | $T$ | $B$ | **$B$** | $B$ |
| $N$ | $T$ | **$B$** | $N$ | $N$ |
| $F$ | $T$ | $B$ | $N$ | $F$ |

Bold entries are those that differ from Belnap–Dunn FOUR, where $B \wedge N = F$ and $B \vee N = T$.

### 3.2 Algebraic characterisation

**Proposition 3.1.** Let $F < N < B < T$. Then `lpres_conjoin` is $\min$ and `lpres_disjoin` is $\max$ with respect to this linear order.

*Proof.* By exhaustion over the 16 pairs (done mechanically for this report). $\square$

**Corollary 3.2.** $(\mathbf{V}, \wedge, \vee)$ is a four-element chain, hence a distributive lattice; both operations are commutative, associative and idempotent, and distribute over each other.

**Proposition 3.3.** $\neg$ is an involution but not an order anti-automorphism of the chain: $N < B$ yet $\neg N = N < B = \neg B$. Consequently $(\mathbf{V}, \wedge, \vee, \neg)$ is not a De Morgan algebra. Exactly the argument pairs $(B, N)$ and $(N, B)$ violate De Morgan's laws: $\neg(B \wedge N) = N \neq B = \neg B \vee \neg N$, and $\neg(B \vee N) = B \neq N = \neg B \wedge \neg N$.

The restriction is tested: `test_combine` in `kernel/src/lpres/test_lpres_hex.c` checks De Morgan on the base of all 4,096 tag pairs, excluding exactly the $\{B, N\}$ pairs. The deviation is deliberate: the owner confirmed on 2026-10-12 that the tables stay as they are, for the reason given in Observation 3.4, and the comments in `lpres_conjoin` and `test_combine` record this. Formally, the tables read $N$ as the bottom of the *knowledge* order, used inside a *truth*-order operation.

**Observation 3.4 (the design rationale).** The subset $\{B, N\}$ is closed under $\neg, \wedge, \vee$ in lpres, but not in FOUR, where $B \vee N = T$. Hence in lpres a formula all of whose atoms are $B$ or $N$ never evaluates to $T$. Since $T$ is the only value that authorises an action (Section 4), adopting FOUR's tables would let a disjunction of a contradiction and a gap authorise. This is why the tables were kept.

### 3.3 Comparison with FOUR, LP and K3

| | Values | Designated | $\wedge,\vee$ on $\{B,N\}$ | De Morgan | Explosion $p, \neg p \models q$ |
|---|---|---|---|---|---|
| FOUR / FDE (Belnap 1977; Dunn 1976) | $T,B,N,F$ | $T, B$ | $F$, $T$ | holds | fails |
| LP (Priest 1979) | $T,B,F$ | $T, B$ | n/a | holds | fails |
| strong Kleene K3 (Kleene 1952) | $T,N,F$ | $T$ | n/a | holds | holds (vacuously) |
| lpres, $D = \{T,B\}$ | $T,B,N,F$ | $T, B$ | $N$, $B$ | fails on $\{B,N\}$ | fails ($p=B$, $q=F$) |
| lpres, $D = \{T\}$ (authority) | $T,B,N,F$ | $T$ | $N$, $B$ | fails on $\{B,N\}$ | holds (vacuously) |

Restricted to $\{T, F, N\}$ the lpres tables coincide with strong Kleene; restricted to $\{T, F, B\}$ they coincide with LP's (both by inspection of the tables above). The header title "Paraconsistent Presence States (K3)" does not refer to Kleene's K3: "K3" is a component label (`K3_LPRES`) in the project's specifications, as `lpres.h` shows.

The kernel defines no consequence relation. The last two rows show the two natural choices of designated set. With $\{T, B\}$ the matrix is paraconsistent in the usual sense (explosion and disjunctive syllogism both fail, the latter at $p = B, q = F$). With $\{T\}$, which is what the authority predicate uses, no valuation makes both $p$ and $\neg p$ designated, so explosion holds vacuously. Neither reading is what the kernel relies on, as Section 5 explains.

## 4 The safety gate

Two predicates govern action (`lpres.c`):

* `lpres_implies_actuator_authority(s)` returns true iff $s = T$. $B$, $F$ and $N$ all withhold authority.
* `lpres_safety_gate_clear(reg)` returns true iff every *non-revoked* attestation is certain, i.e. in $\{T, F\}$ (`lpres_is_certain`). One $B$ or one $N$ blocks the gate.

These are different conditions and should not be conflated. The gate does not require every attestation to be $T$: an attestation that something is absent ($F$) is certain and does not block it. Two further properties are tested in `tests/host/test_lpres.c`: an empty registry clears the gate ("empty registry: gate clear"), and revoking a $B$ record clears it ("revoked contradiction: gate clear"). The first is a vacuous-truth reading. It sits uneasily with the comment on `zxvd_lpres_attest_bringup` in `lpres.c` ("nothing attested must not read as everything permitted"); that bring-up function checks an empty registry's contradiction count and lookup, not the gate. Callers that need "at least one positive attestation" must check it themselves.

## 5 How explosion is avoided

*Ex contradictione quodlibet* is a property of a consequence relation. The kernel has no proof calculus, no inference rules and no derivation search, so there is nothing that could derive an arbitrary $q$ from $p \wedge \neg p$. Non-explosion is achieved operationally by three design facts:

1. **Values are attached to units of work, not to a theory.** A $B$ value on one attestation does not propagate to unrelated attestations. Combination happens only where code explicitly calls `lpres_conjoin`, `lpres_disjoin`, `lpx_and` or `lpx_or`.
2. **Values select actions; they do not license inferences.** The only consumer-facing uses are the authority predicate, the gate (Section 4) and the action table (Section 7), and in all three $B$ maps to "do not act".
3. **Contradictions cannot manufacture authority** (Observation 3.4): with the implemented tables no combination of $B$ and $N$ inputs yields $T$.

We are explicit: **there is no formal soundness proof, no completeness result and no mechanised model of the lpres logic in the repository.** The formal work that exists (`docs/FORMAL_INVARIANTS.md`) concerns ledger invariants checked with TLC over bounded models, not this logic. The claims in this section rest on code reading and exhaustive tests over finite domains.

## 6 Other multi-valued types in the kernel

**`trit_t`** (`kernel/include/m5_types.h`) is not three-valued. It has six enumerators denoting five states: `TRIT_FALSE`, `TRIT_TRUE`, and three gluts `TRIT_GLUT_PLUS`, `TRIT_GLUT_MINUS`, `TRIT_GLUT_NEUTRAL` carrying a "charge" of $+1, -1, 0$ (`trit_charge`). `TRIT_GLUT = 2` is a deprecated alias that `trit_canon` maps to `TRIT_GLUT_NEUTRAL`; the rule is "accept TRIT_GLUT anywhere, never emit it", and `trit_canon_is_sound` checks that the canonical image has `TRIT_CANONICAL_COUNT` $= 5$ members. `kernel/src/modbind/modbind.h` refuses trit-typed module boundaries while the alias stands, because two encodings of one state make marshalling non-injective. `trit_t` has **no gap value**: `lpres_get_presence` (`lpres_core.c`) returns `TRIT_FALSE` for an unattested resource, a closed-world reading, whereas `lpres_state_t` returns $N$ for the same situation. The two presence systems are separate APIs in one directory (see the declaration comments at the end of `lpres_core.c` and `lpres.c`). `trit_t` defines no connectives; the one combiner located is `dvaitadvaita_resolve` (`kernel/src/dharana/dharana.c`), which returns $a$ if $a = b$ and otherwise a glut whose sign is the sum of the two charges, so `TRUE` with `FALSE` gives `TRIT_GLUT_NEUTRAL`, analogous to the knowledge-order join $T \sqcup F = B$ in a bilattice.

**Six-valued sets.** `swarm_hk_truth_t` and the wire's `zt_truth_state_t` add `NEUTRAL` (evidence silent) and `PARADOX` (persistent contradiction) to $T, F$, glut and unknown. Their conjunction differs from lpres: `swarm_hk_and` returns `GLUT` for $T \wedge F$, an evidence-aggregation rather than a truth-functional reading. `zt_harmonic_logic.h` documents explicit, lossy maps between these sets and `lpres_state_t` and states that it "does not pick one silently". A reader must check which value set a given module uses.

## 7 The 64-state tag `lpx_t`

**Definition 7.1** (`lpres_hex.h`). A tag is a 6-bit value $t = (b, \pi, \varphi)$ with base $b$ in bits 5–4, polarity $\pi$ in bits 3–2 and phase $\varphi$ in bits 1–0:

* base: $F = 00$, $N = 01$, $B = 10$, $T = 11$ (a different numbering from `lpres_state_t`, converted by `lpx_base_from_lpres` / `lpx_base_to_lpres`);
* polarity: entropic $00$, neutral $01$, forbidden $10$, negentropic $11$;
* phase: potential $00$, emerging $01$, dissolving $10$, manifest $11$.

The header compares the layout to reading an I Ching hexagram from the top line; this is a naming convention, not a semantic commitment. Rules X1–X6:

* **X1** A tag belongs to a unit of work, never to every byte of data or to tensor weights.
* **X2** The action is one lookup, `lpx_action(t)`, in a 64-entry constant table (`ACTION` in `lpres_hex.c`).
* **X3** Phase moves one step at a time on the Gray cycle potential $\to$ emerging $\to$ manifest $\to$ dissolving $\to$ potential (one bit flip per step); `lpx_advance` refuses a jump.
* **X4** Forbidden is absorbing: `lpx_set` keeps forbidden polarity whatever it is asked to set; only a fresh tag starts clean.
* **X5** `lpx_and`/`lpx_or` apply `lpres_conjoin`/`lpres_disjoin` to the base, take the worse polarity under forbidden $>$ entropic $>$ neutral $>$ negentropic, and the less advanced phase; `lpx_not` negates the base and keeps polarity and phase.
* **X6** `lpx_qlog_append` writes forbidden tags only to an append-only SHA-256 hash chain (domain string `"zxv/lpx-qlog/v1"`); a full log refuses the append and the caller must halt.

**Polarity** is computed by `lpx_polarity` from evidence counts before $(p_0, n_0)$ and after $(p_1, n_1)$: stale evidence gives entropic; otherwise, with margin $m = |p - n|$, $m_1 > m_0$ gives negentropic, $m_1 < m_0$ entropic, and equality neutral. It never returns forbidden; forbidden is set only by an explicit `lpx_forbid` after a real check fails.

**The action table** (phase never changes the action, so 16 rows suffice):

| base \ polarity | entropic | neutral | forbidden | negentropic |
|---|---|---|---|---|
| $T$ | WAIT | PROCEED | ROLLBACK | PROCEED |
| $F$ | REFUSE | REFUSE | ROLLBACK | REFUSE |
| $B$ | ESCALATE | ESCALATE | ROLLBACK | WAIT |
| $N$ | WAIT | WAIT | ROLLBACK | WAIT |

Thus only $T$ proceeds, and only when its evidence is not weakening. $B$ waits while its dispute is converging and escalates to a person otherwise. $N$ always waits; it never escalates and never proceeds. Exactly 16 entries roll back. `test_actions` rebuilds the table from these rules entry by entry and checks phase-independence.

**Remark 7.2.** Because polarity is combined by "worst wins", a forbidden component taints any conjunction or disjunction it enters, independently of the base computation. This is a second, non-truth-functional channel layered on the four-valued one; it is closer to an information-flow label than to a truth value.

## 8 Safety invariants are never $B$

`docs/ARCHITECTURE.md`, rule R5, states: "Hard invariants are always FALSE when broken, never Both. Only genuinely allowed disagreements (e.g. GAAP vs IFRS treatment) become Both." The intent is that a broken safety invariant (arithmetic overflow, a key leak, the accounting identity $A = L + E$, a memory-safety violation) fails closed and is never held as a tolerable contradiction. In the code the rule is realised through the forbidden polarity rather than through the base alone:

* `kernel/src/hwemu/hwr.c` and `kernel/src/aipi/aipi.c` quarantine with base $F$ and forbidden polarity (signature failure; bytes that do not hash to their content identifier).
* `kernel/arch/hosted/zxv_answer_tag.c` marks a model answer forbidden when the forward pass errs or logits saturate (an overflow), but with base $N$, not $F$. This departs from the letter of R5 while keeping its effect, because every forbidden tag rolls back whatever its base.
* `lpres_hex.h` ("HONEST LIMITS") states that forbidden on a memory access means a bounds check or sanitizer caught it, and on a posting that the ledger's own $A = L + E$ check refused it.

We located no code path that tags a key leak. More importantly, the rule is a convention, not a type-level guarantee: `lpres_attest` and `lpres_update_state` accept $B$ for any subject, including `LPRES_SUBJECT_KEY`. What *is* guaranteed by the tag layer is that once a check sets forbidden, no `lpx_set`, `lpx_and`, `lpx_or`, `lpx_not` or `lpx_advance` removes it (tested in `test_forbidden`), and the paradox engine classifies a forbidden tag as `PDX_KIND_NONE` with `forbidden = true`, so every operator refuses and rolls back.

## 9 The paradox engine

`kernel/src/paradox/` treats a $B$ or $N$ item "one level up": it never decides whether the claim is true.

**Classification** (`pdx_classify`, `pdx_kind_of_tag`). $T$ and $F$ are not paradoxes (`PDX_KIND_NONE`). $N$ is `NEUTRAL`. $B$ is `CONSTRUCTIVE` if the evidence margin grows, `DESTRUCTIVE` if it shrinks or is stale, `SPECIAL` if the trend is neutral at the potential phase, and `NEUTRAL` otherwise. A signature (SHA-256 of a caller-supplied canonical description, domain `"zxv/pdx-sig/v1"`) found in the append-only registry overrides the trend.

**Operators.** `pdx_constructive` applies a parameter update only if every value lies in published bounds $[lo, hi]$, keeps the previous values, and rolls back at the next evaluation if tension grew; its API has no balance input. `pdx_destructive` trips on a step limit, budget, repeated state fingerprint or caller invariant, quarantines in the X6 log, restores a checkpoint and fills a `pdx_repair_t` proof. `pdx_neutral` holds up to $N$ candidate branches and settles only when one leads every other by a set margin of net evidence, or the owner picks. `pdx_special` writes a permanent frontier record; `pdx_draft` and `pdx_review` turn it into a new registry entry, requiring owner approval for drafts flagged money or device. Each operator appends to a hash-chained `pdx_log_t` (`pdx_log_verify` recomputes every link).

**Complex paradoxes** (`paradox_graph.c`). Up to `PDX_GRAPH_MAX` $= 64$ nodes with at most 4 dependencies each are sorted by id (the hash of the signed input), and dependencies are stored as 64-bit masks over that order. A pass (`pdx_graph_pass`) proceeds in fixed priority: destructive and forbidden nodes are contained and the transitive closure of their dependants is tainted (sticky); special nodes are sandboxed and their dependants wait; neutral nodes are held; a constructive node is applied only if no contained, waiting or held node reaches it. At `step_limit` passes the graph escalates to the owner.

**Order independence.** Three properties give it: (i) the canonical order is a function of the node *set*; (ii) concurrent updates to one node merge by a "worst wins" maximum (forbidden $>$ destructive $>$ special $>$ neutral $>$ constructive $>$ none; tension by larger magnitude, then direction), which is commutative, associative and idempotent, i.e. a join-semilattice merge in the sense used for replicated data types (Shapiro et al. 2011); (iii) a pass is a deterministic function of the sorted state. The test `test_determinism` (`kernel/src/paradox/test_paradox.c`) checks, for 1,000 generated scenarios, that permuting the node list and the updates *within* each batch yields identical node states, chain and log head; the *sequence* of batches is held fixed. A three-peer simulation with different arrival orders is also tested. Order independence across differently sequenced batches is not claimed by the tests and was not analysed here.

## 10 Why new tests were needed

Conventional unit tests assert a Boolean outcome. Here the object under test is a finite algebra and a policy table, so the tests are exhaustive enumerations over finite domains: all 16 base pairs, all 64 tags, all 4,096 tag pairs for X5 (commutativity, involution of `lpx_not`, De Morgan with its stated exception), tables rebuilt from their rules, absorption of forbidden under every operation, and hash-chain tamper tests that alter each field. Determinism tests replace "the right answer" with "the same answer under permutation". These are model-checking-style properties over small domains, not proofs over arbitrary formulas.

## 11 Status of claims

| Claim | Status | Evidence |
|---|---|---|
| Truth tables of Section 3.1 | Tested (host) | `tests/host/test_lpres.c`; 22/22 pass, run for this report 2026-10-12 |
| Connectives are min/max on $F<N<B<T$; distributive | Implemented, not evaluated in repo (checked by exhaustion for this report) | `kernel/src/lpres/lpres.c` |
| De Morgan fails exactly on $\{B,N\}$ | Tested (host) | `kernel/src/lpres/test_lpres_hex.c` (`test_combine`) |
| Only $T$ grants authority; gate needs all live records in $\{T,F\}$; empty registry clears gate | Tested (host) | `tests/host/test_lpres.c` (`safety_gate_*`) |
| 64-entry action table follows its rules; phase-independent; 16 rollbacks | Tested (host) | `test_lpres_hex.c` (`test_actions`); 76 passed, run 2026-10-12 |
| Forbidden is absorbing; qlog tamper-evident | Tested (host, ASan/UBSan) | `test_lpres_hex.c`; `make -C kernel test-lpres-hex` |
| Paradox operators, registry, graph order independence within batches | Tested (host, ASan/UBSan) | `test_paradox.c`; 246 passed, run 2026-10-12 |
| Paradox engine builds freestanding (aarch64, i386), no libc symbols | Tested (CI target) | `kernel/Makefile` target `test-paradox` |
| lpres line/branch coverage 89.2% / 75.2% (4 files) | Measured | `docs/COVERAGE.md` (2026-10-10); no row for `paradox` |
| `lpres.c` linked in the arm64 image; `lpres_hex.c` and paradox not linked into any image | Implemented | `build_system/Makefile.arm64`; `../supplemental/MULTI_VALUED_LOGIC.md` |
| Hard invariants never $B$ | Design intent (convention; partly realised via forbidden polarity) | `docs/ARCHITECTURE.md` R5; Section 8 |
| Non-explosion | Design intent, argued operationally; no proof | Section 5 |
| Soundness/completeness of any consequence relation | Not claimed; none exists | — |

## 12 Limitations and threats to validity

* No formal semantics, consequence relation or proof is given in the repository; all logical claims here are from code reading and finite tests.
* The $\{B,N\}$ deviation from FOUR is deliberate (Observation 3.4), but it means standard results that depend on De Morgan's laws do not carry over to lpres.
* State and evidence counters can diverge after `lpres_update_state`.
* The gate is vacuously clear on an empty registry, and $F$ does not block it.
* R5 is a convention; the type system does not prevent a $B$ value on a safety-critical subject, and one module (`zxv_answer_tag.c`) uses base $N$ for a broken invariant.
* Several overlapping value sets (`lpres_state_t`, `trit_t`, six-valued swarm and wire sets) have different connectives; the maps between them are lossy.
* The paradox engine detects only what its checks examine (step limit, budget, repeated fingerprint, invariant callback, registry). Signing log heads is the caller's responsibility.
* Order-independence evidence covers permutations within batches, not re-sequencing of batches.

## 13 Relation to prior work

The evidential four values are those of first-degree entailment (Anderson and Belnap 1975; Dunn 1976; Belnap 1977), whose algebraic models are De Morgan lattices (Font 1997); the lpres chain is not one, for the reason in Proposition 3.3. The three-valued fragments coincide with LP (Asenjo 1966; Priest 1979) and strong Kleene (Kleene 1952). The separation of a truth order from an information order, and the knowledge-join reading of glut formation, is the bilattice programme (Ginsberg 1988; Fitting 1991; Arieli and Avron 1996). Holding contradictory evidence without explosion follows the paraconsistent tradition (da Costa 1974; Priest 2006), but the kernel's mechanism (classify, hold, roll back, escalate) is closer to belief revision (Alchourrón, Gärdenfors and Makinson 1985) and to contradiction-tolerant bookkeeping than to a paraconsistent calculus. The tamper-evident logs follow hash-chained time-stamping (Haber and Stornetta 1991) and secure audit logs (Schneier and Kelsey 1999); order-independent merging relates to conflict-free replicated data types (Shapiro et al. 2011).

## References

* Alchourrón, C. E., Gärdenfors, P., Makinson, D. (1985). On the logic of theory change: partial meet contraction and revision functions. *Journal of Symbolic Logic* 50(2), 510–530.
* Anderson, A. R., Belnap, N. D. (1975). *Entailment: The Logic of Relevance and Necessity*, Vol. I. Princeton University Press.
* Arieli, O., Avron, A. (1996). Reasoning with logical bilattices. *Journal of Logic, Language and Information* 5(1), 25–63.
* Asenjo, F. G. (1966). A calculus of antinomies. *Notre Dame Journal of Formal Logic* 7(1), 103–105.
* Belnap, N. D. (1977). A useful four-valued logic. In J. M. Dunn and G. Epstein (eds.), *Modern Uses of Multiple-Valued Logic*, Reidel, 5–37.
* da Costa, N. C. A. (1974). On the theory of inconsistent formal systems. *Notre Dame Journal of Formal Logic* 15(4), 497–510.
* Dunn, J. M. (1976). Intuitive semantics for first-degree entailments and "coupled trees". *Philosophical Studies* 29(3), 149–168.
* Fitting, M. (1991). Bilattices and the semantics of logic programming. *Journal of Logic Programming* 11(2), 91–116.
* Font, J. M. (1997). Belnap's four-valued logic and De Morgan lattices. *Logic Journal of the IGPL* 5(3), 413–440.
* Ginsberg, M. L. (1988). Multivalued logics: a uniform approach to reasoning in artificial intelligence. *Computational Intelligence* 4(3), 265–316.
* Haber, S., Stornetta, W. S. (1991). How to time-stamp a digital document. *Journal of Cryptology* 3(2), 99–111.
* Kleene, S. C. (1952). *Introduction to Metamathematics*. North-Holland.
* Priest, G. (1979). The logic of paradox. *Journal of Philosophical Logic* 8(1), 219–241.
* Priest, G. (2006). *In Contradiction: A Study of the Transconsistent*, 2nd ed. Oxford University Press.
* Schneier, B., Kelsey, J. (1999). Secure audit logs to support computer forensics. *ACM Transactions on Information and System Security* 2(2), 159–176.
* Shapiro, M., Preguiça, N., Baquero, C., Zawirski, M. (2011). Conflict-free replicated data types. In *Stabilization, Safety, and Security of Distributed Systems (SSS 2011)*, LNCS 6976, Springer, 386–400.

