---------------------------- MODULE ledger_replay ----------------------------
(***************************************************************************)
(* Copyright (c) 2024-2026 Michael Laurence Curzi and 36N9 Genetics, LLC   *)
(* SPDX-License-Identifier: Apache-2.0                                     *)
(*                                                                         *)
(* A model of the replay, idempotency and reversal logic of                *)
(* pay_ledger_post() and pay_ledger_reverse() (kernel/src/pay/             *)
(* pay_ledger.c): rules R1 (UETR unique in the journal window), R2         *)
(* (idempotency key: same request -> PAY_DUPLICATE and nothing posted,     *)
(* different request -> PAY_ERR_REPLAY), R3 (optional per-initiator nonce  *)
(* that must strictly increase), the journal as a RING of Window slots     *)
(* indexed by seq mod Window (journal_slot), and PAY_ERR_FUNDS.            *)
(*                                                                         *)
(* It is an abstraction: one asset, three holders and one issuer; the      *)
(* request digest is modelled as the tuple of the fields it hashes; the    *)
(* end-to-end id is modelled as equal to the UETR; one initiator. The      *)
(* balance rules themselves are checked in ledger_conservation.tla.        *)
(*                                                                         *)
(* Configurations:                                                         *)
(*   MC_ledger_replay.cfg         Window >= MaxPosts: the window never     *)
(*       wraps; no request is ever applied twice; no double spend; no     *)
(*       posting is reversed twice.                                        *)
(*   MC_ledger_replay_wrap.cfg    Window < MaxPosts, no nonces: TLC FINDS  *)
(*       a behaviour where the same request (same key, UETR and lines) is  *)
(*       applied twice, once its first application has left the ring.     *)
(*       This is the documented limit of R1/R2 ("within the journal        *)
(*       window"); check.sh requires this counterexample to be found.      *)
(*   MC_ledger_replay_nonce.cfg   Window < MaxPosts, every request carries *)
(*       a nonce > 0: R3 alone prevents the replay.                        *)
(***************************************************************************)
EXTENDS Integers, Sequences, FiniteSets

CONSTANTS Keys, Uetrs, Nonces, Window, MaxPosts

Accts == 1..4          \* 1, 2, 3 holders; 4 the issuer
Issuer == 4
Amt == 2

VARIABLES
    debit, credit,     \* per account
    journal,           \* journal[slot]: [used, seq, key, uetr, nonce, kind, lines, reversed]
    seq,               \* postings ever made (L->seq)
    nonceLast,         \* last nonce of the single initiator (0 = none yet)
    hist               \* ghost: every applied request digest, in order, with its seq

vars == <<debit, credit, journal, seq, nonceLast, hist>>

Slots == 0..(Window - 1)
Empty == [used |-> FALSE, seq |-> 0, key |-> 0, uetr |-> 0, nonce |-> 0,
          kind |-> "none", lines |-> <<>>, target |-> -1, reversed |-> FALSE]

Pay(f, t) == <<[acct |-> f, dd |-> -Amt, dc |-> 0], [acct |-> t, dd |-> Amt, dc |-> 0]>>
Ops == {[kind |-> "transfer", lines |-> Pay(1, 2), target |-> 0],
        [kind |-> "transfer", lines |-> Pay(1, 3), target |-> 0],   \* same funds again
        [kind |-> "transfer", lines |-> Pay(2, 1), target |-> 0]}
       \cup {[kind |-> "return", lines |-> <<>>, target |-> u] : u \in Uetrs}

Reqs == [key : Keys, uetr : Uetrs, nonce : Nonces, op : Ops]

\* pay_ledger_find_uetr: first used slot with that UETR.
Find(u) == {s \in Slots : journal[s].used /\ journal[s].uetr = u}

\* The lines a request posts (pay_ledger_reverse negates the original's).
Lines(req) ==
    IF req.op.kind = "transfer" THEN req.op.lines
    ELSE LET s == CHOOSE x \in Find(req.op.target) : TRUE
             o == journal[s].lines
         IN [i \in 1..Len(o) |-> [acct |-> o[i].acct, dd |-> -o[i].dd, dc |-> -o[i].dc]]

\* The request digest (req_digest hashes these fields and the lines).
Digest(req) == <<req.key, req.uetr, req.nonce, req.op.kind, Lines(req)>>

\* Status of a request in the current state, in code order.
Status(req) ==
    IF req.op.kind = "return" /\ Find(req.op.target) = {} THEN "ERR_NOT_FOUND"
    ELSE IF req.op.kind = "return" /\
            journal[CHOOSE x \in Find(req.op.target) : TRUE].reversed THEN "ERR_STATE"
    ELSE
    LET d == Digest(req)
        keyHit == {s \in Slots : journal[s].used /\ journal[s].key = req.key}
        uetrHit == {s \in Slots : journal[s].used /\ journal[s].uetr = req.uetr}
        ln == Lines(req)
    IN
    IF keyHit # {} THEN
        (IF \E s \in keyHit : <<journal[s].key, journal[s].uetr, journal[s].nonce,
                                 journal[s].kind, journal[s].lines>> = d
         THEN "DUPLICATE" ELSE "ERR_REPLAY")
    ELSE IF uetrHit # {} THEN "ERR_DUP_UETR"
    ELSE IF req.nonce # 0 /\ req.nonce <= nonceLast THEN "ERR_REPLAY"
    ELSE IF \E i \in 1..Len(ln) : debit[ln[i].acct] + ln[i].dd < 0 THEN "ERR_FUNDS"
    ELSE IF \E i \in 1..Len(ln) : credit[ln[i].acct] + ln[i].dc < 0 THEN "ERR_CREDIT"
    ELSE "OK"

Init ==
    /\ debit = [a \in Accts |-> IF a = 1 THEN Amt ELSE 0]   \* issued earlier
    /\ credit = [a \in Accts |-> IF a = Issuer THEN Amt ELSE 0]
    /\ journal = [s \in Slots |-> Empty]
    /\ seq = 0
    /\ nonceLast = 0
    /\ hist = <<>>

Post(req) ==
    /\ Len(hist) < MaxPosts
    /\ Status(req) = "OK"
    /\ LET ln == Lines(req)
           slot == seq % Window
           tgt == IF req.op.kind = "return"
                  THEN CHOOSE x \in Find(req.op.target) : TRUE ELSE -1
           tseq == IF tgt >= 0 THEN journal[tgt].seq ELSE -1
           rec == [used |-> TRUE, seq |-> seq, key |-> req.key, uetr |-> req.uetr,
                   nonce |-> req.nonce, kind |-> req.op.kind, lines |-> ln,
                   target |-> tseq, reversed |-> FALSE]
           \* pay_ledger_reverse sets o->reversed AFTER the post wrote its own
           \* record; o points at a slot, so if the post just reused that slot
           \* the flag lands on the new record (see FORMAL_INVARIANTS.md).
           j1 == [journal EXCEPT ![slot] = rec]
       IN
       /\ debit' = [a \in Accts |-> debit[a] +
                      LET m == {i \in 1..Len(ln) : ln[i].acct = a}
                      IN IF m = {} THEN 0 ELSE ln[CHOOSE i \in m : TRUE].dd]
       /\ credit' = credit
       /\ journal' = IF tgt >= 0 THEN [j1 EXCEPT ![tgt].reversed = TRUE] ELSE j1
       /\ seq' = seq + 1
       /\ nonceLast' = IF req.nonce # 0 THEN req.nonce ELSE nonceLast
       /\ hist' = Append(hist, [d |-> Digest(req), seq |-> seq, kind |-> req.op.kind,
                                target |-> tseq])

Next == \E req \in Reqs : Post(req)

Spec == Init /\ [][Next]_vars

-----------------------------------------------------------------------------
RECURSIVE SumF(_, _)
SumF(S, f) == IF S = {} THEN 0 ELSE LET x == CHOOSE y \in S : TRUE IN f[x] + SumF(S \ {x}, f)

Conservation == SumF(Accts, debit) = SumF(Accts, credit)
NoNegative == \A a \in Accts : debit[a] >= 0 /\ credit[a] >= 0

\* No request (same key, UETR, nonce and lines) is ever applied twice.
ReplayNeverApplied ==
    \A i, j \in 1..Len(hist) : i < j => hist[i].d # hist[j].d

\* No posting is reversed twice.
ReverseOnce ==
    \A i, j \in 1..Len(hist) :
        (i < j /\ hist[i].kind = "return" /\ hist[j].kind = "return")
            => hist[i].target # hist[j].target

\* The 2 units issued to holder 1 are never spent twice: holders together
\* never hold more than was issued.
NoDoubleSpend == debit[1] + debit[2] + debit[3] <= Amt
=============================================================================
