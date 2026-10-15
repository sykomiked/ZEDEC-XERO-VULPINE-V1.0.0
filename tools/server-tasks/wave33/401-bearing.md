# Wave 33 task: Bearing (slot 401), Multi-stop journeys
Read ../APP_WAVE_SPEC.md first. This is an L3 app: manifest says `layer L3` and composes its built_from apps with `use`; it may only start after those waves are merged.
Depends on: wave 0 (085 waymark), wave 12 (147 binnacle, 150 pannier, 154 chainman), wave 17 (211 tradewind).
Manifest: `slot 401`, `layer L3`, `version 1.0.0`, `icon icons/l3/401-bearing.svg`, `budget 233`, model `apps/bearing_model.{h,c}`, blocks under kernel/src/apps/blocks/bearing/, one `use <id> 1` per part below. Each part's manifest is itself the block (C1): an L2 part exports its own entries and the L1 entries it composed (`<part>.<block>.<entry>`). The composed logic lines must fit ZXAPP_LOGIC_MAX (128) and the uses ZXAPP_USES (8); if either fails, stop and report it; do not raise a limit or edit a part.
What it does: Plans multi-stop journeys across land and sea: places (`place.here`, `place.list`, `place.add`), tickets and papers in the Hoard (`files.read`, `files.write`), parcels and messages (`freight.pack`, `net.peers`), ticket payments (`ledger.post`). Timetables and live delays have no method: use only existing methods; list what's missing in your report.
Uses: `use binnacle 1` (bearing and distance); `use tradewind 1` (best order of stops and "within load and hours limits"); `use chainman 1` ("closes within tolerance" for border maps); `use pannier 1` ("send now"); `use waymark 1` (`place_ok`). Call these composed entries; do not re-implement them.
Logic (new decisions only; each `name` is the entry; a critical one has two or three `logic` lines for one `provides`, so the witness holds it as BOTH when they disagree):
- `connect_ok`: (critical) connection limits (each transfer at least the declared minimum minutes AND papers valid for every border) in Ada as Eve + APL as KRON witnessed
- `timing`: (critical) route timing (arrival minutes as a running sum over legs and waits) in FORTRAN as Trebago + APL as KRON witnessed
- `options`: itinerary search over alternatives (every combination within the limits) in APL as KRON
- `timetable`: timetable grid in APL as KRON
- `tickets`: ticket payments (minor units, refusing on overflow) in COBOL as Sutra
- `itinerary`: itinerary tree in MUMPS as MYRA
Paraconsistent (BOTH test, in test_zxapp.c): the timetable says 14:05 and live data says 14:40: the departure is BOTH; the test asserts state 3 and that both plans are shown. Join the sources with `lpres_conjoin` / `lpres_disjoin` in C; never re-derive the tables.
Safety: Payments need the owner's confirmation.
Integers only: no float or double anywhere; a division that is not whole uses kernel/src/rational in C.
Tests: as the spec, plus a compose test (the app composes against a registry holding its parts, and every part entry it calls resolves) and the BOTH test above.
needs: location, fs.read, fs.write, peers, net, ledger.read, ledger.post.
Pending (P1; the best-fit language has no front-end yet, the block above stands in): `pending Prolog options`; `pending Simula_67 timing`; `pending Logo timetable`.
Now runs (Lingua front-end built): Logo (twin Kiteling) for geometry: `logic logo <entry> apps/blocks/bearing/<entry>.lg`, in the twin's canonical form, joined to another language's line for the same entry.
Planned (no runtime yet): Prolog, Simula 67.
