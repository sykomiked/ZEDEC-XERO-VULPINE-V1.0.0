# Wave 16 task: Tiler (slot 203), Dashboards
Read ../APP_WAVE_SPEC.md first. This is an L2 app: manifest says `layer L2` and composes its built_from apps' blocks with `use`; it may only start after those L1 waves are merged.
Depends on: wave 0 (014 wake, 015 tidewatch), wave 2 (002 hollow), wave 6 (040 furrow).
Manifest: `slot 203`, `layer L2`, `icon icons/l2/203-tiler.svg`, `budget 233`, one `use <id> 1` per part below. A part can be used only if its manifest carries `version 1.0.0` and its `provides` lines; the composed logic lines must fit ZXAPP_LOGIC_MAX (128) and the uses ZXAPP_USES (8). If any of these fails, stop and report it; do not raise a limit or edit the part.
What it does: assembles live dashboards from tiles drawn from the machine's data and monitors (`sys.info`, `sys.disk`, `sys.net`, `power.status`, `log.tail`), kept in the Hoard (`files.read`, `files.write`).
Uses: `use hollow 1` (window tiling, window limits); `use wake 1` (`wake.count`); `use tidewatch 1` (`tidewatch.cpu_x100`, `tidewatch.peak`); `use furrow 1` (column sums). Call these composed entries; do not re-implement them.
Logic (critical, new decisions only): tile figures (sum, min, max, mean floor over the tile's series) in FORTRAN as Trebago + APL as KRON witnessed; refresh limits (1..3600 s, at most 64 tiles) in Ada as Eve; tile grid in APL as KRON; dashboard tree in MUMPS as MYRA.
BOTH test: one tile shows 12 GB free from `sys.disk` and another 14 GB from a second source: both are shown side by side as BOTH, not one tidy number.
Tests: as the spec, plus a compose test (the app composes against a registry holding its parts, and every part entry it calls resolves) and the BOTH test above.
needs: sys.read, journal, fs.read, fs.write.
Planned (no runtime yet): SAS, Logo, Visual Basic.
