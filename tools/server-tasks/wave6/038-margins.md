# Wave 6 task: Margins (slot 38), Notes
Read ../APP_WAVE_SPEC.md first.
Matrix: layer 38, build position 61 of 130 (`../MATRIX_ORDER.tsv`). Its layer's code already leans on Coffer (layer 73, wave 1, built before this one: `use coffer 1`); the core (layer 0: reach it through zxvd methods that exist); Lockbox (layer 9, wave 2, built before this one: `use lockbox 1`). Use the ones built before this one: `use <id> 1` and call `<id>.<entry>`. An idea you need from one built after it: when more than one app needs it, write it here and register it in CONCEPTS.tsv with this app as owner (that layer will use it from you); otherwise list it in your report as that layer's. Report the reuse line `concepts.py` prints for this app.
What it does: notes as files in the Hoard's Notes folder (`files.list`, `files.read`, `files.write`, `files.rename`, `files.trash`), found with `search.query`. Build on kernel/src/apps/notes_app.c.
Logic: note size limits in eve; tag index in myra; pinned and recent order in kron.
Text work (words, lines) stays C: its best-fit languages have no runtime yet; say so in the manifest.
needs: fs.read, fs.write, search.
Planned (no runtime yet): SNOBOL, TeX, PostScript.
