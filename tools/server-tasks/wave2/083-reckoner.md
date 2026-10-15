# Wave 2 task: Reckoner (slot 83), Statements and tax reports
Read ../APP_WAVE_SPEC.md first.
Matrix: layer 83, build position 19 of 130 (`../MATRIX_ORDER.tsv`). Its layer's code already leans on the core (layer 0: reach it through zxvd methods that exist); Coffer (layer 73, wave 4, a later wave (a cycle): `use coffer 1`). Take every idea those layers own from them (`use` the built ones and call `<id>.<entry>`; for one not built yet, list the entry in your report and do not write it here). 3 other layers lean on this one, so anything here that a later app also needs is a shared entry: provide it, register it in CONCEPTS.tsv with this app as owner, and keep its name general. Report the reuse line `concepts.py` prints for this app.
What it does: period statements and tax estimates from the books (`ledger.entries`, `ledger.check`, `iso20022.render` camt.053, `jurisdiction.get`), saved through the Banker (`files.write`).
Logic (critical): period totals per account in trebago + sutra witnessed; tax estimate over integer bands in sutra + trebago witnessed; "report allowed" (closed period, `ledger.check` ok) in eve. Books verification is shown as `ledger.check` returns it.
Tax figures are advisory, not tax advice; a person files.
needs: ledger.read, fs.write, print.
Planned (no runtime yet): RPG, Eiffel, Solidity.
