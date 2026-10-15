# Wave 2 task: Meadow (slot 86), The Park
Read ../APP_WAVE_SPEC.md first.
Matrix: layer 86, build position 23 of 130 (`../MATRIX_ORDER.tsv`). Its layer's code already leans on Seedbank (layer 33, wave 3, a later wave (a cycle): `use seedbank 1`); Gatehouse (layer 8, wave 1, an earlier wave: `use gatehouse 1`); Trellis (layer 29, wave 2, an earlier wave: `use trellis 1`); the core (layer 0: reach it through zxvd methods that exist); Commons (layer 27, wave 2, an earlier wave: `use commons 1`). Take every idea those layers own from them (`use` the built ones and call `<id>.<entry>`; for one not built yet, list the entry in your report and do not write it here). 4 other layers lean on this one, so anything here that a later app also needs is a shared entry: provide it, register it in CONCEPTS.tsv with this app as owner, and keep its name general. Report the reuse line `concepts.py` prints for this app.
What it does: find, join and use public spaces (`pspace.find`, `pspace.join`, `pspace.leave`, `pspace.list`, `pspace.members`, `pspace.events`, `pspace.post`, `spaces.list` for park rights). Feature `pspace` is off by default.
Logic (critical): "may this seat post here" (the space kind's rights, post 1..1024 bytes) in eve + myra witnessed (myra looks the seat's role up in the rights record); member and event counts per space in trebago; joined spaces and the last event index in myra.
Posts cannot be deleted (`pspace.post`): the app says so before sending.
needs: peers, net, notify.
Planned (no runtime yet): Logo, C#.
