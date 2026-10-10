<!-- Copyright (c) 2024-2026 Michael Laurence Curzi and 36N9 Genetics, LLC
     SPDX-License-Identifier: Apache-2.0 -->
# Formal invariants

The invariants and lemmas below are stated for four kernel subsystems and
checked by machine on **models** of the C code in [proofs/](../proofs/README.md):
TLA+ specifications checked by TLC over small stated bounds, and Lean 4
transcriptions of C functions. These models are written by hand from the C
code; there is no refinement proof linking them to the C source or the
binary, so this is not formal verification of the kernel. Each entry names the
model, the C function that enforces the property, and the test that exercises
it on the real code. How to run the checks, tool versions and state counts:
[proofs/README.md](../proofs/README.md).

Abbreviations: `LC` = [proofs/ledger_conservation.tla](../proofs/ledger_conservation.tla)
(configs `MC_ledger_conservation.cfg`, `MC_ledger_fee.cfg`), `LR` =
[proofs/ledger_replay.tla](../proofs/ledger_replay.tla), `TL` =
[proofs/triple_ledger.tla](../proofs/triple_ledger.tla), `HC` =
[proofs/headroom_convergence/HeadroomConvergence.lean](../proofs/headroom_convergence/HeadroomConvergence.lean),
`RB` = [proofs/rational_bounds/RationalBounds.lean](../proofs/rational_bounds/RationalBounds.lean).

## 1. Pay ledger (kernel/src/pay/pay_ledger.c)

Enforced by `pay_ledger_post` (every posting, including transfers, issuance,
returns via `pay_ledger_reverse`, and the payment-with-fee helper
`pay_ledger_pay_tithed`, which posts one request). Test: `test_pay`
(kernel/src/pay/test_pay.c, verify-all, also under ASan/UBSan).

| id | invariant | model | kind |
|---|---|---|---|
| P1 | Per (asset, capital form) group, sum DEBIT == sum CREDIT in every reachable state | LC `DebitsEqualCredits` | TLC, bounded |
| P2 | Every accepted posting moves DEBIT and CREDIT equally in each group (L1 per transaction) | LC `PostingBalanced` | TLC, bounded |
| P3 | Per group, total DEBIT == total CREDIT == minted - burned (supply) | LC `SupplyIsMintedMinusBurned` | TLC, bounded |
| P4 | No negative DEBIT or CREDIT; CREDIT only on issuer accounts; both < `PAY_BAL_MAX` | LC `NoNegative`, `NoHolderDebt`, `BelowBalMax` | TLC, bounded |
| P5 | Every intermediate the checks compute, for any request (accepted or refused), stays within `PAY_BAL_MAX + LINE_MAX_DELTA` | LC `NoOverflow` | TLC, bounded |
| P6 | At the real widths (balances < 2^62, line delta in [-2^59, 2^59], <= 8 lines), `debit + d_debit`, `credit + d_credit` and every L1 running sum fit int64 | RB `pay_ledger_no_overflow` | Lean, all values |
| P7 | A FROZEN account's DEBIT never decreases | LC `FrozenNeverPays` | TLC, bounded |
| P8 | A Crown-form account never receives value from another owner | LC `CrownInalienable` | TLC, bounded |
| P9 | A fee is a balanced transfer to a fee-bucket account; conservation holds for any fee rate (including the 0.08889% fee), and a request whose bucket line does not match is refused | LC with `MC_ledger_fee.cfg` | TLC, bounded |
| P10 | Inside the journal window, an applied request (same idempotency key / UETR) is never applied again; the same funds are not spent twice; a posting is reversed at most once | LR `ReplayNeverApplied`, `NoDoubleSpend`, `ReverseOnce` (`MC_ledger_replay.cfg`) | TLC, bounded |
| P11 | With a per-initiator nonce on every request, no replay even after the window wraps | LR `MC_ledger_replay_nonce.cfg` | TLC, bounded |

Limit (shown, not fixed): **without** a nonce, once a posting has left the
journal ring (`Window` slots) the identical request is accepted again;
`MC_ledger_replay_wrap.cfg` requires TLC to find this behaviour. R1/R2 hold
only "within the journal window", as pay_ledger.h states.

## 2. Triple ledger (kernel/src/finance/triple_ledger.c)

Enforced by `triple_ledger_transfer`, `triple_ledger_issue_voucher`,
`triple_ledger_transfer_voucher`, `triple_ledger_redeem_voucher`. Test:
`test_finance_core` (kernel/src/finance/test_finance_core.c, verify-all).

| id | invariant | model | kind |
|---|---|---|---|
| T1 | A transfer posts all six legs (financial, provenance, externality on both accounts) or none; entry capacity respected | TL `EntriesAreWholeTransfers`, `CapacityRespected` | TLC, bounded |
| T2 | Provenance and externality ledgers each net to zero | TL `ProvenanceNetsToZero`, `ExternalityNetsToZero` | TLC, bounded |
| T3 | Conventional balances equal the financial ledger and sum to assets - liabilities | TL `FinancialIsConventional`, `ConventionalMatchesTotals` | TLC, bounded |
| T4 | Trial balance (assets - liabilities) equals exactly the merit of vouchers redeemed so far | TL `TrialBalanceIsRedeemedMerit` | TLC, bounded |
| T5 | A voucher is redeemed at most once (the redeem action, like the C code, requires the redeeming account to belong to the holder) | TL `RedeemOnce`, `RedeemedFlagMatches` | TLC, bounded |

Findings (not fixed, owner decisions): voucher redemption is a one-sided
debit, so "trial balance always zero" is false (`MC_triple_ledger_trial.cfg`
requires the counterexample; AUDIT_REPORT triple_ledger.c:258). `SR_ADD` and
`SR_SUB` on Q32.32 balances are not overflow-checked; the model uses
unbounded integers and says nothing about overflow.

## 3. Headroom budgets (integer recurrences)

No claim is made about the real-valued ISF formula
`Q' = (1 - delta) Q + eta S - C` (kernel/src/surplus/surplus.h); the lemmas
are about the integer arithmetic the code runs.

| id | lemma | C function | model | test |
|---|---|---|---|---|
| H1 | `used <= allotted` after every consume, so `allotted - used` never wraps | `swarm_budget_consume` (swarm/swarm_budget.c) | HC `consume_used_le`, `consume_used_lt_u64` | `test_swarm_budget` |
| H2 | A caller whose every request is >= eps >= 1 drains the headroom within ceil(left0 / eps) calls | `swarm_budget_consume` | HC `consume_decrease`, `drainCalls_le` | `test_swarm_budget` |
| H3 | `Q' >= 0`; `Q' <= Q` when `zt_isf_can_grow` is false | `zt_isf_step`, `zt_isf_can_grow` (tensor/zt_isf.c) | HC `isf_nonneg`, `isf_no_growth` | `test_zt` |
| H4 | If each step's cost beats its input by eps >= 1, `Q` reaches 0 within ceil(Q0 / eps) steps (well-founded descent on the naturals) | `zt_isf_step` | HC `isf_decrease`, `isf_reaches_zero` | `test_zt` |
| H5 | `Q * delta` fits int64 when 0 <= Q < 2^47 and 0 <= delta <= 2^16 | `zt_isf_step` | HC `isf_mul_fits` | (finding: bounds not enforced in C) |
| H6 | The split decay `(Q >> 16) * delta + (((Q & 0xFFFF) * delta) >> 16)` equals floor(Q * delta / 2^16) and never overflows for Q < 2^63, delta <= 2^16 | `ct_budget_headroom`, `ct_budget_step` (cotier/ct_budget.c) | HC `ct_decay_exact`, `ct_decay_fits` | `test_ct_budget` |

## 4. Rationals (kernel/src/rmag)

Test: `test_rmag_div` (kernel/src/rmag/test_rmag_div.c, verify-all) and
`test_rmag_budget`.

| id | lemma | C function | model |
|---|---|---|---|
| R1 | `rational_normalize` never returns den == 0; for den != 0 it returns den > 0 and the same value | `rational_normalize` (include/m5_types.h) | RB `normalize_den_ne_zero`, `normalize_den_pos`, `normalize_value`, `euclid_eq_gcd` |
| R2 | add and mul are commutative and return den > 0 for den != 0 inputs (on mathematical integers) | `rmag_add_quotas`, `rmag_mul_quotas` (rmag_core.c) | RB `addQ_comm`, `mulQ_comm`, `addQ_den_pos`, `mulQ_den_pos` |
| R3 | Division by a zero numerator, or by/of a den == 0 operand, is detected: 0/1, never a fabricated value; the checked variant reports it and int64 overflow, and when it succeeds the result is exact, den > 0, and fits int64 | `rmag_div_quotas`, `rmag_div_quotas_checked` | RB `div_by_zero_detected`, `divQ_den_pos`, `divQ_value`, `divChecked_sound`; old bug `divOld_by_zero` |
| R4 | `mag_cmp` / `rat_cmp` are exact three-way comparisons with no multiplication, so the budget limit check is exact | `mag_cmp`, `rat_cmp`, `rmag_rational_less_than` (rmag.c) | RB `magCmp_spec`, `ratCmp_spec`, `lessThan_exact` |
| R5 | multiply is exact or saturates to UINT64_MAX/1 on detected overflow | `rmag_rational_multiply` | RB `mulU_exact` |
| R6 | add is commutative for every input, including the saturating path | `rmag_rational_add` | RB `addU_comm` |
| R7 | reduce never returns den == 0, keeps the value, never grows the numerator | `rmag_rational_reduce` | RB `reduceU_den_ne_zero`, `reduceU_exact`, `reduceU_num_le` |

Bugs found by the models and fixed in C (with tests): `rmag_div_quotas(5/1, 0/1)`
returned 1/1 (R3); `rmag_rational_add(x, -x)` with an overflowing common
denominator saturated with an order-dependent sign (R6).

Finding (not fixed): `rmag_add_quotas`, `rmag_sub_quotas`, `rmag_mul_quotas`
and unchecked `rmag_div_quotas` do not check int64 overflow of their cross
products (`addQ_overflow_witness`: 2^62/1 + 2^62/1). R2 says nothing about
inputs that overflow.

The only Q16.16 helper in this subsystem, `trit_to_ell_q16`, is a constant
table (0..65536) and is not modelled.
