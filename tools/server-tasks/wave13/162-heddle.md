# Wave 13 task: Heddle (slot 162), Pattern designer
Read ../APP_WAVE_SPEC.md first. This is an L2 app: manifest says `layer L2` and composes its built_from apps' blocks with `use`; it may only start after those L1 waves are merged.
Depends on: wave 0 (115 platen), wave 3 (040 furrow), wave 4 (054 burin), wave 9 (116 dyer).
Manifest: `slot 162`, `layer L2`, `icon icons/l2/162-heddle.svg`, `budget 233`, one `use <id> 1` per part below. A part can be used only if its manifest carries `version 1.0.0` and its `provides` lines; the composed logic lines must fit ZXAPP_LOGIC_MAX (128) and the uses ZXAPP_USES (8). If any of these fails, stop and report it; do not raise a limit or edit the part.
What it does: designs woven and printed patterns and sends them to printers (`print.status`, `print.submit`, `print.job`) or looms as exact instructions, kept in the Hoard (`files.read`, `files.write`). zxvd has no method for loom output: use only existing methods; list what's missing in your report.
Uses: `use burin 1` (curve flattening, hatch lines, path record); `use platen 1` (`platen.sheets`, `platen.copies_ok`); `use dyer 1` (RGB to HSL, contrast); `use furrow 1` (column sums of the yarn sheet). Call these composed entries; do not re-implement them.
Logic (critical, new decisions only): drawdown (threading x tie-up x treadling, as an integer matrix product, cell > 0 is a raised end) in APL as KRON + FORTRAN as Trebago witnessed; yarn length per colour and cost in minor units in COBOL as Sutra + FORTRAN as Trebago witnessed; ends (ends per 10 cm x width) and picks exact in FORTRAN as Trebago; "fits the machine" (shafts, width, colours at most the loom's or printer's) in Ada as Eve + APL as KRON witnessed; pattern tree in MUMPS as MYRA.
BOTH test: the printer preview and the loom drawdown of one pattern differ (64 and 63 ends in row 5): the pattern is BOTH and the job is held before any material is used.
Tests: as the spec, plus a compose test (the app composes against a registry holding its parts, and every part entry it calls resolves) and the BOTH test above.
needs: print, fs.read, fs.write.
Planned (no runtime yet): APT, PostScript, Perl.
