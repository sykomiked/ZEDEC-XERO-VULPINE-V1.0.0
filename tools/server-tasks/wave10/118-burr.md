# Wave 10 task: Burr (slot 118), Sticky notes
Read ../APP_WAVE_SPEC.md first.
Matrix: layer 118, build position 112 of 130 (`../MATRIX_ORDER.tsv`). Its layer leans on no other layer's code yet. Report the reuse line `concepts.py` prints for this app.
What it does: sticky notes on the desktop, each a text file in a Notes folder of the Hoard (`files.list`, `files.read`, `files.write`, `files.mkdir`, `files.trash`); a removed note goes to the Trash and can be put back.
Logic (critical): placement of a new note (cascade by a step, wrap to the top when off screen) in kron + trebago witnessed; limits (at most 256 notes, text at most 49152 bytes, the read_max of `files.read`) in kron.
needs: fs.read, fs.write.
Planned (no runtime yet): SNOBOL, Short Code.
