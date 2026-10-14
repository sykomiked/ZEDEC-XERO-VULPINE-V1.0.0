# The Pit Boss: the supervisor of the floor

The Pit Boss watches everything that runs: the Cage (`docs/CAGE.md`), the
desks, the file custodian (`docs/BANKER.md`), the Rosetta Shell
(`docs/ROSETTA_SHELL.md`) and the node's swarm relations. It enforces the
house rules, routes work to the right desk, and escalates what needs a
person.

It has two parts, and keeping them separate is what makes it trustworthy:

| Part | Is | Does | May act on its own? |
|---|---|---|---|
| **The rulebook** | Deterministic code: checks with fixed thresholds | Halts, refuses, quarantines, shuns | Yes: every action is a rule firing, logged with the rule and the evidence |
| **The advisor** | A small local model | Triages unknown inputs, explains incidents, drafts resolutions and new rules | No: it proposes; the user or a rule decides (the rule of `docs/AI_MODEL.md` §3) |

A model never halts a process, bans a peer or changes a record by itself.
A wrong guess there would be the very kind of uncontrolled behaviour the
Pit Boss exists to stop.

## 1. Invariant watch (the rulebook)

| Rule | How it is detected | Action |
|---|---|---|
| No floating point in exact pipelines | Statically: the Z-Wasm validator refuses float instructions; ZXV kernel images are built without an FPU; legacy inputs are parsed into exact decimal, never into binary floats | Refuse before the first instruction runs, so nothing has to be caught mid-flight |
| Bounded rationals | Each rational pipeline declares its numerator and denominator limit; exceeding it is detected at the operation | Halt with FALSE and an audit entry. The Pit Boss never silently switches to a rounded form: changing exactness is a decision for the user, made explicitly |
| No runaway work | Fuel and memory limits in the Cage; CPU budgets per agent (`swarm_budget`, `ct_budget`) | Stop the run with NEITHER (fuel exhausted), record it, offer more fuel or refusal |
| No contradictory state in the store | Every admission goes through the Cage's clearance | A BOTH is held; it never reaches the store as clean |

Exact rational arithmetic already reduces fractions as it goes. The rule is
about growth that reduction cannot stop, such as a recursion that doubles
the denominator's size each step.

## 2. Floor management (rulebook first, advisor second)

- **Triage.** Known formats are recognised by deterministic detectors: TZif,
  GGUF, Z-Wasm, an EBCDIC copybook layout, packed-decimal fields. An
  EBCDIC batch job therefore reaches the Archives Desk by rule. Only an
  input no detector recognises goes to the advisor, which proposes a desk,
  and the item stays uncleared until a desk's checks pass.
- **Contradictions.** A BOTH first goes to the Arbitration Desk's
  resolution rules (`pdx_agent`: source priority, recency, quorum). If no
  rule resolves it, the Pit Boss escalates to the user with the advisor's
  summary of the two sides and the rules that did not apply.

## 3. Swarm referee

- **Bad peers.** A peer whose result fails a check (AIPI CHECKER or CERT, or
  a QUORUM disagreement where the other peers agree bit for bit) is
  recorded against its identity key in `kernel/src/reputation`. Past a
  rule's threshold it is shunned: no new tasks go to it, and its pending
  tasks are re-sent to other peers.
  - Shunning is local and reversible. It is a decision about whom this
    node trusts, not a penalty imposed on the peer.
  - Every shun names the failed checks.
- **Offering spare capacity.** When the machine is idle, the Pit Boss may
  advertise spare compute to the mesh. It does so only within limits the
  user has set: which resources, how much, at what price, and at which
  hours. The limits follow `ct_budget`'s power and thermal readings. By
  default nothing is offered.

## 4. Reports

Every action the Pit Boss takes writes one record to the custodian's
journal. A record names:
- the rule that fired;
- the evidence: hashes, the check's verdict, fuel used;
- what was done;
- how to undo it, where it can be undone.

The advisor turns these records into plain-language incident notes on
request. The records themselves are written by the rulebook, so they are
exact.

## 5. Build order

| # | Slice | Acceptance test |
|---|---|---|
| 1 | Rulebook core: rules as data (condition, threshold, action), a firing log in the journal | Each built-in rule fires on its test case and logs rule, evidence and action |
| 2 | Invariant rules wired to the Cage (fuel, memory, rational bounds, float refusal) | A float-using module is refused before running; a denominator overflow halts with FALSE |
| 3 | Deterministic triage detectors and desk routing | Each known format reaches its desk; an unknown one waits uncleared |
| 4 | Peer referee: failed checks recorded per identity key, shun thresholds, task re-send | A peer that fails quorum is shunned and its task completes elsewhere |
| 5 | Capacity offers within user limits | Nothing is offered by default; an offer never exceeds the set limits |
| 6 | The advisor: triage proposals, incident summaries, draft rules (needs `docs/AI_MODEL.md` slice 3) | Its proposals change nothing until accepted |

## 6. Limits

- The Pit Boss supervises what runs through ZXV. On a hosted system it
  cannot see or stop other programs on the same machine.
- Thresholds are policy. They start conservative and the user can tighten
  or loosen them; every change is journaled.
