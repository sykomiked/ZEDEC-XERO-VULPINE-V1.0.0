# Paradox engine

`kernel/src/paradox/` (roadmap 6.6b, steps P0.1 to P0.4). It is freestanding
C11 with integers only. It uses no libc, no allocation and no floating point,
and every array it uses is passed in by the caller with a fixed size. Tests are
in `kernel/src/paradox/test_paradox.c` (`make -C kernel test-paradox`, also run
by `verify-all`).

## What it does

A *paradox* here is a unit of work whose four-valued lpres state is **Both**
(the evidence points both ways) or **Neither** (there is no evidence). The
engine never tries to decide whether the claim is "really" true. It works one
level up:

- Level 0 is the evidence about a claim.
- Level 1 is the 64-state tag from `lpres_hex.h`: base, polarity (the evidence
  trend) and lifecycle phase.
- Level 2 is a fixed operator for each kind of paradox. Each operator writes a
  record into a hash-chained log, so a run can be replayed and checked.

It adds no new truth type and does not change the lpres or lpres_hex API.

## The four kinds

| Kind | When | What the engine does |
|---|---|---|
| **Constructive** | Both, and the gap between evidence for and against is growing | Applies a parameter update (budget shares, routing weights, escrow timing) only when every new value is inside its published minimum and maximum. If any value is outside, the update becomes a proposal for the owner. The old values are kept, and the next evaluation puts them back if the tension went up. The function has no balance input, so it cannot change a balance. |
| **Destructive** | Both, and the gap is shrinking or the evidence is stale | Checks for a step limit, budget exhaustion, a repeated state (a cycle) and an invariant callback. It puts the work in the lpres_hex quarantine log, restores the state from its checkpoint, and fills in a RepairProof: the hashes of the state before and after, the step count, which checks fired, and a signature that peers can add to their registry. It only contains the work. It has no way to reply to or act against the sender. |
| **Neutral** | Both with a steady trend (past the first phase), or Neither | Keeps up to N candidate answers (for example two peers' timestamps) and settles only when one candidate leads every other by a set margin of evidence, or when the owner picks one. Nothing settles while it is waiting. |
| **Special** (unclassified) | Both with no trend yet, and no match in the signature registry | Writes a permanent record to a separate append-only log (the frontier), which is never edited or erased. A draft classification (kind and reason code) goes to review. An accepted draft becomes a new registry entry, so the next case like it is recognised. Drafts flagged as touching money or devices need the owner's approval. Other drafts can be accepted by consensus. |

A signature found in the registry (the SHA-256 of a description of the anomaly)
overrides the trend. True and False are not paradoxes.

**Forbidden is separate.** A tag with forbidden polarity means a real check
found a broken invariant (for example A ≠ L + E). Every operator refuses it and
rolls it back, whatever its kind. Use the destructive operator to quarantine
it.

## Complex paradoxes

A complex paradox is a set of linked paradoxes, stored as a graph of up to 64
nodes. Each node has a kind and an integer tension pair (magnitude, direction),
and an edge means one node depends on another. Each pass works in a fixed order:

1. **Destructive and forbidden nodes first.** They are contained, and every
   node that depends on them, directly or through others, is tainted.
   Containment and taint are permanent within the graph.
2. **Unclassified nodes next.** They are sandboxed, and the nodes that depend
   on them wait.
3. **Neutral nodes.** They are held.
4. **Constructive nodes last.** They are applied only if no contained, waiting,
   sandboxed or held node leads to them.
5. **Repeat** when new evidence arrives. When every node is final, the graph is
   resolved. When it reaches the step limit, it goes to the owner
   (`ESCALATE_TO_OWNER`) with the whole graph.

Before resolving, the nodes are sorted by their id (the hash of the signed
input), and dependencies are kept as bit masks over that order. When several
updates for one node arrive in the same batch, the worst one wins (forbidden,
then destructive, special, neutral, constructive). As a result, the same inputs
in any arrival order give the same states. Each pass adds a SHA-256 link to the
graph's chain and writes one log record. The tests check this with 1,000
shuffled scenarios and three simulated peers that each receive the inputs in a
different order.

## Limits

- The engine detects only what its checks look at: the step limit, the budget,
  a repeated state fingerprint, the caller's invariant callback and the
  registry. Anything those checks do not see goes undetected.
- This is contradiction-tolerant bookkeeping. It contains, holds and routes
  contradictions. It does not make both sides of a contradiction true, and
  under conservation (A = L + E) only one of two conflicting spends can ever
  settle.
- Signing log heads and RepairProofs, and sending signatures to peers, are the
  caller's jobs. The engine produces the hashes.
- If a log is full, the operator halts and changes nothing. The exception is
  the destructive rollback, which is always safe and still happens. The caller
  must stop.
- An update that has already been applied stays applied in the graph unless a
  later destructive finding taints it. Undoing it is the constructive
  operator's rollback.
- Results are deterministic for the same inputs. Nodes agree because they apply
  the same order to the same signed inputs, not because of any shared clock or
  instant network-wide change.
