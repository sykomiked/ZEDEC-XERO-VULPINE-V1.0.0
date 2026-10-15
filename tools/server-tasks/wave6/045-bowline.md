# Wave 6 task: Bowline (slot 45), Tasks and reminders
Read ../APP_WAVE_SPEC.md first.
Matrix: layer 45, build position 68 of 130 (`../MATRIX_ORDER.tsv`). Its layer leans on no other layer's code yet. Report the reuse line `concepts.py` prints for this app.
What it does: tasks as files in the Hoard (`files.read`, `files.write`), notices read with `notes.list`. Posting a reminder notice has no method: use only existing methods; list what's missing in your report.
Logic (critical): next due date (every n days or weeks, skipping chosen weekdays) in basic + kron witnessed; "overdue" in eve + kron witnessed; tasks per list in kron; task record in myra.
needs: fs.read, fs.write, notify.
Planned (no runtime yet): LISP, TRAC, TeX.
