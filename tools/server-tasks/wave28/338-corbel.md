# Wave 28 task: Corbel (slot 338), Heritage site guide
Read ../APP_WAVE_SPEC.md first. This is an L3 app: manifest says `layer L3` and composes its built_from apps with `use`; it may only start after those waves are merged.
Depends on: wave 0 (085 waymark), wave 9 (102 fieldguide), wave 12 (154 chainman), wave 19 (238 billow), wave 20 (243 varve).
Manifest: `slot 338`, `layer L3`, `icon icons/l3/338-corbel.svg`, `budget 233`, one `use <id> 1` per part below. A part exports only what it `provides`: call an L2 part's own entries, and an entry it composed only if it re-exports it (as Murmuration re-exports swarm_shares). If an entry you need is not exported, or the composed logic lines pass ZXAPP_LOGIC_MAX (128) or the uses ZXAPP_USES (8), stop and report it; do not edit the part or raise a limit.
What it does: guides visitors through heritage sites: stories, audio tours, conservation records and ticketing (`place.list`, `files.read`, `files.write`, `hall.load`, `hall.play`).
Uses: `use varve 1` (timeline of dated events); `use chainman 1` (parcel area); `use billow 1` (show segments and captions); `use waymark 1` (`waymark.place_ok`); `use fieldguide 1` ("show a caution"). Call these composed entries; do not re-implement them.
Logic (critical, new decisions only): `capacity_ok` (visitors in a room at most its declared capacity) in Ada as Eve + APL as KRON witnessed; `wear` (footfall x the floor's declared wear rate) in FORTRAN as Trebago + APL as KRON witnessed; `ticket_revenue` (in minor units) in COBOL as Sutra + FORTRAN as Trebago witnessed; `site` grid in APL as KRON; `artefacts` hierarchy in MUMPS as MYRA.
BOTH test: account A dates the gate to 1320 and account B to 1410: both are presented as BOTH, each with its sources. Join with `lpres_conjoin` / `lpres_disjoin` in C; never re-derive the tables.
Tests: as the spec, plus a compose test (the app composes against a registry holding its parts, and every part entry it calls resolves) and the BOTH test above.
needs: location, fs.read, fs.write, peers, net.
Planned (no runtime yet): Mojo, LabVIEW, Pascal.
Pending: Mojo (ai): no entry here is an obvious fit; put `pending Mojo <entry>` on the one closest to ai and name it in your report; `pending LabVIEW wear` (signal); Pascal (stepwise): no entry here is an obvious fit; put `pending Pascal <entry>` on the one closest to stepwise and name it in your report.
