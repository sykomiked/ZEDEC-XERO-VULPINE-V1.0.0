------------------------- MODULE ledger_conservation -------------------------
(***************************************************************************)
(* Copyright (c) 2024-2026 Michael Laurence Curzi and 36N9 Genetics, LLC   *)
(* SPDX-License-Identifier: Apache-2.0                                     *)
(*                                                                         *)
(* A model of the balance state machine of pay_ledger_post()               *)
(* (kernel/src/pay/pay_ledger.c). It is an ABSTRACTION of the C code, not  *)
(* a proof about the C code: the validation steps below are transcribed by *)
(* hand from pay_ledger_post, in the same order, and TLC checks that every *)
(* request the validation accepts keeps the invariants, for every request  *)
(* over the bounded account table and line set given in the .cfg.          *)
(*                                                                         *)
(* What is modelled (C name in brackets):                                  *)
(*   - accounts with owner, group (= the (asset, capital form) pair),      *)
(*     ISSUER and FROZEN flags, Crown capital forms     [pay_account_t]    *)
(*   - DEBIT and CREDIT rails; EQUITY is derived (L3 makes it              *)
(*     debit - credit, so it is not stored separately here)                *)
(*   - an arbitrary posting of 1..MaxLines lines, each line any account    *)
(*     and any (d_debit, d_credit) from Deltas                [pay_line_t] *)
(*   - the per-line checks, in code order, on a scratch copy that merges   *)
(*     repeated accounts: LINE_MAX_DELTA, FROZEN, FUNDS, CREDIT,           *)
(*     PAY_BAL_MAX, non-issuer CREDIT                                      *)
(*   - L1 balance per group, L5 Crown same-owner, then atomic apply (L4).  *)
(* What is NOT modelled here: UETR / idempotency / nonce replay (see       *)
(* ledger_replay.tla), the SHA3 provenance chain, the externality sums,    *)
(* string validation, and the 2^62 / 2^59 widths themselves: the widths    *)
(* are scaled down to BalMax / LineMax and the int64 no-overflow argument  *)
(* at the real widths is proved in rational_bounds/RationalBounds.lean     *)
(* (pay_ledger_no_overflow), which this model's NoOverflow invariant       *)
(* composes with: every intermediate the C code computes stays within      *)
(* BalMax + MaxLines * LineMax.                                            *)
(***************************************************************************)
EXTENDS Integers, Sequences, FiniteSets

CONSTANTS
    Accts,      \* finite set of account ids
    Owner,      \* Owner[a]  : owner id                   (pay_account_t.owner)
    Group,      \* Group[a]  : (asset, cap) group id
    Issuer,     \* set of accounts with PAY_ACCT_ISSUER
    Frozen,     \* set of accounts with PAY_ACCT_FROZEN
    Crown,      \* set of accounts whose cap is a Crown form (pay_cap_is_crown)
    BalMax,     \* stands for PAY_BAL_MAX   (2^62)
    LineMax,    \* stands for LINE_MAX_DELTA (2^59)
    MaxLines,   \* stands for PAY_MAX_LINES (8); kept small for TLC
    Deltas,     \* the d_debit / d_credit values the environment may send
    MaxPosts    \* bound on the number of accepted postings explored

VARIABLES
    debit,      \* debit[a]  >= 0
    credit,     \* credit[a] >= 0
    minted,     \* ghost: minted[g] = sum of positive CREDIT deltas applied in g
    burned,     \* ghost: burned[g] = sum of |negative CREDIT deltas| applied in g
    posts       \* ghost: number of accepted postings

vars == <<debit, credit, minted, burned, posts>>

Groups == {Group[a] : a \in Accts}

Line == [acct : Accts, dd : Deltas, dc : Deltas]

\* All requests of 1..MaxLines lines.
Reqs == UNION {[1..n -> Line] : n \in 1..MaxLines}

RECURSIVE SumSeq(_)
SumSeq(s) == IF s = <<>> THEN 0 ELSE Head(s) + SumSeq(Tail(s))

\* Sum of the function f over the finite set S.
RECURSIVE SumSet(_, _)
SumSet(S, f) == IF S = {} THEN 0
                ELSE LET x == CHOOSE y \in S : TRUE IN f[x] + SumSet(S \ {x}, f)

(***************************************************************************)
(* The per-line loop of pay_ledger_post. It walks the lines in order and   *)
(* keeps the scratch copy nw (here: the pair of functions d, c over all    *)
(* accounts, which is the same as the C "ids/nw" merge). It returns a      *)
(* record with the status of the first failing line, or "OK" and the new   *)
(* balances. It also records the largest |intermediate| it computed, so    *)
(* that NoOverflow can be stated.                                          *)
(***************************************************************************)
RECURSIVE Scan(_, _, _, _, _)
Scan(req, i, d, c, peak) ==
    IF i > Len(req) THEN [st |-> "OK", d |-> d, c |-> c, peak |-> peak]
    ELSE
      LET ln == req[i]
          a  == ln.acct
      IN
      IF ln.dd > LineMax \/ ln.dd < -LineMax \/ ln.dc > LineMax \/ ln.dc < -LineMax
        THEN [st |-> "ERR_OVERFLOW", peak |-> peak]
      ELSE IF ln.dd < 0 /\ a \in Frozen
        THEN [st |-> "ERR_POLICY", peak |-> peak]
      ELSE
        LET nd == d[a] + ln.dd       \* int64 nd = debit + d_debit
            nc == c[a] + ln.dc       \* int64 nc = credit + d_credit
            pk == LET m == {peak, nd, -nd, nc, -nc} IN CHOOSE x \in m : \A y \in m : x >= y
        IN
        IF nd < 0 THEN [st |-> "ERR_FUNDS", peak |-> pk]
        ELSE IF nc < 0 THEN [st |-> "ERR_CREDIT", peak |-> pk]
        ELSE IF nd >= BalMax \/ nc >= BalMax THEN [st |-> "ERR_OVERFLOW", peak |-> pk]
        ELSE IF nc > 0 /\ a \notin Issuer THEN [st |-> "ERR_CREDIT", peak |-> pk]
        ELSE Scan(req, i + 1, [d EXCEPT ![a] = nd], [c EXCEPT ![a] = nc], pk)

\* L1: for every line's group, sum d_debit == sum d_credit over that group.
GroupSum(req, g, fld) ==
    SumSeq([j \in 1..Len(req) |->
              IF Group[req[j].acct] = g THEN req[j][fld] ELSE 0])

Balanced(req) ==
    \A i \in 1..Len(req) :
        GroupSum(req, Group[req[i].acct], "dd") = GroupSum(req, Group[req[i].acct], "dc")

\* L5: a posting touching a Crown account may only move value between
\* accounts of one owner (the C code compares every line with lines[0]).
CrownOk(req) ==
    (\E i \in 1..Len(req) : req[i].acct \in Crown) =>
        \A i \in 1..Len(req) : Owner[req[i].acct] = Owner[req[1].acct]

\* The status pay_ledger_post returns (the balance-relevant part), in code
\* order: per-line scan, then L1, then L5.
Result(req) ==
    LET s == Scan(req, 1, debit, credit, 0) IN
    IF s.st # "OK" THEN s
    ELSE IF ~Balanced(req) THEN [st |-> "ERR_UNBALANCED", peak |-> s.peak]
    ELSE IF ~CrownOk(req) THEN [st |-> "ERR_CROWN", peak |-> s.peak]
    ELSE s

Init ==
    /\ debit = [a \in Accts |-> 0]
    /\ credit = [a \in Accts |-> 0]
    /\ minted = [g \in Groups |-> 0]
    /\ burned = [g \in Groups |-> 0]
    /\ posts = 0

\* Positive and negative CREDIT movement of one request inside group g.
CreditUp(req, g) ==
    SumSeq([j \in 1..Len(req) |->
              IF Group[req[j].acct] = g /\ req[j].dc > 0 THEN req[j].dc ELSE 0])
CreditDown(req, g) ==
    SumSeq([j \in 1..Len(req) |->
              IF Group[req[j].acct] = g /\ req[j].dc < 0 THEN -req[j].dc ELSE 0])

\* An accepted posting: applied atomically (L4).
Post(req) ==
    LET r == Result(req) IN
    /\ posts < MaxPosts
    /\ r.st = "OK"
    /\ debit' = r.d
    /\ credit' = r.c
    /\ minted' = [g \in Groups |-> minted[g] + CreditUp(req, g)]
    /\ burned' = [g \in Groups |-> burned[g] + CreditDown(req, g)]
    /\ posts' = posts + 1

\* A refused posting changes nothing (L4): it is a stuttering step, which
\* [Next]_vars already allows, so only accepted postings are actions.
Next == \E req \in Reqs : Post(req)

Spec == Init /\ [][Next]_vars

-----------------------------------------------------------------------------
(* Invariants                                                              *)

TypeOK ==
    /\ debit \in [Accts -> 0..(BalMax - 1)]
    /\ credit \in [Accts -> 0..(BalMax - 1)]
    /\ posts \in 0..MaxPosts

\* L3: no negative DEBIT or CREDIT, CREDIT only on issuers, both < BalMax.
NoNegative ==
    \A a \in Accts : debit[a] >= 0 /\ credit[a] >= 0
NoHolderDebt ==
    \A a \in Accts \ Issuer : credit[a] = 0
BelowBalMax ==
    \A a \in Accts : debit[a] < BalMax /\ credit[a] < BalMax

\* L2: per group, sum DEBIT == sum CREDIT (so sum EQUITY == 0).
GroupDebit(g) == SumSet({a \in Accts : Group[a] = g}, debit)
GroupCredit(g) == SumSet({a \in Accts : Group[a] = g}, credit)
DebitsEqualCredits ==
    \A g \in Groups : GroupDebit(g) = GroupCredit(g)

\* Supply: per group, total DEBIT (what holders hold) and total CREDIT (what
\* issuers owe) both equal minted - burned.
SupplyIsMintedMinusBurned ==
    \A g \in Groups : /\ GroupCredit(g) = minted[g] - burned[g]
                      /\ GroupDebit(g) = minted[g] - burned[g]

\* L5: an account of a Crown form whose owner owns no Crown issuer in its
\* group can never be credited with anything.
CrownInalienable ==
    \A a \in Crown :
        (~\E b \in Crown \cap Issuer : Owner[b] = Owner[a] /\ Group[b] = Group[a])
            => debit[a] = 0

\* NoOverflow: every intermediate value the C code computes for ANY request
\* in this state (accepted or not) is within BalMax + LineMax; the line sums
\* of L1 are within MaxLines * LineMax by construction of the bound check.
NoOverflow ==
    \A req \in Reqs : Result(req).peak <= BalMax - 1 + LineMax

-----------------------------------------------------------------------------
(* Action properties                                                       *)

\* A FROZEN account's DEBIT never decreases (outgoing debit refused).
FrozenNeverPays == [][\A a \in Frozen : debit'[a] >= debit[a]]_vars

\* Every accepted posting was balanced per group: the change in total DEBIT
\* equals the change in total CREDIT in each group (L1 per transaction).
PostingBalanced ==
    [][\A g \in Groups : GroupDebit(g)' - GroupDebit(g) = GroupCredit(g)' - GroupCredit(g)]_vars

=============================================================================
