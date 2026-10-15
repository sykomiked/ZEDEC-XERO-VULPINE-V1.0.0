# Wave 6 task: Reckoner (slot 83), Statements and tax reports
Read ../APP_WAVE_SPEC.md first.
What it does: period statements and tax estimates from the books (`ledger.entries`, `ledger.check`, `iso20022.render` camt.053, `jurisdiction.get`), saved through the Banker (`files.write`).
Logic (critical): period totals per account in trebago + sutra witnessed; tax estimate over integer bands in sutra + trebago witnessed; "report allowed" (closed period, `ledger.check` ok) in eve. Books verification is shown as `ledger.check` returns it.
Tax figures are advisory, not tax advice; a person files.
needs: ledger.read, fs.write, print.
Planned (no runtime yet): RPG, Eiffel, Solidity.
