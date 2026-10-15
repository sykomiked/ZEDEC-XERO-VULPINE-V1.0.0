# Wave 15 task: Kiln (slot 186), Fabrication queue
Read ../APP_WAVE_SPEC.md first. This is an L2 app: manifest says `layer L2` and composes its built_from apps' blocks with `use`; it may only start after those L1 waves are merged.
Depends on: wave 0 (115 platen), wave 3 (060 relief), wave 5 (021 brazier), wave 6 (024 treadle).
Manifest: `slot 186`, `layer L2`, `icon icons/l2/186-kiln.svg`, `budget 233`, one `use <id> 1` per part below. A part can be used only if its manifest carries `version 1.0.0` and its `provides` lines; the composed logic lines must fit ZXAPP_LOGIC_MAX (128) and the uses ZXAPP_USES (8). If any of these fails, stop and report it; do not raise a limit or edit the part.
What it does: queues 3D models from the Hoard (`files.read`) for printing, firing or fabrication (`print.status`, `print.submit`, `print.job`, `print.cancel`) and tracks each piece. zxvd has no method for 3D printer and kiln control: use only existing methods; list what's missing in your report.
Uses: `use brazier 1` (temperature limits); `use treadle 1` (next run time, "within budget"); `use relief 1` (mesh volume, mesh limits); `use platen 1` (`platen.copies_ok`). Call these composed entries; do not re-implement them.
Logic (critical, new decisions only): firing schedule temperature at each step (ramp in tenths of a degree an hour) in FORTRAN as Trebago + APL as KRON witnessed; "schedule allowed" (peak at most the kiln's maximum AND ramp at most the material's) in Ada as Eve + APL as KRON witnessed; job costing (material grams x price plus energy, minor units) in COBOL as Sutra + FORTRAN as Trebago witnessed; layer arrays (layer count, area per layer) in APL as KRON; batch tree in MUMPS as MYRA.
BOTH test: piece 6 passes the visual check and fails the dimension check: it is BOTH and goes to inspection.
Advisory; never starts heating by itself; the owner starts every firing.
Tests: as the spec, plus a compose test (the app composes against a registry holding its parts, and every part entry it calls resolves) and the BOTH test above.
needs: print, fs.read, sys.read, notify.
Planned (no runtime yet): APT, MATLAB, Bash.
