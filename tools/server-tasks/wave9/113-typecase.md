# Wave 9 task: Typecase (slot 113), Fonts
Read ../APP_WAVE_SPEC.md first.
What it does: preview, install and check fonts kept in the Hoard (`files.list`, `files.read`, `files.write`), and which scripts each covers. zxvd has no font-registry method: use only existing methods; list what's missing in your report.
Logic (critical): font accepted (each table's offset + length within the file, at most 256 tables, file at most 32 MiB) in kron + trebago witnessed; script coverage x100 (covered code points / script code points) in kron + trebago witnessed.
needs: fs.read, fs.write.
Planned (no runtime yet): Groovy, Racket, TeX.
