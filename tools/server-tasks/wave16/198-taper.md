# Wave 16 task: Taper (slot 198), Rest
Read ../APP_WAVE_SPEC.md first. This is an L2 app: manifest says `layer L2` and composes its built_from apps' blocks with `use`; it may only start after those L1 waves are merged.
Depends on: wave 4 (133 nightjar), wave 8 (089 wellspring), wave 9 (110 sundial), wave 11 (144 stillwater).
Manifest: `slot 198`, `layer L2`, `icon icons/l2/198-taper.svg`, `budget 233`, one `use <id> 1` per part below. A part can be used only if its manifest carries `version 1.0.0` and its `provides` lines; the composed logic lines must fit ZXAPP_LOGIC_MAX (128) and the uses ZXAPP_USES (8). If any of these fails, stop and report it; do not raise a limit or edit the part.
What it does: guards rest: quiet hours (`screen.status`, `screen.check`, `notes.list`), sleep tracking and gentle night reading from the Hoard (`files.read`), time from `sys.info`.
Uses: `use wellspring 1` (sleep minutes "outside a safe range", log entries); `use sundial 1` (minutes until the next alarm); `use nightjar 1` ("interrupt now", speech limits); `use stillwater 1` ("in a quiet window now", "let this notice through"). Call these composed entries; do not re-implement them.
Logic (critical, new decisions only): "may wake now" (nothing before the declared wake time unless it is an emergency) in Ada as Eve + APL as KRON witnessed; sleep midpoint mean and spread over 14 nights in FORTRAN as Trebago + APL as KRON witnessed; night grid in APL as KRON; household tree in MUMPS as MYRA.
BOTH test: caller 3's urgent rule and the do-not-disturb rule both apply: the notice is BOTH and only a true emergency (Watchfire's) gets through.
Health: advisory; not a medical device.
Tests: as the spec, plus a compose test (the app composes against a registry holding its parts, and every part entry it calls resolves) and the BOTH test above.
needs: notify, screen.time, fs.read, sys.read.
