# Wave 1 task: Tideturn (slot 6), Software updates
Read ../APP_WAVE_SPEC.md first.
Matrix: layer 6, build position 12 of 130 (`../MATRIX_ORDER.tsv`). Its layer's code already leans on the core (layer 0: reach it through zxvd methods that exist); Seedbank (layer 33, wave 3, a later wave (a cycle): `use seedbank 1`); Witness (layer 18, wave 1, an earlier wave: `use witness 1`); Coffer (layer 73, wave 4, a later wave (a cycle): `use coffer 1`). Take every idea those layers own from them (`use` the built ones and call `<id>.<entry>`; for one not built yet, list the entry in your report and do not write it here). 3 other layers lean on this one, so anything here that a later app also needs is a shared entry: provide it, register it in CONCEPTS.tsv with this app as owner, and keep its name general. Report the reuse line `concepts.py` prints for this app.
What it does: dual-signed OS and app updates with an audit trail (`update.check`, `update`).
Logic: version ordering (major.minor.patch compare) in sutra + kron witnessed; "both signatures present and valid" as a truth in eve + kron (inputs are the two verify results as 0/1); update record in myra.
Never install anything in tests; use the daemon's check only.
needs: net, sys.read.
