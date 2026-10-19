# Wave 33 task: Foghorn (slot 400), Navigation aids
Read ../APP_WAVE_SPEC.md first. This is an L3 app: manifest says `layer L3` and composes its built_from apps with `use`; it may only start after those waves are merged.
Depends on: wave 12 (147 binnacle, 156 crier); wave 13 (161 chandler); wave 17 (215 headland); wave 3 (035 aerie).
Manifest: `slot 400`, `layer L3`, `version 1.0.0`, `icon icons/l3/400-foghorn.svg`, `budget 233`, model `apps/foghorn_model.{h,c}`, blocks under kernel/src/apps/blocks/foghorn/, one `use <id> 1` per part below. Each part's manifest is itself the block (C1): an L2 part exports its own entries and the L1 entries it composed (`<part>.<block>.<entry>`). The composed logic lines must fit ZXAPP_LOGIC_MAX (128) and the uses ZXAPP_USES (8); if either fails, stop and report it; do not raise a limit or edit a part.
What it does: Runs navigation aids and harbour signals: hazards broadcast once a person approves (`social.post`, `chat.post`, `pspace.post`, `harmonic.encode`, `ehop.bands`), beacon health (`log.tail`, `journal.query`), places (`place.list`, `place.add`). Beacon and light control has no method: use only existing methods; list what's missing in your report.
Uses: `use headland 1` (trend and "raise an alarm"); `use chandler 1` (tide height and "within load limits"); `use binnacle 1` (the fix and "safe heading"); `use crier 1` ("may broadcast to this channel"); `use aerie 1` (frame tag split and "fits the band"). Call these composed entries; do not re-implement them.
Logic (new decisions only; each `name` is the entry; a critical one has two or three `logic` lines for one `provides`, so the witness holds it as BOTH when they disagree):
- `timing_ok`: (critical) signal timing limits (each light's character within its declared period and phase) in Ada as Eve + APL as KRON witnessed
- `visibility`: (critical) visibility range (nominal range by intensity and the met visibility, integer table) in FORTRAN as Trebago + APL as KRON witnessed
- `broadcast`: hazard broadcast routing (area to channel) in Ada as Eve
- `beacons`: beacon grid in APL as KRON
- `dues`: light dues (minor units, refusing on overflow) in COBOL as Sutra
- `harbour`: harbour tree in MUMPS as MYRA
Paraconsistent (BOTH test, in test_zxapp.c): two reports of a wreck give different positions: the hazard is BOTH; the test asserts state 3 and that it is broadcast as BOTH with both positions and its confidence. Join the sources with `lpres_conjoin` / `lpres_disjoin` in C; never re-derive the tables.
Safety: Life-safety: advisory; never replaces official notices; a broadcast needs a person's approval.
Integers only: no float or double anywhere; a division that is not whole uses kernel/src/rational in C.
Tests: as the spec, plus a compose test (the app composes against a registry holding its parts, and every part entry it calls resolves) and the BOTH test above.
needs: peers, net, journal, location, notify.
Pending (P1; the best-fit language has no front-end yet, the block above stands in): `pending JOVIAL timing_ok`; `pending Go broadcast`; `pending LabVIEW visibility`.
Planned (no runtime yet): JOVIAL, Go, LabVIEW.
