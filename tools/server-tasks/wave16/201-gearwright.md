# Wave 16 task: Gearwright (slot 201), Robot supervisor
Read ../APP_WAVE_SPEC.md first. This is an L2 app: manifest says `layer L2` and composes its built_from apps' blocks with `use`; it may only start after those L1 waves are merged.
Depends on: wave 3 (128 mockingbird), wave 5 (024 treadle), wave 8 (094 homestead), wave 11 (136 pivot).
Manifest: `slot 201`, `layer L2`, `icon icons/l2/201-gearwright.svg`, `budget 233`, one `use <id> 1` per part below. A part can be used only if its manifest carries `version 1.0.0` and its `provides` lines; the composed logic lines must fit ZXAPP_LOGIC_MAX (128) and the uses ZXAPP_USES (8). If any of these fails, stop and report it; do not raise a limit or edit the part.
What it does: programs and supervises robots and actuators with scheduled routines and safety stops, machine facts from `sys.info`. zxvd has no method for actuator control and position readings: use only existing methods; list what's missing in your report.
Uses: `use treadle 1` (next run time, "within budget", task record); `use homestead 1` ("command allowed"); `use mockingbird 1` (boot self-check, device record, so a routine runs on the emulator first); `use pivot 1` (dwell select). Call these composed entries; do not re-implement them.
Logic (critical, new decisions only): end position from joint angles (two-link planar, integer sine table) in FORTRAN as Trebago + APL as KRON witnessed; "motion allowed" (each joint inside its range AND speed at most the maximum AND the end inside the safe zone) in Ada as Eve + APL as KRON witnessed; production records (parts per routine, refused on overflow) in COBOL as Sutra; joint arrays in APL as KRON; machine tree in MUMPS as MYRA.
BOTH test: encoder 1 says joint 2 is at 45.00 degrees and encoder 2 at 47.50, tolerance 0.50: the joint is BOTH and the arm stops safely.
Life-safety: never moves an actuator without a person's approval; the safety stop always wins.
Tests: as the spec, plus a compose test (the app composes against a registry holding its parts, and every part entry it calls resolves) and the BOTH test above.
needs: sys.read, notify, peers.
Planned (no runtime yet): JOVIAL, JCL.
