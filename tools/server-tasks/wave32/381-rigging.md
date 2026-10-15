# Wave 32 task: Rigging (slot 381), Orchestration console
Read ../APP_WAVE_SPEC.md first. This is an L3 app: manifest says `layer L3` and composes its built_from apps with `use`; it may only start after those waves are merged.
Depends on: wave 0 (061 murmuration), wave 15 (189 flue), wave 16 (201 gearwright), wave 18 (225 tributary), wave 20 (251 swell).
Manifest: `slot 381`, `layer L3`, `version 1.0.0`, `icon icons/l3/381-rigging.svg`, `budget 233`, model `apps/rigging_model.{h,c}`, blocks under kernel/src/apps/blocks/rigging/, one `use <id> 1` per part below. Each part's manifest is itself the block (C1): an L2 part exports its own entries and the L1 entries it composed (`<part>.<block>.<entry>`). The composed logic lines must fit ZXAPP_LOGIC_MAX (128) and the uses ZXAPP_USES (8); if either fails, stop and report it; do not raise a limit or edit a part.
What it does: Orchestrates many services, agents and machines from one console, with budgets and safe rollbacks: agents (`swarm.state`, `swarm.ask`), engines (`features.get`, `features.set` owner-approved), machines (`net.peers`, `devmesh.devices`, `sys.info`), budgets (`power.budget`, `power.top`), versions to roll back to (`backup.versions`, `backup.restore` as a new item).
Uses: `use tributary 1` (agreeing answers and "may ask another model"); `use swell 1` (click time per device and latency limits); `use gearwright 1` ("motion allowed"); `use flue 1` (version vector compare and "sync fits"); `use murmuration 1` (budget shares (`swarm_shares`)). Call these composed entries; do not re-implement them.
Logic (new decisions only; each `name` is the entry; a critical one has two or three `logic` lines for one `provides`, so the witness holds it as BOTH when they disagree):
- `deploy_ok`: (critical) budget and rate limits (each job within its budget AND at most the rate per minute) in Ada as Eve + APL as KRON witnessed
- `forecast`: (critical) load forecast (next hour's load from the last n hours, integer least squares) in FORTRAN as Trebago + APL as KRON witnessed
- `rollback`: rollback plan (the last version every dependant agrees on) in APL as KRON
- `jobs`: job arrays (jobs x machines) in APL as KRON
- `billing`: compute billing (minor units, refusing on overflow) in COBOL as Sutra
- `services`: service tree in MUMPS as MYRA
Paraconsistent (BOTH test, in test_zxapp.c): two agents report a branch as running and stopped: the branch is BOTH; the test asserts state 3 and that only that branch pauses while the rest keep running. Join the sources with `lpres_conjoin` / `lpres_disjoin` in C; never re-derive the tables.
Safety: Switch changes and rollbacks are owner-approved; never call `zxvd.shutdown` in tests.
Integers only: no float or double anywhere; a division that is not whole uses kernel/src/rational in C.
Tests: as the spec, plus a compose test (the app composes against a registry holding its parts, and every part entry it calls resolves) and the BOTH test above.
needs: swarm, ai.ask, peers, net, sys.read, backup, notify.
Pending (P1; the best-fit language has no front-end yet, the block above stands in): `pending Occam jobs`; `pending Elixir rollback`; `pending JCL deploy_ok`.
Planned (no runtime yet): Occam, Elixir, JCL.
