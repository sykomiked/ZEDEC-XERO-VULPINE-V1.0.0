# Wave 15 task: Coppice (slot 192), Woodland
Read ../APP_WAVE_SPEC.md first. This is an L2 app: manifest says `layer L2` and composes its built_from apps' blocks with `use`; it may only start after those L1 waves are merged.
Depends on: wave 0 (085 waymark), wave 5 (071 gleaner), wave 6 (080 ninefold), wave 7 (093 understory).
Manifest: `slot 192`, `layer L2`, `icon icons/l2/192-coppice.svg`, `budget 233`, one `use <id> 1` per part below. A part can be used only if its manifest carries `version 1.0.0` and its `provides` lines; the composed logic lines must fit ZXAPP_LOGIC_MAX (128) and the uses ZXAPP_USES (8). If any of these fails, stop and report it; do not raise a limit or edit the part.
What it does: manages woodland: tree inventory, growth, felling plans and carbon held, with places (`place.list`, `place.add`) and records in the Hoard (`files.read`, `files.write`).
Uses: `use gleaner 1` (label counts, row filters); `use ninefold 1` (holding per form, for natural capital); `use waymark 1` (`waymark.place_ok`); `use understory 1` (land entries). Call these composed entries; do not re-implement them.
Logic (critical, new decisions only): carbon held and timber volume (volume x density x factor, kilograms and litres) in COBOL as Sutra + FORTRAN as Trebago witnessed; diameter growth per year in FORTRAN as Trebago + APL as KRON witnessed; "felling allowed" (at most the stand's annual growth AND not a protected stand) in Ada as Eve + APL as KRON witnessed; stand grid in APL as KRON; tree inventory in MUMPS as MYRA.
BOTH test: satellite count 1200 trees in stand 4 and ground count 950: the count is BOTH and no carbon figure is claimed until reconciled.
Carbon figures are advisory, not a certified carbon credit.
Tests: as the spec, plus a compose test (the app composes against a registry holding its parts, and every part entry it calls resolves) and the BOTH test above.
needs: location, fs.read, fs.write, ledger.read.
Planned (no runtime yet): Scheme, Simula 67, Modula-2.
