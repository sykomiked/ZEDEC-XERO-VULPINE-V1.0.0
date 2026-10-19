# Wave 6 task: Larder (slot 82), Household budget
Read ../APP_WAVE_SPEC.md first.
What it does: monthly budget per category against spending read from the books (`ledger.accounts`, `ledger.entries`, `ledger.balance`).
Logic (critical): spent per category in minor units in sutra + trebago witnessed; "over budget" truth in eve + sutra witnessed; month totals and unspent carry-over exact in trebago.
needs: ledger.read, notify.
Planned (no runtime yet): Eiffel, Solidity, PHP.
