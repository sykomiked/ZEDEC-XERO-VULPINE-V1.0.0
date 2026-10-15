# Wave 16 task: Furnace (slot 199), Microgrid
Read ../APP_WAVE_SPEC.md first. This is an L2 app: manifest says `layer L2` and composes its built_from apps' blocks with `use`; it may only start after those L1 waves are merged.
Depends on: wave 5 (021 brazier), wave 8 (088 weatherglass, 093 understory), wave 9 (094 homestead).
Manifest: `slot 199`, `layer L2`, `icon icons/l2/199-furnace.svg`, `budget 233`, one `use <id> 1` per part below. A part can be used only if its manifest carries `version 1.0.0` and its `provides` lines; the composed logic lines must fit ZXAPP_LOGIC_MAX (128) and the uses ZXAPP_USES (8). If any of these fails, stop and report it; do not raise a limit or edit the part.
What it does: runs a home or village microgrid: solar output, storage and when to use power, from `power.status`, `power.top`, `power.budget` and `sys.info`. zxvd has no method for solar inverter, meter and battery bank readings: use only existing methods; list what's missing in your report.
Uses: `use brazier 1` (battery x100, minutes left); `use weatherglass 1` (readings); `use understory 1` (energy cost, "over budget"); `use homestead 1` ("command allowed", energy totals per device). Call these composed entries; do not re-implement them.
Logic (critical, new decisions only): energy accounts (Wh x tariff in minor units) in COBOL as Sutra + FORTRAN as Trebago witnessed; expected solar Wh (panel watts x sun minutes / 60) in FORTRAN as Trebago + APL as KRON witnessed; "within grid limits" (draw at most the inverter's maximum AND battery at least the reserve AND export at most the allowance) in Ada as Eve + APL as KRON witnessed; load grid in APL as KRON; site tree in MUMPS as MYRA.
BOTH test: the meter reads 4200 Wh and the inverter 3900 Wh, tolerance 50: the reading is BOTH and billing waits for reconciliation.
Grid: advisory and human-approved; never switches loads or the grid by itself.
Tests: as the spec, plus a compose test (the app composes against a registry holding its parts, and every part entry it calls resolves) and the BOTH test above.
needs: sys.read, notify, net, peers.
Planned (no runtime yet): IEC 61131-3 Structured Text.
