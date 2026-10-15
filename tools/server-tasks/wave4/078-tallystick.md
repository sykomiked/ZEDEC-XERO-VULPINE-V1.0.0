# Wave 4 task: Tallystick (slot 78), Invoices and receipts
Read ../APP_WAVE_SPEC.md first.
Matrix: layer 78, build position 37 of 130 (`../MATRIX_ORDER.tsv`). Its layer's code already leans on Coffer (layer 73, wave 4, a later wave (a cycle): `use coffer 1`); Murmuration (layer 61, built in wave 0: `use murmuration 1`); Witness (layer 18, wave 1, an earlier wave: `use witness 1`); the core (layer 0: reach it through zxvd methods that exist). Take every idea those layers own from them (`use` the built ones and call `<id>.<entry>`; for one not built yet, list the entry in your report and do not write it here). 1 other layer lean on this one, so anything here that a later app also needs is a shared entry: provide it, register it in CONCEPTS.tsv with this app as owner, and keep its name general. Report the reuse line `concepts.py` prints for this app.
What it does: make invoices and receipts, record payments, render statements (`ledger.post`, `ledger.entries`, `iso20022.render` camt.053), save and print them through the Banker (`files.write`, `print.submit`).
Logic (critical): invoice total (qty x unit price per line, tax at num/den with the stated rounding) in sutra + trebago witnessed; "paid in full" truth (payments at or above total) in eve + sutra witnessed; invoice number sequence as a fixed business record in sutra (refuses on overflow).
Recording a payment needs the owner's confirmation.
needs: ledger.read, ledger.post, fs.read, fs.write, print.
Planned (no runtime yet): Python, Eiffel, Solidity.
