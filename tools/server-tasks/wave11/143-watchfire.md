# Wave 11 task: Watchfire (slot 143), Emergency contacts and medical ID
Read ../APP_WAVE_SPEC.md first.
Matrix: layer 143, build position 129 of 130 (`../MATRIX_ORDER.tsv`). Its layer leans on no other layer's code yet. Report the reuse line `concepts.py` prints for this app.
What it does: emergency contacts and a medical ID shown on the lock screen; on the person's request, ring contacts (`call.start`, signalling only) and share where the machine is (`place.ask`, `place.here`). Contacts' devices from `phone.list`.
Logic (critical): "share location with this contact" = person started the emergency AND owner opted in AND contact listed AND `place.ask` allowed, in eve + kron witnessed; cancel window before any ring (10..60 s, never skipped) in eve + kron witnessed; medical ID and contact records in myra, stored as the person typed them, never computed.
Life-safety: advisory and human-approved; never calls emergency services or anyone by itself; not a medical device; nothing clinical is inferred.
needs: location, peers, notify, fs.read, fs.write.
Planned (no runtime yet): OCaml, Swift (SDK host only), Python (SDK host only).
