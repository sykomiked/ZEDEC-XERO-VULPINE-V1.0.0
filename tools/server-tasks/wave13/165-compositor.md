# Wave 13 task: Compositor (slot 165), Script workbench
Read ../APP_WAVE_SPEC.md first. This is an L2 app: manifest says `layer L2` and composes its built_from apps' blocks with `use`; it may only start after those L1 waves are merged.
Depends on: wave 3 (127 colonnade), wave 4 (121 joinery), wave 5 (013 flint), wave 10 (122 pawprint).
Manifest: `slot 165`, `layer L2`, `icon icons/l2/165-compositor.svg`, `budget 233`, one `use <id> 1` per part below. A part can be used only if its manifest carries `version 1.0.0` and its `provides` lines; the composed logic lines must fit ZXAPP_LOGIC_MAX (128) and the uses ZXAPP_USES (8). If any of these fails, stop and report it; do not raise a limit or edit the part.
What it does: writes and runs scripts in any pillar language from one workbench (`play.catalogue`, `play.run`, `play.draft`), saved in the Hoard (`files.read`, `files.write`), with debugging built in.
Uses: `use flint 1` (scrollback limits, cell fit, history record); `use joinery 1` (line and column to offset, buffer limits, undo record); `use pawprint 1` ("may attach", breakpoint hit, steps counted); `use colonnade 1` (verdict across witnessed runs, fuel left, run limits, language to pillar). Call these composed entries; do not re-implement them.
Logic (critical, new decisions only): "outputs agree" for one script run in two languages (output vectors equal, element by element) in Ada as Eve + APL as KRON witnessed, joined with Colonnade's verdict; job records (job number, fuel spent, refused on overflow) in COBOL as Sutra + APL as KRON witnessed; numeric routines (run time totals) exact in FORTRAN as Trebago; script contracts (declared input ranges) in Ada as Eve; the array REPL is a KRON session; global trees are a MYRA browser.
BOTH test: a fixture script summing 1..10 gives 55 in sutra and 56 in kron: the result is BOTH and is reported as such; neither is trusted.
AI drafts from `play.draft` are untrusted text, held until the person runs them.
Tests: as the spec, plus a compose test (the app composes against a registry holding its parts, and every part entry it calls resolves) and the BOTH test above.
needs: fs.read, fs.write, sys.read, ai.ask.
Planned (no runtime yet): Ruby, Raku.
