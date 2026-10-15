# Wave 25 task: Sightline (slot 298), Trails and patrols
Read ../APP_WAVE_SPEC.md first. This is an L3 app: manifest says `layer L3` and composes its built_from apps with `use`; it may only start after those waves are merged.
Depends on: wave 0 (085 waymark), wave 12 (154 chainman), wave 14 (176 verderer), wave 19 (239 isobar), wave 21 (253 fell).
Manifest: `slot 298`, `layer L3`, `icon icons/l3/298-sightline.svg`, `budget 233`, one `use <id> 1` per part below. A part exports only what it `provides`: call an L2 part's own entries, and an entry it composed only if it re-exports it (as Murmuration re-exports swarm_shares). If an entry you need is not exported, or the composed logic lines pass ZXAPP_LOGIC_MAX (128) or the uses ZXAPP_USES (8), stop and report it; do not edit the part or raise a limit.
What it does: plans and patrols trails and wild land: routes, hazards, check-ins and rescue handoffs (`place.here`, `place.list`, `place.add`, `chat.post`, `pspace.post`, `files.read`, `files.write`).
Uses: `use verderer 1` ("share this place"); `use chainman 1` (parcel area, overlaps); `use isobar 1` (station readings); `use fell 1` (team room, casualty records for a handoff); `use waymark 1` (`waymark.place_ok`). Call these composed entries; do not re-implement them.
Logic (critical, new decisions only): `overdue` (a check-in later than the patrol's declared time limit raises a handoff for a person to confirm) in Ada as Eve + APL as KRON witnessed; `terrain_fire` (fire danger from temperature, humidity and wind, integer index) in FORTRAN as Trebago + APL as KRON witnessed; `permit_fees` (in minor units) in COBOL as Sutra; `trails` grid in APL as KRON; `visitors` records in MUMPS as MYRA.
BOTH test: report 1 says trail 6 is open and report 2 says it is blocked by a fall: the trail is BOTH and marked uncertain. Join with `lpres_conjoin` / `lpres_disjoin` in C; never re-derive the tables.
Life-safety: advisory; rescue handoffs are a person's call; never replaces emergency services.
Tests: as the spec, plus a compose test (the app composes against a registry holding its parts, and every part entry it calls resolves) and the BOTH test above.
needs: location, peers, net, fs.read, fs.write, notify.
Planned (no runtime yet): Prolog, Simula 67, Logo.
Pending: `pending Prolog trails` (search); `pending Simula_67 terrain_fire` (simulation); `pending Logo terrain_fire` (geometry).
