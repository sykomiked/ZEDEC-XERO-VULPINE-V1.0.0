# Wave 13 task: Chandler (slot 161), Vessel log
Read ../APP_WAVE_SPEC.md first. This is an L2 app: manifest says `layer L2` and composes its built_from apps' blocks with `use`; it may only start after those L1 waves are merged.
Depends on: wave 0 (085 waymark), wave 2 (090 caravan), wave 5 (035 aerie), wave 8 (088 weatherglass).
Manifest: `slot 161`, `layer L2`, `icon icons/l2/161-chandler.svg`, `budget 233`, one `use <id> 1` per part below. A part can be used only if its manifest carries `version 1.0.0` and its `provides` lines; the composed logic lines must fit ZXAPP_LOGIC_MAX (128) and the uses ZXAPP_USES (8). If any of these fails, stop and report it; do not raise a limit or edit the part.
What it does: keeps vessel logs, tides, crew and provisions for boats and small fleets in the Hoard (`files.read`, `files.write`), with places (`place.here`, `place.list`) and radio messages (`harmonic.encode`). zxvd has no method for tide predictions source: use only existing methods; list what's missing in your report.
Uses: `use aerie 1` ("fits the band", message log); `use waymark 1` (`waymark.place_ok`); `use weatherglass 1` (wind warnings, readings); `use caravan 1` ("within carrier limits" as the load check pattern, shipment records for stores). Call these composed entries; do not re-implement them.
Logic (critical, new decisions only): provisions (days of supply = stock / (crew x daily ration), floor) and provision spend in minor units in COBOL as Sutra + FORTRAN as Trebago witnessed; tide height at a time by the rule of twelfths from high and low water (millimetres) in FORTRAN as Trebago + APL as KRON witnessed; "within load limits" (cargo, stores and crew at most the vessel's declared maximum) in Ada as Eve + APL as KRON witnessed; tide tables in APL as KRON; crew and vessel tree in MUMPS as MYRA.
BOTH test: crew A logs departure 0610 and crew B logs 0640 for the same leg: the entry is BOTH and is held for the master to settle.
Advisory; not a certified tide source or navigation system; the master decides.
Tests: as the spec, plus a compose test (the app composes against a registry holding its parts, and every part entry it calls resolves) and the BOTH test above.
needs: location, fs.read, fs.write, net, notify.
Planned (no runtime yet): Occam, Java.
