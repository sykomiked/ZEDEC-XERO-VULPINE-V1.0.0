# Peer audit: two-layer verification for the swarm

Module: `kernel/src/peer_audit/` (`peer_audit.h`, `pa_runtime.h`). Test:
`kernel/src/peer_audit/test_peer_audit.c`, run by `make -C kernel verify-all` (plain and under
ASan+UBSan), with a freestanding aarch64/i386 build check (no libc, no 64-bit division helpers).

This is verification logic. It is not yet joined to the network transport (see "Not wired yet").

## Why two layers

A node can be wrong in two ways: by accident (a bug, a bad input, an exhausted budget) or on
purpose. A local check catches the first kind cheaply, before anything leaves the node. It cannot
catch the second kind, because a dishonest node can switch its own checks off. So every
transition is checked twice: once by its author before signing, and again by other nodes that
do not trust the author.

## Layer 1: self-audit on the node, before signing

`pa_emit_record` is the only function that produces a signed record, and it always runs
`pa_precommit_check` first. The check is given a `pa_precommit_t`:

| Check | Rule | Failure |
|---|---|---|
| Value conservation | `sum_before + minted == sum_after + burned`, both sides computed without overflow | `PA_ERR_CONSERVATION` / `PA_ERR_OVERFLOW` |
| Token budget | `tokens_requested <= tokens_remaining` | `PA_ERR_BUDGET` |
| ISF headroom | `h_t > 0` (h_t <= 0 means stop) | `PA_ERR_HEADROOM` |
| Size bounds | output size `<=` limit, inputs and outputs `<= PA_MAX_IO` | `PA_ERR_SIZE` |

`pa_runtime.h` fills these in from the real modules. For a ledger transfer the conserved quantity is
the DEBIT total of the asset over all capital forms (`pay_ledger_totals`), and a ledger that fails
`pay_ledger_check` counts as a conservation failure. For a swarm budget grant the conserved quantity
is the remaining tokens: remaining after plus granted must equal remaining before, and the grant
must not exceed what the model had left (`swarm_budget_remaining`).

On failure nothing is signed: the output record is zeroed. The node moves to
`PA_NODE_CONTRADICTION`, which has the same value as `LPRES_STATE_BOTH` in `lpres.h` (the
paraconsistent state for a contradiction: the node's evidence says both "valid" and "invalid"). As
in the swarm_logic L7 paradox trap, a node in that state produces only S0 output, which has no
effect. Every later emit is refused with `PA_ERR_QUARANTINED` until an operator calls
`pa_node_release`. The event is recorded in a bounded log; the count keeps rising after the log is
full, but no further entries are stored.

## Layer 2: peer audit across the network, trusting no one

### A. Deterministic replay

A record is `{kind, node, seq, prev_state_hash, inputs, claimed_outputs, new_state_hash}` plus the
author's ML-DSA-65 signature (`pqsec`, context `zxv/peer-audit/v1/record`). Its hash is SHA-256
over a fixed little-endian encoding of every field except the signature.

A verifier keeps its own copy of the state (a replica). It checks the signature, checks that its
replica is at `prev_state_hash`, applies the inputs to a scratch copy using the same module code,
and compares both the outputs and the new state hash byte for byte. Two transition kinds are wired
today, and both are integer and deterministic:

- `PA_KIND_LEDGER_TRANSFER`: `pay_ledger_transfer` on a `pay_ledger_t`. The input carries a
  signed amount, so negative and zero challenges use the same format. The UETR, idempotency key
  and end-to-end id all come from the input, so replay gives the same result on every node. The
  state hash covers seq, the provenance chain head and every account.
- `PA_KIND_BUDGET`: `swarm_budget_begin_cycle`, `swarm_budget_consume` and `swarm_budget_end_cycle`
  on a `swarm_budget_t`. The state hash covers every field.

All ledger and state-machine records are replayed (100%).

**Committee.** `pa_committee` builds the witness set from the epoch's peer list. It sorts the list,
removes duplicates and removes the author. It then draws k witnesses with a partial Fisher-Yates
shuffle driven by `SHA-256("zxv-pa-committee" || record_hash || counter)`, using rejection sampling
so there is no modulo bias. The committee depends only on the record and the peer list, so the
author cannot pick its witnesses except by changing the record itself (see the threat model).

**Quorum.** `pa_quorum` counts signed votes (`pa_vote`, context `zxv/peer-audit/v1/vote`) from a
committee of at least 3f+1. A vote is ignored if its signature is bad, its voter is outside the
committee, its voter has already voted, or it names a different record hash. ACCEPT or REJECT needs
2f+1 votes; anything less is UNDECIDED. A verifier that does not hold the right state abstains: it
does not vote either way.

### B. Challenge injection (canaries)

A canary is an ordinary `pa_request_t`, with the same format and size as a real request and a fresh
random nonce. Only the challenger's own table marks it as a canary. Its input is a boundary case:
a negative or zero amount, `INT64_MAX`, or a known bad sequence. The challenger works out the
correct answer by running the request through its own replay, so a correct answer is fail-closed:
refused, with the state unchanged. When the target's signed answer comes back, `pa_canary_check`
compares it to that expected answer. A wrong answer counts as a failed challenge, and because the
answer is signed it is also ordinary evidence (C). Each target gets at most
`cfg.canary_per_window` canaries per tick window, and the table has a fixed size (`PA_ERR_FULL`
when it is full).

### C. Isolation, never confiscation

`pa_evidence_make` builds a `pa_evidence_t`. It contains the accused's own signed record and the
accuser's replay result, and the accuser signs it (context `zxv/peer-audit/v1/evidence`). It
refuses to build evidence against a record that replays correctly. `pa_evidence_check` lets any
third node check the evidence with its own replay, so no node has to trust the accuser:

| Result | Meaning | Action |
|---|---|---|
| `PA_OK` | accused's signature valid, our replay differs from the claim and equals the accuser's result | accused blocklisted locally; host `revoke` callback called with the evidence hash |
| `PA_ERR_FALSE_ACCUSATION` | the record replays correctly, or the accused never signed it | one strike for the accuser (who signed the evidence); at `cfg.strike_limit` the accuser is isolated |
| `PA_ERR_SIG` | accuser signature invalid | dropped; it cannot be attributed, so nobody is penalised |
| `PA_ERR_INCONCLUSIVE` | we lack the state, or the accuser's "correct" result is not ours either | no action |

The evidence object travels to other nodes as it is, and each one checks it independently.

**No funds are moved.** The module never posts to, freezes or seizes any account. Isolation means
the local blocklist plus the host's `pa_revoke_fn`. The host connects that callback to
porter_house (close the peer's port seals or capability tokens) and to vinea (`vna_rt_remove`, which
drops the peer from routing). The test checks that a node's ledger hash and balances are unchanged
after it acts on evidence. Any action involving money still needs the user's confirmation through
the existing pay flows.

### Inference spot checks (cost control)

Model output is not replayed every time. The node publishes a commitment,
`SHA-256("zxv-pa-infer-v1" || model_cid || seed || prompt || tokens)` (`pa_infer_commit`). A
verifier samples a commitment when `SHA-256(beacon || commitment)` falls under the configured rate
(`rate_q16 / 65536`). The beacon must be randomness published after the commitment, so the node
cannot know in advance which outputs will be checked. A sampled output is replayed with the integer
tensor engine (`pa_spot_check` with a `pa_infer_fn`) and compared bit for bit. The engine is
bit-identical across CPUs. The test uses a tiny deterministic model on `zt_quantize`/`zt_matvec`,
not a full GGUF model.

## Threat model

What it stops:

- A node that signs a wrong balance, a wrong output or a wrong state hash, even by one unit. Every
  honest verifier catches it, whether or not the node's self-audit was switched off.
- A node that accepts invalid input (a negative, zero or oversized amount): caught by canaries.
- Up to f Byzantine voters in a 3f+1 committee: the honest verdict still reaches 2f+1. With f+1
  bad voters the test shows a lie is still never accepted, but the round may end UNDECIDED (safety
  is kept, progress is lost).
- False accusers: evidence that does not verify counts against the accuser, who signed it.
- Tampered records, votes or evidence: ML-DSA-65 signatures over SHA-256 hashes, with a separate
  context string for each object type.

What it does not stop:

- **Collusion by more than f committee members.** If 2f+1 witnesses collude, they can accept a lie
  that every other node rejects on replay. Anyone holding the state can still check the record
  afterwards, but this module does not escalate automatically beyond the committee.
- **Sybil identities.** Committee draws are uniform over the peer list. Anyone who can add many
  identities to that list can win a larger share of seats. Making identities costly (ident, the
  bootlegger handshake) is out of scope here.
- **Grinding.** An author can try many variants of a record (for example by changing the nonce) to
  get a different committee. Each try costs a signature and gains nothing unless the attacker
  already controls a large share of the peers. A beacon-mixed seed would close this gap and is not
  implemented.
- **State availability.** A verifier without the previous state abstains. Syncing state between
  nodes is the host's job.
- **Non-deterministic work.** Only the deterministic kinds above are replayed. Floating-point or
  externally timed work cannot be audited this way.
- **Inference that is not sampled** is trusted until it is sampled. The rate sets the trade-off:
  at rate r, a node that cheats on a fraction c of its outputs goes unnoticed for about 1/(r*c)
  outputs on average.

## Not wired yet

- **Transport.** Records, votes, canaries and evidence are not yet carried between machines. Hooks
  needed: on each outbound ledger or budget transition in the app, call `pa_emit_record` instead of
  signing directly. Ship the record over vinea (a new `vinea/v2/...` message type) to the
  committee from `pa_committee`. Collect `pa_vote` replies and apply with `pa_runtime_apply` on
  ACCEPT. Gossip `pa_evidence_t` objects.
- **Revoke wiring.** The host's `pa_revoke_fn` should call `porter_house_close_port` for the
  peer's seals and `vna_rt_remove` for its routing entry. This is not done here because both
  modules are being edited in parallel.
- **Peer list per epoch.** Committees assume every node uses the same peer list. Agreeing on that
  list (for example from ident or the vinea DHT) is not implemented.
- **Full model replay.** Spot checks are tested with a tiny model on the tensor engine. Running
  `zt_model_generate` on a real GGUF file through `pa_infer_fn` is not tested.
- **Persistence.** Blocklists, strikes and the event log live in memory only (`pa_ctx_t`).

## Tests (`test_peer_audit`, 101 checks)

These checks run in the test: self-audit checks and the contradiction state; an honest record
replayed bit-exact by every verifier and accepted by quorum; replicas agree after apply; a +1 state
lie caught by the node's own self-audit; the same lie with self-audit off caught by all 9 other
verifiers and rejected by the committee; an output-only +1 lie caught by all verifiers; evidence
verified on third nodes, with revoke called and no funds moved; false accusations and forged
records counted against the accuser; a bad accuser signature dropped without penalty; committee
determinism, independence from list order and duplicates, author exclusion, and uniformity (5 sigma
over 3000 draws); Byzantine quorum with f and f+1 faulty voters, and forged, duplicate and outsider
votes; canaries that catch a node accepting a negative transfer while honest nodes pass negative,
zero and maximum-amount canaries; canary rate limit and full table; budget replay and the budget
self-audit; spot-check determinism, a single changed token, a wrong model CID, and the sampling rate
within 5 sigma over 20000 commitments; bounded peer table.
