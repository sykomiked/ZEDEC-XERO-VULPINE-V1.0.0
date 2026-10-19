# Wave 3 task: Furrow (slot 40), Spreadsheets
Read ../APP_WAVE_SPEC.md first.
What it does: sheets as files in the Hoard (`files.read`, `files.write`).
Logic (critical): column and row sums exact in trebago + kron witnessed; money cells (fixed scale, refused on overflow) in sutra + kron witnessed; cell store as sparse records in myra; formula limits (depth, cells referenced) in eve.
No floats: a division that is not whole uses kernel/src/rational in C.
needs: fs.read, fs.write.
Planned (no runtime yet): AWK, TeX, PostScript.
