# Wave 33 task: Descant (slot 397), Learning tree
Read ../APP_WAVE_SPEC.md first. This is an L3 app: manifest says `layer L3` and composes its built_from apps with `use`; it may only start after those waves are merged.
Depends on: wave 12 (151 lectern, 149 dibber); wave 14 (169 hornbook); wave 17 (209 freshet); wave 8 (098 sapling).
Manifest: `slot 397`, `layer L3`, `version 1.0.0`, `icon icons/l3/397-descant.svg`, `budget 233`, model `apps/descant_model.{h,c}`, blocks under kernel/src/apps/blocks/descant/, one `use <id> 1` per part below. Each part's manifest is itself the block (C1): an L2 part exports its own entries and the L1 entries it composed (`<part>.<block>.<entry>`). The composed logic lines must fit ZXAPP_LOGIC_MAX (128) and the uses ZXAPP_USES (8); if either fails, stop and report it; do not raise a limit or edit a part.
What it does: Grows a personal learning tree: goals and lessons in the Hoard (`files.read`, `files.write`, `search.query`), lessons run (`play.catalogue`, `play.run`), credentials signed by a teacher and checked (`web4.verify`).
Uses: `use hornbook 1` ("age-appropriate" and the next lesson step); `use lectern 1` ("make a card from this highlight"); `use dibber 1` (days to harvest pattern, reused as days to a goal); `use freshet 1` ("within the template's declared limits"); `use sapling 1` ("lesson unlocked" and progress). Call these composed entries; do not re-implement them.
Logic (new decisions only; each `name` is the entry; a critical one has two or three `logic` lines for one `provides`, so the witness holds it as BOTH when they disagree):
- `credential_ok`: (critical) "issue this credential" (every required skill mastered AND a teacher signed) in Ada as Eve + APL as KRON witnessed
- `mastery`: (critical) mastery estimate (right answers in a row and spaced reviews, integer score) in FORTRAN as Trebago + APL as KRON witnessed
- `pace_ok`: pace limits (lessons per day within the learner's declared pace) in Ada as Eve
- `steps`: the next step shown one at a time in BASIC (twin Hatchling)
- `skills`: skill grid in APL as KRON
- `records`: credential records (refusing on overflow) in COBOL as Sutra
- `tree`: learning tree in MUMPS as MYRA
- Forth and BASIC blocks are committed in twin form: the manifest's logic line names the twin (`logic cairndrake ...`, `logic hatchling ...`).
Paraconsistent (BOTH test, in test_zxapp.c): the tests mark a skill mastered and the teacher does not: it is BOTH; the test asserts state 3 and that both are recorded and no credential is issued yet. Join the sources with `lpres_conjoin` / `lpres_disjoin` in C; never re-derive the tables.
Integers only: no float or double anywhere; a division that is not whole uses kernel/src/rational in C.
Tests: as the spec, plus a compose test (the app composes against a registry holding its parts, and every part entry it calls resolves) and the BOTH test above.
needs: fs.read, fs.write, search, ai.ask, net.
Pending (P1; the best-fit language has no front-end yet, the block above stands in): `pending ML credential_ok`; `pending ALGOL_60 mastery`.
Planned (no runtime yet): ML, ALGOL 60.
