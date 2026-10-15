# Wave 17 task: Tradewind (slot 211), Route planner
Read ../APP_WAVE_SPEC.md first. This is an L2 app: manifest says `layer L2` and composes its built_from apps' blocks with `use`; it may only start after those L1 waves are merged.
Depends on: wave 0 (085 waymark), wave 1 (090 caravan), wave 5 (034 signpost), wave 8 (088 weatherglass).
Manifest: `slot 211`, `layer L2`, `icon icons/l2/211-tradewind.svg`, `budget 233`, one `use <id> 1` per part below. A part can be used only if its manifest carries `version 1.0.0` and its `provides` lines; the composed logic lines must fit ZXAPP_LOGIC_MAX (128) and the uses ZXAPP_USES (8). If any of these fails, stop and report it; do not raise a limit or edit the part.
What it does: plans routes for trips and shipments from saved places (`place.here`, `place.list`, `place.add`), weather and address lookups, kept in the Hoard (`files.read`, `files.write`). zxvd has no method for road network and address geocoding: use only existing methods; list what's missing in your report.
Uses: `use waymark 1` (`waymark.place_ok`); `use caravan 1` (charged weight, "within carrier limits", shipment records); `use weatherglass 1` (warnings); `use signpost 1` (name syntax, lookup cache record). Call these composed entries; do not re-implement them.
Logic (critical, new decisions only): best order of stops (shortest total over the distance matrix, at most 8 stops) in FORTRAN as Trebago + APL as KRON witnessed; "within load and hours limits" (load at most the vehicle's, driving minutes at most the declared) in Ada as Eve + APL as KRON witnessed; stop filters as K queries in APL as KRON; waybills (waybill number, charged weight, refused on overflow) in COBOL as Sutra + FORTRAN as Trebago witnessed; stop hierarchy in MUMPS as MYRA.
BOTH test: the map has road 3-4 open and the carrier has it closed: both routes are held as BOTH and flagged; neither is trusted blindly.
Advisory; drivers follow the road rules.
Tests: as the spec, plus a compose test (the app composes against a registry holding its parts, and every part entry it calls resolves) and the BOTH test above.
needs: location, fs.read, fs.write, notify.
Planned (no runtime yet): Prolog, Simula 67.
