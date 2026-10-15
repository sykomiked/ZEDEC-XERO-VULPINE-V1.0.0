# Wave 18 task: Scree (slot 219), Adaptive puzzles
Read ../APP_WAVE_SPEC.md first. This is an L2 app: manifest says `layer L2` and composes its built_from apps' blocks with `use`; it may only start after those L1 waves are merged.
Depends on: wave 8 (098 sapling), wave 9 (103 cubby, 104 riddle, 105 patience, 107 gnomon).
Manifest: `slot 219`, `layer L2`, `version 1.0.0` and its `provides` lines (so L3 apps can use it, C1), `icon icons/l2/219-scree.svg`, `budget 233`, model `apps/scree_model.{h,c}`, blocks under kernel/src/apps/blocks/scree/, one `use <id> 1` per part below. The composed logic lines must fit ZXAPP_LOGIC_MAX (128) and the uses ZXAPP_USES (8); if either fails, stop and report it; do not raise a limit or edit the part.
What it does: Builds and plays logic and maths puzzles that adapt to a learner's level; puzzles and progress in the Hoard (`files.read`, `files.write`), hints from `swarm.ask` held until the learner asks, time from `screen.check`.
Uses: `use riddle 1` ("solved" and the daily pick); `use gnomon 1` (shoelace area and reduced fractions); `use sapling 1` ("lesson unlocked" and progress x100); `use cubby 1` ("may this child open this now"); `use patience 1` ("legal move" and the seeded deal). Call these composed entries; do not re-implement them; each part's manifest is itself the block (C1).
Logic (new decisions only; a critical one has two or three `logic` lines for one `provides`, so the witness holds it as BOTH when they disagree):
- (critical) next level from the last 10 results (up when the correct share x10000 is at least 8000, down at 4000 or less, else the same) in FORTRAN as Trebago + APL as KRON witnessed
- (critical) hints left on a puzzle (0..3, none once solved) in Ada as Eve + APL as KRON witnessed
- puzzle grid (generate a grid from a seed and a level, every constraint over rows and columns) in APL as KRON
- score records (points per puzzle and the running total, refusing on overflow) in COBOL as Sutra
- learner progress tree (learner, topic, level, results) in MUMPS as MYRA
- the worked solution shown one step at a time in BASIC (twin Hatchling)
- Forth and BASIC blocks are committed in twin form: the manifest's logic line names the twin (`logic cairndrake ...`, `logic hatchling ...`).
Paraconsistent (BOTH test, in test_zxapp.c): a 4x4 grid with two valid fillings: the stored answer differs from the learner's (FALSE) while Riddle's "solved" rule passes (TRUE), giving BOTH; the test asserts state 3 and that the answer is accepted as correct ("another valid answer"), never marked wrong. Join the sources with `lpres_conjoin` / `lpres_disjoin` in C; never re-derive the tables.
Integers only: no float or double anywhere; a division that is not whole uses kernel/src/rational in C.
needs: fs.read, fs.write, ai.ask, screen.time.
Planned (no runtime yet): Python, JCL.
