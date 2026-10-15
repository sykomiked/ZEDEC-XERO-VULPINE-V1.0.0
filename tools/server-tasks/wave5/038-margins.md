# Wave 5 task: Margins (slot 38), Notes
Read ../APP_WAVE_SPEC.md first.
Matrix: layer 38, build position 59 of 130 (`../MATRIX_ORDER.tsv`). Its layer's code already leans on Coffer (layer 73, wave 4, an earlier wave: `use coffer 1`); the core (layer 0: reach it through zxvd methods that exist); Lockbox (layer 9, wave 4, an earlier wave: `use lockbox 1`). Take every idea those layers own from them (`use` the built ones and call `<id>.<entry>`; for one not built yet, list the entry in your report and do not write it here). Report the reuse line `concepts.py` prints for this app.
What it does: notes as files in the Hoard's Notes folder (`files.list`, `files.read`, `files.write`, `files.rename`, `files.trash`), found with `search.query`. Build on kernel/src/apps/notes_app.c.
Logic: note size limits in eve; tag index in myra; pinned and recent order in kron.
Text work (words, lines) stays C: its best-fit languages have no runtime yet; say so in the manifest.
needs: fs.read, fs.write, search.
Planned (no runtime yet): SNOBOL, TeX, PostScript.
