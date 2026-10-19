# Wave 30 task: Crowsnest (slot 358), Site watch
Read ../APP_WAVE_SPEC.md first. This is an L3 app: manifest says `layer L3` and composes its built_from apps with `use`; it may only start after those waves are merged.
Depends on: wave 12 (153 pavise); wave 14 (176 verderer); wave 15 (181 loupe); wave 17 (215 headland); wave 1 (018 witness).
Manifest: `slot 358`, `layer L3`, `version 1.0.0`, `icon icons/l3/358-crowsnest.svg`, `budget 233`, model `apps/crowsnest_model.{h,c}`, blocks under kernel/src/apps/blocks/crowsnest/, one `use <id> 1` per part below. Each part's manifest is itself the block (C1): an L2 part exports its own entries and the L1 entries it composed (`<part>.<block>.<entry>`). The composed logic lines must fit ZXAPP_LOGIC_MAX (128) and the uses ZXAPP_USES (8); if either fails, stop and report it; do not raise a limit or edit a part.
What it does: Watches cameras and sensors across sites, spots unusual activity and logs signed evidence: events (`log.tail`, `journal.query`, `journal.verify`), files (`files.stat`, `search.query`), places (`place.list`), the perimeter (`features.get`, `sys.net`). Camera streams from other sites have no method: use only existing methods; list what's missing in your report.
Uses: `use pavise 1` ("allow" and the exposure score); `use headland 1` (trend and "raise an alarm"); `use loupe 1` ("evidence admissible" and timeline gaps); `use verderer 1` ("share this place"); `use witness 1` ("chain intact"). Call these composed entries; do not re-implement them.
Logic (new decisions only; each `name` is the entry; a critical one has two or three `logic` lines for one `provides`, so the witness holds it as BOTH when they disagree):
- `mask_ok`: (critical) privacy masking (frames outside the owner's property and every masked zone are blanked before storage) in Ada as Eve + APL as KRON witnessed
- `motion`: (critical) motion detection (changed pixels per block over the threshold, integer frame differences) in FORTRAN as Trebago + APL as KRON witnessed
- `escalate`: escalation (two sensors agree, or BOTH, goes to a person) in Ada as Eve
- `frames`: frame arrays in APL as KRON
- `billing`: incident billing (callouts in minor units, refusing on overflow) in COBOL as Sutra
- `sites`: site tree in MUMPS as MYRA
Paraconsistent (BOTH test, in test_zxapp.c): the door sensor reports entry and the camera shows no motion: the event is BOTH; the test asserts state 3 and that it is logged and escalated to a person, not dismissed or alarmed. Join the sources with `lpres_conjoin` / `lpres_disjoin` in C; never re-derive the tables.
Safety: Privacy masking always applies; evidence is never shared without the owner.
Integers only: no float or double anywhere; a division that is not whole uses kernel/src/rational in C.
Tests: as the spec, plus a compose test (the app composes against a registry holding its parts, and every part entry it calls resolves) and the BOTH test above.
needs: journal, sys.read, search, fs.read, location, camera, net, notify.
Pending (P1; the best-fit language has no front-end yet, the block above stands in): `pending ALGOL_60 mask_ok`; `pending Haskell escalate`; `pending JOVIAL motion`.
Planned (no runtime yet): ALGOL 60, Haskell, JOVIAL.
