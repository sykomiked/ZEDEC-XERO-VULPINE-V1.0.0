---------------------------- MODULE triple_ledger ----------------------------
(***************************************************************************)
(* Copyright (c) 2024-2026 Michael Laurence Curzi and 36N9 Genetics, LLC   *)
(* SPDX-License-Identifier: Apache-2.0                                     *)
(*                                                                         *)
(* A model of kernel/src/finance/triple_ledger.c: triple_ledger_transfer   *)
(* (six legs: financial, provenance and externality, on both accounts),    *)
(* triple_ledger_issue_voucher, triple_ledger_transfer_voucher and         *)
(* triple_ledger_redeem_voucher, with the 256-entry and 64-voucher         *)
(* per-account capacities scaled down to EntCap and VCap.                  *)
(*                                                                         *)
(* It is an abstraction. Amounts are small integers standing for Q32.32    *)
(* surplus_real_t values; the C code does NOT check SR_ADD / SR_SUB for    *)
(* int64 overflow (a finding, see docs/FORMAL_INVARIANTS.md), so the model *)
(* uses unbounded integers and says nothing about overflow here. Coverage, *)
(* health and description strings are not modelled.                       *)
(*                                                                         *)
(* What TLC checks (MC_triple_ledger.cfg): every transfer is all six legs  *)
(* or none; per transfer debits == credits on each of the three ledgers;   *)
(* the conventional trial balance (total_assets - total_liabilities) is    *)
(* EXACTLY the merit value of the vouchers redeemed so far; a voucher is   *)
(* redeemed at most once and only by its current holder.                   *)
(* MC_triple_ledger_trial.cfg asks TLC for the stronger "trial balance is  *)
(* always zero" and check.sh REQUIRES the counterexample: a voucher        *)
(* redemption posts a debit with no matching credit.                       *)
(***************************************************************************)
EXTENDS Integers, FiniteSets

CONSTANTS Accts, EntCap, VCap, Amounts, Phases, Merits

VARIABLES
    nent,       \* account.num_entries
    fin,        \* account.balance[LEDGER_FINANCIAL]
    prov,       \* account.balance[LEDGER_PROVENANCE]
    ext,        \* account.balance[LEDGER_EXTERNALITY]
    conv,       \* account.conventional_balance
    assets,     \* tl.total_assets       (sum of financial debits)
    liabs,      \* tl.total_liabilities  (sum of financial credits)
    vouchers,   \* set of [id, home, holder, merit, redeemed]
    nextV,      \* tl.next_voucher_id
    redeems     \* ghost: redeems[id] = number of successful redemptions

vars == <<nent, fin, prov, ext, conv, assets, liabs, vouchers, nextV, redeems>>

Entity(a) == a          \* account a belongs to entity a (one capital type)
MaxV == Cardinality(Accts) * VCap

Init ==
    /\ nent = [a \in Accts |-> 0]
    /\ fin = [a \in Accts |-> 0]
    /\ prov = [a \in Accts |-> 0]
    /\ ext = [a \in Accts |-> 0]
    /\ conv = [a \in Accts |-> 0]
    /\ assets = 0
    /\ liabs = 0
    /\ vouchers = {}
    /\ nextV = 1
    /\ redeems = [i \in 1..MaxV |-> 0]

\* triple_ledger_transfer(tl, f, t, cap, amount, ell, phi, ...)
Transfer(f, t, amt, phi) ==
    /\ f # t
    /\ amt > 0
    /\ nent[f] + 3 <= EntCap /\ nent[t] + 3 <= EntCap
    /\ nent' = [nent EXCEPT ![f] = @ + 3, ![t] = @ + 3]
    /\ fin' = [fin EXCEPT ![f] = @ - amt, ![t] = @ + amt]
    /\ conv' = [conv EXCEPT ![f] = @ - amt, ![t] = @ + amt]
    /\ prov' = [prov EXCEPT ![f] = @ - amt, ![t] = @ + amt]
    /\ ext' = [ext EXCEPT ![f] = @ - phi, ![t] = @ + phi]
    /\ assets' = assets + amt          \* debit leg on t
    /\ liabs' = liabs + amt            \* credit leg on f
    /\ UNCHANGED <<vouchers, nextV, redeems>>

NumV(a) == Cardinality({v \in vouchers : v.home = a})

\* triple_ledger_issue_voucher: stored in the holder's account; there is no
\* check on the issuer and none on the merit value (see the findings).
Issue(issuer, holder, m) ==
    /\ \E a \in Accts : Entity(a) = holder
    /\ LET a == CHOOSE x \in Accts : Entity(x) = holder IN
       /\ NumV(a) < VCap
       /\ vouchers' = vouchers \cup
             {[id |-> nextV, home |-> a, holder |-> holder, merit |-> m, redeemed |-> FALSE]}
       /\ nextV' = nextV + 1
    /\ UNCHANGED <<nent, fin, prov, ext, conv, assets, liabs, redeems>>

\* triple_ledger_transfer_voucher: refused once redeemed.
MoveVoucher(v, h) ==
    /\ v \in vouchers
    /\ ~v.redeemed
    /\ vouchers' = (vouchers \ {v}) \cup {[v EXCEPT !.holder = h]}
    /\ UNCHANGED <<nent, fin, prov, ext, conv, assets, liabs, nextV, redeems>>

\* triple_ledger_redeem_voucher(tl, id, a): one financial DEBIT posting of
\* merit_value on a; marked redeemed only if that posting succeeded.
Redeem(v, a) ==
    /\ v \in vouchers
    /\ ~v.redeemed
    /\ Entity(a) = v.holder
    /\ nent[a] < EntCap
    /\ nent' = [nent EXCEPT ![a] = @ + 1]
    /\ fin' = [fin EXCEPT ![a] = @ + v.merit]
    /\ conv' = [conv EXCEPT ![a] = @ + v.merit]
    /\ assets' = assets + v.merit
    /\ vouchers' = (vouchers \ {v}) \cup {[v EXCEPT !.redeemed = TRUE]}
    /\ redeems' = [redeems EXCEPT ![v.id] = @ + 1]
    /\ UNCHANGED <<prov, ext, liabs, nextV>>

Next ==
    \/ \E f, t \in Accts, amt \in Amounts, phi \in Phases : Transfer(f, t, amt, phi)
    \/ \E i, h \in Accts, m \in Merits : Issue(Entity(i), Entity(h), m)
    \/ \E v \in vouchers, h \in Accts : MoveVoucher(v, Entity(h))
    \/ \E v \in vouchers, a \in Accts : Redeem(v, a)

Spec == Init /\ [][Next]_vars

-----------------------------------------------------------------------------
RECURSIVE SumF(_, _)
SumF(S, f) == IF S = {} THEN 0 ELSE LET x == CHOOSE y \in S : TRUE IN f[x] + SumF(S \ {x}, f)

RECURSIVE SumMerit(_)
SumMerit(S) == IF S = {} THEN 0
               ELSE LET v == CHOOSE y \in S : TRUE IN v.merit + SumMerit(S \ {v})

Redeemed == {v \in vouchers : v.redeemed}

CapacityRespected == \A a \in Accts : nent[a] <= EntCap /\ NumV(a) <= VCap

\* The conventional trial balance is off by exactly the redeemed merit.
TrialBalanceIsRedeemedMerit == assets - liabs = SumMerit(Redeemed)

\* The stronger claim, which the C code does NOT satisfy (expected to fail).
TrialBalanced == assets = liabs

\* Account balances agree with the system totals.
ConventionalMatchesTotals == SumF(Accts, conv) = assets - liabs
FinancialIsConventional == \A a \in Accts : fin[a] = conv[a]

\* Provenance and externality ledgers net to zero (transfers only post them).
ProvenanceNetsToZero == SumF(Accts, prov) = 0
ExternalityNetsToZero == SumF(Accts, ext) = 0

\* No voucher is redeemed twice.
RedeemOnce == \A i \in 1..MaxV : redeems[i] <= 1
RedeemedFlagMatches == \A v \in vouchers : v.redeemed <=> redeems[v.id] = 1

\* Every transfer posts three entries on each side (all six legs or none):
\* so the entry count of an account is 3 * (its transfers) + (its redemptions).
EntriesAreWholeTransfers ==
    \A a \in Accts :
        (nent[a] - Cardinality({v \in Redeemed : Entity(a) = v.holder})) % 3 = 0
=============================================================================
