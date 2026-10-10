<!-- Copyright (c) 2024-2026 Michael Laurence Curzi and 36N9 Genetics, LLC
     SPDX-License-Identifier: Apache-2.0 -->
# proofs/: machine-checked models of kernel subsystems

**Read this first.** Every file here is a *model* of some C code, written by
hand from that code: the TLA+ specifications are state machines checked by
TLC over small, stated bounds, and the Lean files are transcriptions of C
functions into Lean with fixed-width integers modelled explicitly. Nothing
here is a proof about the compiled C code. Unlike seL4, there is no
refinement proof linking these models to the C source or the binary: if the
C code changes and the model is not updated, the model proves nothing about
the new code. A TLC run proves the invariants only for the bounds in its
`.cfg` file, not for all sizes. The list of invariants and the C functions
and tests that enforce them is in
[docs/FORMAL_INVARIANTS.md](../docs/FORMAL_INVARIANTS.md).

## Running the checks

```sh
proofs/check.sh            # everything
proofs/check.sh tla        # only TLC
proofs/check.sh lean       # only Lean
proofs/check.sh tla MC_ledger_fee.cfg   # one TLC configuration
```

Environment: `TLA2TOOLS` (default `proofs/.tools/tla2tools.jar`), `LEAN`
(default `lean` on `PATH`), `PROOFS_LOGDIR` (where TLC/Lean logs go).
`check.sh` exits non-zero if any check fails, if a Lean file contains
`sorry`, `admit` or an `axiom`, if Lean reports a declaration using `sorry`,
or if an expected counterexample (below) is *not* found.

The CI job `proofs` in `.github/workflows/ci.yml` downloads the pinned tools,
verifies their SHA-256, caches them in `proofs/.tools/` (git-ignored) and
runs `check.sh lean` and `check.sh tla`.

| tool | version | download | SHA-256 |
|---|---|---|---|
| TLA+ tools (TLC) | 1.7.4 | `github.com/tlaplus/tlaplus/releases/download/v1.7.4/tla2tools.jar` | `936a262061c914694dfd669a543be24573c45d5aa0ff20a8b96b23d01e050e88` |
| Lean 4 (core only, no Mathlib) | 4.15.0 (`lean-toolchain`) | `github.com/leanprover/lean4/releases/download/v4.15.0/lean-4.15.0-linux.tar.zst` | `af71a2569a9f68337de2434829b3008cd8e32c436e9cb6bd8c84a2c2ba3585c9` |
| Java | 21 (any JRE 11+ runs TLC) | | |

Coq/Rocq is not used: Lean installed, so all theorem-prover models are Lean.

## What each check proves, and what it does not

Numbers are from the local run of 2026-10-10 (TLC 1.7.4, `-workers auto`).

### TLA+ (TLC, exhaustive over the stated bounds)

| config | model of | bounds | result |
|---|---|---|---|
| `MC_ledger_conservation.cfg` | `pay_ledger_post` balance rules (L1-L5) | 6 accounts in 2 groups (holder, frozen holder, issuer; Crown holder, own Crown issuer, other owner's Crown), every request of 1-2 lines with each delta in -2..2, `LineMax` 1, `BalMax` 3, 5 accepted postings | no error; 31,134 states generated, 951 distinct, depth 6 |
| `MC_ledger_fee.cfg` | the same rules on 3-line "payment with fee" requests | 4 accounts (2 holders, fee bucket, issuer), amount 1..2, fee 0..1, mismatched bucket lines included, `BalMax` 4, 6 postings | no error; 1,279 generated, 253 distinct, depth 7 |
| `MC_ledger_replay.cfg` | R1/R2/R3 replay rules, journal ring, `pay_ledger_reverse` | 4 keys, 4 UETRs, nonces 0..2, window 4 = max postings 4 | no error; 192,769 distinct, depth 5 |
| `MC_ledger_replay_wrap.cfg` | the same, window 1 < 3 postings, no nonces | | **counterexample required and found**: once a posting leaves the journal window, the identical request is applied again |
| `MC_ledger_replay_nonce.cfg` | the same, window 1 < 4 postings, every request has a nonce | | no error; 1,387 distinct, depth 4: R3 alone stops the replay |
| `MC_triple_ledger.cfg` | `triple_ledger_transfer` and the voucher functions | 3 accounts, 6 entries and 1 voucher each, amounts 1..2, phase 0..1 | no error; 3,476,236 generated, 356,601 distinct, depth 12 |
| `MC_triple_ledger_trial.cfg` | the same, asks for "trial balance always 0" | | **counterexample required and found**: voucher redemption is a one-sided debit |

Invariants checked by `MC_ledger_conservation` (and, except the Crown and
frozen ones, by `MC_ledger_fee`): debits equal credits per group in every
reachable state and in every accepted posting (`DebitsEqualCredits`,
`PostingBalanced`); per group, total DEBIT and total CREDIT both equal
minted - burned (`SupplyIsMintedMinusBurned`); no negative balance, CREDIT
only on issuers, balances below `PAY_BAL_MAX` (`NoNegative`, `NoHolderDebt`,
`BelowBalMax`); a frozen account never pays (`FrozenNeverPays`); a Crown
account never receives value from another owner (`CrownInalienable`); and
`NoOverflow`: every intermediate the C code computes for any request, accepted
or refused, stays within `BalMax + LineMax`. `ledger_replay` adds: an applied
request is never applied again inside the window, no double spend of the
same funds, a posting is reversed at most once.

**The fee.** The model has no fee rate: a payment with a fee is one balanced
posting whose extra line moves the fee to a fee-bucket account (an ordinary
holder account). Conservation therefore holds for any rate, including the
0.08889% fee that replaces the former tithe; `MC_ledger_fee` checks that
shape explicitly, including requests whose bucket line does not match the fee
(they are refused as unbalanced).

Not modelled: the real widths (2^62, 2^59, 8 lines; scaled down for TLC and
covered by the Lean lemma `pay_ledger_no_overflow`), more than 2 arbitrary
lines (3 only in the fee shape), the SHA3 provenance chain, the externality
sums, string validation, account creation, concurrent callers (the model is sequential),
and `triple_ledger`'s Q32.32 overflow (the
C code does not check `SR_ADD`/`SR_SUB`; reported as a finding).

### Lean 4

`headroom_convergence/HeadroomConvergence.lean` (integer recurrences only;
no claim about the real-valued ISF formula `Q' = (1 - delta) Q + eta S - C`):

* `swarm_budget_consume`: `consume_used_le` (used never exceeds allotted, so
  `allotted - used` never wraps), `drainCalls_le` (a caller whose every
  request is >= eps >= 1 makes at most ceil(left0 / eps) calls before the
  headroom is 0).
* `zt_isf_step` (Q16): `isf_nonneg`, `isf_no_growth`, `isf_decrease`,
  `isf_reaches_zero` (each step with margin eps >= 1 reaches 0 within
  ceil(Q0 / eps) steps: well-founded descent on the naturals), `isf_mul_fits`
  (Q * delta fits int64 for Q < 2^47, delta <= 2^16; the C code does not
  enforce these bounds: finding).
* `ct_budget`: `ct_decay_exact` (the split Q16 decay equals
  floor(Q * delta / 2^16)) and `ct_decay_fits` (no int64 overflow).

`rational_bounds/RationalBounds.lean`:

* `rational_t` (int64 num/den, `rational_normalize`, `rmag_*_quotas`):
  `normalize_den_ne_zero`, `normalize_den_pos`, `normalize_value`,
  `addQ_comm`, `mulQ_comm`; the division bug (`divOld_by_zero`: 5 / 0 used
  to return 1) and the fixed code (`div_by_zero_detected`, `divQ_den_pos`,
  `divChecked_sound`). Finding, not fixed: add/sub/mul and unchecked div do
  not check int64 overflow (`addQ_overflow_witness`); their commutativity is
  proved on mathematical integers only.
* `rmag_rational_t` (u64 magnitude + sign): `magCmp_spec`, `ratCmp_spec`,
  `lessThan_exact` (comparisons are exact and multiplication-free),
  `mulU_exact` (multiply is exact or saturates), `addU_comm` (add is
  commutative for all inputs, which needed the fix in `rmag_rational_add`),
  `reduceU_den_ne_zero`, `reduceU_exact`.
* `pay_ledger_no_overflow`: with `pay_ledger_post`'s bound checks, every
  balance update and every L1 line sum fits int64.

`proofs/sched_bounded.tla` (scheduler run queue) was optional and is not
written.
