# Wave 3 task: Solstice (slot 43), Calendar
Read ../APP_WAVE_SPEC.md first.
What it does: events as files in the Hoard (`files.list`, `files.read`, `files.write`). Time zones and a notice the app can post have no method: use only existing methods; list what's missing in your report.
Logic (critical): day of week and days in month (Gregorian) in fortran77 + basic + zlir witnessed (the number_classics pattern); next date of a repeating event in kron + C++ wasm witnessed; "two events overlap" in eve + kron witnessed; event record in myra.
needs: fs.read, fs.write, notify.
Planned (no runtime yet): Raku, TeX, PostScript.
