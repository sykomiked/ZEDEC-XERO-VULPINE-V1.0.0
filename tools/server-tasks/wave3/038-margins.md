# Wave 3 task: Margins (slot 38), Notes
Read ../APP_WAVE_SPEC.md first.
What it does: notes as files in the Hoard's Notes folder (`files.list`, `files.read`, `files.write`, `files.rename`, `files.trash`), found with `search.query`. Build on kernel/src/apps/notes_app.c.
Logic: note size limits in eve; tag index in myra; pinned and recent order in kron.
Text work (words, lines) stays C: its best-fit languages have no runtime yet; say so in the manifest.
needs: fs.read, fs.write, search.
Planned (no runtime yet): SNOBOL, TeX, PostScript.
