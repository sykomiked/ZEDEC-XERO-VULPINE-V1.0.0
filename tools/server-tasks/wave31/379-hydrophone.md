# Wave 31 task: Hydrophone (slot 379), Underwater listening
Read ../APP_WAVE_SPEC.md first. This is an L3 app: manifest says `layer L3` and composes its built_from apps with `use`; it may only start after those waves are merged.
Depends on: wave 13 (161 chandler), wave 14 (176 verderer), wave 15 (190 shipwright), wave 17 (214 updraft), wave 21 (258 atoll).
Manifest: `slot 379`, `layer L3`, `version 1.0.0`, `icon icons/l3/379-hydrophone.svg`, `budget 233`, model `apps/hydrophone_model.{h,c}`, blocks under kernel/src/apps/blocks/hydrophone/, one `use <id> 1` per part below. Each part's manifest is itself the block (C1): an L2 part exports its own entries and the L1 entries it composed (`<part>.<block>.<entry>`). The composed logic lines must fit ZXAPP_LOGIC_MAX (128) and the uses ZXAPP_USES (8); if either fails, stop and report it; do not raise a limit or edit a part.
What it does: Tracks marine life and ship noise with underwater listening stations, sharing sightings and protecting migration routes: stations (`net.peers`, `devmesh.devices`), messages relayed (`ehop.seal`, `ehop.open`, `harmonic.encode`), places (`place.list`), records (`files.read`, `files.write`, `search.query`). Hydrophone audio has no method: use only existing methods; list what's missing in your report.
Uses: `use shipwright 1` ("warn this district" pattern for protective zones); `use atoll 1` ("may land this catch" and the tide model); `use updraft 1` (link margin); `use chandler 1` (tide height); `use verderer 1` (population estimate and "share this place"). Call these composed entries; do not re-implement them.
Logic (new decisions only; each `name` is the entry; a critical one has two or three `logic` lines for one `provides`, so the witness holds it as BOTH when they disagree):
- `zone_ok`: (critical) zone speed limits (a vessel's speed in a protective zone at most the declared knots while the zone is active) in Ada as Eve + APL as KRON witnessed
- `detect`: (critical) acoustic detection (band energy over the noise floor in integer spectra) in FORTRAN as Trebago + APL as KRON witnessed
- `spectro`: spectrogram arrays in APL as KRON
- `rules`: zone activation rules (a confirmed or BOTH sighting keeps the zone active) in Ada as Eve
- `levy`: shipping levy (minor units, refusing on overflow) in COBOL as Sutra
- `species`: species tree in MUMPS as MYRA
Paraconsistent (BOTH test, in test_zxapp.c): the acoustic station detects a whale and the visual watch reports none: the sighting is BOTH; the test asserts state 3 and that the protective zone stays active until confirmed. Join the sources with `lpres_conjoin` / `lpres_disjoin` in C; never re-derive the tables.
Safety: Advisory; protective zones are the authority's to enforce.
Integers only: no float or double anywhere; a division that is not whole uses kernel/src/rational in C.
Tests: as the spec, plus a compose test (the app composes against a registry holding its parts, and every part entry it calls resolves) and the BOTH test above.
needs: peers, net, location, fs.read, fs.write, search, notify.
Pending (P1; the best-fit language has no front-end yet, the block above stands in): `pending IPL species`; `pending Prolog rules`; `pending Racket detect`.
Planned (no runtime yet): IPL, Prolog, Racket.
