# Wave 2 task: Typecase (slot 113), Fonts
Read ../APP_WAVE_SPEC.md first.
Matrix: layer 113, build position 17 of 130 (`../MATRIX_ORDER.tsv`). Its layer's code already leans on Parley (layer 68, wave 4, built after this one: `use parley 1`). Use the ones built before this one: `use <id> 1` and call `<id>.<entry>`. An idea you need from one built after it: when more than one app needs it, write it here and register it in CONCEPTS.tsv with this app as owner (that layer will use it from you); otherwise list it in your report as that layer's. 3 other layers lean on this one, so anything here that a later app also needs is a shared entry: provide it, register it in CONCEPTS.tsv with this app as owner, and keep its name general. Report the reuse line `concepts.py` prints for this app.
What it does: preview, install and check fonts kept in the Hoard (`files.list`, `files.read`, `files.write`), and which scripts each covers. zxvd has no font-registry method: use only existing methods; list what's missing in your report.
Logic (critical): font accepted (each table's offset + length within the file, at most 256 tables, file at most 32 MiB) in kron + trebago witnessed; script coverage x100 (covered code points / script code points) in kron + trebago witnessed.
needs: fs.read, fs.write.
Planned (no runtime yet): Groovy, Racket, TeX.
