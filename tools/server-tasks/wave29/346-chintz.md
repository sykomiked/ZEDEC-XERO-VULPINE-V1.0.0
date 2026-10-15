# Wave 29 task: Chintz (slot 346), Plant catalogue
Read ../APP_WAVE_SPEC.md first. This is an L3 app: manifest says `layer L3` and composes its built_from apps with `use`; it may only start after those waves are merged.
Depends on: wave 12 (149 dibber); wave 13 (167 tintype, 168 bindery); wave 19 (230 fen); wave 8 (102 fieldguide).
Manifest: `slot 346`, `layer L3`, `version 1.0.0`, `icon icons/l3/346-chintz.svg`, `budget 233`, model `apps/chintz_model.{h,c}`, blocks under kernel/src/apps/blocks/chintz/, one `use <id> 1` per part below. Each part's manifest is itself the block (C1): an L2 part exports its own entries and the L1 entries it composed (`<part>.<block>.<entry>`). The composed logic lines must fit ZXAPP_LOGIC_MAX (128) and the uses ZXAPP_USES (8); if either fails, stop and report it; do not raise a limit or edit a part.
What it does: Identifies, catalogues and monitors plants in the field or greenhouse: photos and labels in the Hoard (`files.read`, `files.write`, `search.query`), the place after `place.ask`, labels printed (`print.submit`), identification suggestions from `swarm.ask` (held).
Uses: `use fen 1` ("may share this specimen's location" and the specimen register); `use dibber 1` (water per bed and "may water now"); `use tintype 1` (exposure value and "possibly altered"); `use bindery 1` ("may lend" and the loans ledger, for herbarium loans); `use fieldguide 1` ("show a caution"). Call these composed entries; do not re-implement them.
Logic (new decisions only; each `name` is the entry; a critical one has two or three `logic` lines for one `provides`, so the witness holds it as BOTH when they disagree):
- `spray_ok`: (critical) pesticide limits (rate at most the label maximum, days since the last spray at least the interval, never on a protected species) in Ada as Eve + APL as KRON witnessed
- `growth`: (critical) growth curve per plant (height mm by date, rate per week) in FORTRAN as Trebago + APL as KRON witnessed
- `probe`: greenhouse probe raw counts to tenths of a degree and moisture, with a range check, in Forth (twin Cairndrake)
- `traits`: trait matrix (specimens x traits) in APL as KRON
- `loans`: herbarium loan records (loans out, refusing on overflow) in COBOL as Sutra
- `taxa`: taxonomy tree in MUMPS as MYRA
- Forth and BASIC blocks are committed in twin form: the manifest's logic line names the twin (`logic cairndrake ...`, `logic hatchling ...`).
Paraconsistent (BOTH test, in test_zxapp.c): two experts give different names for one specimen: the identification is BOTH; the test asserts state 3 and that both names and their evidence stay on the label until they agree. Join the sources with `lpres_conjoin` / `lpres_disjoin` in C; never re-derive the tables.
Safety: Never says a plant is safe to eat; protected species' places are never published.
Integers only: no float or double anywhere; a division that is not whole uses kernel/src/rational in C.
Tests: as the spec, plus a compose test (the app composes against a registry holding its parts, and every part entry it calls resolves) and the BOTH test above.
needs: fs.read, fs.write, search, location, camera, print, ai.ask.
Pending (P1; the best-fit language has no front-end yet, the block above stands in): `pending Julia growth`; `pending Mojo traits`.
Planned (no runtime yet): Julia, Mojo.
