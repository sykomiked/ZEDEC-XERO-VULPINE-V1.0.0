# Wave 18 task: Cirque (slot 227), Building drawings
Read ../APP_WAVE_SPEC.md first. This is an L2 app: manifest says `layer L2` and composes its built_from apps' blocks with `use`; it may only start after those L1 waves are merged.
Depends on: wave 3 (060 relief), wave 7 (039 folio, 054 burin), wave 9 (107 gnomon).
Manifest: `slot 227`, `layer L2`, `version 1.0.0` and its `provides` lines (so L3 apps can use it, C1), `icon icons/l2/227-cirque.svg`, `budget 233`, model `apps/cirque_model.{h,c}`, blocks under kernel/src/apps/blocks/cirque/, one `use <id> 1` per part below. The composed logic lines must fit ZXAPP_LOGIC_MAX (128) and the uses ZXAPP_USES (8); if either fails, stop and report it; do not raise a limit or edit the part.
What it does: Draws buildings from plan to 3D, checks measurements and produces drawing sets: projects in the Hoard (`files.read`, `files.write`), sets printed (`print.submit`, `print.status`).
Uses: `use relief 1` (vertex transform, depth effect offsets, mesh volume); `use burin 1` (curve flattening and hatch lines); `use gnomon 1` (twice a polygon's area); `use folio 1` (page fit). Call these composed entries; do not re-implement them; each part's manifest is itself the block (C1).
Logic (new decisions only; a critical one has two or three `logic` lines for one `provides`, so the witness holds it as BOTH when they disagree):
- (critical) "dimensions close" (each dimension string sums to its overall, in mm) in APL as KRON + FORTRAN as Trebago witnessed
- (critical) code limits (room areas, stair rise and going, door widths from the owner's integer table) in Ada as Eve + APL as KRON witnessed
- structural loads (dead plus live load per span in N/m, integers) in FORTRAN as Trebago, advisory
- dimension grid (gridlines x levels) in APL as KRON
- project records (sheet register, revision counts, refusing on overflow) in COBOL as Sutra
- building element tree (level, room, wall, opening) in MUMPS as MYRA
Paraconsistent (BOTH test, in test_zxapp.c): designer A draws a wall at 4200 mm and designer B at 4350 mm, giving BOTH; the test asserts state 3 and that both versions are kept and highlighted, neither overwritten. Join the sources with `lpres_conjoin` / `lpres_disjoin` in C; never re-derive the tables.
Safety: Load figures and code checks are advisory; a qualified person signs the drawings.
Integers only: no float or double anywhere; a division that is not whole uses kernel/src/rational in C.
needs: fs.read, fs.write, print.
Planned (no runtime yet): Logo, Julia, Bash.
