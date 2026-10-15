# Wave 19 task: Weir (slot 237), Household water
Read ../APP_WAVE_SPEC.md first. This is an L2 app: manifest says `layer L2` and composes its built_from apps' blocks with `use`; it may only start after those L1 waves are merged.
Depends on: wave 0 (014 wake), wave 1 (109 tally), wave 8 (093 understory), wave 9 (094 homestead).
Manifest: `slot 237`, `layer L2`, `version 1.0.0` and its `provides` lines (so L3 apps can use it, C1), `icon icons/l2/237-weir.svg`, `budget 233`, model `apps/weir_model.{h,c}`, blocks under kernel/src/apps/blocks/weir/, one `use <id> 1` per part below. The composed logic lines must fit ZXAPP_LOGIC_MAX (128) and the uses ZXAPP_USES (8); if either fails, stop and report it; do not raise a limit or edit the part.
What it does: Maps household water: pipes, valves, flow, budgets and leak alerts, kept in the Hoard (`files.read`, `files.write`), event history (`journal.query`, `log.tail`). Water meters and valve actuators have no method (as Understory and Homestead): use only existing methods; list what's missing in your report.
Uses: `use understory 1` ("over budget" and rolling totals); `use homestead 1` ("command allowed" for a valve); `use wake 1` (`count` by level); `use tally 1` (rational arithmetic). Call these composed entries; do not re-implement them; each part's manifest is itself the block (C1).
Logic (new decisions only; a critical one has two or three `logic` lines for one `provides`, so the witness holds it as BOTH when they disagree):
- (critical) "leak suspected" (flow above zero for at least 120 night minutes, or meter and fixture totals apart by more than the threshold) in Ada as Eve + APL as KRON witnessed
- (critical) litres from meter pulses (pulses x ml per pulse) in Forth (twin Cairndrake) (the counter's raw-count check at the device) + FORTRAN as Trebago witnessed
- flow model (litres per period, night window) exact in FORTRAN as Trebago
- pressure limits (kPa range) in Ada as Eve
- pipe matrix (pipes x valves x fixtures) in APL as KRON
- utility bills (cubic metres x tariff bands, minor units, refusing on overflow) in COBOL as Sutra
- fixture tree in MUMPS as MYRA
- Forth and BASIC blocks are committed in twin form: the manifest's logic line names the twin (`logic cairndrake ...`, `logic hatchling ...`).
Paraconsistent (BOTH test, in test_zxapp.c): the meter reads 412 L for the day and the fixture sensors sum to 300 L, giving BOTH; the test asserts state 3 and the likely-leak notice naming the gap. Join the sources with `lpres_conjoin` / `lpres_disjoin` in C; never re-derive the tables.
Safety: Closing a valve is human-approved; advisory only.
Integers only: no float or double anywhere; a division that is not whole uses kernel/src/rational in C.
needs: fs.read, fs.write, journal, net, notify.
Planned (no runtime yet): Gleam, Scala.
