# Wave 6 task: Kindred (slot 44), Contacts
Read ../APP_WAVE_SPEC.md first.
Matrix: layer 44, build position 67 of 130 (`../MATRIX_ORDER.tsv`). Its layer leans on no other layer's code yet. Report the reuse line `concepts.py` prints for this app.
What it does: contacts as records kept in the Hoard (`files.read`, `files.write`), a contact's agent card checked with `web4.card_verify`, their node from `net.peers`, chat identity from `chat.me`.
Logic: contact index and name-prefix search in myra; field limits in eve; likely duplicates (same card id or same name codes) in kron.
needs: fs.read, fs.write, peers.
Planned (no runtime yet): SNOBOL, Kotlin, TeX.
