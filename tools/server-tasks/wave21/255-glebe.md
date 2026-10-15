# Wave 21 task: Glebe (slot 255), Harvest planner
Read ../APP_WAVE_SPEC.md first. This is an L2 app: manifest says `layer L2` and composes its built_from apps' blocks with `use`; it may only start after those L1 waves are merged.
Depends on: wave 1 (079 fairground), wave 6 (040 furrow), wave 8 (088 weatherglass, 093 understory).
Manifest: `slot 255`, `layer L2`, `version 1.0.0` and its `provides` lines (so L3 apps can use it, C1), `icon icons/l2/255-glebe.svg`, `budget 233`, model `apps/glebe_model.{h,c}`, blocks under kernel/src/apps/blocks/glebe/, one `use <id> 1` per part below. The composed logic lines must fit ZXAPP_LOGIC_MAX (128) and the uses ZXAPP_USES (8); if either fails, stop and report it; do not raise a limit or edit the part.
What it does: Plans harvests: fields and the place (`place.here`, `place.list`), crop sales (`net.offers`, `net.command` sell, `ledger.entries`), plans in the Hoard (`files.read`, `files.write`), farm energy (`power.status`). There is no weather source (as Weatherglass): use only existing methods; list what's missing in your report.
Uses: `use understory 1` ("over budget" and rolling totals); `use weatherglass 1` (conversions and warnings); `use fairground 1` (the 0.08889% fee and "offer allowed"); `use furrow 1` (column sums and money cells). Call these composed entries; do not re-implement them; each part's manifest is itself the block (C1).
Logic (new decisions only; a critical one has two or three `logic` lines for one `provides`, so the witness holds it as BOTH when they disagree):
- (critical) "fits in storage" (tonnes and moisture within each store's capacity and limit) in Ada as Eve + APL as KRON witnessed
- (critical) crop sale value (kg x price, minor units, refusing on overflow) in COBOL as Sutra + FORTRAN as Trebago witnessed
- yield model (area m2 x yield g/m2, labour hours per hectare) exact in FORTRAN as Trebago
- field grid (fields x days, dry-day runs for weather windows) in APL as KRON
- farm and field tree in MUMPS as MYRA
Paraconsistent (BOTH test, in test_zxapp.c): yield estimate A 12 t and estimate B 18 t give BOTH; the test asserts state 3 and that storage is planned for 18 t and sales for 12 t (both cases). Join the sources with `lpres_conjoin` / `lpres_disjoin` in C; never re-derive the tables.
Safety: Advisory; a sale waits for the owner's approval.
Integers only: no float or double anywhere; a division that is not whole uses kernel/src/rational in C.
needs: location, peers, net, ledger.read, fs.read, fs.write, sys.read.
Planned (no runtime yet): Simula 67, Modula-2.
