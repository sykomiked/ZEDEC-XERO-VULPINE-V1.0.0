# Wave 14 task: Caliper (slot 177), Measurement and tolerance
Read ../APP_WAVE_SPEC.md first. This is an L2 app: manifest says `layer L2` and composes its built_from apps' blocks with `use`; it may only start after those L1 waves are merged.
Depends on: wave 2 (109 tally), wave 3 (060 relief), wave 6 (040 furrow), wave 9 (107 gnomon).
Manifest: `slot 177`, `layer L2`, `icon icons/l2/177-caliper.svg`, `budget 233`, one `use <id> 1` per part below. A part can be used only if its manifest carries `version 1.0.0` and its `provides` lines; the composed logic lines must fit ZXAPP_LOGIC_MAX (128) and the uses ZXAPP_USES (8). If any of these fails, stop and report it; do not raise a limit or edit the part.
What it does: measures parts and structures and checks them against design tolerances with exact integer maths, records in the Hoard (`files.read`, `files.write`). zxvd has no method for instrument readings input: use only existing methods; list what's missing in your report.
Uses: `use furrow 1` (sums); `use relief 1` (vertex transform, mesh volume); `use gnomon 1` (twice the area, squared side lengths); `use tally 1` (rationals, unit factors). Call these composed entries; do not re-implement them.
Logic (critical, new decisions only): "within tolerance" (|measured - nominal| at most the tolerance, micrometres) in Ada as Eve + APL as KRON witnessed; stress (force / area, a reduced rational in kPa) in FORTRAN as Trebago + APL as KRON witnessed; inspection records (record number, part, pass and fail counts, refused on overflow) in COBOL as Sutra; measurement arrays in APL as KRON; assembly tree in MUMPS as MYRA.
BOTH test: instrument 1 reads 25.012 mm and instrument 2 25.040 mm against 25.000 +/- 0.020: the part is BOTH and quarantined, not passed.
Advisory; acceptance of a safety-critical part is a qualified inspector's sign-off.
Tests: as the spec, plus a compose test (the app composes against a registry holding its parts, and every part entry it calls resolves) and the BOTH test above.
needs: fs.read, fs.write.
Planned (no runtime yet): Julia, Oberon.
