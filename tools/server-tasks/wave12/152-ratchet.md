# Wave 12 task: Ratchet (slot 152), Maintenance and repair
Read ../APP_WAVE_SPEC.md first. This is an L2 app: manifest says `layer L2` and composes its built_from apps' blocks with `use`; it may only start after those L1 waves are merged.
Depends on: wave 0 (015 tidewatch), wave 3 (128 mockingbird), wave 6 (024 treadle), wave 9 (094 homestead).
Manifest: `slot 152`, `layer L2`, `icon icons/l2/152-ratchet.svg`, `budget 233`, one `use <id> 1` per part below. A part can be used only if its manifest carries `version 1.0.0` and its `provides` lines; the composed logic lines must fit ZXAPP_LOGIC_MAX (128) and the uses ZXAPP_USES (8). If any of these fails, stop and report it; do not raise a limit or edit the part.
What it does: diagnoses hardware faults from the machine's readings (`sys.info`, `sys.disk`, `power.status`), schedules maintenance and walks a person through a repair step by step. zxvd has no method for readings from machines other than this one: use only existing methods; list what's missing in your report.
Uses: `use tidewatch 1` (`tidewatch.cpu_x100`, `tidewatch.peak`); `use treadle 1` (next run time for a service, within budget, task record); `use homestead 1` ("command allowed", energy totals per device); `use mockingbird 1` (boot self-check, device record). Call these composed entries; do not re-implement them.
Logic (critical, new decisions only): "fault" (a reading outside its safe operating limits) in Ada as Eve + APL as KRON witnessed; next repair step from the check results in BASIC (twin Hatchling) + APL as KRON witnessed (the person sees one step at a time); wear (hours run x load share, summed) and hours to the next service exact in FORTRAN as Trebago; parts and labour costs in minor units in COBOL as Sutra + FORTRAN as Trebago witnessed; sensor grid (sensor by hour) in APL as KRON; machine parts tree in MUMPS as MYRA.
BOTH test: part 4 is within limits on vibration and over limits on temperature: the part is BOTH and an inspection task is raised; it is never ignored.
Twin form: the BASIC block is committed in its twin form (Hatchling) and its manifest logic line names `hatchling`.
Advisory; never actuates a machine; every step that touches a machine begins with "isolate the power" and needs the person's confirmation.
Tests: as the spec, plus a compose test (the app composes against a registry holding its parts, and every part entry it calls resolves) and the BOTH test above.
needs: sys.read, notify.
Planned (no runtime yet): Erlang, Bourne shell.
