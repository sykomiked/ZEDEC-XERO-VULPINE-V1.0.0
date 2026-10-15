# Wave 9 task: Burr (slot 118), Sticky notes
Read ../APP_WAVE_SPEC.md first.
What it does: sticky notes on the desktop, each a text file in a Notes folder of the Hoard (`files.list`, `files.read`, `files.write`, `files.mkdir`, `files.trash`); a removed note goes to the Trash and can be put back.
Logic (critical): placement of a new note (cascade by a step, wrap to the top when off screen) in kron + trebago witnessed; limits (at most 256 notes, text at most 49152 bytes, the read_max of `files.read`) in kron.
needs: fs.read, fs.write.
Planned (no runtime yet): SNOBOL, Short Code.
