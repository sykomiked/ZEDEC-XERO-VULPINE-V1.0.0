# Wave 15 task: Courser (slot 188), Workouts
Read ../APP_WAVE_SPEC.md first. This is an L2 app: manifest says `layer L2` and composes its built_from apps' blocks with `use`; it may only start after those L1 waves are merged.
Depends on: wave 6 (050 warble), wave 8 (089 wellspring), wave 9 (110 sundial).
Manifest: `slot 188`, `layer L2`, `icon icons/l2/188-courser.svg`, `budget 233`, one `use <id> 1` per part below. A part can be used only if its manifest carries `version 1.0.0` and its `provides` lines; the composed logic lines must fit ZXAPP_LOGIC_MAX (128) and the uses ZXAPP_USES (8). If any of these fails, stop and report it; do not raise a limit or edit the part.
What it does: plans and times workouts with music and logs results to the health record in the Hoard (`files.read`, `files.write`), time from `sys.info`.
Uses: `use warble 1` (playlist length, shuffle); `use wellspring 1` ("outside a safe range", weekly totals, log entries); `use sundial 1` (timer limits). Call these composed entries; do not re-implement them.
Logic (critical, new decisions only): pace (seconds per km from time and distance, floor) in FORTRAN as Trebago + APL as KRON witnessed; "over the heart-rate limit" (above the declared maximum) in Ada as Eve + APL as KRON witnessed, shown as a suggestion to ease off; interval grid in APL as KRON; athlete tree in MUMPS as MYRA.
BOTH test: the watch says 152 bpm and the chest strap 118: the reading is BOTH and the interval is not scored.
Health: advisory; not a medical device; limits are the person's or their clinician's.
Tests: as the spec, plus a compose test (the app composes against a registry holding its parts, and every part entry it calls resolves) and the BOTH test above.
needs: fs.read, fs.write, sys.read, notify.
Planned (no runtime yet): Simula I, LabVIEW, Delphi.
