# Wave 15 task: Bodkin (slot 183), Sewing patterns
Read ../APP_WAVE_SPEC.md first. This is an L2 app: manifest says `layer L2` and composes its built_from apps' blocks with `use`; it may only start after those L1 waves are merged.
Depends on: wave 1 (079 fairground), wave 2 (109 tally), wave 3 (060 relief), wave 7 (054 burin).
Manifest: `slot 183`, `layer L2`, `icon icons/l2/183-bodkin.svg`, `budget 233`, one `use <id> 1` per part below. A part can be used only if its manifest carries `version 1.0.0` and its `provides` lines; the composed logic lines must fit ZXAPP_LOGIC_MAX (128) and the uses ZXAPP_USES (8). If any of these fails, stop and report it; do not raise a limit or edit the part.
What it does: turns body measurements into fitted sewing patterns and cut sheets in the Hoard (`files.read`, `files.write`), printed with `print.submit`.
Uses: `use burin 1` (curve flattening, path record); `use relief 1` (vertex transform for a piece); `use fairground 1` (the assurance fee, "offer allowed" for an order); `use tally 1` (rationals). Call these composed entries; do not re-implement them.
Logic (critical, new decisions only): piece size (measurement + ease, graded between sizes, millimetres) in FORTRAN as Trebago + APL as KRON witnessed; "measurements plausible" (each within its declared range) in Ada as Eve + APL as KRON witnessed; order book (order number, price x quantity in minor units, refused on overflow) in COBOL as Sutra + FORTRAN as Trebago witnessed; measurement grid in APL as KRON; garment tree in MUMPS as MYRA.
BOTH test: chest measured 960 mm and 1010 mm, tolerance 15: the measurement is BOTH and a refit is asked for; no cut sheet is made.
Tests: as the spec, plus a compose test (the app composes against a registry holding its parts, and every part entry it calls resolves) and the BOTH test above.
needs: fs.read, fs.write, print, ledger.read.
Planned (no runtime yet): APT, SNOBOL.
