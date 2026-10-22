# Wave 28 task: Ravelin (slot 333), Critical site protection
Read ../APP_WAVE_SPEC.md first. This is an L3 app: manifest says `layer L3` and composes its built_from apps with `use`; it may only start after those waves are merged.
Depends on: wave 2 (030 hedgerow), wave 12 (153 pavise), wave 14 (174 damper), wave 19 (236 levin, 237 weir).
Manifest: `slot 333`, `layer L3`, `icon icons/l3/333-ravelin.svg`, `budget 233`, one `use <id> 1` per part below. A part exports only what it `provides`: call an L2 part's own entries, and an entry it composed only if it re-exports it (as Murmuration re-exports swarm_shares). If an entry you need is not exported, or the composed logic lines pass ZXAPP_LOGIC_MAX (128) or the uses ZXAPP_USES (8), stop and report it; do not edit the part or raise a limit.
What it does: protects critical sites such as power, water and data halls: physical access, cameras, alarms and continuity plans (`journal.query`, `log.tail`, `sys.info`, `features.get`, `files.read`, `files.write`). zxvd has no method for access control, camera and plant sensor feeds: use only existing methods; list what's missing in your report.
Uses: `use pavise 1` (combined "allow"); `use damper 1` ("raise the alarm"); `use levin 1` (circuit loads); `use weir 1` (water maps); `use hedgerow 1` ("is this path reachable"). Call these composed entries; do not re-implement them.
Logic (critical, new decisions only): `interlock_ok` (a door, breaker or valve action allowed only with the site's interlocks satisfied and two people approving) in Ada as Eve + APL as KRON witnessed; `failure_model` (hours of supply left on backup from load and stored energy) in FORTRAN as Trebago + APL as KRON witnessed; `incident_costs` (in minor units) in COBOL as Sutra; `zones` sensor grid in APL as KRON; `site_assets` tree in MUMPS as MYRA.
BOTH test: door 12 reports open and closed at once: it is BOTH and a guard is sent; neither sensor is trusted. Join with `lpres_conjoin` / `lpres_disjoin` in C; never re-derive the tables.
Human-approved: the app never operates plant or doors; site security only, not for any defense use beyond logistics and mapping.
Tests: as the spec, plus a compose test (the app composes against a registry holding its parts, and every part entry it calls resolves) and the BOTH test above.
needs: journal, sys.read, notify, fs.read, fs.write, net.
Planned (no runtime yet): Simula I, LabVIEW, Delphi.
Pending: `pending Simula_I failure_model` (simulation); `pending LabVIEW failure_model` (signal); `pending Delphi site_assets` (structure).
