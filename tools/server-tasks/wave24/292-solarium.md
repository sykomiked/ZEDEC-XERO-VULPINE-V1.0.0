# Wave 24 task: Solarium (slot 292), Garden and greenhouse
Read ../APP_WAVE_SPEC.md first. This is an L3 app: manifest says `layer L3` and composes its built_from apps with `use`; it may only start after those waves are merged.
Depends on: wave 8 (088 weatherglass), wave 9 (094 homestead), wave 12 (149 dibber), wave 19 (230 fen, 239 isobar).
Manifest: `slot 292`, `layer L3`, `icon icons/l3/292-solarium.svg`, `budget 233`, one `use <id> 1` per part below. A part exports only what it `provides`: call an L2 part's own entries, and an entry it composed only if it re-exports it (as Murmuration re-exports swarm_shares). If an entry you need is not exported, or the composed logic lines pass ZXAPP_LOGIC_MAX (128) or the uses ZXAPP_USES (8), stop and report it; do not edit the part or raise a limit.
What it does: looks after a garden or greenhouse: planting plans, watering, soil and light sensors, and harvest logs (`files.read`, `files.write`, `place.here`). zxvd has no method for soil and light sensor readings, valve control: use only existing methods; list what's missing in your report.
Uses: `use dibber 1` (water per bed, "may water now", growing degree days); `use fen 1` (specimen register); `use isobar 1` (station readings); `use homestead 1` ("command allowed" for a valve); `use weatherglass 1` (frost warnings). Call these composed entries; do not re-implement them.
Logic (critical, new decisions only): `water_ok` (Dibber's "may water now" AND soil below the declared moisture AND no rain in the forecast window) in Ada as Eve + APL as KRON witnessed; `evapotranspiration` (daily water loss from temperature and light, integer) in FORTRAN as Trebago + APL as KRON witnessed; `produce_accounts` (seeds and produce in minor units) in COBOL as Sutra; `beds` sensor grid in APL as KRON; `lineage` (plant lineage) in MUMPS as MYRA.
BOTH test: the soil sensor reads moist and the forecast says dry: the bed is BOTH and watering waits for a second reading. Join with `lpres_conjoin` / `lpres_disjoin` in C; never re-derive the tables.
Advisory; valves move only with the owner's approval (Homestead's rule).
Tests: as the spec, plus a compose test (the app composes against a registry holding its parts, and every part entry it calls resolves) and the BOTH test above.
needs: fs.read, fs.write, location, net, notify.
Planned (no runtime yet): IEC 61131-3 Structured Text, Simula I, Modula-2.
Pending: `pending IEC_61131-3_Structured_Text water_ok` (control); `pending Simula_I evapotranspiration` (simulation); `pending Modula-2 lineage` (structure).
