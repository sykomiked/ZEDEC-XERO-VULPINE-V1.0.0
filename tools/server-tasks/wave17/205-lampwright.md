# Wave 17 task: Lampwright (slot 205), Lighting scenes
Read ../APP_WAVE_SPEC.md first. This is an L2 app: manifest says `layer L2` and composes its built_from apps' blocks with `use`; it may only start after those L1 waves are merged.
Depends on: wave 5 (060 relief), wave 7 (094 homestead), wave 9 (116 dyer), wave 11 (138 tint).
Manifest: `slot 205`, `layer L2`, `icon icons/l2/205-lampwright.svg`, `budget 233`, one `use <id> 1` per part below. A part can be used only if its manifest carries `version 1.0.0` and its `provides` lines; the composed logic lines must fit ZXAPP_LOGIC_MAX (128) and the uses ZXAPP_USES (8). If any of these fails, stop and report it; do not raise a limit or edit the part.
What it does: sets ambient lighting and display scenes across rooms and screens with colour care, reading `ui.get`, `sys.info` and `power.status`. zxvd has no method for lighting device control, display colour set: use only existing methods; list what's missing in your report.
Uses: `use relief 1` (vertex transform to place lights in the room model); `use homestead 1` ("command allowed", energy totals); `use dyer 1` (RGB to HSL, contrast); `use tint 1` (flash guard, intensity limits, filter matrix). Call these composed entries; do not re-implement them.
Logic (critical, new decisions only): colour temperature to RGB (table by kelvin, interpolated) in APL as KRON + FORTRAN as Trebago witnessed; "within brightness limits" (at most the room's cap, the night cap after the declared hour, flash guard) in Ada as Eve + APL as KRON witnessed; energy use (Wh x tariff in minor units) in COBOL as Sutra + FORTRAN as Trebago witnessed; light grid in APL as KRON; room tree in MUMPS as MYRA.
BOTH test: person A asks room 2 for 2700 K at 40 percent and person B for 4000 K at 70: the request is BOTH and the scene is blended (mean floor), not overridden.
Tests: as the spec, plus a compose test (the app composes against a registry holding its parts, and every part entry it calls resolves) and the BOTH test above.
needs: sys.read, net, peers, notify.
Planned (no runtime yet): Dart, Ladder logic, Smalltalk.
