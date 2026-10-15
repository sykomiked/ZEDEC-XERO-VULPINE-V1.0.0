# Wave 2 task: Tideturn (slot 6), Software updates
Read ../APP_WAVE_SPEC.md first.
Matrix: layer 6, build position 22 of 130 (`../MATRIX_ORDER.tsv`). Its layer's code already leans on the core (layer 0: reach it through zxvd methods that exist); Seedbank (layer 33, wave 1, built before this one: `use seedbank 1`); Witness (layer 18, wave 1, built before this one: `use witness 1`); Coffer (layer 73, wave 1, built before this one: `use coffer 1`). Use the ones built before this one: `use <id> 1` and call `<id>.<entry>`. An idea you need from one built after it: when more than one app needs it, write it here and register it in CONCEPTS.tsv with this app as owner (that layer will use it from you); otherwise list it in your report as that layer's. 3 other layers lean on this one, so anything here that a later app also needs is a shared entry: provide it, register it in CONCEPTS.tsv with this app as owner, and keep its name general. Report the reuse line `concepts.py` prints for this app.
What it does: dual-signed OS and app updates with an audit trail (`update.check`, `update`).
Logic: version ordering (major.minor.patch compare) in sutra + kron witnessed; "both signatures present and valid" as a truth in eve + kron (inputs are the two verify results as 0/1); update record in myra.
Never install anything in tests; use the daemon's check only.
needs: net, sys.read.
