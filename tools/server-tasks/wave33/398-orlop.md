# Wave 33 task: Orlop (slot 398), Personal data store
Read ../APP_WAVE_SPEC.md first. This is an L3 app: manifest says `layer L3` and composes its built_from apps with `use`; it may only start after those waves are merged.
Depends on: wave 12 (145 cartulary); wave 17 (210 moraine); wave 20 (242 silt); wave 22 (267 holm); wave 0 (010 keepsake, merged).
Manifest: `slot 398`, `layer L3`, `version 1.0.0`, `icon icons/l3/398-orlop.svg`, `budget 233`, model `apps/orlop_model.{h,c}`, blocks under kernel/src/apps/blocks/orlop/, one `use <id> 1` per part below. Each part's manifest is itself the block (C1): an L2 part exports its own entries and the L1 entries it composed (`<part>.<block>.<entry>`). The composed logic lines must fit ZXAPP_LOGIC_MAX (128) and the uses ZXAPP_USES (8); if either fails, stop and report it; do not raise a limit or edit a part.
What it does: Stores and queries your structured data with versioned tables, signed changes and offline copies: tables in the Hoard (`files.read`, `files.write`, `files.list`), versions (`backup.versions`, `backup.list`), the change record (`journal.query`, `journal.verify`), keys in escrow (`recovery.status`).
Uses: `use cartulary 1` (retention and the record hierarchy); `use moraine 1` ("release now"); `use holm 1` ("may leave hardened mode"); `use silt 1` ("may dispose" and the register of records); `use keepsake 1` (`removed` and `keep_ok`). Call these composed entries; do not re-implement them.
Logic (new decisions only; each `name` is the entry; a critical one has two or three `logic` lines for one `provides`, so the witness holds it as BOTH when they disagree):
- `write_ok`: (critical) quota limits (a write within the table's row and byte quota AND the schema's field types) in Ada as Eve + APL as KRON witnessed
- `schema_ok`: (critical) record schemas (fixed field widths, a value that does not fit is refused) in COBOL as Sutra + APL as KRON witnessed
- `query`: column query over arrays (filters and sums) in APL as KRON
- `stats`: query statistics (rows scanned, time) exact in FORTRAN as Trebago
- `keys`: hierarchical keys in MUMPS as MYRA
Paraconsistent (BOTH test, in test_zxapp.c): two offline copies write different values to one cell: the cell is BOTH; the test asserts state 3 and that both values are stored for a later merge. Join the sources with `lpres_conjoin` / `lpres_disjoin` in C; never re-derive the tables.
Integers only: no float or double anywhere; a division that is not whole uses kernel/src/rational in C.
Tests: as the spec, plus a compose test (the app composes against a registry holding its parts, and every part entry it calls resolves) and the BOTH test above.
needs: fs.read, fs.write, backup, journal, peers, sys.read.
Pending (P1; the best-fit language has no front-end yet, the block above stands in): `pending F# schema_ok`; `pending SQL query`; `pending Julia stats`.
Planned (no runtime yet): F#, SQL, Julia.
