# Wave 25 task: Cistern (slot 296), Water utility
Read ../APP_WAVE_SPEC.md first. This is an L3 app: manifest says `layer L3` and composes its built_from apps with `use`; it may only start after those waves are merged.
Depends on: wave 4 (078 tallystick), wave 12 (154 chainman), wave 15 (190 shipwright), wave 16 (197 millrace), wave 19 (237 weir).
Manifest: `slot 296`, `layer L3`, `icon icons/l3/296-cistern.svg`, `budget 233`, one `use <id> 1` per part below. A part exports only what it `provides`: call an L2 part's own entries, and an entry it composed only if it re-exports it (as Murmuration re-exports swarm_shares). If an entry you need is not exported, or the composed logic lines pass ZXAPP_LOGIC_MAX (128) or the uses ZXAPP_USES (8), stop and report it; do not edit the part or raise a limit.
What it does: runs a water utility: pipes, pumps, leaks, meter readings, quality tests and customer bills (`files.read`, `files.write`, `journal.query`, `ledger.entries`, `ledger.post`, `print.submit`). zxvd has no method for meter, pump and quality sensor readings: use only existing methods; list what's missing in your report.
Uses: `use weir 1` (household water maps, leak alerts); `use millrace 1` ("accept more", flow rate); `use shipwright 1` ("warn this district"); `use chainman 1` (parcel grid for pipe routes); `use tallystick 1` (invoice total, "paid in full"). Call these composed entries; do not re-implement them.
Logic (critical, new decisions only): `pressure_dose_ok` (pressure inside the declared band AND disinfectant dose inside its band) in Ada as Eve + APL as KRON witnessed; `hydraulic` (flow and head loss per pipe from the integer tables) in FORTRAN as Trebago + APL as KRON witnessed; `billing` (cubic metres x tariff bands in minor units) in COBOL as Sutra + FORTRAN as Trebago witnessed; `meters` arrays in APL as KRON; `network_assets` tree in MUMPS as MYRA.
BOTH test: meter 210 reads 18 m3 and the flow model expects 11: the figure is BOTH, a possible leak is flagged, and no bill is made from it. Join with `lpres_conjoin` / `lpres_disjoin` in C; never re-derive the tables.
Advisory; dosing and valves are operated by qualified staff; never controls plant.
Tests: as the spec, plus a compose test (the app composes against a registry holding its parts, and every part entry it calls resolves) and the BOTH test above.
needs: fs.read, fs.write, journal, ledger.read, ledger.post, print, notify.
Planned (no runtime yet): JOVIAL.
Pending: `pending JOVIAL pressure_dose_ok` (control).
