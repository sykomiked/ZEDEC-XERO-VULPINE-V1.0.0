<!--
Copyright (c) 2024-2026 Michael Laurence Curzi and 36N9 Genetics, LLC
SPDX-License-Identifier: Apache-2.0
-->

# Paper I: The End of Global Consensus Bottlenecks

*Four-Valued Paraconsistent Logic (`lpres`) and 64-State Hexagram Scheduling
(`lpx_t`) at Ring 0*

**Authors:** Michael Laurence Curzi & 36N9 Genetics, LLC
**Implementation Reference:** `kernel/src/lpres/`, `kernel/src/pce/`, `kernel/src/paradox/`
**License:** Apache-2.0

---

## Abstract

Contemporary distributed systems—whether Raft/Paxos replicated state machines,
globally serialized cloud databases, or Byzantine fault-tolerant blockchains—treat
disagreement between nodes as a fatal fault that must be prevented before state
advances. The cost is synchronous global locking: every participant waits for the
slowest quorum round-trip, and a conflicting observation either halts progress
or is silently overwritten by a majority vote or Dempster-Shafer normalization.
In classical two-valued logic, this fragility is unavoidable because the principle
of explosion ($A \land \neg A \vdash B$) allows a single contradiction to corrupt
every downstream inference.

This paper presents the Ring-0 logic architecture of the ZXV operating system.
Rather than enforcing global consensus prior to local execution, ZXV embeds a
four-valued paraconsistent logic (`lpres_state_t`: `NEITHER`, `TRUE`, `FALSE`,
`BOTH`) directly into kernel attestation, hardware driver probing, financial
clearing, and multi-agent decision engines. By separating the **information join**
across independent sources (`lpres_kjoin`) from the **chain-ordered truth
operations** across requirements (`lpres_conjoin`, `lpres_disjoin`), and pairing
every work unit with a 6-bit, 64-state hexagram lifecycle tag (`lpx_t`), nodes
execute asynchronously at wire speed while tracking conflict (`BOTH`) and
ignorance (`NEITHER`) as first-class, bounded, hash-chained objects.

---

## 1. The Pathology of Synchronous Consensus and Classical Explosion

Distributed systems face three recurring failure modes when built on classical
Boolean state:

1. **Quorum Latency Amplification.** In Raft, Paxos, and global BFT protocols,
   committing a state transition requires $O(N)$ or $O(N^2)$ messages across
   geographic links before any local actuator or ledger entry can proceed.
   High-frequency multi-agent workloads stall on network round-trip times.
2. **False Resolution of Conflict.** When Sensor $A$ or Auditor $A$ reports a
   claim with $99\%$ confidence as `TRUE` and Sensor $B$ reports the same claim
   with $99\%$ confidence as `FALSE`, classical normalization rules (such as
   Dempster's rule of combination) divide out the conflicting mass and report a
   calm $50\% / 50\%$ split—indistinguishable from total ignorance.
3. **Unintended Authority from Ambiguity.** In standard Belnap four-valued
   lattices ($\mathcal{F}_4$), the truth-ordering supremum of `BOTH` ($\top$) and
   `NEITHER` ($\bot$) yields $\top \lor \bot = \text{TRUE}$. In an operating
   system kernel or financial clearinghouse, allowing a contradiction combined
   with an unknown to evaluate to `TRUE` is a critical safety violation: a
   conflicted sensor paired with an offline sensor would grant actuator authority.

ZXV solves all three problems at the lowest layer of the kernel (`Stratum S1`,
`kernel/src/lpres/`).

---

## 2. The Four-Valued Presence Algebra (`lpres`)

Every attestation, hardware bind verdict, and decision claim in ZXV takes one
of four discrete states (`kernel/src/lpres/lpres.h`):

$$\mathcal{V} = \{ \text{NEITHER } (0),\; \text{TRUE } (1),\; \text{FALSE } (2),\; \text{BOTH } (3) \}$$

### 2.1 Bitwise Epistemic Encoding and the Information Join (`lpres_kjoin`)

When two independent sources report on the **same** subject, bit $0$ encodes
*told-true* and bit $1$ encodes *told-false*:

- `NEITHER` ($00_2 = 0$): no evidence submitted.
- `TRUE` ($01_2 = 1$): positive evidence only.
- `FALSE` ($10_2 = 2$): negative evidence only.
- `BOTH` ($11_2 = 3$): both positive and negative evidence recorded.

Combining observations from independent sources $s_1, s_2$ over a single subject
uses the **knowledge join** `lpres_kjoin(a, b)`, which is the bitwise OR
($a \mid b$) in this encoding:

| `lpres_kjoin` | `TRUE` (1) | `FALSE` (2) | `BOTH` (3) | `NEITHER` (0) |
|---|---|---|---|---|
| **`TRUE` (1)** | `TRUE` | `BOTH` | `BOTH` | `TRUE` |
| **`FALSE` (2)** | `BOTH` | `FALSE` | `BOTH` | `FALSE` |
| **`BOTH` (3)** | `BOTH` | `BOTH` | `BOTH` | `BOTH` |
| **`NEITHER` (0)** | `TRUE` | `FALSE` | `BOTH` | `NEITHER` |

Properties of `lpres_kjoin`:
- **Idempotent, commutative, associative:** Order of packet arrival across an
  asynchronous mesh never alters the resulting state; CRDT convergence is
  immediate with zero lock coordination.
- **No silent erasure:** `TRUE` from Node $A$ and `FALSE` from Node $B$ yields
  `BOTH`. The contradiction is preserved intact.
- **Silence is neutral:** A node in state `NEITHER` contributes $00_2$, leaving
  authenticated observations from live peers unchanged.

### 2.2 The Kernel Chain Order for `lpres_conjoin` and `lpres_disjoin`

Once observations per subject have been joined via `lpres_kjoin`, combining
**multiple distinct subjects** that must all hold (conjunction) or of which at
least one must hold (disjunction) uses ZXV's **chain ordering**, not Belnap's
bilattice:

$$\text{FALSE} < \text{NEITHER} < \text{BOTH} < \text{TRUE}$$

Under this total chain:
- `lpres_conjoin(a, b)` takes the chain minimum.
- `lpres_disjoin(a, b)` takes the chain maximum.
- `lpres_negate(TRUE) = FALSE`, `lpres_negate(FALSE) = TRUE`, while `BOTH` and
  `NEITHER` are fixed points of negation.

Crucially, because `NEITHER` and `BOTH` both lie strictly below `TRUE` in the
chain:

$$\text{lpres\_disjoin}(\text{BOTH},\; \text{NEITHER}) = \text{BOTH} \neq \text{TRUE}$$

A contradiction combined with an unknown **never** fabricates `TRUE`. Actuator
authority (`lpres_implies_actuator_authority`) requires `state == LPRES_STATE_TRUE`
 strictly, and `lpres_safety_gate_clear` requires zero contradictions and zero
unattested subjects across the registry.

---

## 3. The 64-State Hexagram Lifecycle Tag (`lpx_t`)

Representing `BOTH` prevents logical explosion, but an operating system must
still decide *what action to execute on the current cycle* when a subsystem
encounters `BOTH` or `NEITHER`. ZXV solves this in `kernel/src/lpres/lpres_hex.h`
by packing a 6-bit hexagram tag (`lpx_t`, values $0..63$) onto every unit of
work (an attestation, a ledger posting, a driver probe, or an agent task):

```
  bits 5..4   BASE       00 FALSE | 01 NEITHER | 10 BOTH | 11 TRUE
  bits 3..2   POLARITY   00 ENTROPIC (diverging/stale)
                         01 NEUTRAL  (steady margin)
                         10 FORBIDDEN (broken hard invariant — absorbing wall)
                         11 NEGENTROPIC (converging evidence margin)
  bits 1..0   PHASE      Gray cycle: 00 POTENTIAL -> 01 EMERGING -> 11 MANIFEST -> 10 DISSOLVING
```

### 3.1 Polarity Dynamics Without Floating Point

Given prior evidence counts $(\text{pos}_0, \text{neg}_0)$ and current counts
$(\text{pos}_1, \text{neg}_1)$ with a boolean staleness window, `lpx_polarity`
computes the trajectory of the dispute using exact unsigned integer margins
$m_0 = |\text{pos}_0 - \text{neg}_0|$ and $m_1 = |\text{pos}_1 - \text{neg}_1|$:

- If `stale` is true: `LPX_P_ENTROPIC`.
- If $m_1 > m_0$: `LPX_P_NEGENTROPIC` (evidence is converging toward resolution).
- If $m_1 < m_0$: `LPX_P_ENTROPIC` (disagreement is widening).
- If $m_1 = m_0$: `LPX_P_NEUTRAL`.

### 3.2 Single-Table Deterministic Dispatch (`lpx_action`)

Every work unit's next transition is determined by a single $O(1)$ lookup in a
64-entry constant table `lpx_action(tag)` governed by five hardware-verifiable
laws (`X1–X6` in `lpres_hex.h`):

| Base State | Polarity | `lpx_action` Verdict | Operational Semantics |
|---|---|---|---|
| Any (`00..11`) | `FORBIDDEN` (`10`) | `LPX_ACT_ROLLBACK` | Absorbing wall (`X4`). Appends to the SHA-256 hash-chained quarantine log (`lpx_qlog_t`, `X6`). Never replayed. |
| `TRUE` (`11`) | `NEGENTROPIC` / `NEUTRAL` | `LPX_ACT_PROCEED` | Invariants and attestations hold; state commits immediately without global lock. |
| `TRUE` (`11`) | `ENTROPIC` (`00`) | `LPX_ACT_WAIT` | A weakening or stale truth pauses before irreversible actuation. |
| `FALSE` (`00`) | Non-forbidden | `LPX_ACT_REFUSE` | Clean refusal; no state was mutated prior to check, so nothing needs rollback. |
| `BOTH` (`10`) | `NEGENTROPIC` (`11`) | `LPX_ACT_WAIT` | A converging dispute stays in local escrow while additional peer frames arrive. |
| `BOTH` (`10`) | `NEUTRAL` / `ENTROPIC` | `LPX_ACT_ESCALATE` | A steady or diverging contradiction is surfaced with both evidence branches intact to the human owner or arbiter. |
| `NEITHER` (`01`) | Non-forbidden | `LPX_ACT_WAIT` | Unknown state never guesses and never acts. |

### 3.3 Gray-Coded Phase Transitions (`X3`)

The two phase bits advance along a single-bit-flip Gray cycle:

$$00_2 \text{ (Potential)} \to 01_2 \text{ (Emerging)} \to 11_2 \text{ (Manifest)} \to 10_2 \text{ (Dissolving)} \to 00_2$$

`lpx_advance(&tag, next_phase)` rejects any transition that flips more than one
phase bit. A work unit cannot jump from `POTENTIAL` directly to `MANIFEST`
without passing through `EMERGING`, preventing unjournaled commits by construction.

---

## 4. Exact-Rational Paraconsistent Decision Engines (`kernel/src/pce/`)

Above `lpres`, ZXV provides four deterministic analytical engines in
`kernel/src/pce/` that operate exclusively on reduced 64-bit signed rationals
(`rational_t`, `num/den` with checked overflow) and emit hash-chained records
to `pdx_log_t`:

1. **Four-Valued Probability (`pce_prob.h`).** A belief mass $m = (m_T, m_F, m_B, m_N)$
   consists of four exact rationals summing to $1$. Unlike scalar probability,
   $m_B$ measures conflict and $m_N$ measures ignorance separately. When two
   sources are $99\%$ confident in opposite conclusions:
   - Classical Dempster-Shafer normalization divides out the $98.02\%$ conflict
     mass and outputs $(0.50, 0.50)$—hiding the contradiction.
   - `pce_prob_combine` joins over `lpres_kjoin` and outputs
     $m_B = \frac{9802}{10000}$, returning verdict `BOTH`.
2. **Multi-Evaluator Tactics (`pce_tac.h`).** Alpha-beta minimax search runs
   across multiple objective evaluators plus a robust worst-case leaf evaluator.
   If all evaluators select the same move, the verdict is `TRUE`; if they split,
   the verdict is `BOTH` and every evaluator's preferred line plus the minimax
   robust move are retained. Comparing agreement at depth $d$ against depth
   $d-1$ sets the hexagram polarity (`NEGENTROPIC` if converging with depth,
   `ENTROPIC` if diverging).
3. **Interval-Dominance Strategy (`pce_strat.h`).** Evaluates options against
   scenarios whose probabilities are intervals $[p_{\min}, p_{\max}]$ derived
   from `pce_prob`. Three decision rules—maximin lower expectation, minimax
   regret, and worst-case floor—vote; unanimity yields `TRUE`, split votes yield
   `BOTH` with full pareto bounds.
4. **Paraconsistent Financial Reconciliation (`pce_fin.h`).** Reconciles
   multi-source financial statements. Any statement violating the accounting
   identity $\text{Assets} = \text{Liabilities} + \text{Equity}$ is immediately
   tagged `LPX_P_FORBIDDEN` and quarantined in `lpx_qlog_t` so corrupt numbers
   can never contaminate downstream ratios.

---

## 5. Ring-0 Hardware Probing Without Boot Fragility (`kernel/src/hwdrv/`)

Even PCIe hardware initialization in ZXV uses four-valued verdicts
(`hw_verdict_t`: `HW_N = 0`, `HW_T = 1`, `HW_F = 2`, `HW_B = 3`):

- `HW_T`: Device bound, self-tested, and registered (`[DRIVER ONLINE]`).
- `HW_N`: BARs unassigned or device absent; skipped cleanly.
- `HW_F`: Device present in PCI config space but failed reset, version
  negotiation, or DMA allocation (`[DRIVER FAILED]`).
- `HW_B`: Device responded with a contradictory hardware state—for example, a
  NIC (`e1000`, `vnet`, or `vmx3`) whose factory station MAC address has the
  IEEE multicast group bit set (`mac[0] & 1`), or an NVMe namespace reporting
  contradictory geometry (`[DRIVER HELD] contradiction`).

Because `hw_probe_all()` processes `HW_F` and `HW_B` locally without panicking,
a faulty or spoofed virtual PCI device is quarantined while the kernel continues
booting over remaining healthy buses.

---

## 6. Empirical Verification

All properties described in this paper are mechanically verified in the
repository's test gate:

- `kernel/src/lpres/test_lpres.c` & `test_lpres_hex.c`: Exhaustively verifies
  all $4 \times 4$ truth/knowledge tables, De Morgan boundaries, all $64$
  `lpx_action` entries, Gray-cycle step enforcement, and SHA-256 quarantine log
  tamper detection.
- `kernel/src/pce/test_pce.c`: Verifies four-valued Bayesian combination against
  Dempster's paradox, alpha-beta vs. brute-force minimax across 400 random game
  trees, and linear-programming corner bounds across 500 random interval
  strategy matrices.
- `kernel/src/hwdrv/test/test_*.c`: Verifies `HW_T`, `HW_F`, `HW_B`, and `HW_N`
  paths across 15 register-level hardware device models under AddressSanitizer
  and UndefinedBehaviorSanitizer.
