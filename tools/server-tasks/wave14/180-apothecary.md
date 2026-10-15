# Wave 14 task: Apothecary (slot 180), Clinical records
Read ../APP_WAVE_SPEC.md first. This is an L2 app: manifest says `layer L2` and composes its built_from apps' blocks with `use`; it may only start after those L1 waves are merged.
Depends on: wave 1 (009 lockbox), wave 4 (047 sealwax), wave 7 (089 wellspring), wave 11 (143 watchfire).
Manifest: `slot 180`, `layer L2`, `icon icons/l2/180-apothecary.svg`, `budget 233`, one `use <id> 1` per part below. A part can be used only if its manifest carries `version 1.0.0` and its `provides` lines; the composed logic lines must fit ZXAPP_LOGIC_MAX (128) and the uses ZXAPP_USES (8). If any of these fails, stop and report it; do not raise a limit or edit the part.
What it does: keeps clinical records and prescriptions in the Hoard (`files.read`, `files.write`) with signed approvals and medical ID. A clinician signs an approval with a passkey (the passkeys methods, under sys.read, added for this). Anything else missing: use only existing methods; list it in your report.
Uses: `use lockbox 1` (key limits, expiry); `use sealwax 1` ("ready to sign", field record); `use wellspring 1` ("outside a safe range", log entries); `use watchfire 1` (medical ID records, never computed). Call these composed entries; do not re-implement them.
Logic (critical, new decisions only): "may sign this prescription" (a clinician seat with prescribing rights AND dose at most the declared maximum AND no recorded allergy to it) in Ada as Eve + APL as KRON witnessed; dose for display (dose per kg x weight, a reduced rational) in FORTRAN as Trebago + APL as KRON witnessed; billing codes (fee per code in minor units, totals) in COBOL as Sutra + FORTRAN as Trebago witnessed; lab result grid in APL as KRON; patient record tree in MUMPS as MYRA.
BOTH test: clinician A records diagnosis code 410 and clinician B 786 for one visit: both stay on the chart as BOTH; neither is dropped.
Clinical: advisory and human-approved; the app never prescribes; signing is the clinician's act; not a medical device.
Tests: as the spec, plus a compose test (the app composes against a registry holding its parts, and every part entry it calls resolves) and the BOTH test above.
needs: fs.read, fs.write, notify, sys.read.
Planned (no runtime yet): Haskell, ML, Tcl.
