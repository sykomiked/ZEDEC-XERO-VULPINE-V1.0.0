# Wave 12 task: Dibber (slot 149), Garden planner
Read ../APP_WAVE_SPEC.md first. This is an L2 app: manifest says `layer L2` and composes its built_from apps' blocks with `use`; it may only start after those L1 waves are merged.
Depends on: wave 7 (040 furrow, 043 solstice), wave 8 (088 weatherglass, 093 understory).
Manifest: `slot 149`, `layer L2`, `icon icons/l2/149-dibber.svg`, `budget 233`, one `use <id> 1` per part below. A part can be used only if its manifest carries `version 1.0.0` and its `provides` lines; the composed logic lines must fit ZXAPP_LOGIC_MAX (128) and the uses ZXAPP_USES (8). If any of these fails, stop and report it; do not raise a limit or edit the part.
What it does: plans plantings, watering and harvests for a plot from weather and land budgets, kept in the Hoard (`files.read`, `files.write`), for where the plot is (`place.here`). zxvd has no method for a weather source (as Weatherglass reports), soil sensor readings: use only existing methods; list what's missing in your report.
Uses: `use furrow 1` (column and row sums for the bed sheet); `use solstice 1` (next date of a repeating task); `use weatherglass 1` (unit conversion, frost warnings, readings); `use understory 1` ("over budget" for water, rolling-window totals). Call these composed entries; do not re-implement them.
Logic (critical, new decisions only): water per bed per week (bed area in square decimetres x depth in mm = litres) exact in FORTRAN as Trebago + APL as KRON witnessed; "may water now" (within the week's water budget AND no frost warning) in Ada as Eve + APL as KRON witnessed; water limits in Ada as Eve; seed and supply costs in minor units in COBOL as Sutra + FORTRAN as Trebago witnessed; days to harvest from growing degree days (sum of max(0, mean tenths - base)) exact in FORTRAN as Trebago; bed grid (crop family per bed per year, no repeat on the same bed) in APL as KRON; plot hierarchy in MUMPS as MYRA.
BOTH test: two soil moisture readings for bed 4 on the same day, 18 and 41 percent: the bed is BOTH, the planting suggestion is held, and a re-test is suggested.
Advisory; never controls a pump or a valve.
Tests: as the spec, plus a compose test (the app composes against a registry holding its parts, and every part entry it calls resolves) and the BOTH test above.
needs: fs.read, fs.write, location, notify.
Planned (no runtime yet): Simula 67, Oberon.
