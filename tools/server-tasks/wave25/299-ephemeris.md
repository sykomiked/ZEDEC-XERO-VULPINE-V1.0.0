# Wave 25 task: Ephemeris (slot 299), Observing nights
Read ../APP_WAVE_SPEC.md first. This is an L3 app: manifest says `layer L3` and composes its built_from apps with `use`; it may only start after those waves are merged.
Depends on: wave 9 (101 orrery), wave 13 (160 astrolabe), wave 14 (172 escapement), wave 16 (200 linkman), wave 19 (239 isobar).
Manifest: `slot 299`, `layer L3`, `icon icons/l3/299-ephemeris.svg`, `budget 233`, one `use <id> 1` per part below. A part exports only what it `provides`: call an L2 part's own entries, and an entry it composed only if it re-exports it (as Murmuration re-exports swarm_shares). If an entry you need is not exported, or the composed logic lines pass ZXAPP_LOGIC_MAX (128) or the uses ZXAPP_USES (8), stop and report it; do not edit the part or raise a limit.
What it does: plans observing nights: object positions, rise and set times, weather and a log of what you saw (`place.here`, `sys.info`, `net.peers`, `files.write`). zxvd has no method for weather source: use only existing methods; list what's missing in your report.
Uses: `use astrolabe 1` (next time above the altitude, "session go"); `use linkman 1` (median latency, for a remote telescope link); `use escapement 1` (clock offset); `use isobar 1` (station readings); `use orrery 1` (day number, mean longitude). Call these composed entries; do not re-implement them.
Logic (critical, new decisions only): `exposure_ok` (exposure and session length inside the declared limits) in Ada as Eve + APL as KRON witnessed; `orbit` (rise, transit and set in minutes of day) in FORTRAN as Trebago + APL as KRON witnessed; `equipment` ledger (items in minor units) in COBOL as Sutra; `sky` coordinate arrays in APL as KRON; `observations` catalogue in MUMPS as MYRA.
BOTH test: catalogue A places object 33 at RA 10h 12m and catalogue B at 10h 16m: both are kept as BOTH until your own observation decides. Join with `lpres_conjoin` / `lpres_disjoin` in C; never re-derive the tables.
Tests: as the spec, plus a compose test (the app composes against a registry holding its parts, and every part entry it calls resolves) and the BOTH test above.
needs: location, sys.read, camera, fs.write, peers.
Now runs (Lingua front-end built): Logo (twin Kiteling) for geometry: `logic logo <entry> apps/blocks/ephemeris/<entry>.lg`, in the twin's canonical form, joined to another language's line for the same entry.
Planned (no runtime yet): Simula I, Oberon.
Pending: `pending Simula_I orbit` (simulation); `pending Logo orbit` (geometry); `pending Oberon observations` (structure).
