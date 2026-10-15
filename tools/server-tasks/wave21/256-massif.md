# Wave 21 task: Massif (slot 256), Distributed integer compute
Read ../APP_WAVE_SPEC.md first. This is an L2 app: manifest says `layer L2` and composes its built_from apps' blocks with `use`; it may only start after those L1 waves are merged.
Depends on: wave 0 (015 tidewatch), wave 1 (029 trellis), wave 4 (070 retort), wave 5 (020 warren), wave 10 (123 gait).
Manifest: `slot 256`, `layer L2`, `version 1.0.0` and its `provides` lines (so L3 apps can use it, C1), `icon icons/l2/256-massif.svg`, `budget 233`, model `apps/massif_model.{h,c}`, blocks under kernel/src/apps/blocks/massif/, one `use <id> 1` per part below. The composed logic lines must fit ZXAPP_LOGIC_MAX (128) and the uses ZXAPP_USES (8); if either fails, stop and report it; do not raise a limit or edit the part.
What it does: Spreads large integer compute jobs across machines and tracks results: functions and data (`aipi.put`, `aipi.get`, `aipi.publish`, `aipi.execute`, `aipi.status`), other nodes (`peer.ask`, `peer.status`, `net.peers`), energy and budgets (`power.budget`, `power.status`, `swarm.state`).
Uses: `use tidewatch 1` (`cpu_x100` and `peak`); `use warren 1` (energy share per engine); `use trellis 1` (peers per state); `use retort 1` (score totals and run limits); `use gait 1` (share x100 and p50, p95). Call these composed entries; do not re-implement them; each part's manifest is itself the block (C1).
Logic (new decisions only; a critical one has two or three `logic` lines for one `provides`, so the witness holds it as BOTH when they disagree):
- (critical) work-unit plan (a range split into units, each assigned to at least two nodes) in APL as KRON + FORTRAN as Trebago witnessed
- (critical) "may send this unit" (fuel, energy and price per unit within the owner's caps) in Ada as Eve + APL as KRON witnessed
- (critical) job cost (price per unit plus the 0.08889% fee = 8889/10,000,000, minor units, refusing on overflow) in COBOL as Sutra + FORTRAN as Trebago witnessed
- the job's own integer kernels (sums, modular powers) in FORTRAN as Trebago
- job tree in MUMPS as MYRA
Paraconsistent (BOTH test, in test_zxapp.c): two nodes return different results for the same unit, giving BOTH; the test (loopback only) asserts state 3 and a re-run on a third node, the result not accepted. Join the sources with `lpres_conjoin` / `lpres_disjoin` in C; never re-derive the tables.
Safety: Every priced `peer.ask` is within caps the owner set; tests stay on loopback.
Integers only: no float or double anywhere; a division that is not whole uses kernel/src/rational in C.
needs: peers, net, swarm, sys.read, ledger.read, notify.
Planned (no runtime yet): Occam, Bash.
