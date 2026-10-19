# Wave 2 task: Threshold (slot 3), Login and accounts
Read ../APP_WAVE_SPEC.md first.
What it does: seats and their locks (`seats.list`, `seats.add`, `seats.remove`, `seats.unlock`, `seats.lock`, `seats.set_lock`, `seats.auto`, `seats.set_auto`) and passkeys (`passkeys.register_begin`, `passkeys.register_finish`, `passkeys.auth_begin`, `passkeys.auth_finish`, `passkeys.list`, `passkeys.revoke`). Unlimited users: the model has no seat limit of its own; when zxvd answers SEAT_ERR_FULL, show it and list the daemon's limit in your report.
Logic (critical): role rights (owner may add, remove and change any lock; a person only their own; a guest never has a lock) in eve + kron witnessed; "skip the login" (one seat, no lock, auto on, as 0/1) in eve + kron witnessed; passphrase length limits (12 to 1024) in eve; seat record in myra (never a hash or a passphrase).
Back-off after a wrong entry is the daemon's (`wait_ms`); the model shows it and never retries on its own.
needs: sys.read, notify.
Planned (no runtime yet): BCPL, C#.
