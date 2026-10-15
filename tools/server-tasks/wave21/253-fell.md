# Wave 21 task: Fell (slot 253), Search and rescue
Read ../APP_WAVE_SPEC.md first. This is an L2 app: manifest says `layer L2` and composes its built_from apps' blocks with `use`; it may only start after those L1 waves are merged.
Depends on: wave 0 (085 waymark), wave 2 (027 commons), wave 5 (035 aerie), wave 11 (143 watchfire).
Manifest: `slot 253`, `layer L2`, `version 1.0.0` and its `provides` lines (so L3 apps can use it, C1), `icon icons/l2/253-fell.svg`, `budget 233`, model `apps/fell_model.{h,c}`, blocks under kernel/src/apps/blocks/fell/, one `use <id> 1` per part below. The composed logic lines must fit ZXAPP_LOGIC_MAX (128) and the uses ZXAPP_USES (8); if either fails, stop and report it; do not raise a limit or edit the part.
What it does: Coordinates search and rescue teams: maps and places (`place.here`, `place.list`, `place.add`), radio messages (`ehop.bands`, `ehop.seal`, `ehop.open`, `harmonic.encode`, `harmonic.decode`, `freight.pack`), the team room (`chat.post`, `chat.feed`, `pspace.post`), casualty records in the Hoard (`files.read`, `files.write`).
Uses: `use waymark 1` (`place_ok`); `use aerie 1` (frame tag split and "fits the band"); `use watchfire 1` (share-location truth and medical ID records); `use commons 1` ("may I post here"). Call these composed entries; do not re-implement them; each part's manifest is itself the block (C1).
Logic (new decisions only; a critical one has two or three `logic` lines for one `provides`, so the witness holds it as BOTH when they disagree):
- (critical) sector priority (probability of area x10000 per sector, shifted by searched coverage, summing exactly to 10000) in FORTRAN as Trebago + APL as KRON witnessed; a suggestion for the incident commander
- (critical) crew safety (shift at most 12 hours, check-in at most every 60 minutes, overdue raised) in Ada as Eve + APL as KRON witnessed
- sector grid in APL as KRON, and the board's filters (unsearched sectors, sightings by time) as K queries in APL as KRON
- callout records (crew hours and mileage claims in minor units, refusing on overflow) in COBOL as Sutra
- casualty tree in MUMPS as MYRA
Paraconsistent (BOTH test, in test_zxapp.c): a sighting in sector 4 at 14:10 and another in sector 9 at 14:12 give BOTH; the test asserts state 3 and that neither sector is dropped. Join the sources with `lpres_conjoin` / `lpres_disjoin` in C; never re-derive the tables.
Safety: Life-safety: advisory and human-approved; the incident commander decides; never calls emergency services by itself; never transmits in tests.
Integers only: no float or double anywhere; a division that is not whole uses kernel/src/rational in C.
needs: location, peers, net, fs.read, fs.write, notify.
Planned (no runtime yet): Go, Prolog.
