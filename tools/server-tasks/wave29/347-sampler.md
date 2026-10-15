# Wave 29 task: Sampler (slot 347), Collection catalogue
Read ../APP_WAVE_SPEC.md first. This is an L3 app: manifest says `layer L3` and composes its built_from apps with `use`; it may only start after those waves are merged.
Depends on: wave 12 (145 cartulary); wave 13 (168 bindery); wave 20 (243 varve, 242 silt); wave 0 (049 glint, merged).
Manifest: `slot 347`, `layer L3`, `version 1.0.0`, `icon icons/l3/347-sampler.svg`, `budget 233`, model `apps/sampler_model.{h,c}`, blocks under kernel/src/apps/blocks/sampler/, one `use <id> 1` per part below. Each part's manifest is itself the block (C1): an L2 part exports its own entries and the L1 entries it composed (`<part>.<block>.<entry>`). The composed logic lines must fit ZXAPP_LOGIC_MAX (128) and the uses ZXAPP_USES (8); if either fails, stop and report it; do not raise a limit or edit a part.
What it does: Catalogues a museum or private collection: objects, provenance, condition reports, loans and exhibition labels in the Hoard (`files.read`, `files.write`, `search.query`), versions (`backup.versions`), the record proven by the journal (`journal.query`, `journal.verify`), labels printed (`print.submit`).
Uses: `use cartulary 1` (retention schedule and "may this version be pruned"); `use bindery 1` ("may lend" and the loans ledger); `use varve 1` (the date shown for an event); `use silt 1` ("may dispose" and the register of records); `use glint 1` (Glint's item record (`item_ok`)). Call these composed entries; do not re-implement them.
Logic (new decisions only; each `name` is the entry; a critical one has two or three `logic` lines for one `provides`, so the witness holds it as BOTH when they disagree):
- `loan_ok`: (critical) "may lend this object" (condition at least the declared grade AND the borrower's humidity within the object's limits AND insured value recorded) in Ada as Eve + APL as KRON witnessed
- `insured`: (critical) loan and insurance values (minor units, refusing on overflow) in COBOL as Sutra + FORTRAN as Trebago witnessed
- `decay`: decay rates (condition grade change per year from the condition reports) exact in FORTRAN as Trebago
- `humidity`: humidity limits (per material, per cent x10) in Ada as Eve
- `catalogue`: catalogue arrays (objects x fields, counts per gallery) in APL as KRON
- `provenance`: provenance tree in MUMPS as MYRA
Paraconsistent (BOTH test, in test_zxapp.c): two provenance histories name different owners for 1932: the record is BOTH; the test asserts state 3 and that both histories stay on the object, neither erased. Join the sources with `lpres_conjoin` / `lpres_disjoin` in C; never re-derive the tables.
Integers only: no float or double anywhere; a division that is not whole uses kernel/src/rational in C.
Tests: as the spec, plus a compose test (the app composes against a registry holding its parts, and every part entry it calls resolves) and the BOTH test above.
needs: fs.read, fs.write, search, backup, journal, print.
Pending (P1; the best-fit language has no front-end yet, the block above stands in): `pending Objective-C provenance`; `pending SAS catalogue`; `pending ALGOL_60 loan_ok`; `pending Haskell insured`.
Planned (no runtime yet): Objective-C, SAS, ALGOL 60, Haskell.
