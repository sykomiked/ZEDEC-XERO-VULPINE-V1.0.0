# Wave 29 task: Athanor (slot 351), Process runs
Read ../APP_WAVE_SPEC.md first. This is an L3 app: manifest says `layer L3` and composes its built_from apps with `use`; it may only start after those waves are merged.
Depends on: wave 12 (155 alembic), wave 14 (174 damper, 179 cupel), wave 20 (249 bourn).
Manifest: `slot 351`, `layer L3`, `version 1.0.0`, `icon icons/l3/351-athanor.svg`, `budget 233`, model `apps/athanor_model.{h,c}`, blocks under kernel/src/apps/blocks/athanor/, one `use <id> 1` per part below. Each part's manifest is itself the block (C1): an L2 part exports its own entries and the L1 entries it composed (`<part>.<block>.<entry>`). The composed logic lines must fit ZXAPP_LOGIC_MAX (128) and the uses ZXAPP_USES (8); if either fails, stop and report it; do not raise a limit or edit a part.
What it does: Plans and runs chemistry or brewing processes: recipes, batch records and hazard checks in the Hoard (`files.read`, `files.write`, `search.query`), heat and power readings (`sys.info`, `power.status`), alarms (`log.tail`, `journal.query`). Process sensors have no method: use only existing methods; list what's missing in your report.
Uses: `use alembic 1` (amount of substance, reagent stock, "step allowed"); `use bourn 1` (strength from gravities, duty due); `use damper 1` ("raise the alarm" and temperature rise); `use cupel 1` (the leapfrog step and precision bounds). Call these composed entries; do not re-implement them.
Logic (new decisions only; each `name` is the entry; a critical one has two or three `logic` lines for one `provides`, so the witness holds it as BOTH when they disagree):
- `vessel_ok`: (critical) pressure and heat limits (kPa and tenths of a degree within the vessel's rating, rise per minute within the declared rate) in Ada as Eve + APL as KRON witnessed
- `batch_cost`: (critical) batch costing (reagents and energy in minor units, refusing on overflow) in COBOL as Sutra + FORTRAN as Trebago witnessed
- `kinetics`: reaction kinetics (first-order conversion by step from an integer rate table) in FORTRAN as Trebago
- `schedule`: run schedule (step start times as a running sum) in FORTRAN as Trebago
- `conc`: concentration arrays (species x step) in APL as KRON
- `lineage`: batch lineage tree in MUMPS as MYRA
Paraconsistent (BOTH test, in test_zxapp.c): the lab result passes a batch and the in-line sensor fails it: the batch is BOTH; the test asserts state 3 and that it is quarantined, neither shipped nor discarded. Join the sources with `lpres_conjoin` / `lpres_disjoin` in C; never re-derive the tables.
Safety: Hazard checks are advisory; a person starts every step; never controls heating by itself.
Integers only: no float or double anywhere; a division that is not whole uses kernel/src/rational in C.
Tests: as the spec, plus a compose test (the app composes against a registry holding its parts, and every part entry it calls resolves) and the BOTH test above.
needs: fs.read, fs.write, search, sys.read, journal, notify.
Pending (P1; the best-fit language has no front-end yet, the block above stands in): `pending JOSS kinetics`; `pending JCL schedule`; `pending Simula_I conc`.
Planned (no runtime yet): JOSS, JCL, Simula I.
