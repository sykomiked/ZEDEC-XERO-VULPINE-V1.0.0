# Wave 15 task: Loupe (slot 181), Incident investigation
Read ../APP_WAVE_SPEC.md first. This is an L2 app: manifest says `layer L2` and composes its built_from apps' blocks with `use`; it may only start after those L1 waves are merged.
Depends on: wave 0 (005 scent, 014 wake), wave 1 (018 witness), wave 10 (122 pawprint).
Manifest: `slot 181`, `layer L2`, `icon icons/l2/181-loupe.svg`, `budget 233`, one `use <id> 1` per part below. A part can be used only if its manifest carries `version 1.0.0` and its `provides` lines; the composed logic lines must fit ZXAPP_LOGIC_MAX (128) and the uses ZXAPP_USES (8). If any of these fails, stop and report it; do not raise a limit or edit the part.
What it does: investigates incidents by tracing logs (`log.tail`), events (`journal.query`, `journal.verify`) and changes to files (`search.query`, `files.stat`) into a chain of evidence.
Uses: `use scent 1` (`scent.score`); `use wake 1` (`wake.count`); `use witness 1` (entry record, chain intact); `use pawprint 1` ("may attach": own program and owner approved, to look at a suspect process). Call these composed entries; do not re-implement them.
Logic (critical, new decisions only): "evidence admissible" (content id unchanged since it was registered AND every custody hand-off recorded AND hand-off times rising) in Ada as Eve + APL as KRON witnessed; evidence register (item number, custody hand-offs, never reused, refused on overflow) in COBOL as Sutra; timeline gaps (differences between event times in ms, the largest gap) exact in FORTRAN as Trebago; event matrix (source by minute counts) in APL as KRON; case tree in MUMPS as MYRA.
BOTH test: the journal has event 900 at 10:01 and the app log has nothing from 10:00 to 10:05: the window is BOTH, shown as possible tampering, never smoothed over.
Inspection only: the app changes nothing it investigates.
Tests: as the spec, plus a compose test (the app composes against a registry holding its parts, and every part entry it calls resolves) and the BOTH test above.
needs: journal, search, fs.read, sys.read.
Planned (no runtime yet): Elixir.
