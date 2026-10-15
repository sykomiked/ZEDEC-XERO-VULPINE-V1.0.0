# Wave 15 task: Mummer (slot 182), Show production
Read ../APP_WAVE_SPEC.md first. This is an L2 app: manifest says `layer L2` and composes its built_from apps' blocks with `use`; it may only start after those L1 waves are merged.
Depends on: wave 3 (051 kinetoscope), wave 5 (050 warble), wave 7 (039 folio, 041 footlight), wave 9 (095 bonfire).
Manifest: `slot 182`, `layer L2`, `icon icons/l2/182-mummer.svg`, `budget 233`, one `use <id> 1` per part below. A part can be used only if its manifest carries `version 1.0.0` and its `provides` lines; the composed logic lines must fit ZXAPP_LOGIC_MAX (128) and the uses ZXAPP_USES (8). If any of these fails, stop and report it; do not raise a limit or edit the part.
What it does: produces shows: scripts, cues, music and slides in the Hoard (`files.read`, `files.write`) and the audience event as a watch hall (`pspace.create`, `hall.load`, `hall.play`, `hall.pause`, `hall.status`).
Uses: `use folio 1` (page fit for the script); `use footlight 1` (slide scale, rehearsal timing, run order); `use warble 1` (playlist length, volume limits); `use bonfire 1` ("may this guest join", seats left); `use kinetoscope 1` (fit to the window, position to frame). Call these composed entries; do not re-implement them.
Logic (critical, new decisions only): cue start times (running sum of cue durations, ms) in FORTRAN as Trebago + APL as KRON witnessed; ticket ledger (tickets per performance, seats left, amounts in minor units, refused on overflow) in COBOL as Sutra + FORTRAN as Trebago witnessed; "within stage safety limits" (audience at most the venue's capacity, level at most the cap, at most 3 flashes a second) in Ada as Eve + APL as KRON witnessed; cue grid in APL as KRON; production tree in MUMPS as MYRA.
BOTH test: script revisions 5 and 6 differ in scene 3: both are kept as BOTH so the directors can rehearse either before deciding.
Tests: as the spec, plus a compose test (the app composes against a registry holding its parts, and every part entry it calls resolves) and the BOTH test above.
needs: fs.read, fs.write, peers, net, notify.
Planned (no runtime yet): Groovy, OCaml, LabVIEW.
