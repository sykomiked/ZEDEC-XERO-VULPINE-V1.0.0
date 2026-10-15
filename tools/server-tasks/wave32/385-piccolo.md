# Wave 32 task: Piccolo (slot 385), Pollinator gardens
Read ../APP_WAVE_SPEC.md first. This is an L3 app: manifest says `layer L3` and composes its built_from apps with `use`; it may only start after those waves are merged.
Depends on: wave 9 (102 fieldguide), wave 12 (149 dibber), wave 15 (185 skep, 192 coppice), wave 19 (230 fen).
Manifest: `slot 385`, `layer L3`, `version 1.0.0`, `icon icons/l3/385-piccolo.svg`, `budget 233`, model `apps/piccolo_model.{h,c}`, blocks under kernel/src/apps/blocks/piccolo/, one `use <id> 1` per part below. Each part's manifest is itself the block (C1): an L2 part exports its own entries and the L1 entries it composed (`<part>.<block>.<entry>`). The composed logic lines must fit ZXAPP_LOGIC_MAX (128) and the uses ZXAPP_USES (8); if either fails, stop and report it; do not raise a limit or edit a part.
What it does: Plans pollinator-friendly gardens and tracks bees, birds and blooms with neighbours: beds and sightings in the Hoard (`files.read`, `files.write`, `search.query`), places (`place.here`, `place.list`), the neighbours' circle (`social.post`, `social.feed`).
Uses: `use dibber 1` (water per bed and the bed grid); `use skep 1` (task placement and the grant ledger); `use fen 1` ("may share this specimen's location"); `use coppice 1` ("felling allowed"); `use fieldguide 1` ("show a caution"). Call these composed entries; do not re-implement them.
Logic (new decisions only; each `name` is the entry; a critical one has two or three `logic` lines for one `provides`, so the witness holds it as BOTH when they disagree):
- `spray_ok`: (critical) pesticide limits (none while a declared pollinator plant blooms; label rate otherwise) in Ada as Eve + APL as KRON witnessed
- `bloom`: (critical) bloom timing (degree days to flowering by species table) in FORTRAN as Trebago + APL as KRON witnessed
- `planting`: planting layout across beds (bloom gaps filled month by month) in APL as KRON
- `calendar`: bloom calendar grid in APL as KRON
- `swaps`: seed swap credits (refusing on overflow) in COBOL as Sutra
- `species`: species tree in MUMPS as MYRA
Paraconsistent (BOTH test, in test_zxapp.c): two neighbours report a rare bee and a common one for the same photo: the sighting is BOTH; the test asserts state 3 and that both stay until verified. Join the sources with `lpres_conjoin` / `lpres_disjoin` in C; never re-derive the tables.
Safety: Seed swap credits are not money; a rare species' place is coarsened before it is shared.
Integers only: no float or double anywhere; a division that is not whole uses kernel/src/rational in C.
Tests: as the spec, plus a compose test (the app composes against a registry holding its parts, and every part entry it calls resolves) and the BOTH test above.
needs: fs.read, fs.write, search, location, peers, net.
Pending (P1; the best-fit language has no front-end yet, the block above stands in): `pending APT planting`; `pending Simula_67 bloom`; `pending Modula-2 species`.
Planned (no runtime yet): APT, Simula 67, Modula-2.
