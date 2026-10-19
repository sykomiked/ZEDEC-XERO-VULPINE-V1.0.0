# Wave 20 task: Bourn (slot 249), Brewing log
Read ../APP_WAVE_SPEC.md first. This is an L2 app: manifest says `layer L2` and composes its built_from apps' blocks with `use`; it may only start after those L1 waves are merged.
Depends on: wave 1 (024 treadle); wave 3 (040 furrow); wave 6 (083 reckoner); wave 7 (092 hearth).
Manifest: `slot 249`, `layer L2`, `version 1.0.0` and its `provides` lines (so L3 apps can use it, C1), `icon icons/l2/249-bourn.svg`, `budget 233`, model `apps/bourn_model.{h,c}`, blocks under kernel/src/apps/blocks/bourn/, one `use <id> 1` per part below. The composed logic lines must fit ZXAPP_LOGIC_MAX (128) and the uses ZXAPP_USES (8); if either fails, stop and report it; do not raise a limit or edit the part.
What it does: Logs brewing batches: recipes, temperatures, gravity, ageing and duty, kept in the Hoard (`files.read`, `files.write`), the books for duty (`ledger.entries`), the jurisdiction (`jurisdiction.get`). Sensor input has no method: use only existing methods; list what's missing in your report.
Uses: `use hearth 1` (scaled quantities and allergens); `use treadle 1` (next run time and "within budget"); `use furrow 1` (column sums and money cells); `use reckoner 1` (period totals and tax estimate). Call these composed entries; do not re-implement them; each part's manifest is itself the block (C1).
Logic (new decisions only; a critical one has two or three `logic` lines for one `provides`, so the witness holds it as BOTH when they disagree):
- (critical) strength x100 from gravities x1000 ((OG - FG) x 13125 / 100, floor) in FORTRAN as Trebago + APL as KRON witnessed
- (critical) duty due (litres x strength x rate, minor units, refusing on overflow) in COBOL as Sutra + FORTRAN as Trebago witnessed
- temperature limits (the yeast's range in tenths of a degree) in Ada as Eve
- batch grid (batches x days x readings) in APL as KRON
- recipe and lot tree in MUMPS as MYRA
Paraconsistent (BOTH test, in test_zxapp.c): the hydrometer reads 1012 and the sensor 1020, giving BOTH; the test asserts state 3 and that the batch is held for a check, not marked finished. Join the sources with `lpres_conjoin` / `lpres_disjoin` in C; never re-derive the tables.
Safety: Duty figures are advisory; a person files.
Integers only: no float or double anywhere; a division that is not whole uses kernel/src/rational in C.
needs: fs.read, fs.write, ledger.read, notify.
Planned (no runtime yet): CPL, JCL.
