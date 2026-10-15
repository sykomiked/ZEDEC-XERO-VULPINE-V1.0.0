# Wave 14 task: Cupel (slot 179), Simulation bench
Read ../APP_WAVE_SPEC.md first. This is an L2 app: manifest says `layer L2` and composes its built_from apps' blocks with `use`; it may only start after those L1 waves are merged.
Depends on: wave 1 (070 retort), wave 3 (060 relief), wave 7 (040 furrow), wave 9 (107 gnomon).
Manifest: `slot 179`, `layer L2`, `icon icons/l2/179-cupel.svg`, `budget 233`, one `use <id> 1` per part below. A part can be used only if its manifest carries `version 1.0.0` and its `provides` lines; the composed logic lines must fit ZXAPP_LOGIC_MAX (128) and the uses ZXAPP_USES (8). If any of these fails, stop and report it; do not raise a limit or edit the part.
What it does: simulates atoms, molecules and physical systems on the integer engine and compares models, runs kept in the Hoard (`files.read`, `files.write`), machine facts from `sys.info`.
Uses: `use furrow 1` (sums); `use relief 1` (vertex transform, mesh limits); `use retort 1` (score totals, run limits, run record); `use gnomon 1` (twice the area, squared lengths). Call these composed entries; do not re-implement them.
Logic (critical, new decisions only): one integer leapfrog step (fixed-point positions and velocities, forces from a pair table) in FORTRAN as Trebago + APL as KRON witnessed; precision bounds (fixed-point scale, at most the declared steps, energy drift at most the bound or the run halts) in Ada as Eve + APL as KRON witnessed; lattice arrays in APL as KRON; model tree and run records in MUMPS as MYRA.
BOTH test: model A's energy after 1000 steps is 10000 units and model B's 10450, tolerance 100: the result is BOTH and both are shown.
Tests: as the spec, plus a compose test (the app composes against a registry holding its parts, and every part entry it calls resolves) and the BOTH test above.
needs: fs.read, fs.write, sys.read.
Planned (no runtime yet): Simula 67, Python.
