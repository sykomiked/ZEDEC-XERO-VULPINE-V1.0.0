# Wave 31 task: Conclave (slot 369), Your swarm
Read ../APP_WAVE_SPEC.md first. This is an L3 app: manifest says `layer L3` and composes its built_from apps with `use`; it may only start after those waves are merged.
Depends on: wave 15 (185 skep); wave 16 (196 stoker); wave 18 (225 tributary); wave 5 (063 sieve, 062 kitling).
Manifest: `slot 369`, `layer L3`, `version 1.0.0`, `icon icons/l3/369-conclave.svg`, `budget 233`, model `apps/conclave_model.{h,c}`, blocks under kernel/src/apps/blocks/conclave/, one `use <id> 1` per part below. Each part's manifest is itself the block (C1): an L2 part exports its own entries and the L1 entries it composed (`<part>.<block>.<entry>`). The composed logic lines must fit ZXAPP_LOGIC_MAX (128) and the uses ZXAPP_USES (8); if either fails, stop and report it; do not raise a limit or edit a part.
What it does: Runs your local AI swarm: choose models for this machine (`model.info`, `power.status`, `power.top`), set token budgets (`power.budget`, `swarm.state`), see each agent's work (`swarm.state`, `journal.query`) and accept or refuse its suggestions (`swarm.ask` answers held, `play.draft` untrusted). Changing the model slot and per-agent token budgets have no method beyond `power.budget`: use only existing methods; list what's missing in your report.
Uses: `use stoker 1` ("admit this model", tokens per second per watt, layer placement); `use tributary 1` (agreeing answers, "may ask another model"); `use skep 1` ("within the agent's budget" and task placement); `use sieve 1` ("may this be applied" and held counts); `use kitling 1` (growth stage and Zeckendorf charge). Call these composed entries; do not re-implement them.
Logic (new decisions only; each `name` is the entry; a critical one has two or three `logic` lines for one `provides`, so the witness holds it as BOTH when they disagree):
- `accept_ok`: (critical) "act on this suggestion" (the person accepted it AND Sieve's `may this be applied` TRUE AND the agents agree or the person chose a side) in Ada as Eve + APL as KRON witnessed; nothing acts on BOTH
- `budget`: (critical) token budget per agent per cycle over the D1-D9 faces (sums exact, refusing on overflow) in FORTRAN as Trebago + COBOL as Sutra witnessed
- `budget_ok`: budget limits (no agent above its cap, the swarm total within the owner's) in Ada as Eve
- `agents`: agent tensor tables (agents x tasks x tokens) in APL as KRON
- `cycles`: cycle ledger (cycles run and tokens spent, refusing on overflow) in COBOL as Sutra
- `records`: agent records in MUMPS as MYRA
Paraconsistent (BOTH test, in test_zxapp.c): two agents give different answers with evidence: the result is BOTH; the test asserts state 3 and that both answers and their evidence are shown and nothing is acted on until the person accepts one. Join the sources with `lpres_conjoin` / `lpres_disjoin` in C; never re-derive the tables.
Safety: Nothing runs or spends without the owner's accept (M3); the swarm never moves money.
Integers only: no float or double anywhere; a division that is not whole uses kernel/src/rational in C.
Tests: as the spec, plus a compose test (the app composes against a registry holding its parts, and every part entry it calls resolves) and the BOTH test above.
needs: ai.ask, swarm, sys.read, journal, notify.
Pending (P1; the best-fit language has no front-end yet, the block above stands in): `pending Python agents`; `pending Occam budget`; `pending Simula_67 budget_ok`.
Planned (no runtime yet): Python, Occam, Simula 67.
