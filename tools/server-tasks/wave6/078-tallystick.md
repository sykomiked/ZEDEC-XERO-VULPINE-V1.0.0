# Wave 6 task: Tallystick (slot 78), Invoices and receipts
Read ../APP_WAVE_SPEC.md first.
What it does: make invoices and receipts, record payments, render statements (`ledger.post`, `ledger.entries`, `iso20022.render` camt.053), save and print them through the Banker (`files.write`, `print.submit`).
Logic (critical): invoice total (qty x unit price per line, tax at num/den with the stated rounding) in sutra + trebago witnessed; "paid in full" truth (payments at or above total) in eve + sutra witnessed; invoice number sequence as a fixed business record in sutra (refuses on overflow).
Recording a payment needs the owner's confirmation.
needs: ledger.read, ledger.post, fs.read, fs.write, print.
Planned (no runtime yet): Python, Eiffel, Solidity.
