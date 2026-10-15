# Wave 3 task: Halloo (slot 28), Voice and video calls
Read ../APP_WAVE_SPEC.md first.
Matrix: layer 28, build position 36 of 130 (`../MATRIX_ORDER.tsv`). Its layer leans on no other layer's code yet. 1 other layer lean on this one, so anything here that a later app also needs is a shared entry: provide it, register it in CONCEPTS.tsv with this app as owner, and keep its name general. Report the reuse line `concepts.py` prints for this app.
What it does: place, answer and end calls (`call.start`, `call.accept`, `call.hangup`, `call.status`, `call.signal`). Media (capture, codecs, RTP) is not wired (H5): use only existing methods; list what's missing in your report.
Logic (critical): "may this call ring now" (known contact, not blocked, quiet hours as minutes of day) in eve + kron witnessed; ring timeout limits in eve; call duration (ms to h:m:s) in kron; call history record in myra.
Say on screen that Halloo cannot reach emergency services. Signalling travels only inside an encrypted session.
needs: peers, mic, camera, notify.
Planned (no runtime yet): Gleam, Python, LabVIEW.
