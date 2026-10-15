# Wave 15 task: Shipwright (slot 190), Flood watch
Read ../APP_WAVE_SPEC.md first. This is an L2 app: manifest says `layer L2` and composes its built_from apps' blocks with `use`; it may only start after those L1 waves are merged.
Depends on: wave 0 (014 wake), wave 7 (088 weatherglass, 093 understory), wave 11 (143 watchfire).
Manifest: `slot 190`, `layer L2`, `icon icons/l2/190-shipwright.svg`, `budget 233`, one `use <id> 1` per part below. A part can be used only if its manifest carries `version 1.0.0` and its `provides` lines; the composed logic lines must fit ZXAPP_LOGIC_MAX (128) and the uses ZXAPP_USES (8). If any of these fails, stop and report it; do not raise a limit or edit the part.
What it does: forecasts tides, floods and surges for districts (`place.here`, `place.list`) and sends alerts to people in harm's way (`pspace.post`) once a person approves, with history from `journal.query`. zxvd has no method for gauge and tide readings: use only existing methods; list what's missing in your report.
Uses: `use wake 1` (`wake.count`); `use weatherglass 1` (warnings, readings); `use understory 1` (rolling-window totals); `use watchfire 1` (contact records, the cancel window). Call these composed entries; do not re-implement them.
Logic (critical, new decisions only): water level ahead (gauge trend plus rainfall in mm x the catchment factor) in FORTRAN as Trebago + APL as KRON witnessed; "warn this district" (level at or above the threshold, or a gauge reading BOTH) in Ada as Eve + APL as KRON witnessed; alert thresholds in Ada as Eve; gauge grid in APL as KRON; district tree in MUMPS as MYRA.
BOTH test: gauge 1 reads 2.10 m and gauge 2 3.40 m against a 3.00 m threshold: the level is BOTH and the district is warned.
Life-safety: advisory and human-approved; never replaces official warnings; an operator approves every alert.
Tests: as the spec, plus a compose test (the app composes against a registry holding its parts, and every part entry it calls resolves) and the BOTH test above.
needs: location, journal, peers, net, notify.
Planned (no runtime yet): Erlang.
