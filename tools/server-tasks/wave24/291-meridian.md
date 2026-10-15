# Wave 24 task: Meridian (slot 291), Voyage planner
Read ../APP_WAVE_SPEC.md first. This is an L3 app: manifest says `layer L3` and composes its built_from apps with `use`; it may only start after those waves are merged.
Depends on: wave 0 (085 waymark), wave 12 (147 binnacle, 154 chainman), wave 13 (161 chandler), wave 19 (239 isobar).
Manifest: `slot 291`, `layer L3`, `icon icons/l3/291-meridian.svg`, `budget 233`, one `use <id> 1` per part below. A part exports only what it `provides`: call an L2 part's own entries, and an entry it composed only if it re-exports it (as Murmuration re-exports swarm_shares). If an entry you need is not exported, or the composed logic lines pass ZXAPP_LOGIC_MAX (128) or the uses ZXAPP_USES (8), stop and report it; do not edit the part or raise a limit.
What it does: plans voyages by sea or land: routes, tides, weather windows, fuel and crew watches (`place.here`, `place.list`, `place.add`, `files.read`, `files.write`). zxvd has no method for weather and tide sources: use only existing methods; list what's missing in your report.
Uses: `use binnacle 1` (fix, "safe heading", distance run); `use chandler 1` (provisions, tide height, "within load limits"); `use chainman 1` (parcel area, for a land route's parcels); `use isobar 1` (station readings); `use waymark 1` (`waymark.place_ok`). Call these composed entries; do not re-implement them.
Logic (critical, new decisions only): `window_go` (wind, wave and visibility inside the declared limits for every leg of the window) in Ada as Eve + APL as KRON witnessed; `tide_current` (current along a leg from tide times, integer) in FORTRAN as Trebago + APL as KRON witnessed; `voyage_accounts` (fuel and stores in minor units) in COBOL as Sutra + FORTRAN as Trebago witnessed; `routes` grid in APL as KRON; `crew_vessel` records in MUMPS as MYRA.
BOTH test: the satellite fix and dead reckoning are 2 km apart, tolerance 300 m: the position is BOTH and a cross-check is asked for. Join with `lpres_conjoin` / `lpres_disjoin` in C; never re-derive the tables.
Advisory; the master or leader decides; not a certified navigation system.
Tests: as the spec, plus a compose test (the app composes against a registry holding its parts, and every part entry it calls resolves) and the BOTH test above.
needs: location, fs.read, fs.write, net, notify.
Planned (no runtime yet): Prolog, Simula 67, Logo.
Pending: `pending Prolog routes` (search); `pending Simula_67 tide_current` (simulation); `pending Logo tide_current` (geometry).
