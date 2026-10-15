# Wave 32 task: Patchwork (slot 388), Family history
Read ../APP_WAVE_SPEC.md first. This is an L3 app: manifest says `layer L3` and composes its built_from apps with `use`; it may only start after those waves are merged.
Depends on: wave 7 (044 kindred), wave 12 (145 cartulary), wave 18 (220 glen, 223 spindrift), wave 20 (243 varve).
Manifest: `slot 388`, `layer L3`, `version 1.0.0`, `icon icons/l3/388-patchwork.svg`, `budget 233`, model `apps/patchwork_model.{h,c}`, blocks under kernel/src/apps/blocks/patchwork/, one `use <id> 1` per part below. Each part's manifest is itself the block (C1): an L2 part exports its own entries and the L1 entries it composed (`<part>.<block>.<entry>`). The composed logic lines must fit ZXAPP_LOGIC_MAX (128) and the uses ZXAPP_USES (8); if either fails, stop and report it; do not raise a limit or edit a part.
What it does: Gathers a family's history: stories, photos, records and voices in the Hoard (`files.read`, `files.write`, `search.query`), versions (`backup.versions`), the family space (`spaces.get`, `spaces.set_members`), published to the family by CID (`aipi.put`, `web4.cid`).
Uses: `use varve 1` (the date shown and source rules); `use cartulary 1` (retention and "may this version be pruned"); `use glen 1` (the availability grid and family tree); `use spindrift 1` ("may publish" and the branch matrix); `use kindred 1` (contact index and likely duplicates). Call these composed entries; do not re-implement them.
Logic (new decisions only; each `name` is the entry; a critical one has two or three `logic` lines for one `provides`, so the witness holds it as BOTH when they disagree):
- `privacy_ok`: (critical) privacy limits (a living person's records shared only with their consent; others by the family's rule) in Ada as Eve + APL as KRON witnessed
- `dates`: (critical) date estimation from family sources (bounds intersected) in FORTRAN as Trebago + APL as KRON witnessed
- `timeline`: timeline grid in APL as KRON
- `share_out`: sharing to relatives (who gets which branch) in Ada as Eve
- `storage`: archive storage costs (bytes and fees, refusing on overflow) in COBOL as Sutra
- `family`: family tree in MUMPS as MYRA
Paraconsistent (BOTH test, in test_zxapp.c): two relatives give different years for a wedding: the date is BOTH; the test asserts state 3 and that both accounts are kept. Join the sources with `lpres_conjoin` / `lpres_disjoin` in C; never re-derive the tables.
Safety: Nothing about a living person is shared without consent.
Integers only: no float or double anywhere; a division that is not whole uses kernel/src/rational in C.
Tests: as the spec, plus a compose test (the app composes against a registry holding its parts, and every part entry it calls resolves) and the BOTH test above.
needs: fs.read, fs.write, search, backup, peers, net, ai.ask.
Pending (P1; the best-fit language has no front-end yet, the block above stands in): `pending Julia timeline`; `pending Gleam share_out`; `pending Python dates`.
Planned (no runtime yet): Julia, Gleam, Python.
