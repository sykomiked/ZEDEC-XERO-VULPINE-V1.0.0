# Wave 33 task: Quadrant (slot 394), Satellite links
Read ../APP_WAVE_SPEC.md first. This is an L3 app: manifest says `layer L3` and composes its built_from apps with `use`; it may only start after those waves are merged.
Depends on: wave 5 (035 aerie), wave 13 (160 astrolabe), wave 16 (200 linkman), wave 17 (214 updraft), wave 18 (225 tributary).
Manifest: `slot 394`, `layer L3`, `version 1.0.0`, `icon icons/l3/394-quadrant.svg`, `budget 233`, model `apps/quadrant_model.{h,c}`, blocks under kernel/src/apps/blocks/quadrant/, one `use <id> 1` per part below. Each part's manifest is itself the block (C1): an L2 part exports its own entries and the L1 entries it composed (`<part>.<block>.<entry>`). The composed logic lines must fit ZXAPP_LOGIC_MAX (128) and the uses ZXAPP_USES (8); if either fails, stop and report it; do not raise a limit or edit a part.
What it does: Links remote places by satellite: book passes, relay messages and earth images, share ground-station time. Bands and frames (`ehop.bands`, `ehop.seal`, `ehop.open`, `harmonic.encode`, `harmonic.decode`, `freight.pack`, `freight.unpack`), peers (`net.peers`, `peer.status`), images in the Hoard (`files.read`, `files.write`). Satellite pass data and radio hardware have no method: use only existing methods; list what's missing in your report.
Uses: `use updraft 1` (link margin and "transmit allowed"); `use linkman 1` ("trusted peer"); `use tributary 1` (agreeing answers); `use astrolabe 1` (next time an object is above the declared altitude); `use aerie 1` (frame tag split and "fits the band"). Call these composed entries; do not re-implement them.
Logic (new decisions only; each `name` is the entry; a critical one has two or three `logic` lines for one `provides`, so the witness holds it as BOTH when they disagree):
- `tx_ok`: (critical) transmit power limits (at most the licensed maximum for the band, only during a booked pass) in Ada as Eve + APL as KRON witnessed
- `orbit`: (critical) pass prediction (rise, culmination and set minutes from integer orbital elements) in FORTRAN as Trebago + APL as KRON witnessed
- `relay`: message relay plan (pass by pass) in Ada as Eve
- `imagery`: imagery arrays in APL as KRON
- `report`: pass log summaries (passes, bytes per station) in APL as KRON
- `billing`: pass billing (minor units, refusing on overflow) in COBOL as Sutra
- `stations`: station tree in MUMPS as MYRA
Paraconsistent (BOTH test, in test_zxapp.c): one relayed message arrives in two different forms: it is BOTH; the test asserts state 3 and that both are kept, nothing silently corrupted. Join the sources with `lpres_conjoin` / `lpres_disjoin` in C; never re-derive the tables.
Safety: The owner keeps to licensed bands; never transmits in tests.
Integers only: no float or double anywhere; a division that is not whole uses kernel/src/rational in C.
Tests: as the spec, plus a compose test (the app composes against a registry holding its parts, and every part entry it calls resolves) and the BOTH test above.
needs: net, peers, fs.read, fs.write, location, sys.read.
Pending (P1; the best-fit language has no front-end yet, the block above stands in): `pending Elixir relay`; `pending AWK report`; `pending MATLAB imagery`.
Planned (no runtime yet): Elixir, AWK, MATLAB.
