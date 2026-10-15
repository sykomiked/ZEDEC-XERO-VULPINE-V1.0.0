# Wave 31 task: Counterpoint (slot 378), Couples' shared life
Read ../APP_WAVE_SPEC.md first. This is an L3 app: manifest says `layer L3` and composes its built_from apps with `use`; it may only start after those waves are merged.
Depends on: wave 14 (172 escapement); wave 17 (207 fellowcraft); wave 19 (234 neap, 240 isthmus); wave 6 (082 larder).
Manifest: `slot 378`, `layer L3`, `version 1.0.0`, `icon icons/l3/378-counterpoint.svg`, `budget 233`, model `apps/counterpoint_model.{h,c}`, blocks under kernel/src/apps/blocks/counterpoint/, one `use <id> 1` per part below. Each part's manifest is itself the block (C1): an L2 part exports its own entries and the L1 entries it composed (`<part>.<block>.<entry>`). The composed logic lines must fit ZXAPP_LOGIC_MAX (128) and the uses ZXAPP_USES (8); if either fails, stop and report it; do not raise a limit or edit a part.
What it does: Shares money, plans and chores as a couple, with joint goals and each person's view kept: joint accounts (`ledger.accounts`, `ledger.balance`, `ledger.entries`, `ledger.post`, `ledger.hold`), plans in the Hoard (`files.read`, `files.write`), the shared clock (`sys.info`).
Uses: `use neap 1` ("entry may post"); `use escapement 1` (schedule matrix and timesheets); `use fellowcraft 1` (element differences and the comparison ledger); `use isthmus 1` ("next step allowed" and the settlement split); `use larder 1` (spent per category and "over budget"). Call these composed entries; do not re-implement them.
Logic (new decisions only; each `name` is the entry; a critical one has two or three `logic` lines for one `provides`, so the witness holds it as BOTH when they disagree):
- `spend_ok`: (critical) spending limits (each person's solo cap; above it both approve) in Ada as Eve + COBOL as Sutra witnessed
- `forecast`: (critical) budget forecast (monthly sums and the end-of-month balance from planned items) in FORTRAN as Trebago + COBOL as Sutra witnessed
- `joint`: joint account lines (minor units, refusing on overflow) in COBOL as Sutra
- `chores`: chore grid (people x tasks x weeks, fair share counts) in APL as KRON
- `planner`: shared plan structure (goals, owners, steps) in Ada as Eve
- `household`: household tree in MUMPS as MYRA
Paraconsistent (BOTH test, in test_zxapp.c): the partners record the same dinner as 64.00 and 46.00: the expense is BOTH; the test (scratch books) asserts state 3 and that both entries stay until they talk. Join the sources with `lpres_conjoin` / `lpres_disjoin` in C; never re-derive the tables.
Safety: Money moves only with the required approvals; tests use scratch books.
Integers only: no float or double anywhere; a division that is not whole uses kernel/src/rational in C.
Tests: as the spec, plus a compose test (the app composes against a registry holding its parts, and every part entry it calls resolves) and the BOTH test above.
needs: ledger.read, ledger.post, fs.read, fs.write, sys.read, peers, notify.
Pending (P1; the best-fit language has no front-end yet, the block above stands in): `pending Go chores`; `pending Simula_I forecast`; `pending Delphi planner`.
Planned (no runtime yet): Go, Simula I, Delphi.
