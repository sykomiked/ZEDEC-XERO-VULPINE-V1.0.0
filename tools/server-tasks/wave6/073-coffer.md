# Wave 6 task: Coffer (slot 73), Wallet and books
Read ../APP_WAVE_SPEC.md first.
What it does: accounts, balances, entries, transfers and the books check (`ledger.accounts`, `ledger.balance`, `ledger.entries`, `ledger.post`, `ledger.open_account`, `currency.enable`, `ledger.check`).
Logic (critical): balance per account in minor units, refusing on overflow, in sutra + trebago witnessed; "transfer allowed" (balanced, amount > 0, not below zero, under the owner's per-day cap) in eve + sutra witnessed; holdings total exact in trebago. Copy the fee_check pattern (blocks/fee_check/fee.sutra).
Every post waits for the owner's confirmation; tests post only to a scratch books file, never real money.
needs: ledger.read, ledger.post, notify.
Planned (no runtime yet): SNOBOL, TeX, Eiffel, Solidity.
