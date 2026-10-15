# Wave 3 task: Kindred (slot 44), Contacts
Read ../APP_WAVE_SPEC.md first.
What it does: contacts as records kept in the Hoard (`files.read`, `files.write`), a contact's agent card checked with `web4.card_verify`, their node from `net.peers`, chat identity from `chat.me`.
Logic: contact index and name-prefix search in myra; field limits in eve; likely duplicates (same card id or same name codes) in kron.
needs: fs.read, fs.write, peers.
Planned (no runtime yet): SNOBOL, Kotlin, TeX.
