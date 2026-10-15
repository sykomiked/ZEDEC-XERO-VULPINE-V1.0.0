# Wave 10 task: Graft (slot 124), Version control
Read ../APP_WAVE_SPEC.md first.
Matrix: layer 124, build position 117 of 130 (`../MATRIX_ORDER.tsv`). Its layer leans on no other layer's code yet. Report the reuse line `concepts.py` prints for this app.
What it does: commits, history and merge of folders in the Hoard; file versions from `backup.versions` and `backup.list`, bring one back with `backup.restore` (never over what is there); work through `files.*`. zxvd has no commit, branch or diff method: use only existing methods; list what's missing in your report.
Logic (critical): history intact (each commit's parent index equals the previous commit's) in kron + sutra witnessed; merge per line as four values (ours changed, theirs changed, same or not: equal change TRUE, one side only TRUE, conflicting BOTH kept for the person, untouched NEITHER) in eve + kron witnessed, joined with `lpres_conjoin` order, never re-derived; lines added and removed exact in trebago; limits (file at most 16 MiB to diff, at most 65536 lines) in eve; commit record (id, parent, author, time, message length) in myra.
A BOTH is never resolved by the app; the person picks.
needs: fs.read, fs.write, backup.
Planned (no runtime yet): Elm, OCaml, Bourne shell.
