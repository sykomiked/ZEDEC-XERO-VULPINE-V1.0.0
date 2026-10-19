# Wave 8 task: Cubby (slot 103), A safe space for children
Read ../APP_WAVE_SPEC.md first.
What it does: a child's seat with allowed apps and time (`screen.check`, `screen.status`, `screen.set_limit`, `screen.lock`, `seats.list`).
Logic (critical): "may this child open this now" (on the allowed list, minutes left, within hours, not locked) in Rust wasm + eve + kron witnessed (three witnesses); minutes left in kron; activity in myra.
No contact with strangers: public spaces and social stay closed; limit changes need the parent's passphrase.
needs: screen.time, sys.read, notify.
Planned (no runtime yet): Pascal.
