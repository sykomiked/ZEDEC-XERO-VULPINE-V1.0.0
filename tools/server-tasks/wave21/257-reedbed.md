# Wave 21 task: Reedbed (slot 257), Wetland monitoring
Read ../APP_WAVE_SPEC.md first. This is an L2 app: manifest says `layer L2` and composes its built_from apps' blocks with `use`; it may only start after those L1 waves are merged.
Depends on: wave 0 (016 sounding), wave 2 (071 gleaner), wave 8 (093 understory, 094 homestead).
Manifest: `slot 257`, `layer L2`, `version 1.0.0` and its `provides` lines (so L3 apps can use it, C1), `icon icons/l2/257-reedbed.svg`, `budget 233`, model `apps/reedbed_model.{h,c}`, blocks under kernel/src/apps/blocks/reedbed/, one `use <id> 1` per part below. The composed logic lines must fit ZXAPP_LOGIC_MAX (128) and the uses ZXAPP_USES (8); if either fails, stop and report it; do not raise a limit or edit the part.
What it does: Monitors wetlands and water quality with low-power sensor nodes: sites (`place.list`), records in the Hoard (`files.read`, `files.write`, `search.query`), the dataset published locally (`aipi.put`, `web4.cid`), the link (`sys.net`). Sensor input has no method: use only existing methods; list what's missing in your report.
Uses: `use understory 1` (rolling totals); `use homestead 1` (device records); `use gleaner 1` (label counts and row filters); `use sounding 1` (`rate`). Call these composed entries; do not re-implement them; each part's manifest is itself the block (C1).
Logic (new decisions only; a critical one has two or three `logic` lines for one `provides`, so the witness holds it as BOTH when they disagree):
- (critical) alert thresholds (pH x100 within 650..850, dissolved oxygen at least 5000 ug/L, the owner's other bounds) in Ada as Eve + APL as KRON witnessed
- (critical) the node's raw count to a reading with its range check in Forth (twin Cairndrake) (on the node) + FORTRAN as Trebago witnessed
- water chemistry (oxygen saturation x100 from a temperature table, temperature-corrected conductivity) exact in FORTRAN as Trebago
- sensor grid (sensors x hours) in APL as KRON
- permit records (sample counts per permit period, refusing on overflow) in COBOL as Sutra
- site tree in MUMPS as MYRA
- Forth and BASIC blocks are committed in twin form: the manifest's logic line names the twin (`logic cairndrake ...`, `logic hatchling ...`).
Paraconsistent (BOTH test, in test_zxapp.c): two drifting sensors in one pool read pH 7.2 and 8.9, giving BOTH; the test asserts state 3, a calibration flag, and both readings kept. Join the sources with `lpres_conjoin` / `lpres_disjoin` in C; never re-derive the tables.
Safety: Alerts are advisory, not a regulatory submission.
Integers only: no float or double anywhere; a division that is not whole uses kernel/src/rational in C.
needs: fs.read, fs.write, search, net, sys.read, notify.
Planned (no runtime yet): JOVIAL, Scala.
