# Wave 8 task: Larder (slot 82), Household budget
Read ../APP_WAVE_SPEC.md first.
Matrix: layer 82, build position 92 of 130 (`../MATRIX_ORDER.tsv`). Its layer leans on no other layer's code yet. Report the reuse line `concepts.py` prints for this app.
What it does: monthly budget per category against spending read from the books (`ledger.accounts`, `ledger.entries`, `ledger.balance`).
Logic (critical): spent per category in minor units in sutra + trebago witnessed; "over budget" truth in eve + sutra witnessed; month totals and unspent carry-over exact in trebago.
needs: ledger.read, notify.
Planned (no runtime yet): Eiffel, Solidity, PHP.
