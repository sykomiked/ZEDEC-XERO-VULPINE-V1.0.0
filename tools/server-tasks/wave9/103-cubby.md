# Wave 9 task: Cubby (slot 103), A safe space for children
Read ../APP_WAVE_SPEC.md first.
Matrix: layer 103, build position 100 of 130 (`../MATRIX_ORDER.tsv`). Its layer leans on no other layer's code yet. Report the reuse line `concepts.py` prints for this app.
What it does: a child's seat with allowed apps and time (`screen.check`, `screen.status`, `screen.set_limit`, `screen.lock`, `seats.list`).
Logic (critical): "may this child open this now" (on the allowed list, minutes left, within hours, not locked) in Rust wasm + eve + kron witnessed (three witnesses); minutes left in kron; activity in myra.
No contact with strangers: public spaces and social stay closed; limit changes need the parent's passphrase.
needs: screen.time, sys.read, notify.
Now runs (Lingua front-end built): Pascal (twin Tutordrake) for step-by-step teaching: `logic pascal <entry> apps/blocks/cubby/<entry>.pas`, in the twin's canonical form, joined to another language's line for the same entry.
