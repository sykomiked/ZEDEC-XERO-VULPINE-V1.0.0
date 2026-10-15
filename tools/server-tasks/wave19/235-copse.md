# Wave 19 task: Copse (slot 235), Woodworking planner
Read ../APP_WAVE_SPEC.md first. This is an L2 app: manifest says `layer L2` and composes its built_from apps' blocks with `use`; it may only start after those L1 waves are merged.
Depends on: wave 3 (045 bowline); wave 5 (060 relief); wave 8 (107 gnomon); wave 9 (109 tally).
Manifest: `slot 235`, `layer L2`, `version 1.0.0` and its `provides` lines (so L3 apps can use it, C1), `icon icons/l2/235-copse.svg`, `budget 233`, model `apps/copse_model.{h,c}`, blocks under kernel/src/apps/blocks/copse/, one `use <id> 1` per part below. The composed logic lines must fit ZXAPP_LOGIC_MAX (128) and the uses ZXAPP_USES (8); if either fails, stop and report it; do not raise a limit or edit the part.
What it does: Plans woodworking: designs, cut lists, joints, materials and cost, kept in the Hoard (`files.read`, `files.write`) and printed (`print.submit`).
Uses: `use relief 1` (vertex transform and mesh volume); `use tally 1` (rational arithmetic and currency amounts); `use gnomon 1` (area and reduced fractions); `use bowline 1` (next due and "overdue" for build steps). Call these composed entries; do not re-implement them; each part's manifest is itself the block (C1).
Logic (new decisions only; a critical one has two or three `logic` lines for one `provides`, so the witness holds it as BOTH when they disagree):
- (critical) cut plan (parts packed onto boards, longest first, kerf counted, offcut exact) in FORTRAN as Trebago + APL as KRON witnessed
- (critical) "within tolerance" (part and joint sizes within the set plus or minus mm) in Ada as Eve + APL as KRON witnessed
- material cost (volume or length x price, minor units, refusing on overflow) in COBOL as Sutra + FORTRAN as Trebago witnessed
- cut grid (boards x parts) in APL as KRON
- assembly tree in MUMPS as MYRA
Paraconsistent (BOTH test, in test_zxapp.c): one board measured at 1203 mm and again at 1198 mm with a 2 mm tolerance gives BOTH; the test asserts state 3 and that the cuts on that board are held with "re-measure before cutting". Join the sources with `lpres_conjoin` / `lpres_disjoin` in C; never re-derive the tables.
Integers only: no float or double anywhere; a division that is not whole uses kernel/src/rational in C.
needs: fs.read, fs.write, print.
Planned (no runtime yet): APT, LISP, Simula 67.
