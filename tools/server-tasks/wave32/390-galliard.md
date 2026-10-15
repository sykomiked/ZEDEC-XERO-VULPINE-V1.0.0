# Wave 32 task: Galliard (slot 390), Movement training
Read ../APP_WAVE_SPEC.md first. This is an L3 app: manifest says `layer L3` and composes its built_from apps with `use`; it may only start after those waves are merged.
Depends on: wave 8 (089 wellspring), wave 13 (167 tintype), wave 15 (182 mummer, 188 courser), wave 18 (224 tarn).
Manifest: `slot 390`, `layer L3`, `version 1.0.0`, `icon icons/l3/390-galliard.svg`, `budget 233`, model `apps/galliard_model.{h,c}`, blocks under kernel/src/apps/blocks/galliard/, one `use <id> 1` per part below. Each part's manifest is itself the block (C1): an L2 part exports its own entries and the L1 entries it composed (`<part>.<block>.<entry>`). The composed logic lines must fit ZXAPP_LOGIC_MAX (128) and the uses ZXAPP_USES (8); if either fails, stop and report it; do not raise a limit or edit a part.
What it does: Trains martial arts, dance or movement with pose capture, drills, grading and injury-safe limits: sessions in the Hoard (`files.read`, `files.write`), the clock (`sys.info`), classes shown in a hall (`hall.load`, `hall.play`, `hall.status`). Pose capture from the camera has no method: use only existing methods; list what's missing in your report.
Uses: `use courser 1` (pace and "over the heart-rate limit"); `use mummer 1` (cue start times); `use tarn 1` (breath pacing and session limits); `use tintype 1` (exposure value); `use wellspring 1` ("outside a safe range"). Call these composed entries; do not re-implement them.
Logic (new decisions only; each `name` is the entry; a critical one has two or three `logic` lines for one `provides`, so the witness holds it as BOTH when they disagree):
- `joint_ok`: (critical) joint angle limits (each angle within the student's declared range) in Ada as Eve + APL as KRON witnessed
- `pose`: (critical) pose geometry (angles from key points with an integer arctangent table) in FORTRAN as Trebago + APL as KRON witnessed
- `motion`: motion arrays in APL as KRON
- `fees`: dojo fees (minor units, refusing on overflow) in COBOL as Sutra
- `students`: student tree in MUMPS as MYRA
Paraconsistent (BOTH test, in test_zxapp.c): the camera grades a form 7 and the instructor 9: the grade is BOTH; the test asserts state 3 and that both stand until reviewed. Join the sources with `lpres_conjoin` / `lpres_disjoin` in C; never re-derive the tables.
Safety: Advice only; the instructor grades.
Integers only: no float or double anywhere; a division that is not whole uses kernel/src/rational in C.
Tests: as the spec, plus a compose test (the app composes against a registry holding its parts, and every part entry it calls resolves) and the BOTH test above.
needs: camera, fs.read, fs.write, sys.read, peers.
