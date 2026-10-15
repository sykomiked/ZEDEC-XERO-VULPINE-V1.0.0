# Wave 14 task: Escapement (slot 172), Shared clock
Read ../APP_WAVE_SPEC.md first. This is an L2 app: manifest says `layer L2` and composes its built_from apps' blocks with `use`; it may only start after those L1 waves are merged.
Depends on: wave 1 (024 treadle), wave 3 (043 solstice), wave 6 (078 tallystick), wave 9 (110 sundial).
Manifest: `slot 172`, `layer L2`, `icon icons/l2/172-escapement.svg`, `budget 233`, one `use <id> 1` per part below. A part can be used only if its manifest carries `version 1.0.0` and its `provides` lines; the composed logic lines must fit ZXAPP_LOGIC_MAX (128) and the uses ZXAPP_USES (8). If any of these fails, stop and report it; do not raise a limit or edit the part.
What it does: keeps exact time across devices (`sys.info`, `devmesh.devices`, `net.peers`) and runs shifts, timesheets and timed jobs from it. zxvd has no method for a peer clock-offset query: use only existing methods; list what's missing in your report.
Uses: `use treadle 1` (next run time, "within budget", task record); `use solstice 1` (day of week, "two events overlap"); `use tallystick 1` (the sequence numbering pattern for timesheets); `use sundial 1` (local time for a zone). Call these composed entries; do not re-implement them.
Logic (critical, new decisions only): clock offset (median of the peers' offsets in ms) in FORTRAN as Trebago + APL as KRON witnessed; "job may run" (inside its window, deadline not passed) in Ada as Eve + APL as KRON witnessed; timesheets (minutes per shift per person, weekly sums refused on overflow) in COBOL as Sutra + FORTRAN as Trebago witnessed; schedule matrix (person by shift, nobody booked twice at once) in APL as KRON; shift tree in MUMPS as MYRA.
BOTH test: device A reads 12:00:00.000 and device B 12:00:02.500, tolerance 500 ms: the time is BOTH, the witnesses (three devices or more) vote the median, and no clock is silently overwritten.
Tests: as the spec, plus a compose test (the app composes against a registry holding its parts, and every part entry it calls resolves) and the BOTH test above.
needs: sys.read, peers, notify, fs.read, fs.write.
Planned (no runtime yet): Go, Bourne shell, Crystal.
