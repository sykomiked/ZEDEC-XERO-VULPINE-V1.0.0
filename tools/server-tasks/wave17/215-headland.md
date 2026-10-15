# Wave 17 task: Headland (slot 215), Fleet watch
Read ../APP_WAVE_SPEC.md first. This is an L2 app: manifest says `layer L2` and composes its built_from apps' blocks with `use`; it may only start after those L1 waves are merged.
Depends on: wave 0 (014 wake, 015 tidewatch, 016 sounding), wave 11 (143 watchfire).
Manifest: `slot 215`, `layer L2`, `icon icons/l2/215-headland.svg`, `budget 233`, one `use <id> 1` per part below. A part can be used only if its manifest carries `version 1.0.0` and its `provides` lines; the composed logic lines must fit ZXAPP_LOGIC_MAX (128) and the uses ZXAPP_USES (8). If any of these fails, stop and report it; do not raise a limit or edit the part.
What it does: watches a machine or fleet for trouble (`sys.info`, `sys.disk`, `sys.net`, `log.tail`, `journal.query`) and guides the owner with prioritised alerts (`notes.list`). zxvd has no method for readings from other machines in the fleet: use only existing methods; list what's missing in your report.
Uses: `use wake 1` (`wake.count`); `use tidewatch 1` (`tidewatch.cpu_x100`, `tidewatch.peak`); `use sounding 1` (`sounding.rate`); `use watchfire 1` (contact records, the cancel window before anyone is rung). Call these composed entries; do not re-implement them.
Logic (critical, new decisions only): trend (integer least-squares slope over the last n readings) in FORTRAN as Trebago + APL as KRON witnessed; "raise an alarm" (over the threshold, or a pair of readings BOTH) in Ada as Eve + APL as KRON witnessed; alert priority order in APL as KRON; metric grid in APL as KRON; fleet hierarchy in MUMPS as MYRA.
BOTH test: sensor A reads 95.0 C and sensor B 45.0 C for one CPU: a BOTH alert is raised, naming a possibly faulty instrument.
Tests: as the spec, plus a compose test (the app composes against a registry holding its parts, and every part entry it calls resolves) and the BOTH test above.
needs: sys.read, journal, net, notify, peers.
Planned (no runtime yet): Go.
