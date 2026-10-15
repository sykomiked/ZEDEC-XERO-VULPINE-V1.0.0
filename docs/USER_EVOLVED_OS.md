# A user-evolved OS and a personal chiglet

ZXV has no release versions. The people who use it maintain and update it.
Anyone may fork it. Forks may differ, and every install grows to fit its
person, but every install stays compatible with every other one. Each
person's chiglet learns their habits on their own device, has its own
digital DNA and hobbies, and can visit other people's chiglets when both
owners allow it.

This page describes how that works in code and what is not built yet.

## 1. Updates come from the community, not from a release

`kernel/src/evolve` already treats a fork as a signed lineage record in a
DAG, with no version numbers (`evo.h`). `evo_comm.h` adds four rules on top
of it.

| Rule | What it guarantees | Code |
|---|---|---|
| U1 Compatibility contract | Every build speaks the pinned wire formats and passes the money-path conformance checks. A fork may add optional fields. It may not drop or retype a pinned field, add a new required one, or drop a pinned family. | `evo_contract_check` |
| U1 Contract growth | The contract has no version. Its identity is its CID, and a new contract may replace an old one only if old installs can still talk to new ones. | `evo_contract_extends` |
| U2 Independent audits | An auditor signs a PASS or FAIL for one record, against one contract CID, with the conformance it observed. An audit may name a bounty claim. | `evo_audit_open` |
| U3 Install gate | A record installs only when it is in the person's lineage DAG (signed and conflict-free), keeps the contract, and has passed k distinct auditors from the person's own list (default 2, never its author). No listed auditor may have failed it, the person must not have blocked it or its author, and the person must have said yes to that exact record before the yes expired. | `evo_community_gate` |
| U4 Rollback | A new build runs as a trial. A failed health check, or no confirmation within the trial window, rolls back to the last kept build. The person can roll back at any time. | `evo_journal_*` |

Bounties: when the gate's result has `bounty_ready` set, k independent
auditors passed a change that names a bounty claim. The "Bug and
improvement bounties" work pays the fixer in Vino from that signal; this
module pays nothing itself.

## 2. The chiglet learns its person (on the device only)

`kernel/src/chiglet/chg_bond.h`: a small private model of one person's
habits.

- Three experts read the context: hour of the week, the app in front, and
  the previous action. Each keeps a weight row over the person's own
  actions.
- Each row follows the Interaction Surplus Framework open-system rule:
  Q ← Q − δQ + ηS − C. Doing something adds η. Accepting a suggestion adds η
  again, turning one down costs C, and old habits fade by δ.
- The experts are combined by how independent their evidence is, using
  u = 1 − (a·b)²/(|a|²|b|²) in exact integers. Experts that point the same
  way count as one voice, so repeating evidence never adds confidence. Each
  suggestion reports R, the number of independent reasons behind it.
- It only suggests when the top action has a clear share and was seen more
  than once. Suggestions are held until the person accepts them.
- The person owns it. They can pause learning, see why something was
  suggested, and forget one action, one app or everything. Export works
  only after the owner opts in, and import accepts only the same owner's
  data, for moving to their own new device.
- `chgb_app_affinity` lets the shell arrange each install around how its
  person actually works.

## 3. Digital DNA, hobbies and visits (section 5d of the model lineup)

`kernel/src/chiglet/chg_kin.h` builds the design in
`zxv-docs/SWARM_MODEL_LINEUP_2026-10-15.md` §5d.

- **DNA.** Each DNA record holds the owner key id, species, lineage, parent
  DNA, birth seed, the eight genes `swarm_dna` grows from the seed, and the
  generation. Its id is SHA3-256 of the record, and a clash in the registry
  re-seeds the record at birth. Agents spawned by the agency carry their
  parent's id, so the family tree is a hash chain. The genes are the ones
  `zui_chiglet` draws, so a visiting chiglet looks like itself on any
  screen. A person names their chiglet once (`chg_name`). The name gives
  the seed and leans temperament, warmth and pace, and `chgk_dna_birth_named`
  stores those leaned genes in the hashed record, so the personality is
  fixed by the id. A named birth never re-seeds on a clash, and the name and
  its salt never travel.
- **Hobbies.** Each member gets 3 to 5 hobbies out of a catalogue of 144,
  picked by its DNA id, and no two members of one agency share a set.
  Hobbies keep deviation constructive without drift:
  - Hobby time comes only from the idle growth-band tokens.
  - Hobbies only break ties between equally scored proposals and never
    change a score.
  - Hobby weights move by at most 3 per cycle, and only while the task is
    going well.
  - Drift from the DNA profile is measured with the ISF u. Past the cap the
    weights reset to the DNA, and the reset is counted.
- **Visits.** When two people connect, both owners sign a grant that names
  the topics, the depth (claims, or claims and verdicts) and the time
  window. Either owner can revoke it, a used grant is refused, and either
  side can end the visit at any time. Only claims and verdicts with
  evidence hashes can pass between chiglets; no message kind exists for raw
  data, files or keys. Every exchange goes into a hash-chained log the
  owner can recompute.
- **Experience.** A chiglet keeps only the claims its own checker passes.
  Each one earns experience scaled by the ISF surplus
  f(u) = ln(1 + 7u) of the two genomes, so a more different friend teaches
  more. The gain is divided by how often the two chiglets have met, so
  farming one friend does not pay. Cooperation also earns Social capital.

## 4. Tests

`make -C kernel verify-all` runs the following:

- `test_evo_comm`: contract, extension, ML-DSA-65-signed audits, every gate
  rule, and the rollback journal with a 20,000-step random invariant check.
- `test_chg_bond`: the ISF parameter against a float reference, a learned
  weekly routine, the duplication check, feedback, decay, eviction, owner
  controls, tamper-checked export, and a 60,000-step random run.
- `test_chg_kin`: DNA, unique hobby sets across a 60-member agency, the
  drift cap, the budget and tie-break, grants (both owners sign, replay and
  revoke), scope, the log chain, and that experience grows with genome
  difference.
- A freestanding check that all three build for aarch64 and riscv32 with no
  libc or libgcc calls.

## 5. Not built yet

- The chiglet popping up on screen during a visit. The visitor's genes
  arrive in its DNA record and `zui_chiglet_draw` can already draw them; the
  window and call surface are UI work.
- Carrying grants and claims over Web 4 envelopes (`web4_agent`
  `w4_env_*`) on the Vinea mesh, and the zxvd RPCs that drive the bond,
  the gate and visits.
- The launch contract itself, meaning the list of wire formats every
  install must speak. It should be pinned once the zxvd RPC surface
  settles.
- Bond data feeding the companion's mood and suggestions in the shell.
