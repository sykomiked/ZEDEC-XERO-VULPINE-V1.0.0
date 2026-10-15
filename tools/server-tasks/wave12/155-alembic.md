# Wave 12 task: Alembic (slot 155), Lab notebook
Read ../APP_WAVE_SPEC.md first. This is an L2 app: manifest says `layer L2` and composes its built_from apps' blocks with `use`; it may only start after those L1 waves are merged.
Depends on: wave 2 (109 tally), wave 4 (070 retort), wave 6 (038 margins, 040 furrow).
Manifest: `slot 155`, `layer L2`, `icon icons/l2/155-alembic.svg`, `budget 233`, one `use <id> 1` per part below. A part can be used only if its manifest carries `version 1.0.0` and its `provides` lines; the composed logic lines must fit ZXAPP_LOGIC_MAX (128) and the uses ZXAPP_USES (8). If any of these fails, stop and report it; do not raise a limit or edit the part.
What it does: logs experiments, recipes and results as a lab notebook in the Hoard (`files.read`, `files.write`, `search.query`) with exact integer calculations.
Uses: `use margins 1` (note size limits, tag index); `use furrow 1` (column and row sums); `use retort 1` (score totals per run, run record); `use tally 1` (rational add, subtract, multiply, divide; unit factors). Call these composed entries; do not re-implement them.
Logic (critical, new decisions only): amount of substance (mass in micrograms x 1000 / molar mass, as a reduced rational) in FORTRAN as Trebago + APL as KRON witnessed; reagent stock in milligrams, a withdrawal refused below zero or on overflow, in COBOL as Sutra + FORTRAN as Trebago witnessed; "step allowed" (every reagent's hazard class among the bench's declared classes AND quantity at most the declared maximum) in Ada as Eve + APL as KRON witnessed; result tables (result vector per run, difference from an earlier run against the tolerance) in APL as KRON; experiment tree in MUMPS as MYRA.
BOTH test: run 2 gives 412 and run 1 gave 388 for the same recipe, tolerance 10: the result is BOTH, both runs are kept, and the entry is marked for replication.
Hazard checks are advisory; the person follows their lab's safety rules.
Tests: as the spec, plus a compose test (the app composes against a registry holding its parts, and every part entry it calls resolves) and the BOTH test above.
needs: fs.read, fs.write, search.
Planned (no runtime yet): AWK, ALGOL 68, TeX.
