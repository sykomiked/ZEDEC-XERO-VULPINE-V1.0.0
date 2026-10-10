------------------------- MODULE MC_ledger_fee -------------------------
(* Copyright (c) 2024-2026 Michael Laurence Curzi and 36N9 Genetics, LLC  *)
(* SPDX-License-Identifier: Apache-2.0                                    *)
(* ledger_conservation restricted to the request SHAPES that matter for   *)
(* a payment with a fee: pay_ledger_pay_tithed (to be renamed with the    *)
(* fee change) posts ONE request {from: -(amount + fee), to: +amount,     *)
(* bucket: +fee}. The fee rate itself is not modelled: any fee value is a *)
(* plain balanced transfer to a fee bucket account, so the conservation   *)
(* invariants of ledger_conservation cover every rate, including the      *)
(* 0.08889% fee. Requests whose bucket line does not match (unbalanced)   *)
(* are included and must be refused.                                      *)
(*                                                                        *)
(*   acct  owner  role                                                    *)
(*   1     1      holder (payer / payee)                                  *)
(*   2     2      holder (payer / payee)                                  *)
(*   3     3      fee bucket (an ordinary holder account)                 *)
(*   4     4      issuer                                                  *)
EXTENDS ledger_conservation

FAccts == 1..4
FOwner == <<1, 2, 3, 4>>
FGroup == <<1, 1, 1, 1>>
FIssuer == {4}
FNone == {}
FDeltas == -3..3

L(a, d, c) == [acct |-> a, dd |-> d, dc |-> c]

\* issuance / redemption: issuer CREDIT and holder DEBIT move together
MintBurn == {<<L(4, 0, k), L(h, k, 0)>> : h \in {1, 2}, k \in {-2, -1, 1, 2}}

\* a payment with a fee: payer -(x + fee), payee +x, bucket +bf, where
\* bf = fee is the balanced request and bf # fee must be refused
FeePay == {<<L(f, -(x + fee), 0), L(t, x, 0), L(3, bf, 0)>> :
             f \in {1, 2}, t \in {1, 2}, x \in 1..2, fee \in 0..1, bf \in 0..1}

FReqs == MintBurn \cup FeePay
========================================================================
