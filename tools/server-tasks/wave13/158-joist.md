# Wave 13 task: Joist (slot 158), Construction planner
Read ../APP_WAVE_SPEC.md first. This is an L2 app: manifest says `layer L2` and composes its built_from apps' blocks with `use`; it may only start after those L1 waves are merged.
Depends on: wave 1 (109 tally), wave 3 (060 relief), wave 7 (040 furrow, 045 bowline).
Manifest: `slot 158`, `layer L2`, `icon icons/l2/158-joist.svg`, `budget 233`, one `use <id> 1` per part below. A part can be used only if its manifest carries `version 1.0.0` and its `provides` lines; the composed logic lines must fit ZXAPP_LOGIC_MAX (128) and the uses ZXAPP_USES (8). If any of these fails, stop and report it; do not raise a limit or edit the part.
What it does: plans construction projects: structure models, materials, costs and task schedules, kept in the Hoard (`files.read`, `files.write`).
Uses: `use furrow 1` (sums, money cells); `use bowline 1` (next due, overdue); `use relief 1` (vertex transform, mesh volume, mesh limits); `use tally 1` (rationals, unit factors). Call these composed entries; do not re-implement them.
Logic (critical, new decisions only): bill of quantities (quantity x unit price per line, totals in minor units, refused on overflow) in COBOL as Sutra + FORTRAN as Trebago witnessed; member load (sum of the loads on it) exact in FORTRAN as Trebago + APL as KRON witnessed; "within the safety margin" (load x 100 / rated capacity at most the declared utilisation) in Ada as Eve + APL as KRON witnessed; material grid (material by task) in APL as KRON; project tree in MUMPS as MYRA.
BOTH test: design says wall 3 is 2400 mm, the site measures 2460 mm, tolerance 20: the wall is BOTH and the task "frame wall 3" is blocked until checked.
Advisory; structural work is signed off by a qualified engineer; the app never certifies a structure.
Tests: as the spec, plus a compose test (the app composes against a registry holding its parts, and every part entry it calls resolves) and the BOTH test above.
needs: fs.read, fs.write, notify.
Planned (no runtime yet): Delphi, Simula I, Julia.
