# Paraconsistent ledger and four-valued agents

Roadmap 6.6b, steps P1.1 to P1.4 (the contradiction-tolerant ledger) and P2.2
(agents that carry contradictory premises). Both are built on the paradox
engine (`kernel/src/paradox/`, see [PARADOX_ENGINE.md](PARADOX_ENGINE.md)) and
the 64-state hexagram tag (`kernel/src/lpres/lpres_hex.h`). Both are
freestanding C11 with integers only: no libc, no allocation, no floating point.
Every array is the caller's, with a fixed capacity.

- `kernel/src/paradox/pdx_ledger.h` / `.c` (prefix `plg_`)
- `kernel/src/paradox/pdx_agent.h` / `.c` (prefix `pag_`)
- tests: `kernel/src/paradox/test_pdx_ledger.c` (`make -C kernel test-pdx-ledger`,
  also run by `verify-all`)

## The rule that never bends

A contradiction is **held**, never exploded, and **conservation always holds**.
"Held" means both conflicting claims are kept, the money they fight over sits
in escrow, and nothing settles until a published rule, a witness majority, an
arbiter or the owner picks one. Under A = L + E only one of two conflicting
spends can ever settle. Resolving a paradox means choosing by a published rule,
not letting both succeed. A broken invariant (A ≠ L + E) is not a paradox: it is
FORBIDDEN, and the ledger halts.

## P1: the ledger

### Model

- **Accounts** are numbered `0 .. n-1` with genesis balances. The ledger also
  stores its reserve assets **A** and the operator's equity **E**, fixed at
  genesis. Liabilities **L** are every balance plus everything in escrow.
  `A = L + E` is checked by summing every balance from scratch after every
  epoch of the replay, never from a running total.
- A **claim** is a signed spend: its id (the hash of the signed input), the
  sender, the recipient, the amount, the sender's **nonce** and a signed
  sequence number. The caller verifies the signature. A claim with a bad
  signature, a zero amount, an unknown account or a self-transfer is refused at
  the door and never stored.
- A **slot** is `(sender, nonce)`. A sender can spend each nonce once. Two
  claims on one slot are a double spend: the slot is **Both**.
- A **vote** is a signed statement about a claim: a **witness** attests it saw
  that claim (for or against), an **arbiter** rules it the winner, or the
  **owner** rules it the winner.
- Every claim and vote carries an **epoch**. The ledger's **horizon** is the
  epoch up to which every peer's events are known (the caller's finality
  protocol decides when to move it). Events at or below the horizon are
  **final**; later ones are **conditional**.

### State is a function of the event set (P1.3)

The ledger stores claims and votes as two sets, kept sorted in a canonical
order (epoch first, then slot, then id). Balances, escrow and every claim's
state are recomputed by a deterministic replay of the final events, epoch by
epoch. So:

- **Merge is set union.** `plg_merge` adds the other ledger's events.
  Adding the same event twice changes nothing, and merging in any order gives
  the same set. Two nodes with the same events and horizon get the same
  balances and the same state digest, byte for byte.
- **Final outcomes never change.** Epoch `e` is replayed using only events with
  epoch ≤ `e`, and nothing at or below the horizon can be added later (it is
  refused as LATE), so moving the horizon forward never rewrites an earlier
  epoch.
- **Partitions accept conditional claims.** While a node is cut off, its horizon
  stays put: local claims are accepted as PENDING (conditional) and move no
  money. A local claim is refused only when it exceeds the sender's final
  balance minus their other pending claims, as a courtesy check; claims that
  arrive by merge are never refused for funds, because the union must stay a
  union. On reconnect, after merge and once the horizon passes their epoch,
  claims that cannot all be honoured are decided by the replay: a slot with two
  claims is Both, and a spend the sender cannot fund is INSUFFICIENT and
  quarantined. Value is never created during a partition.

### One epoch of the replay

1. **New claims.** For each slot touched in this epoch, in canonical order,
   repeating until nothing changes (so A→B then B→C in the same epoch both
   settle whatever the account numbers):
   - one claim on a new slot: pay it if the sender can, else wait for the next
     pass;
   - two or more on a new slot (**P1.1**, Both): apply the default rule; if it
     names a winner, pay that one; otherwise move the largest claimed amount
     into **escrow** (HELD);
   - a claim on a HELD slot joins the dispute and the escrow is topped up to
     the new largest amount;
   - a claim on a slot that already closed (settled or insufficient) is LOST.
   After the last pass, unfunded single claims are **INSUFFICIENT** (closed,
   quarantined) and unresolved disputes hold whatever the sender has left.
2. **Rulings.** Every HELD slot is checked against the votes up to this epoch,
   in the dispute funnel order (**P1.2**):
   1. an **owner** ruling;
   2. an **arbiter** ruling (not for a FROZEN slot);
   3. **witness evidence**: each witness counts once per claim, and a claim
      settles when its net evidence leads every other claim by the configured
      margin. This is the paradox engine's neutral operator (`pdx_hold`).
   The winner is paid from escrow and the rest is refunded. If the winner asks
   for more than escrow plus what the sender has left, it is INSUFFICIENT
   and the whole escrow goes back.
3. **Check.** `sum(balances) + escrow` must equal the genesis liabilities and
   `A` must equal `L + E`. If not, the replay stops: the state is FORBIDDEN.

The **default rule** (first valid signature) picks the claim with the lowest
signed sequence number. A tie is not broken by hash (an attacker could grind
ids): the slot stays HELD for the funnel. The rule can be turned off, so every
double spend goes to witnesses, arbiter or owner.

A slot with more conflicting claims than the configured limit is
**destructive** (a spam or grinding pattern): it is FROZEN. Its funds stay in
escrow and only the owner can rule.

### Claim states and their tags

| State | Meaning | Tag (base / polarity / phase) | Kind | Action |
|---|---|---|---|---|
| PENDING | conditional, past the horizon | Neither / neutral / emerging | neutral | wait |
| SETTLED | won its slot and was paid | True / neutral / manifest | none | proceed |
| LOST | its slot went to another claim | False / neutral / manifest | none | refuse |
| HELD | in a dispute, funds in escrow | Both / neutral / manifest | neutral | escalate (to arbiter or owner; money waits) |
| FROZEN | dispute past the claim limit | Both / entropic / manifest | destructive | escalate (owner only) |
| INSUFFICIENT | the sender could not fund it | False / forbidden / manifest | none | roll back, quarantine |

### Finality and the logs

`plg_advance` moves the horizon. It replays, checks conservation and appends a
record (horizon and state digest) to the paradox engine's hash-chained log.
Every claim that becomes INSUFFICIENT for the first time gets an
`lpx_qlog` quarantine record. Every LOST claim is a signed **equivocation** by the sender (only the
sender can sign for its slot): it gets a quarantine record whose subject binds
both claim ids, as evidence for peers. Nothing is slashed: slashing needs posted
bonds and signed slash terms, which are outside this module. When conservation
fails, the ledger halts, writes a FORBIDDEN quarantine record and refuses every
later call.

## P2.2: agents with contradictory premises

An agent keeps, for each proposition, two **support sets**: the sources that
told it the proposition is true and the sources that told it it is false
(bit masks over up to 64 sources). That is Belnap–Dunn four-valued logic:

| told true | told false | value |
|---|---|---|
| no | no | Neither |
| yes | no | True |
| no | yes | False |
| yes | yes | Both |

- **Rules** `head ← l1 ∧ l2 ∧ …` (literals may be negated) are run to a fixed
  point. A conjunction is told true when every literal is (support: the union),
  and told false when any literal is (support: the union of the false ones). A
  head is told true by any rule whose body is told true, and told false by its
  rules only when every rule for it is told false (Fitting's semantics); facts
  about the head add to both sets. The sets only grow, so the fixed point is
  reached within a bounded number of rounds.
- **No explosion.** From `p` and `¬p` the agent concludes nothing about an
  unrelated `q`: `q` stays Neither. A conclusion that depends on `p` becomes
  Both, which is correct: it carries the contradiction instead of failing.
  Note: this follows Belnap–Dunn exactly, so `Neither ∧ Both` is False (the
  Both side is told false). `lpres_conjoin` returns Neither for that one case;
  the agent does not use lpres's table.
- **A source that contradicts itself** (says `p` and `¬p`) breaks the agent's
  invariant: it is FORBIDDEN. Its premises are masked out of every derivation
  and it gets a quarantine record. This is computed from the fact set, so the
  result does not depend on arrival order.
- **Swarm merge** is a bitwise OR of support sets: commutative, associative and
  idempotent, so agents converge whatever order they gossip in.
- **Decisions.** `pag_decide` turns a proposition into a tag: the base from the
  table above, the polarity from the trend of the support counts since the last
  snapshot (`lpx_polarity`), phase manifest. The paradox engine classifies it
  (`pdx_classify`) and `lpx_action` gives the action: **proceed**, **wait**
  (defer), **escalate** (ask a person), **refuse** or **roll back**
  (quarantine). Two jurisdictions that disagree on a rule give Both: the agent
  asks instead of acting.

### Agents and the ledger together

An agent may act as a ledger witness. It votes for a claim only when its own
decision on "this claim is the valid one" is **proceed** (True and not
weakening). A Both or Neither belief abstains. So a split swarm never pushes a
dispute to settle: the money stays in escrow until the evidence is decisive or
a person rules. The tests check conservation after every step of this.

## Honest limits

- Finality is only as good as the caller's horizon protocol. If a node moves
  its horizon before it has every peer's events up to that epoch, a late claim
  is refused there and accepted elsewhere, and the nodes diverge. The digest
  shows it; the ledger cannot prevent it.
- "First valid signature" trusts the signed sequence number. A sender can
  backdate their own claims; the rule only orders a sender's own competing
  claims, which is why both are kept as evidence of equivocation.
- Every node must run the same published configuration (conflict limit,
  witness margin, default rule, witness set). Two nodes with different
  settings reach different results from the same events.
- Witness voting is only as strong as the witness set; registering witnesses
  (Sybil resistance) is the caller's job.
- Agents reason over the premises they are given. Lower confident wrong answers
  when sources conflict is the goal; it does not end hallucinations.
- Capacities are fixed. A full claim, vote or log store refuses the call and the
  caller must stop.
